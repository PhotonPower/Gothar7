# Runs the game with ARGS and checks exit code and output; a hang (window opened, engine started) fails
# by timeout. Usage: cmake -DGOTHAR=<exe> -DARGS=<a;b> -DEXPECT_RC=<0|nonzero> -DEXPECT=<regex> -P check_cli.cmake
execute_process(
    COMMAND "${GOTHAR}" ${ARGS}
    TIMEOUT 20
    RESULT_VARIABLE rc
    OUTPUT_VARIABLE out
    ERROR_VARIABLE err)
set(all "${out}${err}")
if(rc MATCHES "timeout|Process terminated")
    message(FATAL_ERROR "gothar ${ARGS} did not exit within 20 s (${rc})")
endif()
if(EXPECT_RC STREQUAL "0" AND NOT rc EQUAL 0)
    message(FATAL_ERROR "gothar ${ARGS}: exit code ${rc}, expected 0\n${all}")
endif()
if(EXPECT_RC STREQUAL "nonzero" AND rc EQUAL 0)
    message(FATAL_ERROR "gothar ${ARGS}: exit code 0, expected a failure\n${all}")
endif()
if(NOT all MATCHES "${EXPECT}")
    message(FATAL_ERROR "gothar ${ARGS}: output does not match '${EXPECT}'\n${all}")
endif()
