
set(CMAKE_CXX_STANDARD 11)
      
# set the main() files for the executables we want to build. 
add_executable(UnitTest_MemPoolHeap ExampleMempoolHeap.cpp)

# attach the library to the exectuables, and any other dependenciees we need
target_link_libraries(UnitTest_MemPoolHeap Lib_MemPoolHeap ) # pthread atomic rt)

target_compile_options(UnitTest_MemPoolHeap PRIVATE $<$<CXX_COMPILER_ID:GNU>: -Werror -Wall -Wextra> )