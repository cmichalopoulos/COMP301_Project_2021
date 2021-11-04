
#include "tinyos.h"
#include "kernel_sched.h"
#include "kernel_proc.h"

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
	return (Tid_t) cur_thread()->ptcb;
}

/**
  @brief Join the given thread.
  */
int sys_ThreadJoin(Tid_t tid, int* exitval)
{
	return -1;
}

/**
  @brief Detach the given thread.
  */
int sys_ThreadDetach(Tid_t tid)
{
	return -1;
}

/**
  @brief Terminate the current thread.
  */
void sys_ThreadExit(int exitval)
{

}

