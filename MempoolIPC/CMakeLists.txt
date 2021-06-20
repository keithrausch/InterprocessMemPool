
option(BUILD_LIB_MEMPOOLIPC "Build the mempool-ipc library" ON)
option(BUILD_LIB_MEMPOOLIPC_EXAMPLES "Build the mempool-ipc library examples" ON)

if (BUILD_LIB_MEMPOOLIPC)
    add_subdirectory(src)
endif()

if (BUILD_LIB_MEMPOOLIPC_EXAMPLES)
    add_subdirectory(examples)
endif()