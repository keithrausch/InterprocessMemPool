
option(BUILD_LIB_MEMPOOLHEAP "Build the mempool-heap library" ON)
option(BUILD_LIB_MEMPOOLHEAP_EXAMPLES "Build the mempool-heap library examples" ON)


if (BUILD_LIB_MEMPOOLHEAP)
    add_subdirectory(src)
endif()

if (BUILD_LIB_MEMPOOLHEAP_EXAMPLES)
    add_subdirectory(examples)
endif()