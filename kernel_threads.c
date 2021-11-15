
#include "tinyos.h"
#include "kernel_sched.h"
#include "kernel_proc.h"
#include "kernel_cc.h"
#include "kernel_streams.h"
#include "kernel_sys.h"
#include "util.h"


// Function  to create process threads, based on start_main_thread()
void start_process_thread(){
  int exitval;

  Task task = (cur_thread()->ptcb->task);
  int argl = (cur_thread()->ptcb->argl);
  void* args = (cur_thread()->ptcb->args); 

  exitval = task(argl, args);
  sys_ThreadExit(exitval);
}

/** 
  @brief Create a new thread in the current process.
  */
Tid_t sys_CreateThread(Task task, int argl, void* args)
{
   // PCB* curproc_pcb = CURPROC;
   // TCB* tcb = spawn_thread(CURPROC, start_process_thread);  // Initialize and return a new TCB
   // CURPROC -> thread_count++;                                    // Increase by 1 the thread counter
   // Initialize arguments, like I did in sys_Exec

      PTCB* ptcb = (PTCB* )xmalloc(sizeof(PTCB));   // Memory allocation for a PTCB block 
     
      ptcb->task = task;
      ptcb->argl = argl;
      ptcb->args = args;

      ptcb->exited = 0;
      ptcb->detached = 0;
      ptcb->exit_cv = COND_INIT;
      ptcb->refcount = 1;
      
      // Dealing with rlnode list
      rlnode_init(&(ptcb->ptcb_list_node), ptcb);                     //Initializing the rlnode in PTCB
      
      if(task != NULL){
      
      TCB* new_tcb = spawn_thread(CURPROC, start_process_thread);  // Initialize and return a new TCB
      // Connections through PTCB, TCB
      ptcb->tcb = new_tcb; // PTCB with new_TCB
      new_tcb->ptcb = ptcb; // new_TCB with PTCB
      
      PCB* curproc = CURPROC;
      curproc->thread_count ++;

      rlist_push_back(&(curproc->ptcb_list), &(ptcb->ptcb_list_node));  //Pushing back on the list the current PTCB element 
    
      wakeup(new_tcb);
      return (Tid_t)ptcb;
    }
    return -1;
}

/**
  @brief Return the Tid of the current thread.
 */
Tid_t sys_ThreadSelf()
{
	return (Tid_t) (cur_thread()->ptcb);   // easy
}

/**
  @brief Join the given thread.
  */
int sys_ThreadJoin(Tid_t tid, int* exitval)
{
  // Locate locally a ptcb
  PTCB* ptcb = (PTCB* ) tid;
  if(!rlist_find(&(CURPROC->ptcb_list), ptcb, NULL)){
    return -1;
  }

  // First, some checks to see if i can join
  if((Tid_t) (cur_thread()->ptcb) == tid){      // Cannot join self obviously...
    return -1;
  }
  if (ptcb->detached == 1 && ptcb->exited == 1) {
    return -1;
  } 

  // Check for test_detach_other
  if(ptcb->detached == 1){
    if(ptcb->exitval == 0) {
      return -1;
    }
  }
	
  // If you passed the checks, congratulations, now you have to wait and sleep, until the thread running finishes its work
  // If there is a thread running right now, it has to put to sleep all other threads joining.
  while(ptcb->detached == 0  && ptcb->exited == 0){
      	ptcb->refcount = ptcb->refcount + 1; // there is a new ptcb in town 
	kernel_wait(&(ptcb->exit_cv), SCHED_USER);      // kernel_wait puts to temporary sleep incoming threads, waiting for the condvar of current thread to become detached or exited
  	ptcb->refcount = ptcb->refcount - 1; // ptcb has left the chat
  }
  
  //if(exitval!=NULL){
  //    exitval = &(ptcb->exitval);
  //    return 0;
  //} // else {exitval = NULL;}
  
  // Check case when thread is alone in process
  if(ptcb->refcount == 0){
    rlist_remove(&(ptcb->ptcb_list_node));
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
  if(!rlist_find(&CURPROC->ptcb_list, ptcb, NULL)){
    return -1;
  }

  if(ptcb->exited == 0){
    if(ptcb->refcount>0){
      // Detaching current and waking up next in line...
      ptcb->detached = 1;
      kernel_broadcast(&(ptcb->exit_cv)); // wakeup from your sleep, time to work 
      ptcb->refcount = 0;  //nobody will join you 
    }
   
   //Current thread you are detached, free to go
   // ptcb->detached == 1;
   // free(ptcb);
    return 0;
}
return -1;

}

/**
  @brief Terminate the current thread.
  */
void sys_ThreadExit(int exitval)
{

PTCB* ptcb = (cur_thread()->ptcb);

if(ptcb!=NULL){
  ptcb->exited = 1;
  ptcb->exitval = exitval;

  PCB* curproc = CURPROC;

  kernel_broadcast(&ptcb->exit_cv);
  CURPROC->thread_count--;

  // See you soon...
  if(cur_thread()->ptcb->refcount == 0){
    free(cur_thread()->ptcb);
    curproc->thread_count = curproc->thread_count -1;
  }

  if(get_pid(CURPROC) == 1){
    while(sys_WaitChild(NOPROC, NULL) != NOPROC);
  } else {
      // If you are the final thread
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
  curproc->exitval = exitval;
}

  
  /* Bye-bye cruel world */
  kernel_sleep(EXITED, SCHED_USER);

}

