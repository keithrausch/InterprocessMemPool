
set(CMAKE_CXX_STANDARD 17) # using std::filesystem, shared_mutex, all else is c++11

set(LIB_NAME lib_beastNetworking)


# all headers
set(HEADERS_${LIB_NAME}

    # top level classes
    MultiClientSender.hpp
    MultiClientReceiver.hpp
    listener.hpp
    rate_limiter.hpp
    
    # general utils
    UtilsASIO.hpp
    
    # websocket utility files
    http_session.hpp
    net.hpp
    shared_state.hpp
    websocket_session.hpp
    )
    
    
    # all source files
set(SRC_${LIB_NAME}
    http_session.cpp
    listener.cpp
    shared_state.cpp
    websocket_session.cpp    
    rate_limiter.cpp
    )

#
# SSL SUPPORT REQUIRES OPENSSL TO BE INSTALL:
# sudo apt-get install libssl-dev
#
find_package(OpenSSL REQUIRED)

# find_package(Boost 1.70 REQUIRED COMPONENTS system thread filesystem)
include_directories(../boost/boost_1_76_0/boost)
  
# set up the library we are building
add_library(${LIB_NAME} STATIC ${HEADERS_${LIB_NAME}} ${SRC_${LIB_NAME}})
set_target_properties(${LIB_NAME} PROPERTIES LINKER_LANGUAGE CXX)

target_include_directories(${LIB_NAME} PUBLIC .)

target_link_libraries(${LIB_NAME}
    atomic
    OpenSSL::SSL 
    OpenSSL::Crypto
)

set_target_properties(${LIB_NAME} PROPERTIES POSITION_INDEPENDENT_CODE ON)

