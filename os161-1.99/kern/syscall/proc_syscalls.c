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

  lock_acquire(waitPidLock);
  //int* availPid = kmalloc(sizeof(int));
  //*availPid = curEntry->pId;
  //q_addtail(openEntries, availPid);
  lock_release(waitPidLock);
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
  struct addrspace *as;
  struct addrspace *new_as;
  struct vnode *v;
  vaddr_t entrypoint, stackptr;
  int result;

  int argc = 0;

  if(program == NULL) {
    return EFAULT;
  }
  // Count number of arguments
  while(args[argc] != NULL) {
    argc++;
  }

  char **kernelargs = kmalloc(sizeof(char) * (argc + 1));

  // Copy arguments into the kernel
  for(int i = 0; i < argc; i++) {
    kernelargs[i] = kmalloc(sizeof(char) * (strlen(args[i]) + 1));
    result = copyinstr((userptr_t) args[i], kernelargs[i], strlen(args[i]) + 1, NULL);
    if(result) {
      return result;
    }
  }
  kernelargs[argc] = NULL;

  // Copy program path into kernel
  char* progpath;
  progpath = kstrdup(program);

  /* Open the file. */
  result = vfs_open(progpath, O_RDONLY, 0, &v);
  kfree(progpath);
  if (result) {
    return result;
  }

  /* Create a new address space. */
  new_as = as_create();
  if (new_as ==NULL) {
    vfs_close(v);
    return ENOMEM;
  }

  /* Switch to it and activate it. */
  curproc_setas(new_as);
  as_activate();

  /* Load the executable. */
  result = load_elf(v, &entrypoint);
  if (result) {
    /* p_addrspace will go away when curproc is destroyed */
    vfs_close(v);
    curproc_setas(as);
    return result;
  }

  /* Done with the file now. */
  vfs_close(v);



  /* Define the user stack in the address space */
  result = as_define_stack(new_as, &stackptr);
  if (result) {
    /* p_addrspace will go away when curproc is destroyed */
    return result;
  }

  // Alignment stack pointer
  while(stackptr % 8 != 0) {
    stackptr--;
  }

  vaddr_t addrparams[argc + 1]; 
  // Copy arguments to new address space
  for(int i = argc - 1 ; i >= 0; i--) {
    stackptr -= strlen(kernelargs[i]) + 1;
    int err = copyoutstr(kernelargs[i], (userptr_t) stackptr, strlen(kernelargs[i]) + 1, NULL);
    if(err) {
      return err;
    }
    addrparams[i] = stackptr;
  } 

  addrparams[argc] = 0;

  // Align character pointers
  while(stackptr % 4 != 0) {
    stackptr--;
  }

  for(int j = argc; j >= 0; j--) {
    stackptr -= ROUNDUP(sizeof(vaddr_t), 4);
    int err = copyout(&addrparams[j], (userptr_t)stackptr, sizeof(vaddr_t));
    if(err) {
      return err;
    }
  }

  as_destroy(as);

  /* Warp to user mode. */
   enter_new_process(argc /*argc*/, (userptr_t) stackptr /*userspace addr of argv*/,
        stackptr, entrypoint);
  
  /* enter_new_process does not return. */
  panic("enter_new_process returned\n");
  return EINVAL;
}
#endif
