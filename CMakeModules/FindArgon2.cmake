# - Find Argon2
# Find the Argon2 include directory and library
#
#  ARGON2_INCLUDE_DIR    - where to find <nettle/sha.h>, etc.
#  ARGON2_LIBRARIES      - List of libraries when using libnettle.
#  ARGON2_FOUND          - True if libnettle found.

IF (ARGON2_INCLUDE_DIR)
  # Already in cache, be silent
  SET(ARGON2_FIND_QUIETLY TRUE)
ENDIF (ARGON2_INCLUDE_DIR)

FIND_PATH(ARGON2_INCLUDE_DIR argon2.h)
FIND_LIBRARY(ARGON2_LIBRARY argon2)

# handle the QUIETLY and REQUIRED arguments and set ARGON2_FOUND to TRUE if 
# all listed variables are TRUE
INCLUDE(FindPackageHandleStandardArgs)
FIND_PACKAGE_HANDLE_STANDARD_ARGS(Argon2 DEFAULT_MSG ARGON2_LIBRARY ARGON2_INCLUDE_DIR)

IF(ARGON2_FOUND)
  SET(ARGON2_LIBRARIES ${ARGON2_LIBRARY})
ENDIF(ARGON2_FOUND)
