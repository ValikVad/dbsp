cmake_minimum_required (VERSION 3.12)

project(sp VERSION 1.0 LANGUAGES CXX C)
add_subdirectory(api)
add_subdirectory(impl)
add_subdirectory(utils)
add_subdirectory(examples)

execute_process(COMMAND
  git describe  --match=NeVeRmAtCh --always --abbrev=11 --dirty
  WORKING_DIRECTORY  ${CMAKE_SOURCE_DIR}
  OUTPUT_VARIABLE GIT_COMMIT_HASH
  ERROR_QUIET OUTPUT_STRIP_TRAILING_WHITESPACE)
target_compile_definitions(benchmark PRIVATE "GIT_COMMIT_HASH=\"${GIT_COMMIT_HASH}\"")

execute_process(COMMAND
  date
  WORKING_DIRECTORY  ${CMAKE_SOURCE_DIR}
  OUTPUT_VARIABLE BUILD_DATE
  ERROR_QUIET OUTPUT_STRIP_TRAILING_WHITESPACE)
target_compile_definitions(benchmark PRIVATE "BUILD_DATE=\"${BUILD_DATE}\"")

target_compile_features(sp_api
    INTERFACE cxx_std_17
)
