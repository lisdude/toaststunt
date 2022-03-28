# - Find PQXX
# Find the PostGres include directory and library
#
#  POSTGRESQL_INCLUDE_DIR    - where to find <pqxx>, etc.
#  POSTGRESQL_LIBRARIES      - List of libraries when using pqxx.
#  POSTGRESQL_FOUND          - True if pqxx found.

# Look for the header file.
FIND_PATH(POSTGRESQL_INCLUDE_DIR NAMES pqxx)

# Look for the library.
FIND_LIBRARY(POSTGRESQL_LIBRARY NAMES pqxx libpqxx)

# handle the QUIETLY and REQUIRED arguments and set POSTGRESQL_FOUND to TRUE if 
# all listed variables are TRUE
INCLUDE(FindPackageHandleStandardArgs)
FIND_PACKAGE_HANDLE_STANDARD_ARGS(PostgreSQL DEFAULT_MSG POSTGRESQL_LIBRARY POSTGRESQL_INCLUDE_DIR)

IF(POSTGRESQL_FOUND)
  SET(POSTGRESQL_LIBRARIES ${PQXX_LIBRARY})
ENDIF(POSTGRESQL_FOUND)