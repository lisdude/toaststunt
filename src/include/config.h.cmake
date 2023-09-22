#cmakedefine ONLY_32_BITS @ONLY_32_BITS@

#ifdef ONLY_32_BITS
#   define PRId32 "d"
#   define PRIi32 "i"
#   define PRIo32 "o"
#   define PRIu32 "u"
#   define PRIx32 "x"
#   define PRIX32 "X"
#   define SCNd32 "d"
#   define SCNi32 "i"
#   define SCNo32 "o"
#   define SCNu32 "u"
#   define SCNx32 "x"
#else
#   if !defined(__MACH__) && !defined(__arm__)
#       define PRId64 "ld"
#       define PRIi64 "li"
#       define PRIo64 "lo"
#       define PRIu64 "lu"
#       define PRIx64 "lx"
#       define PRIX64 "lX"
#       define SCNd64 "ld"
#       define SCNi64 "li"
#       define SCNo64 "lo"
#       define SCNu64 "lu"
#       define SCNx64 "lx"
#   else
#       define PRId64 "lld"
#       define PRIi64 "lli"
#       define PRIo64 "llo"
#       define PRIu64 "llu"
#       define PRIx64 "llx"
#       define PRIX64 "llX"
#       define SCNd64 "lld"
#       define SCNi64 "lli"
#       define SCNo64 "llo"
#       define SCNu64 "llu"
#       define SCNx64 "llx"
#   endif
#endif

#cmakedefine VERSION_MAJOR @VERSION_MAJOR@
#cmakedefine VERSION_MINOR @VERSION_MINOR@
#cmakedefine VERSION_RELEASE @VERSION_RELEASE@
#cmakedefine VERSION_EXT "@VERSION_EXT@"
#ifndef VERSION_RELEASE
// Work around VERSION_RELEASE not being defined when 0.
#   define VERSION_RELEASE 0
#endif

#cmakedefine01 HAVE_STRFTIME
#cmakedefine01 HAVE_TM_ZONE
#cmakedefine01 HAVE_TZNAME
#cmakedefine01 HAVE_SELECT
#cmakedefine01 HAVE_POLL
#cmakedefine01 HAVE_RANDOM
#cmakedefine01 HAVE_LRAND48
#cmakedefine01 HAVE_WAITPID
#cmakedefine01 HAVE_WAIT2
#cmakedefine01 HAVE_WAIT3
#cmakedefine01 HAVE_SIGEMPTYSET
#cmakedefine01 HAVE_SIGRELSE
#cmakedefine01 HAVE_ACCEPT4
#cmakedefine IS_WSL

#if @HAVE_STRTOIMAX@
# ifdef HAVE_LONG_LONG
#  define strtoimax strtoll
#  define strtoumax strtoull
# else
#  define strtoimax strtol
#  define strtoumax strtoul
# endif
#endif

#cmakedefine ARGON2_FOUND
#cmakedefine ASPELL_FOUND
#cmakedefine CURL_FOUND
#cmakedefine PCRE_FOUND
#cmakedefine SQLITE3_FOUND
#cmakedefine OPENSSL_FOUND
#cmakedefine JEMALLOC_FOUND

#ifndef OPENSSL_FOUND
 #undef USE_TLS
#endif
