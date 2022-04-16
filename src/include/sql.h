#ifdef SQL_FOUND

#define SQL_SOFT_MAX_CONNECTIONS    5   // The max number of connections to allow in a pool.
                                        // This is a soft cap, so instead of preventing
                                        // additional connections from being made, this will
                                        // instead just close connections that are created over
                                        // the cap after they're done their work.

#define SQL_PARSE_TYPES      2   // Return all strings if unset
#define SQL_PARSE_OBJECTS    4   // Turn "#100" into OBJ
#define SQL_SANITIZE_STRINGS 8   // Strip newlines from returned strings.

// Forward declarations
extern int parse_float(const char *, double *);                 // from numbers.cc

#endif // SQL_FOUND