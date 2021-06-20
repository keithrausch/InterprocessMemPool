
set(CMAKE_CXX_STANDARD 11)

# all headers
set(HEADERS_MEMPOOL_IPC
    UtilsIPC.h
    RouterIPC.h
    MemPoolIPC.h
    )
    
# all source files
set(SRC_MEMPOOL_IPC)

# headers and source files together for convenience
set(HEADERS_AND_SRC_MEMPOOL_IPC_LIB
    ${HEADERS_MEMPOOL_IPC}
    ${SRC_MEMPOOL_IPC})

# set up the library we are building
add_library(Lib_MemPoolIPC ${HEADERS_AND_SRC_MEMPOOL_IPC_LIB})
set_target_properties(Lib_MemPoolIPC PROPERTIES LINKER_LANGUAGE CXX)

target_include_directories(Lib_MemPoolIPC PUBLIC .)
