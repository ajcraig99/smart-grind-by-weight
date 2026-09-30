# WebAssembly build of the digital twin (same sources as the native build).
#   source /opt/sim-tools/emsdk/emsdk_env.sh   (or any emsdk)
#   emcmake cmake -S sim/wasm -B sim/out/wasm -G Ninja && ninja -C sim/out/wasm
cmake_minimum_required(VERSION 3.20)
project(grind_twin_wasm LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

get_filename_component(SIM_DIR "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
get_filename_component(REPO_DIR "${SIM_DIR}/.." ABSOLUTE)
include("${SIM_DIR}/cmake/twin_sources.cmake")

set(SIMPLANT_BUILD_TESTS OFF CACHE BOOL "" FORCE)
twin_add_targets()

set(TWIN_EXPORTS
  _twin_init _twin_set_scenario _twin_boot _twin_run_ms _twin_set_param _twin_get_param _twin_action
  _twin_fault _twin_touch _twin_framebuffer _twin_framebuffer_dirty _twin_operator_start
  _twin_restart_requested _twin_param_table _twin_state_json _twin_records_json _twin_log_read
  _twin_trace_read _twin_trace_header _twin_persist_size _twin_persist_export _twin_persist_import
  _twin_trace_suppress_header _twin_begin_post_reset _malloc _free)
list(JOIN TWIN_EXPORTS "," TWIN_EXPORTS_CSV)

add_executable(grindtwin "${SIM_DIR}/core/js_api.cpp")
target_link_libraries(grindtwin PRIVATE twin_firmware)
set_target_properties(grindtwin PROPERTIES SUFFIX ".js")
target_link_options(grindtwin PRIVATE
  --no-entry
  -sSINGLE_FILE=1
  -sMODULARIZE=1
  -sEXPORT_NAME=createGrindTwin
  -sENVIRONMENT=web,node
  -sASYNCIFY=1
  -sALLOW_MEMORY_GROWTH=1
  -sINITIAL_MEMORY=67108864
  -sSTACK_SIZE=1048576
  -sEXPORTED_FUNCTIONS=${TWIN_EXPORTS_CSV}
  -sEXPORTED_RUNTIME_METHODS=cwrap,ccall,UTF8ToString,HEAPU8,HEAPU16
  -sFILESYSTEM=0
)
