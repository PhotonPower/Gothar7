# The data of a release (M17, docs/modules/release.md part C), run by the target g7_release_data:
# 1. the version line from the game, the tag check (a tag vX.Y.Z on HEAD must equal project(VERSION));
# 2. the licence check over assets/source, which writes CREDITS.txt (breaks off on any problem);
# 3. cooking assets/source into one archive data.g7pak (KTX2 textures, everything anew).
# Inputs: GOTHAR, COOK, SOURCE_DIR, OUT, VERSION.
file(MAKE_DIRECTORY "${OUT}")
execute_process(COMMAND "${GOTHAR}" --version OUTPUT_VARIABLE _line OUTPUT_STRIP_TRAILING_WHITESPACE
                RESULT_VARIABLE _rc)
if(NOT _rc EQUAL 0 OR NOT _line MATCHES "^Gothar ")
    message(FATAL_ERROR "gothar --version failed (${_rc}): ${_line}")
endif()
find_package(Git QUIET)
if(GIT_EXECUTABLE)
    execute_process(COMMAND "${GIT_EXECUTABLE}" describe --tags --exact-match --match "v*" HEAD
                    WORKING_DIRECTORY "${SOURCE_DIR}" OUTPUT_VARIABLE _tag OUTPUT_STRIP_TRAILING_WHITESPACE
                    ERROR_QUIET RESULT_VARIABLE _tagRc)
    if(_tagRc EQUAL 0 AND NOT _tag STREQUAL "v${VERSION}")
        message(FATAL_ERROR "tag ${_tag} does not match project(VERSION ${VERSION}) in CMakeLists.txt")
    endif()
endif()
execute_process(COMMAND "${COOK}" --check-licences "${SOURCE_DIR}/assets" "${OUT}/CREDITS.txt" "${_line}"
                RESULT_VARIABLE _rc)
if(NOT _rc EQUAL 0)
    message(FATAL_ERROR "licence check failed - nothing is packed")
endif()
execute_process(COMMAND "${COOK}" --source "${SOURCE_DIR}/assets/source" --out "${OUT}/cooked" --pack data.g7pak
                        --full --clean
                RESULT_VARIABLE _rc)
if(NOT _rc EQUAL 0)
    message(FATAL_ERROR "cooking failed (${_rc})")
endif()
file(COPY_FILE "${OUT}/cooked/data.g7pak" "${OUT}/data.g7pak")
message(STATUS "release data: ${_line}, ${OUT}/data.g7pak, ${OUT}/CREDITS.txt")
