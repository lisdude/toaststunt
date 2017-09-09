#include "extension-background.h"

/*
  An example of how to suspend tasks in the MOO until an external thread is finished doing work.

  Demonstrates:
     - Suspending tasks
     - Spawning external threads
     - Resuming tasks with data from external threads
*/


/* @forked will use the enumerator to find relevant tasks in your external queue, so everything we've spawned will need to return TEA_CONTINUE to get counted
 * The enumerator handles cases where you kill_task from inside the MOO. */
static task_enum_action
background_enumerator(task_closure closure, void *data)
{
    std::map<int, background_waiter*>::iterator it;
    for (it = background_process_table.begin(); it != background_process_table.end(); it++)
    {
        task_enum_action tea = (*closure) (it->second->the_vm, "waiting on thread", data);

        if (tea == TEA_KILL) {
            pthread_cancel(it->second->thread);
            deallocate_background_waiter(it->second);
        }
        if (tea != TEA_CONTINUE)
            return tea;
    }

    return TEA_CONTINUE;
}

// The thread that does all of the actual work. Receives a pointer to the relevant background_waiter struct.
void *background_thread(void *bw)
{
    background_waiter *w = (background_waiter*)bw;
    int wait = (w->args.v.list[0].v.num >= 2 ? w->args.v.list[2].v.num : 5);
    sleep(wait);
    Var v;
    v.type = TYPE_STR;
    if (w->args.v.list[0].v.num == 0)
        v.v.str = str_dup("Hello, world.");
    else
        v.v.str = str_dup(w->args.v.list[1].v.str);
    resume_task(w->the_vm, v);

    deallocate_background_waiter(w);

    pthread_exit(NULL);
}

// Creates the background_waiter struct and starts the worker thread.
static enum error
background_suspender(vm the_vm, void *data)
{
    background_waiter *w = (background_waiter*)data;
    w->the_vm = the_vm;

    // Create our worker thread and make the background_waiter aware of its existance
    int thread_error = 0;

    // Create our new worker thread as detached so that resources are immediately freed
    pthread_t new_thread;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    pthread_attr_setstacksize(&attr, 1000000);          // Stack size of 1 MB should be sufficient. 

    if ((thread_error = pthread_create(&new_thread, &attr, background_thread, data)))
    {
        errlog("Failed to create a background thread for bf_background. Error code: %i\n", thread_error);
        pthread_attr_destroy(&attr);
        deallocate_background_waiter(w);
        return E_QUOTA;
    } else {
        w->thread = new_thread;
        pthread_attr_destroy(&attr);
    }

    return E_NONE;
}

static package
bf_background(Var arglist, Byte next, void *vdata, Objid progr)
{
    // Make sure we don't overrun the background thread limit.
    if (next_background_handle > server_int_option("max_background_threads", MAX_BACKGROUND_THREADS))
    {
        errlog("No space left in the background process table. Aborting bf_background call...\n");
        return make_error_pack(E_QUOTA);
    }

    background_waiter *w = (background_waiter*)mymalloc(sizeof(background_waiter), M_TASK);
    initialize_background_waiter(w);
    w->args = arglist;

    return make_suspend_pack(background_suspender, (void*)w);
}

/********************************************************************************************************/

void deallocate_background_waiter(background_waiter *waiter)
{
    int handle = waiter->handle;
    free_var(waiter->args);
    myfree(waiter, M_TASK);
    background_process_table.erase(handle);

    if (background_process_table.size() == 0)
        next_background_handle = 1;
}

void initialize_background_waiter(background_waiter *waiter)
{
    // Insert the background_waiter into the process table for housekeeping.
    waiter->handle = next_background_handle;
    waiter->the_vm = NULL;
    background_process_table[next_background_handle] = waiter;
    next_background_handle++;
}


void
register_background()
{
    register_task_queue(background_enumerator);
    register_function("background", 0, 2, bf_background, TYPE_STR, TYPE_INT);
}
