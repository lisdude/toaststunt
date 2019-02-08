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
    for (auto& it : background_process_table)
    {
        if (it.second->active)
        {
            char *thread_name = 0;
            asprintf(&thread_name, "waiting on thread %d", it.first);
            task_enum_action tea = (*closure) (it.second->the_vm, thread_name, data);
            free(thread_name);

            if (tea == TEA_KILL) {
                // When the task gets killed, it's responsible for cleaning up after itself by checking active from time to time.
                it.second->active = false;
            }
            if (tea != TEA_CONTINUE)
                return tea;
        }
    }

    return TEA_CONTINUE;
}

/* The default thread callback function: Responsible for calling the function specified in the original
 * background function call and then passing it off to the network callback to resume the MOO task. */
void run_callback(void *bw)
{
    background_waiter *w = (background_waiter*)bw;

    w->callback(bw, &w->return_value);

    // Write to our network pipe to resume the MOO loop
    write(w->fd[1], "1", 1);
}

/* The function called by the network when data has been read. This is the final stage and
 * is responsible for actually resuming the task and cleaning up the associated mess. */
void network_callback(int fd, void *data)
{
	background_waiter *w = (background_waiter*)data;

    /* Resume the MOO task if it hasn't already been killed. */
    if (w->active)
        resume_task(w->the_vm, var_ref(w->return_value));

    deallocate_background_waiter(w);
}

/* Creates the background_waiter struct and starts the worker thread. */
static enum error
background_suspender(vm the_vm, void *data)
{
    background_waiter *w = (background_waiter*)data;
    w->the_vm = the_vm;
    w->active = true;

    // Register so we can write to the pipe and resume the main loop if the MOO is idle
    network_register_fd(w->fd[0], network_callback, NULL, data);

    thpool_add_work(background_pool, run_callback, data);

    return E_NONE;
}

/* Create a new background thread, supplying a callback function, a Var of data, and a string of explanatory text for what the thread is.
 * You should check can_create_thread from your own functions before relying on moo_background_thread. */
package
background_thread(void (*callback)(void*, Var*), Var* data, char *human_title)
{
    if (!can_create_thread())
    {
        errlog("Can't create a new thread\n");
        return make_error_pack(E_QUOTA);
    }

    background_waiter *w = (background_waiter*)mymalloc(sizeof(background_waiter), M_STRUCT);
    initialize_background_waiter(w);
    w->callback = callback;
    w->data = *data;
    w->human_title = human_title;
    if (pipe(w->fd) == -1)
    {
        errlog("Failed to create pipe for background thread\n");
        deallocate_background_waiter(w);
        return make_error_pack(E_QUOTA);
    }

    return make_suspend_pack(background_suspender, (void*)w);
}

/********************************************************************************************************/

/* Make sure creating a new thread won't exceed MAX_BACKGROUND_THREADS or $server_options.max_background_threads */
bool can_create_thread()
{
    // Make sure we don't overrun the background thread limit.
    if (background_process_table.size() > server_int_option("max_background_threads", MAX_BACKGROUND_THREADS))
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
    network_unregister_fd(waiter->fd[0]);
    close(waiter->fd[0]);
    close(waiter->fd[1]);
    free_var(waiter->return_value);
    free_var(waiter->data);
    free(waiter->human_title);
    myfree(waiter, M_STRUCT);
    background_process_table.erase(handle);

    if (background_process_table.size() == 0)
        next_background_handle = 1;
}

/********************************************************************************************************/

static package
bf_threads(Var arglist, Byte next, void *vdata, Objid progr)
{
    free_var(arglist);

    if (!is_wizard(progr))
        return make_error_pack(E_PERM);

    int count = 0;
    Var r = new_list(background_process_table.size());
    for (auto& it : background_process_table)
        r.v.list[++count] = Var::new_int(it.first);

    return make_var_pack(r);
}

/* Returns a list of information about the thread:
 * {human title, ?active (aka @killed)}
 * Intended primarily for debugging, but possibly useful. */
static package
bf_thread_info(Var arglist, Byte next, void *vdata, Objid progr)
{
    int handle = arglist.v.list[1].v.num;
    free_var(arglist);

    if (!is_wizard(progr))
        return make_error_pack(E_INVARG);

    if (background_process_table.count(handle) == 0)
        return make_error_pack(E_INVARG);

    background_waiter *w = background_process_table[handle];
    Var ret = new_list(2);
    ret.v.list[1] = str_dup_to_var(w->human_title);
    ret.v.list[2] = Var::new_int(w->active);

    return make_var_pack(ret);
}

/********************************************************************************************************/

/* The background testing function. Accepts a string argument and a time argument. Its goal is simply
 * to spawn a helper thread, sleep, and then return the string back to you. */
static package
bf_background_test(Var arglist, Byte next, void *vdata, Objid progr)
{
    char *human_string = 0;
    asprintf(&human_string, "background_test suspending for %" PRIdN " with string \"%s\"", arglist.v.list[2].v.num, arglist.v.list[1].v.str);
    return background_thread(background_test_callback, &arglist, human_string);
}

/* The actual callback function for our background_test function. This function does all of the actual work
 * for the background_test. Receives a pointer to the relevant background_waiter struct. */
void background_test_callback(void *bw, Var *ret)
{
    background_waiter *w = (background_waiter*)bw;
    Var args = w->data;
    int wait = (args.v.list[0].v.num >= 2 ? args.v.list[2].v.num : 5);

    sleep(wait);

    if (w->active)
    {
        ret->type = TYPE_STR;
        if (args.v.list[0].v.num == 0)
            ret->v.str = str_dup("Hello, world.");
        else
            ret->v.str = str_dup(args.v.list[1].v.str);
    }
}

void
register_background()
{
    register_task_queue(background_enumerator);
    background_pool = thpool_init(TOTAL_BACKGROUND_THREADS);
    register_function("threads", 0, 0, bf_threads);
    register_function("thread_info", 1, 1, bf_thread_info, TYPE_INT);
#ifdef BACKGROUND_TEST
    register_function("background_test", 0, 2, bf_background_test, TYPE_STR, TYPE_INT);
#endif
}
