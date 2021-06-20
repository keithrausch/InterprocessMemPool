
set(CMAKE_CXX_STANDARD 11)

# find external packages
find_package(Boost 1.70 REQUIRED COMPONENTS filesystem) # system thread filesystem)
 
# set the main() files for the executables we want to build. 
add_executable(Example_MemPoolIPC       Example_MemPoolIPC.cpp)
add_executable(Example_MemPoolArrayIPC  Example_MemPoolArrayIPC.cpp)
add_executable(Example_MemPoolGrowerIPC Example_MemPoolGrowerIPC.cpp)

target_compile_options(Example_MemPoolIPC       PRIVATE $<$<CXX_COMPILER_ID:GNU>: -Werror -Wall -Wextra> )
target_compile_options(Example_MemPoolArrayIPC  PRIVATE $<$<CXX_COMPILER_ID:GNU>: -Werror -Wall -Wextra> )
target_compile_options(Example_MemPoolGrowerIPC PRIVATE $<$<CXX_COMPILER_ID:GNU>: -Werror -Wall -Wextra> )

# attach the library to the exectuables, and any other dependenciees we need
target_link_libraries(Example_MemPoolIPC Lib_MemPoolIPC 
    $<$<CXX_COMPILER_ID:GNU>:pthread>
    $<$<CXX_COMPILER_ID:GNU>:rt>
    Boost::filesystem) 

target_link_libraries(Example_MemPoolArrayIPC Lib_MemPoolIPC
    $<$<CXX_COMPILER_ID:GNU>:pthread>
    $<$<CXX_COMPILER_ID:GNU>:rt>
    Boost::filesystem) 

target_link_libraries(Example_MemPoolGrowerIPC Lib_MemPoolIPC
    $<$<CXX_COMPILER_ID:GNU>:pthread>
    $<$<CXX_COMPILER_ID:GNU>:rt>
    Boost::filesystem) 
