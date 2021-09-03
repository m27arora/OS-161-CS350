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

#include "opt-A2.h"

#include <kern/fcntl.h>
#include <vm.h>
#include <test.h>

#if OPT_A2
#include <mips/trapframe.h>
#include <synch.h>
#include <vfs.h>
#endif


  /* this implementation of sys__exit does not do anything with the exit code */
  /* this needs to be fixed to get exit() and waitpid() working properly */

void sys__exit(int exitcode) {

  struct addrspace *as;
  struct proc *p = curproc;
  /* for now, just include this to keep the compiler from complaining about
     an unused variable */
  (void)exitcode;

  DEBUG(DB_SYSCALL,"Syscall: _exit(%d)\n",exitcode);

#if OPT_A2

lock_acquire(curproc->proc_lock);
  
  for(unsigned int i=0; i<array_num(curproc->children); i++) {

    struct proc *child = array_get(curproc->children, i);

 if(child){
    lock_acquire(child->proc_lock);
      if(!child->isAlive) { //zombie child
    lock_release(child->proc_lock);
    proc_destroy(child);
      }
      else{
        child->parentProc = NULL;
        lock_release(child->proc_lock);
        }
  }
  }

p->isAlive = false;
p->exit_code = exitcode;
lock_release(curproc->proc_lock);


//x is the process that called exit
//if parent of x is alive, make x a zombie
//if parent is not alive (dead/zombie), fully delete x
//while deleting x, check x's children. if there is any zombie child, fully delete that child
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
   
 // proc_destroy(p);
 if(p->parentProc){//do not destroy p

  lock_acquire(p->parentProc->proc_lock);
  if(p->parentProc->isAlive) {
    lock_release(p->parentProc->proc_lock);
    cv_signal(p->proc_cv, p->proc_lock);
    }
    else{
      lock_release(p->parentProc->proc_lock);
      proc_destroy(p);
      }
  }
  else{ //destroy p
    proc_destroy(p);
  }

#else
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

#endif

  thread_exit();
  /* thread_exit() does not return, so we should never get here */
  panic("return from thread_exit in sys_exit\n");
}

/* stub handler for getpid() system call                */
int
sys_getpid(pid_t *retval)
{
  /* for now, this is just a stub that always returns a PID of 1 */
  /* you need to fix this to make it work properly */
#if OPT_A2
  *retval = curproc->proc_pid;
#else
  *retval = 1;
#endif
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

#if OPT_A2
  bool checkPid = false;

lock_acquire(curproc->proc_lock);
  struct proc *child = NULL;
  for(unsigned int i=0; i<array_num(curproc->children); i++) {
   child = array_get(curproc->children, i);
   if(child!=NULL && child->proc_pid == pid) {
     checkPid = true;
     break;
   }
  }
lock_release(curproc->proc_lock);
//acquire lock of child process to check if child is alive
  if(checkPid){
    lock_acquire(child->proc_lock);
    while(child->isAlive) {
      cv_wait(child->proc_cv, child->proc_lock);
      }
      lock_release(child->proc_lock);
    //go to sleep on childs cv
    }

   // if child is not alive, fully destroy the child before exiting the func

  //check if pid is one of your children, if not, return error code just to check
  //if it is your child, check if child is alive or not
  // give every process cv and lock
exitstatus = _MKWAIT_EXIT(child->exit_code);
#else

exitstatus = 0;

#endif


  if (options != 0) {
    return(EINVAL);
  }
  /* for now, just pretend the exitstatus is 0 */
 // exitstatus = 0;
  result = copyout((void *)&exitstatus,status,sizeof(int));
  if (result) {
    return(result);
  }
  *retval = pid;
  return(0);
}

#if OPT_A2
int
sys_fork(pid_t *retval, struct trapframe *tf_p) {

  struct proc *childProc = proc_create_runprogram(curproc->p_name);
  as_copy(curproc_getas(), &(childProc->p_addrspace));


// create realtionship array

  childProc->parentProc = curproc; //create field

  array_add(curproc->children, childProc, NULL);

  //trapframe

  struct trapframe *tmp_tf = kmalloc(sizeof(struct trapframe));
  memcpy(tmp_tf, tf_p, sizeof(struct trapframe));
  thread_fork(childProc->p_name, childProc, (void *) &enter_forked_process, tmp_tf, 0);

  *retval = childProc->proc_pid;
  return(0);
}


/*
 * Load program "progname" and start running it in usermode.
 * Does not return except on error.
 *
 * Calls vfs_open on progname and thus may destroy it.
 */
 
int
sys_execv(const char *programName, char **args)
{
	struct addrspace *as;
	struct vnode *v;
	vaddr_t entrypoint, stackptr;
	int result;
  size_t max_len = 128;
  size_t ptr = 5;
  size_t pntr = 5;

 //count args
 int count = 1;//for null terminator
  for(int i=0; args[i]!= NULL; i++) {
    count++;
  }
  //kprintf("number is %d\n", count-1);
  
  //print args

 char ** copy_args = kmalloc(count * sizeof(char));
  for(int i=0; i<count-1; i++) {
    copy_args[i] = kmalloc(128 * sizeof(char));
    copyinstr((const_userptr_t) (args[i]), copy_args[i], strlen(args[i]) + 1, &pntr);
   // kprintf(copy_args[i]);
  //  kprintf("\n");
  }

  //copy the prognam name into kernel
  char *progname = kmalloc (128 * sizeof(char));
  int copy_result = copyinstr((const_userptr_t) programName, progname, max_len , &ptr);

 KASSERT(copy_result == 0);

	/* Open the file. */
	result = vfs_open(progname, O_RDONLY, 0, &v);
	if (result) {
		return result;
	}

	/* We should be a new process. */
	//KASSERT(curproc_getas() == NULL);

	/* Create a new address space. */
	as = as_create();
	if (as ==NULL) {
		vfs_close(v);
		return ENOMEM;
	}

  KASSERT(curproc->p_addrspace != NULL);
  as_deactivate();
  struct addrspace* tempr = curproc_setas(NULL);
  as_destroy(tempr);

	/* Switch to it and activate it. */
	curproc_setas(as);
	as_activate();

	/* Load the executable. */
	result = load_elf(v, &entrypoint);
	if (result) {
		/* p_addrspace will go away when curproc is destroyed */
		vfs_close(v);
		return result;
	}

	/* Done with the file now. */
	vfs_close(v);

	/* Define the user stack in the address space */
	result = as_define_stack(as, &stackptr);
	if (result) {
		/* p_addrspace will go away when curproc is destroyed */
		return result;
	}

  //passing to user stack
 //passing to user stack
  vaddr_t top = stackptr;
  vaddr_t *addr_array = kmalloc((count) * 4);

  int i=count-1;
  top=top-4;

  addr_array[i]=(vaddr_t) NULL;
  i--;
  while(i>=0) {
   size_t len = strlen(copy_args[i])+1;
    top = top - len;
    copyoutstr(copy_args[i], (userptr_t) top, len+1, &ptr);
    addr_array[i] = top;
    i--;
  }

  top = top - 4;


  if((count%2) == 0) { //odd args
  top = ROUNDUP(top, 8);
  }
  else {
    top = ROUNDUP(top, 4);
    if(top%8 == 0){
      top=top-4;
      }
    }

  for (i = count-1; i >= 0; i--) {
    top = top - 4;
    copyout((void *) &addr_array[i], (userptr_t) top, 4);
	}

  /* Warp to user mode. */
	enter_new_process(count-1/*argc*/, (userptr_t) top /*userspace addr of argv*/, top,  entrypoint);

	/* enter_new_process does not return. */
	panic("enter_new_process returned\n");
	return EINVAL;
}

#endif
