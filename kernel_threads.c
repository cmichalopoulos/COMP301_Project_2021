#include "tinyos.h"
#include "kernel_sched.h"
#include "kernel_streams.h"
#include "kernel_proc.h"
#include "kernel_cc.h"

/**
  @brief New start thread, for creating new thread in the current process
  */
void start_new_thread()
{
  int exitval;

  Task call =  cur_thread()->ptcb->task;
  int argl = cur_thread()->ptcb->argl;
  void* args = cur_thread()->ptcb->args;

  exitval = call(argl,args);
  sys_ThreadExit(exitval);
}

/** 
  @brief Create a new thread in the current process.

  The new thread is executed in the same process as 
  the calling thread. If this thread returns from function
  task, the return value is used as an argument to 
  `ThreadExit`.

  The new thread is created by executing function `task`,
  with the arguments of `argl` and `args` passed to it.
  Note that, unlike `Exec`, where argl and args must define
  a byte buffer, here there is no such requirement! 
  The two arguments are passed to the new thread verbatim,
  and can be unrelated. It is the responsibility of the
  programmer to define their meaning.

  @param task a function to execute

  */
Tid_t sys_CreateThread(Task task, int argl, void* args)
{
  // Aquire current process global system variable
  PCB * curproc_pcb = CURPROC;
  // TCB main thread
  TCB* new_tcb = spawn_thread(curproc_pcb, start_new_thread);
  //Acquiring a new PTCB, by allocating in parallel mem for it
  PTCB* new_ptcb = (PTCB*)xmalloc(sizeof(PTCB));
  //Making connections with PCB, TCB
  new_ptcb->task = task;
  new_ptcb->argl = argl;
  new_ptcb->args = args;

  new_ptcb->exited = 0;
  new_ptcb->detached = 0;
  new_ptcb->exit_cv = COND_INIT;
  new_ptcb->refcount = 0;

  rlnode_init (&(new_ptcb->ptcb_list_node), new_ptcb);
  rlist_push_back(&(curproc_pcb->ptcb_list), &(new_ptcb->ptcb_list_node));
  new_ptcb->tcb = new_tcb;
  new_tcb->ptcb = new_ptcb;
  curproc_pcb->thread_count += 1;
  wakeup(new_ptcb->tcb);
  
  return (Tid_t)new_ptcb;
}

/**
  @brief Return the Tid of the current thread. DONE
 */
Tid_t sys_ThreadSelf()
{
  return (Tid_t) cur_thread()->ptcb;
}

/**
  @brief Join the given thread.

  This function will wait for the thread with the given
  tid to exit, and return its exit status in `*exitval`.
  The tid must refer to a legal thread owned by the same
  process that owns the caller. Also, the thread must 
  be undetached, or an error is returned.

  After a call to join succeeds, subsequent calls will fail
  (unless tid was re-cycled to a new thread). 

  It is possible that multiple threads try to join the
  same thread. If these threads block, then all must return the
  exit status correctly.

  @param tid the thread to join
  @param exitval a location where to store the exit value of the joined 
              thread. If NULL, the exit status is not returned.
  @returns 0 on success and -1 on error. Possible errors are:
    - there is no thread with the given tid in this process.
    - the tid corresponds to the current thread.
    - the tid corresponds to a detached thread.

  */
int sys_ThreadJoin(Tid_t tid, int* exitval)
{
  PTCB* ptcb = (PTCB*)tid;

    if(!rlist_find(&CURPROC->ptcb_list,ptcb,NULL)){
      return-1;
    }

    if((ptcb == NULL) || (ptcb->detached == 1) || (ptcb->tcb == cur_thread()) || (ptcb->tcb == NULL)){
      return -1;
    }else {
      
      ptcb->refcount += 1;
    
      // Loop for checking exited or detached condition
      while((ptcb->exited == 0) && (ptcb->detached == 0)){
        kernel_wait(&(ptcb->exit_cv), SCHED_USER);
      }

      ptcb->refcount = ptcb->refcount - 1;

      if (ptcb->refcount <= 0){
        rlist_remove(&(ptcb->ptcb_list_node));
        free(ptcb);
      }

      if (exitval != NULL) {
        *exitval = ptcb->exitval;
        return 0;
      }
    }
  
}

/**
  @brief Detach the given thread.

  This function makes the thread tid a detached thread.
  A detached thread is not joinable (ThreadJoin returns an
  error). 

  Once a thread has exited, it cannot be detached. A thread
  can detach itself.

  @param tid the tid of the thread to detach
  @returns 0 on success, and -1 on error. Possibe errors are:
    - there is no thread with the given tid in this process.
    - the tid corresponds to an exited thread.
    */
int sys_ThreadDetach(Tid_t tid)
{
  PTCB* ptcb = (PTCB*) tid;

  if(!rlist_find(&CURPROC->ptcb_list,ptcb,NULL) || (ptcb->tcb->state == EXITED)) {
    return-1;
  }else {
    ptcb->detached = 1;
    kernel_broadcast(&(ptcb->exit_cv));
    return 0;
  }
}

/**
  @brief Terminate the current thread.
  */
void sys_ThreadExit(int exitval)
{

  PCB* curproc = CURPROC;  /* cache for efficiency */
  PTCB* ptcb = cur_thread()->ptcb;

  if(curproc->thread_count <= 0) {
    /* Reparent any children of the exiting process to the 
       initial task */
    PCB* initpcb = get_pcb(1);
    while(!is_rlist_empty(& curproc->children_list)) {
      rlnode* child = rlist_pop_front(& curproc->children_list);
      child->pcb->parent = initpcb;
      rlist_push_front(& initpcb->children_list, child);
    }

    /* Add exited children to the initial task's exited list 
       and signal the initial task */
    if(!is_rlist_empty(& curproc->exited_list)) {
      rlist_append(& initpcb->exited_list, &curproc->exited_list);
      kernel_broadcast(& initpcb->child_exit);
    }

    /* Put me into my parent's exited list */
    rlist_push_front(& curproc->parent->exited_list, &curproc->exited_node);
    kernel_broadcast(& curproc->parent->child_exit);


    assert(is_rlist_empty(& curproc->children_list));
    assert(is_rlist_empty(& curproc->exited_list));


    /* 
      Do all the other cleanup we want here, close files etc. 
    */

    /* Release the args data */
    if(curproc->args) {
      free(curproc->args);
      curproc->args = NULL;
    }

    /* Clean up FIDT */
    for(int i=0;i<MAX_FILEID;i++) {
      if(curproc->FIDT[i] != NULL) {
      FCB_decref(curproc->FIDT[i]);
      curproc->FIDT[i] = NULL;
      }
    }

    /* Disconnect my main_thread */
    curproc->main_thread = NULL;

    /* Now, mark the process as exited. */
    curproc->pstate = ZOMBIE;
    curproc->exitval = exitval;
  } else {
    ptcb->exited = 1;
    curproc->thread_count -= 1;
    kernel_broadcast(&(ptcb->exit_cv));
  }

  kernel_sleep(EXITED, SCHED_USER);
}

