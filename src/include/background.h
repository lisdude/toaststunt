#ifndef EXTENSION_BACKGROUND_H
#define EXTENSION_BACKGROUND_H 1

#include <map>
#include <mutex>
#include <condition_variable>

#include "functions.h"
#include "thpool.h"        // thread pool

#define MAX_BACKGROUND_THREADS  20      /* The total number threads allowed to be queued from within the MOO.
                                           Can be overridden with $server_options.max_background_threads */

typedef struct background_waiter {
    Var return_value;                   // The final return value that gets sucked up by the network callback.
    Var data;                           // Any MOO data the callback function should be aware of. (Typically arglist.)
    vm the_vm;                          // Where we resume when we're done.
    void (*callback)(Var, Var*, void*); // The callback function that does the actual work.
    void (*cleanup)(void*);             // Optional function to perform cleanup after success or error. Receives extra_data.
    void *extra_data;                   // Additional non-Var-specific data for the callback function.
                                        // NOTE: You must manage the memory of this yourself.
    int fd[2];                          // The pipe used to resume the task immediately.
    uint16_t handle;                    // Our position in the process table.
    bool active;                        // @kill will set active to false and the callback should handle it accordingly.
} background_waiter;

extern pthread_mutex_t shutdown_mutex;
extern pthread_cond_t shutdown_condition;
extern uint16_t shutdown_complete;

// User-visible functions
extern package background_thread(void (*callback)(Var, Var*, void*), Var* data, void *extra_data = nullptr, void (*cleanup)(void*) = nullptr);
extern void make_error_map(enum error error_type, const char *msg, Var *ret);
extern void background_shutdown();

#endif /* EXTENSION_BACKGROUND_H */
