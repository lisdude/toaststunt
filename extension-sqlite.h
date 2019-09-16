#ifndef EXTENSION_SQLITE_H
#define EXTENSION_SQLITE_H

#include <sqlite3.h>
#include <stdbool.h>

#include "functions.h"
#include "numbers.h"
#include "utils.h"
#include "list.h"
#include "storage.h"
#include "log.h"
#include "server.h"
#include "map.h"

#define SQLITE_MOO_VERSION      "2.1"
#define SQLITE_MAX_HANDLES      20  /* Maximum number of SQLite databases that can be open
                                     * at a single time. Can be overridden with an INT in
                                     * $server_options.sqlite_max_handles */

#define SQLITE_PARSE_TYPES      2   // Return all strings if unset
#define SQLITE_PARSE_OBJECTS    4   // Turn "#100" into OBJ
#define SQLITE_SANITIZE_STRINGS 8   // Strip newlines from returned strings.

typedef struct sqlite_conn
{
    sqlite3 *id;
    char *path;
    unsigned char options;
    int locks;
} sqlite_conn;

/* In order to ensure thread safety, the last result should be unique
 * to each running thread. So we pass this temporary struct around instead
 * of the connection itself. */
typedef struct sqlite_result
{
    sqlite_conn *connection;
    Var last_result;
} sqlite_result;

// Forward declarations
extern const char *file_resolve_path(const char *);             // from fileio.cc
extern int parse_number(const char *, int *, int);              // from numbers.cc
extern int parse_float(const char *, double *);                 // from numbers.cc

// Other helper functions
bool valid_handle(int handle);
int next_handle();
int allocate_handle();
void deallocate_handle(int handle, bool shutdown);
int database_already_open(const char *path);
int callback(void *, int, char **, char **);
void sanitize_string_for_moo(char *);
Var string_to_moo_type(char *, bool, bool);
Stream* object_to_string(Var *);
void sqlite_shutdown();

#endif /* EXTENSION_SQLITE_H */
