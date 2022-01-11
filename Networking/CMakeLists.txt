
option(BUILD_LIB_BEASTNETWORKING "Build the networking library" ON)
option(BUILD_LIB_BEASTNETWORKING_EXAMPLES "Build the networking library examples" ON)

if (BUILD_LIB_BEASTNETWORKING)
    add_subdirectory(src)
endif()

# if (BUILD_LIB_BEASTNETWORKING_EXAMPLES)
#     add_subdirectory(examples)
# endif()