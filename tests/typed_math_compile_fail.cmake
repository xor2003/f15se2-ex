file(MAKE_DIRECTORY "${BINARY_DIR}/math-compile-checks")
set(source "${SOURCE_DIR}/tests/typed_math_constraints.cpp")
set(object "${BINARY_DIR}/math-compile-checks/constraints.o")
if(USE_MSVC)
    set(flags /nologo /std:c++17 /EHsc /c "/I${SOURCE_DIR}/src" "${source}" "/Fo${object}")
    set(define /D)
else()
    set(flags -std=c++17 "-I${SOURCE_DIR}/src" -c "${source}" -o "${object}")
    set(define -D)
endif()
# First prove that this compiler/environment can compile the valid API example.
execute_process(COMMAND "${CXX}" ${flags} RESULT_VARIABLE result OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "Valid typed math example failed: ${out}\n${err}")
endif()
foreach(case PRIMITIVE_ANGLE PRIMITIVE_EULER RAW_EXTRACTION BACKEND_MIXING
             MATRIX_MIXING QUANTITY_MIXING PRIVATE_STORAGE UNGUARDED_BOUNDARY
             MATRIX_INDEX MATRIX_POINTER)
    execute_process(COMMAND "${CXX}" ${flags} "${define}TEST_${case}"
        RESULT_VARIABLE result OUTPUT_VARIABLE out ERROR_VARIABLE err)
    if(result EQUAL 0)
        message(FATAL_ERROR "Compiler accepted forbidden math use: ${case}")
    endif()
endforeach()
