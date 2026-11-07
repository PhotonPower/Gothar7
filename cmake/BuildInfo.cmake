# Writes the build info (docs/modules/release.md, part A) into OUT, run at every build (cmake -P). The file is
# rewritten only when its content changes, so a build without a new commit or new changes compiles nothing.
# Inputs: OUT, SOURCE_DIR, GIT_EXECUTABLE (may be empty), VERSION, BUILD_TYPE, TEMPLATE.
set(G7_GIT_HASH "unknown")
set(G7_GIT_DIRTY "false")
set(G7_COMMIT_DATE "")
if(GIT_EXECUTABLE)
    execute_process(COMMAND "${GIT_EXECUTABLE}" rev-parse --short=7 HEAD
        WORKING_DIRECTORY "${SOURCE_DIR}" OUTPUT_VARIABLE _hash RESULT_VARIABLE _hashResult
        OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    if(_hashResult EQUAL 0 AND _hash)
        set(G7_GIT_HASH "${_hash}")
        # Changed files that git knows (untracked files do not count: build output, local notes).
        execute_process(COMMAND "${GIT_EXECUTABLE}" status --porcelain --untracked-files=no
            WORKING_DIRECTORY "${SOURCE_DIR}" OUTPUT_VARIABLE _status ERROR_QUIET)
        if(_status)
            set(G7_GIT_DIRTY "true")
        endif()
        execute_process(COMMAND "${GIT_EXECUTABLE}" log -1 --format=%cs
            WORKING_DIRECTORY "${SOURCE_DIR}" OUTPUT_VARIABLE G7_COMMIT_DATE
            OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    endif()
endif()
set(G7_VERSION "${VERSION}")
set(G7_BUILD_TYPE "${BUILD_TYPE}")
if(NOT G7_BUILD_TYPE)
    set(G7_BUILD_TYPE "unknown")
endif()
string(TOLOWER "${G7_BUILD_TYPE}" G7_BUILD_TYPE)
configure_file("${TEMPLATE}" "${OUT}" @ONLY)
