#ifndef EXTENSION_BACKGROUND_H
#define EXTENSION_BACKGROUND_H 1

#include <map>

#include "bf_register.h"
#include "functions.h"
#include "my-unistd.h"                  // sleep()
#include "storage.h"                    // myfree, mymalloc
#include "tasks.h"                      // TEA
#include "utils.h"                      // var_dup
#include "server.h"                     // server options
#include "list.h"                       // listappend
#include "net_multi.h"                  // network_fd shenanigans
#include "dependencies/thpool.h"        // thread pool

#define THREAD_MOO_VERSION      "2.5"   // Version of our MOO threading library.
#define TOTAL_BACKGROUND_THREADS 2      // The total number of background threads running in the pool.
#define MAX_BACKGROUND_THREADS  20      /* The total number threads allowed to be run from within the MOO.
                                           Can be overridden with $server_options.max_background_threads */

typedef struct background_waiter {
    vm the_vm;                          // Where we resume when we're done.
    int handle;                         // Our position in the process table.
    void (*callback)(Var, Var*);        // The callback function that does the actual work.
    Var data;                           // Any data the callback function should be aware of.
    bool active;                        // @kill will set active to false and the callback should handle it accordingly.
    int fd[2];                          // The pipe used to resume the task immediately.
    Var return_value;                   // The final return value that gets sucked up by the network callback.
    char *human_title;                  // A human readable explanation for the thread's existance.
} background_waiter;

static std::map <int, background_waiter*> background_process_table;
static int next_background_handle = 1;
static threadpool background_pool;

// User-visible functions
extern package background_thread(void (*callback)(Var, Var*), Var* data, char *human_title);
extern bool can_create_thread();

// Other helper functions
extern void deallocate_background_waiter(background_waiter *waiter);
void initialize_background_waiter(background_waiter *waiter);
void run_callback(void *bw);

// Example functions
void background_test_callback(Var args, Var *ret);

#endif /* EXTENSION_BACKGROUND_H */
