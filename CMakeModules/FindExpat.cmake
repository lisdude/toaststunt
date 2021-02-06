# - Find Expat
# Find the Expat include directory and library
#
#  EXPAT_INCLUDE_DIR    - where to find <nettle/sha.h>, etc.
#  EXPAT_LIBRARIES      - List of libraries when using libnettle.
#  EXPAT_FOUND          - True if libnettle found.

IF (EXPAT_INCLUDE_DIR)
  # Already in cache, be silent
  SET(EXPAT_FIND_QUIETLY TRUE)
ENDIF (EXPAT_INCLUDE_DIR)

FIND_PATH(EXPAT_INCLUDE_DIR expat.h)
FIND_LIBRARY(EXPAT_LIBRARY expat)

# handle the QUIETLY and REQUIRED arguments and set EXPAT_FOUND to TRUE if 
# all listed variables are TRUE
INCLUDE(FindPackageHandleStandardArgs)
FIND_PACKAGE_HANDLE_STANDARD_ARGS(Expat DEFAULT_MSG EXPAT_LIBRARY EXPAT_INCLUDE_DIR)

IF(EXPAT_FOUND)
  SET(EXPAT_LIBRARIES ${EXPAT_LIBRARY})
ENDIF(EXPAT_FOUND)
