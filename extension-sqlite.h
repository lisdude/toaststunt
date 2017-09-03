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

#define SQLITE_MOO_VERSION      "1.0"
#define SQLITE_MAX_CON          20  // Maximum number of SQLite databases
                                    // that can be open at a single time.

#define SQLITE_PARSE_TYPES      2   // Return all strings if unset
#define SQLITE_PARSE_OBJECTS    4   // Turn "#100" into OBJ
#define SQLITE_SANITIZE_STRINGS 8   // Strip newlines from returned strings.

typedef struct SQLITE_CONN
{
    sqlite3 *id;
    bool active;
    char *path;
    unsigned char options;
} SQLITE_CONN;

// Array of open connections
static SQLITE_CONN SQLITE_CONNECTIONS[SQLITE_MAX_CON];

// The result of our last query from the callback
// so the MOO can copy it into a Var from the builtin function.
Var last_result;

// Forward declarations
extern const char *file_resolve_path(const char *);             // from extension-fileio.c
extern int parse_number(const char *, int *, int);              // from numbers.c
extern int parse_float(const char *, double *);                 // from numbers.c

// Other helper functions
void initialize_sqlite_connections();
int find_next_index();
int database_already_open(const char *path);
int callback(void *, int, char **, char **);
void sanitize_string_for_moo(char *);
Var string_to_moo_type(char *, bool, bool);
Stream* object_to_string(Var *);

#endif /* EXTENSION_SQLITE_H */
