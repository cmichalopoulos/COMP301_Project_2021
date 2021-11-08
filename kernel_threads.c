
#include "tinyos.h"
#include "kernel_sched.h"
#include "kernel_proc.h"
#include "kernel_cc.h"
#include "kernel_streams.h"


// Function  to create process threads, based on start_main_thread()
void start_process_thread(){
  int exitval;

  Task call = cur_thread()->ptcb->task;
  int argl = cur_thread()->ptcb->argl;
  void* args = cur_thread()->ptcb->args; 

  exitval = call(argl, args);
  ThreadExit(exitval);
}



/** 
  @brief Create a new thread in the current process.
  */
Tid_t sys_CreateThread(Task task, int argl, void* args)
{
  if(task != NULL){
    TCB* tcb = spawn_thread(CURPROC, start_process_thread);  // Initialize and return a new TCB
    CURPROC -> thread_count++;                                    // Increase by 1 the thread counter

    // Initialize arguments, like I did in sys_Exec
    PTCB* ptcb = (PTCB* )xmalloc(sizeof(PTCB));   // Memory allocation for a PTCB block 

    ptcb->task = task;
    ptcb->argl = argl;
    if(args != NULL){
      ptcb->args = args; 
    } else {
      ptcb->args = NULL;
    }
    ptcb->exitval = CURPROC->exitval;
    ptcb->exited = 0;
    ptcb->detached = 0;
    ptcb->exit_cv = COND_INIT;
    ptcb->refcount = 0;

    // Connections through PCB, PTCB, TCB
    ptcb->tcb = tcb; // PTCB with TCB
    tcb->ptcb = ptcb; // TCB with PTCB
    // tcb->owner_pcb = pcb->parent; // TCB with PCB, but must check this.

   
    // Dealing with rlnode list
    rlnode_init(&ptcb->ptcb_list_node, ptcb);                     //Initializing the rlnode in PTCB
    rlist_push_back(&CURPROC->ptcb_list, &ptcb->ptcb_list_node);  //Pushing back on the list the current PTCB element 
    
    wakeup(ptcb->tcb);
    return (Tid_t)ptcb;
  }
	return NOTHREAD;
}

/**
  @brief Return the Tid of the current thread.
 */
Tid_t sys_ThreadSelf()
{
	return (Tid_t) cur_thread()->ptcb;   // easy
}

/**
  @brief Join the given thread.
  */
int sys_ThreadJoin(Tid_t tid, int* exitval)
{
  PTCB* ptcb = (PTCB* ) tid;
  PTCB* new_ptcb;

  if(rlist_find(&CURPROC->ptcb_list, ptcb, NULL)!=NULL){
    new_ptcb = ptcb;
  } else {
    return -1;
  }

  // First, some checks to see if i can join
  // Cannot join the thread with the same tid as mine
  if((Tid_t) cur_thread()->ptcb == tid) {
    return -1;
  }
  // Cannot join if the thread is detached
  if(new_ptcb->detached == 1) {
    return -1;
  }
  // Cannot join if the thread is exited 
  if(new_ptcb->exited == 1) {
    return -1;
  }

  if(ptcb->tcb = cur_thread()){
    return -1;
  }

  if(ptcb->tcb = NULL){
    return -1;
  }


  //if(ptcb != NULL){                                                  // If ptcb exists AND
  // if (ptcb->tcb != NULL){                                          // If corresponding tcb exists AND
  //    if(ptcb->detached == 0) {                                      // it is not detached AND
  //      if(ptcb->tcb->owner_pcb == cur_thread()->owner_pcb){         // the joinee and the joiner are under the same process
          // If you passed the checks, congratulations, now you have to wait and sleep, until the thread running finishes its work
          // If there is a thread running right now, it has to put to sleep all other threads joining.
          if(new_ptcb->detached != 1 && new_ptcb->exited != 1){
            new_ptcb->refcount++; // there is a new ptcb in town 
            kernel_wait(&new_ptcb->exit_cv, SCHED_USER);      // kernel_wait puts to temporary sleep incoming threads, waiting for the condvar of current thread to become detached or exited
            new_ptcb->refcount--; // ptcb has left the chat
          }
        
  //    }
  // }
  //}
    // Check case when thread is alone in process
    if(ptcb->refcount==0){
      rlist_remove(&new_ptcb->ptcb_list_node);
      free(new_ptcb);
    }
    if(new_ptcb->exited == 1){
      if(exitval!=NULL){
        exitval = &new_ptcb->exitval;
      }
    }

	return 0;
}

/**
  @brief Detach the given thread.
  */
int sys_ThreadDetach(Tid_t tid)
{
  // Finding the thread to join, connecting it with ptcb
  PTCB* ptcb = (PTCB* ) tid;
  PTCB* new_ptcb;

  if(rlist_find(&CURPROC->ptcb_list, ptcb, NULL) != NULL){
    new_ptcb = ptcb;
  } else {
    return -1;
  }

  // Detaching current and waking up next in line...
  new_ptcb->detached == 1;  //Current thread you are detached, free to go
  if(new_ptcb->refcount>0) {
    kernel_broadcast(&new_ptcb->exit_cv); // wakeup from your sleep, time to work 
    ptcb->refcount == 0;  //nobody will join you 
  }
	return -1;
}

/**
  @brief Terminate the current thread.
  */
void sys_ThreadExit(int exitval)
{

  PTCB* ptcb = cur_thread()->ptcb;

  if(ptcb != NULL){
    ptcb->exitval = exitval;
    ptcb->exited = 1; 
    PCB* curproc = CURPROC;
    kernel_broadcast(&ptcb->exit_cv);
  
  if(cur_thread()->ptcb->refcount = 0){
    free(cur_thread()->ptcb);
    curproc->thread_count --;
  }
  else{
      if(curproc->thread_count==0){
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
    }
  }

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
}
  /* Bye-bye cruel world */
  kernel_sleep(EXITED, SCHED_USER);

}

