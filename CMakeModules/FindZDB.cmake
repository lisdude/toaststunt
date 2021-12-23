# - Find ZDB
# Find the LibZDB include directory and library
#
#  ZDB_INCLUDE_DIR    - where to find <zdbpp>, etc.
#  ZDB_LIBRARIES      - List of libraries when using ZDB.
#  ZDB_FOUND          - True if ZDB found.

# Look for the header file.
FIND_PATH(ZDB_INCLUDE_DIR NAMES zdbpp.h PATHS /usr/include/zdb /usr/local/include/zdb)

# Look for the library.
FIND_LIBRARY(ZDB_LIBRARY NAMES zdb libzdb PATHS /usr/lib)

# handle the QUIETLY and REQUIRED arguments and set ZDB_FOUND to TRUE if 
# all listed variables are TRUE
INCLUDE(FindPackageHandleStandardArgs)
FIND_PACKAGE_HANDLE_STANDARD_ARGS(ZDB DEFAULT_MSG ZDB_LIBRARY ZDB_INCLUDE_DIR)

IF(ZDB_FOUND)
  SET(ZDB_LIBRARIES ${ZDB_LIBRARY})
  SET(ZDB_INCLUDE_DIRS ${ZDB_INCLUDE_DIR})
ENDIF(ZDB_FOUND)