
set(CMAKE_CXX_STANDARD 17)

# find external packages
find_package(Boost 1.70 REQUIRED COMPONENTS filesystem) # system thread filesystem)
 
# set the main() files for the executables we want to build. 
add_executable(Example_Networking Example_Networking.cpp )

target_compile_options(Example_Networking PRIVATE $<$<CXX_COMPILER_ID:GNU>: -Werror -Wall -Wextra> -Wno-unused-function)

# attach the library to the exectuables, and any other dependenciees we need
target_link_libraries(Example_Networking lib_beastNetworking Lib_MemPoolIPC
    $<$<CXX_COMPILER_ID:GNU>:pthread>
    $<$<CXX_COMPILER_ID:GNU>:rt>
    Boost::filesystem 
    )

