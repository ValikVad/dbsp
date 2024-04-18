add_executable(benchmark
    benchmark.cpp
    src/sharded_cache.cpp
    src/worker.cpp
)

target_include_directories(benchmark
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
)

find_package(Threads REQUIRED)
find_package (Boost
  COMPONENTS
    program_options REQUIRED
)

target_link_libraries(benchmark
    PRIVATE
        sp_api
        sp_impl
        sp_utils
        
        Boost::program_options
)
