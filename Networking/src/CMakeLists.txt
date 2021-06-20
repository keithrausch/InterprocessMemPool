
set(CMAKE_CXX_STANDARD 11)

# all headers
set(HEADERS_NETWORKING

    # top level classes
    MultiClientSender.h
    MultiClientReceiver.h

    # general utils
    UtilsASIO.h

    # websocket utility files
    net.h
    beast.h
    listener.h
    listener.cpp
    shared_state.h
    shared_state.cpp
    websocket_session.h
    websocket_session.cpp    
    websocket_client_async.h
    )
    
# all source files
set(SRC_NETWORKING)

# headers and source files together for convenience
set(HEADERS_AND_SRC_NETWORKING_LIB
    ${HEADERS_NETWORKING}
    ${SRC_NETWORKING})
  
# set up the library we are building
add_library(Lib_Networking ${HEADERS_AND_SRC_NETWORKING_LIB})
set_target_properties(Lib_Networking PROPERTIES LINKER_LANGUAGE CXX)

target_include_directories(Lib_Networking PUBLIC .)

