execute_process(COMMAND git log --pretty=format:%H -n 1
                OUTPUT_VARIABLE GIT_REV
                ERROR_QUIET)

# Check whether we got any revision (which isn't
# always the case, e.g. when someone downloaded a zip
# file from Github instead of a checkout)
if ("${GIT_REV}" STREQUAL "")
    set(GIT_REV "N/A")
    set(GIT_DIFF "")
    set(GIT_BRANCH "N/A")
else()
    execute_process(
        COMMAND bash -c "git diff --quiet --exit-code || echo +"
        OUTPUT_VARIABLE GIT_DIFF)
    execute_process(
        COMMAND git rev-parse --abbrev-ref HEAD
        OUTPUT_VARIABLE GIT_BRANCH)
    execute_process(
        COMMAND git --version
        OUTPUT_VARIABLE GIT_VERSION)

    string(STRIP "${GIT_REV}" GIT_REV)
    #    string(SUBSTRING "${GIT_REV}" 1 7 GIT_REV)
    string(STRIP "${GIT_DIFF}" GIT_DIFF)
    string(STRIP "${GIT_BRANCH}" GIT_BRANCH)
    string(STRIP "${GIT_VERSION}" GIT_VERSION)
    string(SUBSTRING "${GIT_VERSION}" 12 -1 GIT_VERSION)
endif()

# Why the following values? They're pulled from LambdaMOO's version_src guidelines in /docs

set(VERSION "
#define VERSION_SOURCE(DEF) \\
    DEF(vcs,\"git\") \\
    DEF(vcs_version,\"${GIT_VERSION}\") \\
    DEF(commit,\"${GIT_REV}${GIT_DIFF}\") \\
    DEF(branch,\"${GIT_BRANCH}\") \\
    DEF(url,\"https://github.com/lisdude/toaststunt\")
")

if(EXISTS ${CMAKE_BINARY_DIR}/version_src.h)
    file(READ ${CMAKE_BINARY_DIR}/version_src.h VERSION_)
else()
    set(VERSION_ "")
endif()

if (NOT "${VERSION}" STREQUAL "${VERSION_}")
    file(WRITE ${CMAKE_BINARY_DIR}/version_src.h "${VERSION}")
endif()
