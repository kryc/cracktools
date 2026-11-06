file(MAKE_DIRECTORY "${TEST_DIR}")
set(HASH "0cc175b9c0f1b6a831c399e269772661")
file(WRITE "${TEST_DIR}/hashes.txt" "${HASH}\n")
file(WRITE "${TEST_DIR}/words.txt" "a\n")
file(WRITE "${TEST_DIR}/empty.txt" "")
# Fast workers are deliberately likely to finish before the old main startup.
foreach(ITERATION RANGE 1 20)
    execute_process(COMMAND "${CRACKLIST}" --md5 -P -t 4
        "${TEST_DIR}/hashes.txt" "${TEST_DIR}/words.txt"
        RESULT_VARIABLE RESULT OUTPUT_VARIABLE OUTPUT ERROR_VARIABLE ERROR TIMEOUT 10)
    if(NOT RESULT STREQUAL "0" OR NOT OUTPUT STREQUAL "a\n")
        message(FATAL_ERROR "cracklist iteration ${ITERATION}: ${RESULT}; output=[${OUTPUT}]; ${ERROR}")
    endif()
    execute_process(COMMAND "${CRACKLIST}" --md5 -P -t 4
        "${TEST_DIR}/hashes.txt" "${TEST_DIR}/empty.txt"
        RESULT_VARIABLE RESULT OUTPUT_VARIABLE OUTPUT ERROR_VARIABLE ERROR TIMEOUT 10)
    if(NOT RESULT STREQUAL "0" OR NOT OUTPUT STREQUAL "")
        message(FATAL_ERROR "empty cracklist iteration ${ITERATION}: ${RESULT}; ${ERROR}")
    endif()
endforeach()
execute_process(COMMAND "${SIMDCRACK}" --md5 -t 4 --min 1 --max 1 -c abc "${HASH}"
    RESULT_VARIABLE RESULT OUTPUT_VARIABLE OUTPUT ERROR_VARIABLE ERROR TIMEOUT 10)
if(NOT RESULT STREQUAL "0" OR NOT OUTPUT MATCHES "${HASH}:a")
    message(FATAL_ERROR "simdcrack: ${RESULT}; output=[${OUTPUT}]; ${ERROR}")
endif()