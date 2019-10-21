cmake_minimum_required(VERSION 2.8.12)
project(TOASTSTUNT LANGUAGES CXX C)
SET(VERSION_MAJOR 2)
SET(VERSION_MINOR 6)
SET(VERSION_RELEASE 0)

# CMake Includes and Modules
INCLUDE(${CMAKE_SOURCE_DIR}/CMakeModules/AddCCompilerFlag.cmake)
SET(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/CMakeModules/")

# Header Files
INCLUDE_DIRECTORIES(${CMAKE_SOURCE_DIR}/src/include)
INCLUDE_DIRECTORIES(${CMAKE_SOURCE_DIR}/src/dependencies)
INCLUDE_DIRECTORIES(${CMAKE_BINARY_DIR})

# Compile Options
OPTION(ONLY_32_BITS "Use 32-bit integers instead of 64-bit." OFF)

# Compiler Flags
ADD_C_COMPILER_FLAG(-Wall)
ADD_C_COMPILER_FLAG(-Wno-unused-parameter)
ADD_C_COMPILER_FLAG(-march=native)
ADD_C_COMPILER_FLAG(-O3)
ADD_C_COMPILER_FLAG(-Wno-sign-compare)
ADD_C_COMPILER_FLAG(-Wno-switch)
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_C_STANDARD 99)

# Compatibility Checks Galore
INCLUDE(CheckStructHasMember)
INCLUDE(CheckSymbolExists)
INCLUDE(CheckIncludeFile)
INCLUDE(CheckFunctionExists)
INCLUDE(CheckTypeSize)
INCLUDE(CheckLibraryExists)

CHECK_TYPE_SIZE("long" SIZEOF_LONG)
CHECK_TYPE_SIZE("float" SIZEOF_FLOAT)

CHECK_INCLUDE_FILE(unistd.h HAVE_UNISTD_H)
CHECK_INCLUDE_FILE(stdlib.h HAVE_STDLIB_H)
CHECK_INCLUDE_FILE(endian.h HAVE_MACHINE_ENDIAN_H)

CHECK_FUNCTION_EXISTS(getrusage HAVE_GETRUSAGE)
CHECK_FUNCTION_EXISTS(strerror HAVE_STRERROR)
CHECK_FUNCTION_EXISTS(strftime HAVE_STRFTIME)
CHECK_FUNCTION_EXISTS(mkfifo HAVE_MKFIFO)
CHECK_FUNCTION_EXISTS(remove HAVE_REMOVE)
CHECK_FUNCTION_EXISTS(rename HAVE_RENAME)
CHECK_FUNCTION_EXISTS(select HAVE_SELECT)
CHECK_FUNCTION_EXISTS(strtoul HAVE_STRTOUL)
CHECK_FUNCTION_EXISTS(lrand48 HAVE_LRAND48)
CHECK_FUNCTION_EXISTS(random HAVE_RANDOM)
CHECK_FUNCTION_EXISTS(waitpid HAVE_WAITPID)
CHECK_FUNCTION_EXISTS(wait2 HAVE_WAIT2)
CHECK_FUNCTION_EXISTS(wait3 HAVE_WAIT3)
CHECK_FUNCTION_EXISTS(sigemptyset HAVE_SIGEMPTYSET)
CHECK_FUNCTION_EXISTS(sigprocmask HAVE_SIGPROCMASK)
CHECK_FUNCTION_EXISTS(sigrelse HAVE_SIGRELSE)
CHECK_FUNCTION_EXISTS(fstat FSTAT_WORKS_ON_FIFOS)
CHECK_FUNCTION_EXISTS(poll POLL_WORKS_ON_FIFOS)
CHECK_FUNCTION_EXISTS(select SELECT_WORKS_ON_FIFOS)
CHECK_FUNCTION_EXISTS(strtoimax HAVE_STRTOIMAX)

CHECK_SYMBOL_EXISTS(tzname time.h HAVE_TZNAME)

CHECK_STRUCT_HAS_MEMBER("struct tm"  tm_zone  time.h  HAVE_TM_ZONE)

# Yacc (-d is a default flag on my bison)
FIND_PACKAGE(BISON REQUIRED)
BISON_TARGET(MOOParser src/parser.y ${CMAKE_BINARY_DIR}/parser.cc COMPILE_FLAGS "-y")

# Keywords
SET(KEYWORDS ${CMAKE_BINARY_DIR}/keywords.cc)
ADD_CUSTOM_COMMAND(
    OUTPUT ${KEYWORDS}
    COMMAND gperf --language=C++ --ignore-case --readonly-tables --struct-type --omit-struct-type --key-positions=1,3,$$ ${CMAKE_SOURCE_DIR}/src/keywords.gperf | sed -e 's/\#include <ctype.h>/\#include \"my-ctype.h\"/' > ${CMAKE_BINARY_DIR}/keywords.cc
    COMMENT "Generating keywords.cc"
)

# This should really only happen if options.h changes...
EXECUTE_PROCESS(COMMAND ${CMAKE_SOURCE_DIR}/src/include/version_opt_gen.pl)

# Version_src is generated with a Perl script, but also pulled in by other things. So...
file(TOUCH ${CMAKE_BINARY_DIR}/version_src.h)

# Millions of source files
SET(src_CSRCS
    src/dependencies/thpool.c
    src/dependencies/sosemanuk.c
    src/dependencies/linenoise.c
    src/dependencies/pcrs.c
    src/dependencies/strnatcmp.c
    src/http_parser.c
    src/regexpr.c)

SET(src_YAJL
    src/dependencies/yajl/yajl.c
    src/dependencies/yajl/yajl_alloc.c
    src/dependencies/yajl/yajl_buf.c
    src/dependencies/yajl/yajl_encode.c
    src/dependencies/yajl/yajl_gen.c
    src/dependencies/yajl/yajl_lex.c
    src/dependencies/yajl/yajl_parser.c
    src/dependencies/yajl/yajl_version.c)

SET(src_CRYPT
    src/dependencies/crypt/crypt_blowfish.c
    src/dependencies/crypt/crypt_gensalt.c)

SET(src_CXXSRCS
    src/ast.cc
    src/base64.cc
    src/code_gen.cc
    src/collection.cc
    src/crypto.cc
    src/db_file.cc
    src/db_io.cc
    src/db_objects.cc
    src/db_properties.cc
    src/db_verbs.cc
    src/decompile.cc
    src/disassemble.cc
    src/eval_env.cc
    src/eval_vm.cc
    src/exec.cc
    src/execute.cc
    src/extensions.cc
    src/fileio.cc
    src/functions.cc
    src/garbage.cc
    src/json.cc
    ${KEYWORDS}
    src/log.cc
    src/list.cc
    src/map.cc
    src/match.cc
    src/name_lookup.cc
    src/network.cc
    src/net_mplex.cc
    src/net_mplex.cc
    src/net_proto.cc
    src/numbers.cc
    src/objects.cc
    src/parse_cmd.cc
    src/pattern.cc
    src/program.cc
    src/property.cc
    src/quota.cc
    src/server.cc
    src/storage.cc
    src/streams.cc
    src/str_intern.cc
    src/sym_table.cc
    src/system.cc
    src/tasks.cc
    src/timers.cc
    src/unparse.cc
    src/utils.cc
    src/verbs.cc
    src/version.cc
    src/sqlite.cc
    src/pcre_moo.cc
    src/background.cc
    src/waif.cc
    src/simplexnoise.cc
    src/argon2.cc
    src/spellcheck.cc
    src/curl.cc)

SET(src_GRAMMAR
    ${BISON_MOOParser_OUTPUTS})

SET(src_SERVER
    ${src_GRAMMAR}
    ${src_YAJL}
    ${src_CRYPT}
    ${src_CSRCS}
    ${src_CXXSRCS})

ADD_EXECUTABLE(moo ${src_SERVER})

# Requiredish packages
FIND_PACKAGE(Threads REQUIRED)
    if(THREADS_HAVE_PTHREAD_ARG)
        TARGET_COMPILE_OPTIONS(moo PUBLIC "-pthread")
    endif()
    if(CMAKE_THREAD_LIBS_INIT)
        TARGET_LINK_LIBRARIES(moo "${CMAKE_THREAD_LIBS_INIT}")
    endif()

FIND_PACKAGE(PCRE)
IF(PCRE_FOUND)
    INCLUDE_DIRECTORIES(${PCRE_INCLUDE_DIRS})
    TARGET_LINK_LIBRARIES(moo ${PCRE_LIBRARIES})
ENDIF()

FIND_PACKAGE(Nettle REQUIRED)
    INCLUDE_DIRECTORIES(${NETTLE_INCLUDE_DIRS})
    TARGET_LINK_LIBRARIES(moo ${NETTLE_LIBRARIES})

FIND_PACKAGE(CURL)
IF(CURL_FOUND)
    INCLUDE_DIRECTORIES(${CURL_INCLUDE_DIRS})
    TARGET_LINK_LIBRARIES(moo ${CURL_LIBRARIES})
ENDIF()

FIND_PACKAGE(ASPELL)
IF(ASPELL_FOUND)
    INCLUDE_DIRECTORIES(${ASPELL_INCLUDE_DIRS})
    TARGET_LINK_LIBRARIES(moo ${ASPELL_LIBRARIES})
ENDIF()

FIND_PACKAGE(Argon2 REQUIRED)
    INCLUDE_DIRECTORIES(${ARGON2_INCLUDE_DIRS})
    TARGET_LINK_LIBRARIES(moo ${ARGON2_LIBRARIES})

FIND_PACKAGE(SQLite3)
IF(SQLITE3_FOUND)
    INCLUDE_DIRECTORIES(${SQLite3_INCLUDE_DIRS})
    TARGET_LINK_LIBRARIES(moo ${SQLite3_LIBRARIES})
ENDIF()

CONFIGURE_FILE(${CMAKE_SOURCE_DIR}/src/include/config.h.cmake
               ${CMAKE_BINARY_DIR}/config.h)

TARGET_LINK_LIBRARIES(moo -lm)

IF(NOT APPLE)
    TARGET_LINK_LIBRARIES(moo -lcrypt)
ENDIF()
