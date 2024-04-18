
# find libglib2.0-dev
find_package(PkgConfig REQUIRED)
pkg_check_modules(deps REQUIRED IMPORTED_TARGET glib-2.0)

add_library(sp_utils INTERFACE)
target_include_directories(sp_utils INTERFACE
${CMAKE_CURRENT_SOURCE_DIR}/readers/include
${CMAKE_CURRENT_SOURCE_DIR}/predictor_utils/include
)

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
target_link_libraries(sp_utils INTERFACE Threads::Threads)

