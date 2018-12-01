/*
 * Copyright (c) 2000, 2001, 2002, 2003, 2004, 2005, 2008, 2009
 *	The President and Fellows of Harvard College.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE UNIVERSITY AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE UNIVERSITY OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <spl.h>
#include <spinlock.h>
#include <proc.h>
#include <current.h>
#include <mips/tlb.h>
#include <addrspace.h>
#include <vm.h>
#include <opt-A3.h>
#include <sfs.h>


/*
 * Dumb MIPS-only "VM system" that is intended to only be just barely
 * enough to struggle off the ground.
 */

/* under dumbvm, always have 48k of user stack */
#define DUMBVM_STACKPAGES    12



struct coremap_entry
{
	paddr_t parent;
	paddr_t paddr;
	volatile bool isAvailable;
};

struct coremap
{
	struct coremap_entry* entries;
	unsigned long size;
};


/*
 * Wrap rma_stealmem in a spinlock.
 */
static struct spinlock stealmem_lock = SPINLOCK_INITIALIZER;
static struct spinlock coremap_lock = SPINLOCK_INITIALIZER;
static volatile bool coremap_initialized = false;
static struct coremap *coremap;



void create_coremap() {
	/*
	paddr_t start = 0;
	paddr_t end = 0;

	ram_getsize(&start, &end);
	spinlock_init(&coremap_lock);
	coremap = kmalloc(sizeof(struct coremap));
	coremap->size = (end - start) / PAGE_SIZE;

	coremap->entries = kmalloc(sizeof(struct coremap_entry) * coremap->size);
	kprintf("Initialized coremap \n");
	for(unsigned long i = 0; i < coremap->size; i++) {
		coremap->entries[i].paddr = start + (i * PAGE_SIZE);
		coremap->entries[i].parent = 0;
		coremap->entries[i].isAvailable = true;
	}
	paddr_t start2 = 0;
	ram_getsize(&start2, &end);
	unsigned long x = 0;
	while(coremap->entries[x].paddr < start2) {
		coremap->entries[x].isAvailable = false;
		x++;
	} */

  coremap = kmalloc(sizeof(struct coremap*));

  if (coremap == NULL) {
    panic("could not create coremap");
  }

  spinlock_init(&coremap_lock);

  paddr_t startpaddr;
  paddr_t endpaddr;

  // destroys startpaddr and endpaddr, stealmem is now useless
  ram_getsize(&startpaddr, &endpaddr);

  // Get the total number of physical frames available in the system, record as length of coremap
  // subtract one because the last one is the END of the last paddr we can use, not the START of the last physical page
  // we can use.
  coremap->size = (endpaddr - startpaddr) / PAGE_SIZE - 1;

  // We would normally kmalloc coremap to put it on kernel heap, but
  // since kmalloc is in the weird stage between not working after ram_getsizing
  // and not working until we have coremap setup (because of ram_getsize), we need to do math, getting the kernel
  // memory required for coremap and putting it there.
  coremap->entries = (struct coremap_entry*)PADDR_TO_KVADDR(startpaddr);

  // Record the total size of the coremaps needed, in bytes
  size_t totalCoremapSize = coremap->size * sizeof(struct coremap_entry);

  // The number of coremap entries that must be made unassignable, seeing as they would reference coremaps
  size_t metaCoremaps = SFS_ROUNDUP(totalCoremapSize, PAGE_SIZE) / PAGE_SIZE;
  //DEBUG(DB_EXEC, "TOTAL COREMAPS USED FOR COREMAPS: %d\n", metaCoremaps);

  // Advance free space past the space allocated for all the coremap entries
  size_t freepaddr = startpaddr + totalCoremapSize;

  // Round up to the nearest page size, want it page-aligned.
  freepaddr = SFS_ROUNDUP(freepaddr, PAGE_SIZE);

  // The coremap contained enough entries for covering all of startpaddr to endpaddr. But
  // the first section of that memory must hold the coremaps. Therefore all coremaps that would
  // describe space that is occupied by the coremaps must be unassignable.

  // Setup each coremap entry.
  for (size_t i = 0; i < coremap->size; i++) {

    if (i < metaCoremaps) {
      coremap->entries[i].isAvailable = false;
      coremap->entries[i].parent = 0;
    }

    else {
      //coremap->cm_entries[i].vaddr = PADDR_TO_KVADDR(freepaddr + PAGE_SIZE * i);
      coremap->entries[i].paddr = freepaddr + PAGE_SIZE * i;
      coremap->entries[i].isAvailable = true;
      coremap->entries[i].parent = coremap->entries[i].paddr;
    }
  }
}

void
vm_bootstrap(void)
{
	create_coremap();
	mem_transfer_control();
	coremap_initialized = true;
}


/*
static
paddr_t
getppages(unsigned long npages)
{
	paddr_t addr;

    if (!coremap_initialized) {
        spinlock_acquire(&stealmem_lock);
        addr = ram_stealmem(npages);
        spinlock_release(&stealmem_lock);
        return addr;
    }

    spinlock_acquire(&coremap_lock);
    unsigned long numBlocks = 0;
    for(unsigned long i = 0; i < coremap->size; i++) {
    	if(coremap->entries[i].isAvailable) {
    		numBlocks++; 
    		if(numBlocks == npages) {
    			unsigned long startIdx = (i + 1) - numBlocks;
    			addr = coremap->entries[startIdx].paddr;
    			for(unsigned long j = startIdx; j < startIdx + numBlocks; j++) {
	    			coremap->entries[j].parent = addr;
    				coremap->entries[j].isAvailable = false;
    			}
    			spinlock_release(&coremap_lock);
    			return addr;
    		}
    	} else {
    		numBlocks = 0;
    	}
    }
    spinlock_release(&coremap_lock);
    return 0;
}*/


static
paddr_t
getppages(unsigned long npages)
{
	paddr_t addr = 0;

        if (!coremap_initialized)
        {
            spinlock_acquire(&stealmem_lock);

            addr = ram_stealmem(npages);
	
            spinlock_release(&stealmem_lock);
            return addr;
        }

        spinlock_acquire(&coremap_lock);


        unsigned int blockCount = 0;
    	kprintf("Getting pages %d \n", (int)npages);
        for(unsigned long i = 0; i<coremap->size; ++i)
        {
            if(coremap->entries[i].isAvailable)
            {
                for (unsigned long j = i; j<coremap->size; ++j)
                {
                    if(coremap->entries[j].isAvailable)
                    {
                        ++blockCount;
                    }
                    
                    if(blockCount == npages)
                    {
                        addr = coremap->entries[i].paddr;
                        for (unsigned long k = i; k<=j; ++k)
                        {
                            coremap->entries[k].parent = addr;
                            coremap->entries[k].isAvailable = false;

                        }

                        spinlock_release(&coremap_lock);
                        return addr;
                    }

                    if(!coremap->entries[j].isAvailable)
                    {
                        break;
                    }
                }
                blockCount = 0;
            }
        }

        kprintf("no more pages avail \n");
        spinlock_release(&coremap_lock);
        return 0;
}

/* Allocate/free some kernel-space virtual pages */
vaddr_t 
alloc_kpages(int npages)
{
	paddr_t pa;
	pa = getppages(npages);
	if (pa==0) {
		return 0;
	}
	return PADDR_TO_KVADDR(pa);
}

void free_pages_helper(paddr_t paddr) {
	(void)paddr;
	/*spinlock_acquire(&coremap_lock);
	for(unsigned long i = 0; i < coremap->size; i++) {
		if(coremap->entries[i].paddr == paddr) {
			kprintf("found address, freeing \n");
			while(coremap->entries[i].parent == paddr) {
				coremap->entries[i].parent = 0;
				coremap->entries[i].isAvailable = true;
				i++;
			}
			spinlock_release(&coremap_lock);
			return;
		}
	}
	spinlock_release(&coremap_lock);*/
}

void 
free_kpages(vaddr_t addr)
{
	(void) addr;
	return;
	/*
	kprintf("in free_kpages \n");
	paddr_t paddr = KVADDR_TO_PADDR(addr);
	free_pages_helper(paddr);*/
}

void
vm_tlbshootdown_all(void)
{
	panic("dumbvm tried to do tlb shootdown?!\n");
}

void
vm_tlbshootdown(const struct tlbshootdown *ts)
{
	(void)ts;
	panic("dumbvm tried to do tlb shootdown?!\n");
}

int
vm_fault(int faulttype, vaddr_t faultaddress)
{
	vaddr_t vbase1, vtop1, vbase2, vtop2, stackbase, stacktop;
	paddr_t paddr;
	int i;
	uint32_t ehi, elo;
	struct addrspace *as;
	int spl;

	faultaddress &= PAGE_FRAME;

	DEBUG(DB_VM, "dumbvm: fault: 0x%x\n", faultaddress);

	switch (faulttype) {
	    case VM_FAULT_READONLY:
		/* We always create pages read-write, so we can't get this */
		//panic("dumbvm: got VM_FAULT_READONLY\n");
			return 1;
	    case VM_FAULT_READ:
	    case VM_FAULT_WRITE:
		break;
	    default:
		return EINVAL;
	}

	if (curproc == NULL) {
		/*
		 * No process. This is probably a kernel fault early
		 * in boot. Return EFAULT so as to panic instead of
		 * getting into an infinite faulting loop.
		 */
		return EFAULT;
	}

	as = curproc_getas();
	if (as == NULL) {
		/*
		 * No address space set up. This is probably also a
		 * kernel fault early in boot.
		 */
		return EFAULT;
	}

	/* Assert that the address space has been set up properly. */
	KASSERT(as->as_vbase1 != 0);
	KASSERT(as->as_pbase1 != 0);
	KASSERT(as->as_npages1 != 0);
	KASSERT(as->as_vbase2 != 0);
	KASSERT(as->as_pbase2 != 0);
	KASSERT(as->as_npages2 != 0);
	KASSERT(as->as_stackpbase != 0);
	KASSERT((as->as_vbase1 & PAGE_FRAME) == as->as_vbase1);
	KASSERT((as->as_pbase1 & PAGE_FRAME) == as->as_pbase1);
	KASSERT((as->as_vbase2 & PAGE_FRAME) == as->as_vbase2);
	KASSERT((as->as_pbase2 & PAGE_FRAME) == as->as_pbase2);
	KASSERT((as->as_stackpbase & PAGE_FRAME) == as->as_stackpbase);

	vbase1 = as->as_vbase1;
	vtop1 = vbase1 + as->as_npages1 * PAGE_SIZE;
	vbase2 = as->as_vbase2;
	vtop2 = vbase2 + as->as_npages2 * PAGE_SIZE;
	stackbase = USERSTACK - DUMBVM_STACKPAGES * PAGE_SIZE;
	stacktop = USERSTACK;

	if (faultaddress >= vbase1 && faultaddress < vtop1) {
		paddr = (faultaddress - vbase1) + as->as_pbase1;
	}
	else if (faultaddress >= vbase2 && faultaddress < vtop2) {
		paddr = (faultaddress - vbase2) + as->as_pbase2;
	}
	else if (faultaddress >= stackbase && faultaddress < stacktop) {
		paddr = (faultaddress - stackbase) + as->as_stackpbase;
	}
	else {
		return EFAULT;
	}

	/* make sure it's page-aligned */
	KASSERT((paddr & PAGE_FRAME) == paddr);

	/* Disable interrupts on this CPU while frobbing the TLB. */
	spl = splhigh();

	for (i=0; i<NUM_TLB; i++) {
		tlb_read(&ehi, &elo, i);
		if (elo & TLBLO_VALID) {
			continue;
		} 
		ehi = faultaddress;
		elo = paddr | TLBLO_DIRTY | TLBLO_VALID;
		
		#if OPT_A3
		// Text segment and elf_loaded
		if (as->text_seg_loaded && faultaddress >= vbase1 && faultaddress < vtop1) {
			elo &= ~TLBLO_DIRTY;
		}
		#endif
		
		DEBUG(DB_VM, "dumbvm: 0x%x -> 0x%x\n", faultaddress, paddr);
		tlb_write(ehi, elo, i);
		splx(spl);
		return 0;
	}

	#if OPT_A3
	//kprintf("dumbvm: Ran out of TLB entries - cannot handle page fault\n");
	DEBUG(DB_VM, "randomly replacing tlb entry" );
	ehi = faultaddress;
	elo = paddr | TLBLO_DIRTY | TLBLO_VALID;
	if (as->text_seg_loaded && faultaddress >= vbase1 && faultaddress < vtop1) {
		elo &= ~TLBLO_DIRTY;
	}
	tlb_random(ehi, elo);
	#endif

	splx(spl);
	return 0;
}

struct addrspace *
as_create(void)
{
	struct addrspace *as = kmalloc(sizeof(struct addrspace));
	if (as==NULL) {
		return NULL;
	}

	as->as_vbase1 = 0;
	as->as_pbase1 = 0;
	as->as_npages1 = 0;
	as->as_vbase2 = 0;
	as->as_pbase2 = 0;
	as->as_npages2 = 0;
	as->as_stackpbase = 0;
	return as;
}

void
as_destroy(struct addrspace *as)
{
	free_pages_helper(as->as_pbase1);
	//kprintf("freeing pbase_2 \n");
	free_pages_helper(as->as_pbase2);
	kprintf("freeing as_stackpbase \n");
	free_pages_helper(as->as_stackpbase);
	kfree(as);
}

void
as_activate(void)
{
	int i, spl;
	struct addrspace *as;

	as = curproc_getas();
#ifdef UW
        /* Kernel threads don't have an address spaces to activate */
#endif
	if (as == NULL) {
		return;
	}

	/* Disable interrupts on this CPU while frobbing the TLB. */
	spl = splhigh();

	for (i=0; i<NUM_TLB; i++) {
		tlb_write(TLBHI_INVALID(i), TLBLO_INVALID(), i);
	}

	splx(spl);
}

void
as_deactivate(void)
{
	/* nothing */
}

int
as_define_region(struct addrspace *as, vaddr_t vaddr, size_t sz,
		 int readable, int writeable, int executable)
{
	size_t npages; 

	/* Align the region. First, the base... */
	sz += vaddr & ~(vaddr_t)PAGE_FRAME;
	vaddr &= PAGE_FRAME;

	/* ...and now the length. */
	sz = (sz + PAGE_SIZE - 1) & PAGE_FRAME;

	npages = sz / PAGE_SIZE;

	/* We don't use these - all pages are read-write */
	(void)readable;
	(void)writeable;
	(void)executable;

	if (as->as_vbase1 == 0) {
		as->as_vbase1 = vaddr;
		as->as_npages1 = npages;
		return 0;
	}

	if (as->as_vbase2 == 0) {
		as->as_vbase2 = vaddr;
		as->as_npages2 = npages;
		return 0;
	}

	/*
	 * Support for more than two regions is not available.
	 */
	kprintf("dumbvm: Warning: too many regions\n");
	return EUNIMP;
}

static
void
as_zero_region(paddr_t paddr, unsigned npages)
{
	bzero((void *)PADDR_TO_KVADDR(paddr), npages * PAGE_SIZE);
}

int
as_prepare_load(struct addrspace *as)
{
	KASSERT(as->as_pbase1 == 0);
	KASSERT(as->as_pbase2 == 0);
	KASSERT(as->as_stackpbase == 0);

	as->as_pbase1 = getppages(as->as_npages1);
	kprintf("address for pbase1 %p \n", (void *)as->as_pbase1);
	if (as->as_pbase1 == 0) {
		return ENOMEM;
	}

	as->as_pbase2 = getppages(as->as_npages2);
	kprintf("address for pbase2 %p \n", (void *)as->as_pbase2);
	if (as->as_pbase2 == 0) {
		return ENOMEM;
	}

	as->as_stackpbase = getppages(DUMBVM_STACKPAGES);
	kprintf("address for stackpbase %p \n", (void *)as->as_stackpbase);
	if (as->as_stackpbase == 0) {
		return ENOMEM;
	}
	
	as_zero_region(as->as_pbase1, as->as_npages1);
	as_zero_region(as->as_pbase2, as->as_npages2);
	as_zero_region(as->as_stackpbase, DUMBVM_STACKPAGES);

	return 0;
}

int
as_complete_load(struct addrspace *as)
{
	(void)as;
	return 0;
}

int
as_define_stack(struct addrspace *as, vaddr_t *stackptr)
{
	KASSERT(as->as_stackpbase != 0);

	*stackptr = USERSTACK;
	return 0;
}

int
as_copy(struct addrspace *old, struct addrspace **ret)
{
	struct addrspace *new;

	new = as_create();
	if (new==NULL) {
		return ENOMEM;
	}

	new->as_vbase1 = old->as_vbase1;
	new->as_npages1 = old->as_npages1;
	new->as_vbase2 = old->as_vbase2;
	new->as_npages2 = old->as_npages2;

	/* (Mis)use as_prepare_load to allocate some physical memory. */
	if (as_prepare_load(new)) {
		kprintf("MISUSE OF PREPARE LOAD \n");
		as_destroy(new);
		return ENOMEM;
	}

	KASSERT(new->as_pbase1 != 0);
	KASSERT(new->as_pbase2 != 0);
	KASSERT(new->as_stackpbase != 0);

	memmove((void *)PADDR_TO_KVADDR(new->as_pbase1),
		(const void *)PADDR_TO_KVADDR(old->as_pbase1),
		old->as_npages1*PAGE_SIZE);

	memmove((void *)PADDR_TO_KVADDR(new->as_pbase2),
		(const void *)PADDR_TO_KVADDR(old->as_pbase2),
		old->as_npages2*PAGE_SIZE);

	memmove((void *)PADDR_TO_KVADDR(new->as_stackpbase),
		(const void *)PADDR_TO_KVADDR(old->as_stackpbase),
		DUMBVM_STACKPAGES*PAGE_SIZE);
	
	*ret = new;
	return 0;
}
