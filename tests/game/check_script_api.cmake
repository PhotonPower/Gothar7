# docs/script-api.md must match what the engine generates from its script bindings (M7).
# Usage: cmake -DGOTHAR=<exe> -DOUT=<generated.md> -DDOC=<docs/script-api.md> -P check_script_api.cmake
execute_process(
    COMMAND "${GOTHAR}" --smoke-test "--script-api=${OUT}"
    TIMEOUT 60
    RESULT_VARIABLE rc
    OUTPUT_VARIABLE out
    ERROR_VARIABLE err)
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "gothar --script-api failed (${rc})\n${out}${err}")
endif()
file(READ "${OUT}" generated)
file(READ "${DOC}" committed)
string(REPLACE "\r\n" "\n" committed "${committed}")
if(NOT generated STREQUAL committed)
    message(FATAL_ERROR "docs/script-api.md is out of date - regenerate it:\n"
                        "  build\\debug\\game\\gothar.exe --smoke-test --script-api=docs/script-api.md")
endif()
