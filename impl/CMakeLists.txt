add_library(sp_impl
    src/factory.cpp
    src/lru.cpp
    src/dbsp.cpp
)
configure_file(config.h.in config.h)

target_include_directories(sp_impl
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PUBLIC
        ${CMAKE_CURRENT_BINARY_DIR}  # config.h
)

target_link_libraries (sp_impl
    PRIVATE sp_api 
)
