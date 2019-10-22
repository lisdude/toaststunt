# - Find gperf
#  gperf_EXECUTABLE     - Path to the gperf executable
#  gperf_FOUND          - True if gperf found.

IF (gperf_FOUND)
  # Already in cache, be silent
  SET(gperf_FIND_QUIETLY TRUE)
ENDIF ()

FIND_PROGRAM(gperf_EXECUTABLE gperf)

# handle the QUIETLY and REQUIRED arguments and set gperf_FOUND to TRUE if 
# all listed variables are TRUE
INCLUDE(FindPackageHandleStandardArgs)
FIND_PACKAGE_HANDLE_STANDARD_ARGS(gperf DEFAULT_MSG gperf_EXECUTABLE)
