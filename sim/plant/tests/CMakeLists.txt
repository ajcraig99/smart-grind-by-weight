# Plant unit tests, registered with CTest (included from ../CMakeLists.txt).
set(PLANT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/..)

set(PLANT_TESTS rng relay motor burr chute transport hx711 loadcell cup timing determinism conservation)
foreach(t ${PLANT_TESTS})
  add_executable(test_${t} test_${t}.c)
  target_link_libraries(test_${t} PRIVATE simplant)
  target_compile_options(test_${t} PRIVATE -Wall -Wextra -Wshadow)
  add_test(NAME ${t} COMMAND test_${t})
  set_tests_properties(${t} PROPERTIES TIMEOUT 300)
endforeach()

# Repository root (two levels above sim/plant) for verifying cited upstream lines.
get_filename_component(PLANT_REPO_ROOT ${PLANT_DIR}/../.. ABSOLUTE)
if(EXISTS ${PLANT_REPO_ROOT}/src/config/debug.h)
  set(PLANT_REPO_ARG ${PLANT_REPO_ROOT})
else()
  set(PLANT_REPO_ARG "")
endif()

add_executable(test_params test_params.c)
target_link_libraries(test_params PRIVATE simplant)
target_compile_options(test_params PRIVATE -Wall -Wextra -Wshadow)
add_test(NAME params
  COMMAND test_params ${PLANT_DIR}/ASSUMPTIONS_PLANT.md ${PLANT_DIR}/plant.h ${PLANT_REPO_ARG})

# Generated files: params_default.json, params.schema.json, PARAMS.md must match the C table.
add_executable(plant_paramgen plant_paramgen.c)
target_link_libraries(plant_paramgen PRIVATE simplant)
set(GEN_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
add_test(NAME paramgen_run
  COMMAND ${CMAKE_COMMAND} -E make_directory ${GEN_DIR})
add_test(NAME paramgen_generate
  COMMAND plant_paramgen ${GEN_DIR}/params_default.json ${GEN_DIR}/params.schema.json ${GEN_DIR}/PARAMS.md)
set_tests_properties(paramgen_run PROPERTIES FIXTURES_SETUP paramgen_dir)
set_tests_properties(paramgen_generate PROPERTIES FIXTURES_REQUIRED paramgen_dir FIXTURES_SETUP paramgen_files)
foreach(f params_default.json params.schema.json PARAMS.md)
  add_test(NAME uptodate_${f}
    COMMAND ${CMAKE_COMMAND} -E compare_files ${GEN_DIR}/${f} ${PLANT_DIR}/${f})
  set_tests_properties(uptodate_${f} PROPERTIES FIXTURES_REQUIRED paramgen_files)
endforeach()

find_program(PLANT_PYTHON NAMES python3 python)
if(PLANT_PYTHON)
  add_test(NAME schema_valid
    COMMAND ${PLANT_PYTHON} ${CMAKE_CURRENT_SOURCE_DIR}/validate_params.py
            ${PLANT_DIR}/params_default.json ${PLANT_DIR}/params.schema.json)
endif()

# Regenerate the committed generated files after editing plant_params.h:
#   cmake --build <build> --target plant_regen_params
add_custom_target(plant_regen_params
  COMMAND plant_paramgen ${PLANT_DIR}/params_default.json ${PLANT_DIR}/params.schema.json ${PLANT_DIR}/PARAMS.md
  DEPENDS plant_paramgen
  COMMENT "Regenerating sim/plant params_default.json, params.schema.json and PARAMS.md")
