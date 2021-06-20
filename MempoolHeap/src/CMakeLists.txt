
set(CMAKE_CXX_STANDARD 11)
  
# all headers
set(HEADERS_MEMPOOL_HEAP
    MemPoolHeap.h)
    
# all source files
set(SRC_MEMPOOL_HEAP)

# headers and source files together for convenience
set(HEADERS_AND_SRC_MEMPOOL_HEAP_LIB
    ${HEADERS_MEMPOOL_HEAP}
    ${SRC_MEMPOOL_HEAP})

# set up the library we are building
add_library(Lib_MemPoolHeap ${HEADERS_AND_SRC_MEMPOOL_HEAP_LIB})
set_target_properties(Lib_MemPoolHeap PROPERTIES LINKER_LANGUAGE CXX)

target_include_directories(Lib_MemPoolHeap PUBLIC .)
