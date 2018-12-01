#include <types.h>
#include <kern/errno.h>
#include <kern/unistd.h>
#include <kern/wait.h>
#include <lib.h>
#include <syscall.h>
#include <current.h>
#include <proc.h>
#include <thread.h>
#include <addrspace.h>
#include <copyinout.h>
#include <mips/trapframe.h>
#include <synch.h>
#include "opt-A2.h"
#include <vfs.h>
#include <vm.h>
#include <test.h>
#include <kern/fcntl.h>
  /* this implementation of sys__exit does not do anything with the exit code */
  /* this needs to be fixed to get exit() and waitpid() working properly */

void sys__exit(int exitcode) {
  struct addrspace *as;
  struct proc *p = curproc;
  #if OPT_A2
  lock_acquire(waitPidLock);
  struct procEntry *curEntry = getProcess(p->pId);
  curEntry->exitCode = exitcode;
  if(curEntry->parentId != P_NOID) {
    //struct procEntry *parent = getProcess(curEntry->parentId);
    //if(parent != NULL) {
      curEntry->status = P_ZOMBIE;
      cv_broadcast(ptCV, waitPidLock);
    //}
  }
  curEntry->status = P_EXIT;
  //removeProcess(curEntry->pid);

  lock_release(waitPidLock);
  #endif
  DEBUG(DB_SYSCALL,"Syscall: _exit(%d)\n",exitcode);

  KASSERT(curproc->p_addrspace != NULL);
  as_deactivate();
  /*
   * clear p_addrspace before calling as_destroy. Otherwise if
   * as_destroy sleeps (which is quite possible) when we
   * come back we'll be calling as_activate on a
   * half-destroyed address space. This tends to be
   * messily fatal.
   */
  as = curproc_setas(NULL);
  as_destroy(as);

  /* detach this thread from its process */
  /* note: curproc cannot be used after this call */
  proc_remthread(curthread);

  /* if this is the last user process in the system, proc_destroy()
     will wake up the kernel menu thread */
  proc_destroy(p);
  
  thread_exit();
  /* thread_exit() does not return, so we should never get here */
  panic("return from thread_exit in sys_exit\n");
}


#if OPT_A2
pid_t 
sys_fork(struct trapframe *curTf, pid_t *retval) {
  struct proc *p = curproc;
  // Create new process and assign process id to child
  struct proc *childProc = proc_create_runprogram(p->p_name);
  struct procEntry *childEntry = getProcess(childProc->pId);
  childEntry->parentId = p->pId;
  if(childProc == NULL) {
    DEBUG(DB_SYSCALL, "fork error, unable to make new process.\n");
    return ENOMEM;
  }

  // Copy address space from parent (curProcess) to child
  as_copy(curproc_getas(), &(childProc->p_addrspace));

  if(childProc->p_addrspace == NULL) {
    DEBUG(DB_SYSCALL, "error copying address space from parent to child \n");
    kfree(childEntry);
    proc_destroy(childProc);
    return ENPROC;
  }
  //as_activate();
  //curproc_setas(childProc->p_addrspace);

  struct trapframe *newTf = kmalloc(sizeof(struct trapframe));
  if(newTf == NULL) {
    DEBUG(DB_SYSCALL, "error creating trap frame\n");
    kfree(childEntry);
    proc_destroy(childProc);
    return ENOMEM;
  }
  memcpy(newTf,curTf, sizeof(struct trapframe));
  DEBUG(DB_SYSCALL, "trap frame copied\n");

  int err = thread_fork(curthread->t_name, childProc, &enter_forked_process, (void *)newTf, 0);
  if(err) {
    kfree(newTf);
    kfree(childEntry);
    proc_destroy(childProc);
    return err;
  }

  *retval = childProc->pId;
  return 0;
}
#endif

#if OPT_A2
/* stub handler for getpid() system call                */
int
sys_getpid(pid_t *retval)
{
  /* for now,  this is just a stub that always returns a PID of 1 */
  /* you need to fix this to make it work properly */
  *retval = curproc->pId;
  return(0);
}
#endif

/* stub handler for waitpid() system call                */
#if OPT_A2
int
sys_waitpid(pid_t pid,
	    userptr_t status,
	    int options,
	    pid_t *retval)
{
  int exitstatus;
  int result;

  if (options != 0) {
    return(EINVAL);
  }

  lock_acquire(waitPidLock);
  struct procEntry *childProc = getProcess(pid);

  if(childProc == NULL || childProc->parentId != curproc->pId) {
    lock_release(waitPidLock);
    return ECHILD;
  }

  // if (options == WNOHANG && childProc->status != P_EXIT) {
  //   lock_release(waitPidLock);
  //   return ECHILD;
  // }
  DEBUG(DB_SYSCALL, "waiting for PID %d \n", pid);
  while(childProc->status == P_RUN) {
    DEBUG(DB_SYSCALL, "waiting for child %d \n", childProc->pId);
    cv_wait(ptCV, waitPidLock);
  }
  DEBUG(DB_SYSCALL, "free from PID %d \n", pid);
  exitstatus = childProc->exitCode;

  exitstatus = _MKWAIT_EXIT(exitstatus);
  result = copyout((void *)&exitstatus,status,sizeof(int));
  if (result) {
    lock_release(waitPidLock);
    return(result);
  }
  *retval = pid;
  lock_release(waitPidLock);
  return(0);
}
#endif

#if OPT_A2
int sys_execv(const char *program, char **args) {
 //kprintf("SYS_execv\n");
    struct addrspace *as;
    struct vnode *v;
    vaddr_t entrypoint, stackptr;
    int result;

    if(progname == NULL)
    {
        *retval = -1;
        return EFAULT;
    }

    //copy in progname to pname
    size_t pnameLen = strlen(progname);
    size_t pnameAllocSize = (pnameLen + 1) * sizeof(char);

    char *pname =  (char *)kmalloc(pnameAllocSize);
    if(pname == NULL)
    {
        *retval = -1;
        return ENOMEM;
    }

    result = copyin((const_userptr_t)progname, (void *)pname, pnameAllocSize);
    if(result)
    {
        kfree(pname);
        *retval = -1;
        return result;
    }

    //count args
    int argc = 0;
    while(args[argc] != NULL)
    {
        ++argc;
    }

    //copy in args to argv

    char **argv = (char **)kmalloc((argc + 1) * sizeof(char *));
    if(argv == NULL)
    {
        kfree(pname);
        *retval = -1;
        return ENOMEM;
    }

    for(int i = 0; i < argc; ++i)
    {
        size_t len = strlen(args[i]) + 1;
        argv[i] = (char *)kmalloc(len * sizeof(char));
        if(argv[i] == NULL)
        {
            kfree(pname);
            for(int j = 0; j<i; ++j)
            {
                kfree(argv[j]);
            }
            kfree(argv);
            *retval = -1;
            return ENOMEM;
        }
        result = copyin((const_userptr_t)args[i],argv[i],len * sizeof(char));
        if(result)
        {
            kfree(pname);
            for(int j = 0; j<=i; ++j)
            {
                kfree(argv[j]);
            }
            kfree(argv);
            *retval = -1;
            return result;
        }        
    }
    argv[argc] = NULL;
    
    //open file
    result = vfs_open(pname, O_RDONLY, 0, &v);
    if(result)
    {
        kfree(pname);

        for(int i = 0; i < argc; ++i)
        {
            kfree(argv[i]);
        }
        kfree(argv);
        *retval = -1;
        return result;
    }

    //create address space
    as = as_create();
    if(as == NULL)
    {
        kfree(pname);
        for(int i = 0; i < argc; ++i)
        {
            kfree(argv[i]);
        }
        kfree(argv);
        vfs_close(v);
        *retval = -1;
        return ENOMEM;
    }

    struct addrspace *oldas = curproc_setas(as);

    //load elf
    result = load_elf(v,&entrypoint);
    if(result)
    {
        kfree(pname);
        for(int i = 0; i < argc; ++i)
        {
            kfree(argv[i]);
        }
        kfree(argv);
        as_deactivate();
        as = curproc_setas(oldas);
        as_destroy(as);
        vfs_close(v);
        *retval = -1;
        return result;
    }

    
    vfs_close(v);
    kfree(pname);


    //define user stack
    result = as_define_stack(as,&stackptr);
    if(result)
    {
        for(int i = 0; i < argc; ++i)
        {
            kfree(argv[i]);
        }
        kfree(argv);
        as_deactivate();
        as = curproc_setas(oldas);
        as_destroy(as);
        *retval = -1;
        return result;
    }

        
    /*------copy args to user stack-----------*/

    vaddr_t *argPtrs = (vaddr_t *)kmalloc((argc + 1) * sizeof(vaddr_t));
    if(argPtrs == NULL)
    {
        for(int i = 0; i < argc; ++i)
        {
            kfree(argv[i]);
        }
        kfree(argv);
        as_deactivate();
        as = curproc_setas(oldas);
        as_destroy(as);
        *retval = -1;
        return ENOMEM;
    }

    for(int i = argc-1; i >= 0; --i)
    {
        //arg length with null
        size_t curArgLen = strlen(argv[i]) + 1;
        
        size_t argLen = ROUNDUP(curArgLen,4);
            
        stackptr -= (argLen * sizeof(char));
        
        //kprintf("copying arg: %s to addr: %p\n", temp, (void *)stackptr);

        //copy to stack
        result = copyout((void *) argv[i], (userptr_t)stackptr, curArgLen);
        if(result)
        {
            kfree(argPtrs);
            for(int i = 0; i < argc; ++i)
            {
                kfree(argv[i]);
            }
            kfree(argv);
            as_deactivate();
            as = curproc_setas(oldas);
            as_destroy(as);
            *retval = -1;
            return result;
        }
        
        argPtrs[i] = stackptr;        
    }    
        
    argPtrs[argc] = (vaddr_t)NULL;
    
    //copy arg pointers
    for(int i = argc; i >= 0; --i)
    {
        stackptr -= sizeof(vaddr_t);
        result = copyout((void *) &argPtrs[i], ((userptr_t)stackptr),sizeof(vaddr_t));
        if(result)
        {
            kfree(argPtrs);
            for(int i = 0; i < argc; ++i)
            {
                kfree(argv[i]);
            }
            kfree(argv);
            as_deactivate();
            as = curproc_setas(oldas);
            as_destroy(as);
            *retval = -1;
            return result;
        }
    }
    
    
    kfree(argPtrs);



    
    vaddr_t baseAddress = USERSTACK;

    vaddr_t argvPtr = stackptr;

    vaddr_t offset = ROUNDUP(USERSTACK - stackptr,8);

    stackptr = baseAddress - offset;

/*
    for(vaddr_t i = baseAddress; i >= stackptr; --i)
    {
        char *temp;
        temp = (char *)i;
        //kprintf("%p: %c\n",(void *)i,*temp);
        kprintf("%p: %x\n", (void *)i, *temp & 0xff);
    }
*/
    

    /*-done-copy args to user stack-----------*/

    for(int i = 0; i < argc; ++i)
    {
        kfree(argv[i]);
    }
    kfree(argv);
    
    as_deactivate();
    as_destroy(oldas);
    as_activate();
    
    //enter new process
    enter_new_process(argc,(userptr_t)argvPtr,
                      stackptr, entrypoint);

    
    panic("enter_new_process returned\n");
    return EINVAL;;
}
#endif
