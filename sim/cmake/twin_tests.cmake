# Host tests of the twin (ctest).
add_executable(test_scheduler "${SIM_DIR}/host/tests/test_scheduler.cpp")
target_link_libraries(test_scheduler PRIVATE twin_firmware)
add_test(NAME twin_scheduler COMMAND test_scheduler)
add_test(NAME twin_smoke COMMAND "${SIM_DIR}/host/tests/smoke.sh" $<TARGET_FILE:grindsim> "${SIM_DIR}/scenarios/normal.json")
add_test(NAME twin_determinism COMMAND "${SIM_DIR}/host/tests/determinism.sh" $<TARGET_FILE:grindsim> "${SIM_DIR}/scenarios/normal.json")
