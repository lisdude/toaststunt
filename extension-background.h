#ifndef EXTENSION_BACKGROUND_H
#define EXTENSION_BACKGROUND_H

#include <pthread.h>
#include <map>

#include "bf_register.h"
#include "functions.h"
#include "my-unistd.h"      // sleep()
#include "log.h"            // oklog
#include "storage.h"        // myfree, mymalloc
#include "tasks.h"          // TEA
#include "utils.h"          // var_dup
#include "server.h"         // server options

#define THREAD_MOO_VERSION      "1.0" // Version of our MOO threading library.
#define MAX_BACKGROUND_THREADS  20    /* Maximum number of background threads that can be open
                                       * at a single time. Can be overridden with an INT in
                                       * $server_options.max_background_threads */

typedef struct background_waiter {
    vm the_vm;
    pthread_t thread;          // The thread data so we can pthread_cancel if the task gets killed in-MOO
    int handle;                // Our position in the process table.
    Var args;
} background_waiter;

static std::map <int, background_waiter*> background_process_table;
static int next_background_handle = 1;

// Other helper functions
void deallocate_background_waiter(background_waiter *waiter);
void initialize_background_waiter(background_waiter *waiter);

#endif /* EXTENSION_BACKGROUND_H */
