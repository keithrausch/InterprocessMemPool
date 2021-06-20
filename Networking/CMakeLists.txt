
option(BUILD_LIB_NETWORKING "Build the networking library" ON)
option(BUILD_LIB_NETWORKING_EXAMPLES "Build the networking library examples" ON)

if (BUILD_LIB_NETWORKING)
    add_subdirectory(src)
endif()

if (BUILD_LIB_NETWORKING_EXAMPLES)
    add_subdirectory(examples)
endif()