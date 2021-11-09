# - Find PQXX
# Find the PostGres include directory and library
#
#  PQXX_INCLUDE_DIR    - where to find <pqxx>, etc.
#  PQXX_LIBRARIES      - List of libraries when using pqxx.
#  PQXX_FOUND          - True if pqxx found.

# Look for the header file.
FIND_PATH(PQXX_INCLUDE_DIR NAMES pqxx)

# Look for the library.
FIND_LIBRARY(PQXX_LIBRARY NAMES pqxx libpqxx)

# handle the QUIETLY and REQUIRED arguments and set PQXX_FOUND to TRUE if 
# all listed variables are TRUE
INCLUDE(FindPackageHandleStandardArgs)
FIND_PACKAGE_HANDLE_STANDARD_ARGS(PQXX DEFAULT_MSG PQXX_LIBRARY PQXX_INCLUDE_DIR)

IF(PQXX_FOUND)
  SET(PQXX_LIBRARIES ${PQXX_LIBRARY})
ENDIF(PQXX_FOUND)