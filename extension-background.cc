#include "extension-background.h"
#include "log.h"

/*
  A general-purpose extension for doing work in separate threads. The entrypoint (background_thread)
  will suspend the MOO task, create a thread, run the callback function on the thread, and then resume
  the MOO task with the return value from the callback thread. A sample function (background_test)
  is provided for demonstration purposes. Additionally, you can set $server_options.max_background_threads
  to limit the number of threads that the MOO can spawn at any given moment.

  Your callback function should periodically (or oftenly) check the status of the background_waiter's active
  member, which will indicate whether or not the MOO task has been killed or not. If active is 0, the task is dead
  and your function should clean up and not bother worrying about returning anything.

  Demonstrates:
     - Suspending tasks
     - Spawning external threads
     - Resuming tasks with data from external threads
*/


/* @forked will use the enumerator to find relevant tasks in your external queue, so everything we've spawned
 * will need to return TEA_CONTINUE to get counted. The enumerator handles cases where you kill_task from inside the MOO. */
static task_enum_action
background_enumerator(task_closure closure, void *data)
{
    std::map<int, background_waiter*>::iterator it;
    for (it = background_process_table.begin(); it != background_process_table.end(); it++)
    {
        if (it->second->active)
        {
            task_enum_action tea = (*closure) (it->second->the_vm, "waiting on thread", data);

            if (tea == TEA_KILL) {
                // When the task gets killed, it's responsible for cleaning up after itself by checking active.
                it->second->active = false;
            }
            if (tea != TEA_CONTINUE)
                return tea;
        }
    }

    return TEA_CONTINUE;
}

/* The default thread callback function: This will first call the actual callback function
 * and then handle resuming the MOO task and cleaning up the temporary Var and background waiter. */
void *run_callback(void *bw)
{
    background_waiter *w = (background_waiter*)bw;

    Var ret;
    w->callback(bw, &ret);

    if (w->active)
        resume_task(w->the_vm, ret);
    else
        free_var(ret);

    deallocate_background_waiter(w);

    pthread_exit(NULL);
}

/* Creates the background_waiter struct and starts the worker thread. */
static enum error
background_suspender(vm the_vm, void *data)
{
    background_waiter *w = (background_waiter*)data;
    w->the_vm = the_vm;
    w->active = true;

    // Create our new worker thread as detached so that resources are immediately freed
    pthread_t new_thread;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    pthread_attr_setstacksize(&attr, 1000000);          // Stack size of 1 MB should be sufficient.

    int thread_error = 0;
    if ((thread_error = pthread_create(&new_thread, &attr, run_callback, data)))
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

/* Create a new background thread, supplying a callback function and a Var of data.
 * You should check can_create_thread from your own functions before relying on moo_background_thread. */
package
background_thread(void (*callback)(void*, Var*), void* data)
{
    if (!can_create_thread())
        return make_error_pack(E_QUOTA);

    background_waiter *w = (background_waiter*)mymalloc(sizeof(background_waiter), M_STRUCT);
    initialize_background_waiter(w);
    w->callback = callback;
    w->data = data;

    return make_suspend_pack(background_suspender, (void*)w);
}

/********************************************************************************************************/

/* Make sure creating a new thread won't exceed MAX_BACKGROUND_THREADS or $server_options.max_background_threads */
bool can_create_thread()
{
    // Make sure we don't overrun the background thread limit.
    if (next_background_handle > server_int_option("max_background_threads", MAX_BACKGROUND_THREADS))
        return false;
    else
        return true;
}

/* Insert the background waiter into the process table. */
void initialize_background_waiter(background_waiter *waiter)
{
    waiter->handle = next_background_handle;
    background_process_table[next_background_handle] = waiter;
    next_background_handle++;
}

/* Remove the background waiter from the process table, free any memory,
 * and reset the maximum handle if there are no threads running. */
void deallocate_background_waiter(background_waiter *waiter)
{
    int handle = waiter->handle;
    myfree(waiter->data, M_STRUCT);
    myfree(waiter, M_STRUCT);
    background_process_table.erase(handle);

    if (background_process_table.size() == 0)
        next_background_handle = 1;
}

/********************************************************************************************************/

/* The background testing function. Accepts a string argument and a time argument. Its goal is simply
 * to spawn a helper thread, sleep, and then return the string back to you. */
static package
bf_background_test(Var arglist, Byte next, void *vdata, Objid progr)
{
    Var *sample = (Var*)mymalloc(sizeof(arglist), M_STRUCT);
    *sample = var_dup(arglist);
    free_var(arglist);

    return background_thread(background_test_callback, sample);
}

/* The actual callback function for our background_test function. This function does all of the actual work
 * for the background_test. Receives a pointer to the relevant background_waiter struct. */
void background_test_callback(void *bw, Var *ret)
{
    background_waiter *w = (background_waiter*)bw;
    Var *args = (Var*)w->data;
    int wait = (args->v.list[0].v.num >= 2 ? args->v.list[2].v.num : 5);

    sleep(wait);

    if (w->active)
    {
        ret->type = TYPE_STR;
        if (args->v.list[0].v.num == 0)
            ret->v.str = str_dup("Hello, world.");
        else
            ret->v.str = str_dup(args->v.list[1].v.str);
    } else {
        oklog("INACTIVE\n");
    }
}

void
register_background()
{
#ifdef background_test
    register_task_queue(background_enumerator);
    register_function("background_test", 0, 2, bf_background_test, TYPE_STR, TYPE_INT);
#endif
}
