# - Find SQLite3
# Find the SQLite3 include directory and library
#
#  SQLITE3_INCLUDE_DIR    - where to find <sqlite3.h>, etc.
#  SQLITE3_LIBRARIES      - List of libraries when using sqlite.
#  SQLITE3_FOUND          - True if sqlite found.

IF (SQLITE3_INCLUDE_DIR)
  # Already in cache, be silent
  SET(SQLITE3_FIND_QUIETLY TRUE)
ENDIF (SQLITE3_INCLUDE_DIR)

FIND_PATH(SQLITE3_INCLUDE_DIR NAMES sqlite3.h)
FIND_LIBRARY(SQLITE3_LIBRARY NAMES sqlite3)

# handle the QUIETLY and REQUIRED arguments and set SQLITE3_FOUND to TRUE if 
# all listed variables are TRUE
INCLUDE(FindPackageHandleStandardArgs)
FIND_PACKAGE_HANDLE_STANDARD_ARGS(SQLITE3 DEFAULT_MSG SQLITE3_LIBRARY SQLITE3_INCLUDE_DIR)

IF(SQLITE3_FOUND)
  SET(SQLITE3_LIBRARIES ${SQLITE3_LIBRARY})
ENDIF(SQLITE3_FOUND)
