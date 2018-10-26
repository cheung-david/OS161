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
#include "opt-A2.h"

  /* this implementation of sys__exit does not do anything with the exit code */
  /* this needs to be fixed to get exit() and waitpid() working properly */

void sys__exit(int exitcode) {

  struct addrspace *as;
  struct proc *p = curproc;
  /* for now, just include this to keep the compiler from complaining about
     an unused variable */
  (void)exitcode;

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
  childProc->parentId = p->pId;
  if(childProc == NULL) {
    DEBUG(DB_SYSCALL, "fork error, unable to make new process.\n");
    return ENOMEM;
  }

  // Copy address space from parent (curProcess) to child
  as_copy(curproc_getas(), &(childProc->p_addrspace));

  if(childProc->p_addrspace == NULL) {
    DEBUG(DB_SYSCALL, "error copying address space from parent to child \n");
    proc_destroy(childProc);
    return ENPROC;
  }
  as_activate();
  //curproc_setas(childProc->p_addrspace);

  struct trapframe *newTf = kmalloc(sizeof(struct trapframe));
  if(newTf == NULL) {
    DEBUG(DB_SYSCALL, "error creating trap frame\n");
    proc_destroy(childProc);
    return ENOMEM;
  }
  memcpy(newTf,curTf, sizeof(struct trapframe));
  DEBUG(DB_SYSCALL, "trap frame copied\n");

  int err = thread_fork(curthread->t_name, childProc, &enter_forked_process, (void *)newTf, 0);
  if(err) {
    kfree(newTf);
    proc_destroy(childProc);
    return err;
  }

  *retval = childProc->pId;
  return 0;
}
#endif

/* stub handler for getpid() system call                */
int
sys_getpid(pid_t *retval)
{
  /* for now,  this is just a stub that always returns a PID of 1 */
  /* you need to fix this to make it work properly */
  *retval = curproc->pId;
  return(0);
}

/* stub handler for waitpid() system call                */

int
sys_waitpid(pid_t pid,
	    userptr_t status,
	    int options,
	    pid_t *retval)
{
  int exitstatus;
  int result;

  /* this is just a stub implementation that always reports an
     exit status of 0, regardless of the actual exit status of
     the specified process.   
     In fact, this will return 0 even if the specified process
     is still running, and even if it never existed in the first place.

     Fix this!
  */

  if (options != 0) {
    return(EINVAL);
  }
  /* for now, just pretend the exitstatus is 0 */
  exitstatus = 0;
  result = copyout((void *)&exitstatus,status,sizeof(int));
  if (result) {
    return(result);
  }
  *retval = pid;
  return(0);
}

