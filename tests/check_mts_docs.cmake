execute_process(COMMAND "${GENERATOR}" OUTPUT_FILE "${OUTPUT}" RESULT_VARIABLE rc)
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "MTS documentation generator failed")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" -E compare_files "${OUTPUT}" "${REFERENCE}"
                RESULT_VARIABLE different)
if(NOT different EQUAL 0)
  message(FATAL_ERROR "MTS command reference is stale; regenerate it with mts-docgen")
endif()
