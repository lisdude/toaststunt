#include <limits.h>

#ifdef @ONLY_32_BITS@
#  if INT_MAX == 2147483647
// TODO: ^^ Replace with SIZEOF_LONG
#    define PRId64 "d"
#    define PRIi64 "i"
#    define PRIo64 "o"
#    define PRIu64 "u"
#    define PRIx64 "x"
#    define PRIX64 "X"
#    define SCNd64 "d"
#    define SCNi64 "i"
#    define SCNo64 "o"
#    define SCNu64 "u"
#    define SCNx64 "x"
#  elif LONG_MAX == 2147483647
#    define PRId64 "ld"
#    define PRIi64 "li"
#    define PRIo64 "lo"
#    define PRIu64 "lu"
#    define PRIx64 "lx"
#    define PRIX64 "lX"
#    define SCNd64 "ld"
#    define SCNi64 "li"
#    define SCNo64 "lo"
#    define SCNu64 "lu"
#    define SCNx64 "lx"
#   endif

#else

#    define PRId64 "lld"
#    define PRIi64 "lli"
#    define PRIo64 "llo"
#    define PRIu64 "llu"
#    define PRIx64 "llx"
#    define PRIX64 "llX"
#    define SCNd64 "lld"
#    define SCNi64 "lli"
#    define SCNo64 "llo"
#    define SCNu64 "llu"
#    define SCNx64 "llx"
#endif

#cmakedefine VERSION_MAJOR @VERSION_MAJOR@
#cmakedefine VERSION_MINOR @VERSION_MINOR@
#cmakedefine VERSION_RELEASE @VERSION_RELEASE@
#ifndef VERSION_RELEASE
// Work around VERSION_RELEASE not being defined when 0.
#define VERSION_RELEASE 0
#endif

#cmakedefine01 HAVE_UNISTD_H
#cmakedefine01 HAVE_STDLIB_H
#cmakedefine01 HAVE_STRFTIME
#cmakedefine01 HAVE_STRERROR
#cmakedefine01 HAVE_TM_ZONE
#cmakedefine01 HAVE_TZNAME
#cmakedefine01 HAVE_CRYPT
#cmakedefine01 HAVE_MKFIFO
#cmakedefine01 HAVE_REMOVE
#cmakedefine01 HAVE_RENAME
#cmakedefine01 HAVE_SELECT
#cmakedefine01 HAVE_POLL
#cmakedefine01 HAVE_STRTOUL
#cmakedefine01 HAVE_RANDOM
#cmakedefine01 HAVE_LRAND48
#cmakedefine01 HAVE_WAITPID
#cmakedefine01 HAVE_WAIT2
#cmakedefine01 HAVE_WAIT3
#cmakedefine01 HAVE_SIGEMPTYSET
#cmakedefine01 HAVE_SIGPROCMASK
#cmakedefine01 HAVE_SIGRELSE
#cmakedefine01 FSTAT_WORKS_ON_FIFOS
#cmakedefine01 POLL_WORKS_ON_FIFOS
#cmakedefine01 SELECT_WORKS_ON_FIFOS



//#cmakedefine01 HAVE_RANDOM_DEVICE
//#cmakedefine01 RANDOM_DEVICE

#ifndef HAVE_STRTOIMAX
# ifdef HAVE_LONG_LONG
#  define strtoimax strtoll
#  define strtoumax strtoull
# else
#  define strtoimax strtol
#  define strtoumax strtoul
# endif
#endif


#cmakedefine01 HAS_ARGON2
#cmakedefine01 HAS_ASPELL
#cmakedefine01 HAS_CURL
#cmakedefine01 USING_REL
