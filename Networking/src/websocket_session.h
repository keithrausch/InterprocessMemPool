//
// Copyright (c) 2016-2019 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/vinniefalco/CppCon2018
//
// Modified by Keith Rausch

#ifndef BOOST_BEAST_EXAMPLE_WEBSOCKET_CHAT_MULTI_WEBSOCKET_SESSION_HPP
#define BOOST_BEAST_EXAMPLE_WEBSOCKET_CHAT_MULTI_WEBSOCKET_SESSION_HPP

#include "net.h"
#include "beast.h"
#include "shared_state.h"

#include <cstdlib>
#include <memory>
#include <string>
#include <queue>

// Forward declaration
class shared_state;

/** Represents an active WebSocket connection to the server
*/
class websocket_session : public std::enable_shared_from_this<websocket_session>
{
    beast::flat_buffer buffer_;
    websocket::stream<beast::tcp_stream> ws_;
    // websocket::stream<boost::asio::ssl::stream<beast::tcp_stream>> ws_;
    std::shared_ptr<shared_state> state_;


    std::queue<shared_state::SharedSerializedAndReturnedT> queue_; // no mutex needed, only ever modified inside handlers, which are in a strang


    void on_error(beast::error_code ec);
    void on_accept(beast::error_code ec);
    void on_read(beast::error_code ec, std::size_t bytes_transferred);
    void on_write(beast::error_code ec, std::size_t bytes_transferred);

public:
    tcp::endpoint endpoint;
    
    websocket_session(
        tcp::socket&& socket,
        // boost::asio::ssl::stream<beast::tcp_stream>&& socket,
        std::shared_ptr<shared_state> const& state,
        const tcp::endpoint &endpoint);

    ~websocket_session();

    void
    run();

    // Send a message
    void
    sendAsync(shared_state::SharedSerializedAndReturnedT serialized);

private:
    void
    on_send(shared_state::SharedSerializedAndReturnedT serialized);
};


#endif