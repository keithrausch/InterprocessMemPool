//
// Copyright (c) 2016-2019 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/vinniefalco/CppCon2018
//
// Modified by Keith Rausch

#ifndef BEASTNETWORKING_WEBSOCKET_SESSION_HPP
#define BEASTNETWORKING_WEBSOCKET_SESSION_HPP

#include "net.hpp"
// #include "beast.h"
#include "shared_state.hpp"

// #include <cstdlib>
// #include <memory>
// #include <string>
// #include <queue>
// #include <mutex>

#include <boost/beast/websocket.hpp>
#include "rate_limiter.hpp"


namespace BeastNetworking
{

// Echoes back all received WebSocket messages.
// This uses the Curiously Recurring Template Pattern so that
// the same code works with both SSL streams and regular sockets.
template<class Derived>
class websocket_session
{
    // Access the derived class, this is part of
    // the Curiously Recurring Template Pattern idiom.
    Derived& derived();

    beast::flat_buffer buffer_;
    std::shared_ptr<shared_state> state_;
    std::deque<shared_state::SpanAndHandlerT> queue_; // no mutex needed, only ever modified inside handlers, which are in a strand


    std::shared_ptr<tcp::resolver> resolver_;
    RateLimiting::RateEnforcer rate_enforcer;




protected:
    void on_error(beast::error_code ec);






  void on_resolve(beast::error_code ec, tcp::resolver::results_type results);




    void on_accept(beast::error_code ec);


    void on_read( beast::error_code ec, std::size_t bytes_transferred);


    void on_send(const void* msgPtr, size_t msgSize, shared_state::CompletionHandlerT &&completionHandler, bool force_send=false, size_t max_queue_size=std::numeric_limits<size_t>::max());

    void on_write( beast::error_code ec, std::size_t bytes_transferred);

public:
    std::string serverAddress;
    unsigned short serverPort;

    tcp::endpoint endpoint;



  void on_handshake(beast::error_code ec);

    // Start the asynchronous operation
    template<class Body, class Allocator>
    void runServer(http::request<Body, http::basic_fields<Allocator>> req)
    {
        // // Accept the WebSocket upgrade request
        // do_accept(std::move(req));

        // Set suggested timeout settings for the websocket
        derived().ws().set_option(
            websocket::stream_base::timeout::suggested(
                beast::role_type::server));

        // Set a decorator to change the Server of the handshake
        derived().ws().set_option(websocket::stream_base::decorator(
            [](websocket::response_type& res)
            {
                res.set(http::field::server,
                    std::string(BOOST_BEAST_VERSION_STRING) +
                        " websocket-chat-multi");
            }));

        // Accept the websocket handshake
        derived().ws().async_accept(
            req,
            beast::bind_front_handler(
                &websocket_session::on_accept,
                derived().shared_from_this()));

        
        derived().ws().binary(true); 
        // https://stackoverflow.com/questions/7730260/binary-vs-string-transfer-over-a-stream
        // means bytes sent are bytes received, no UTF-8 text encode/decode
    }

    websocket_session(
        std::shared_ptr<shared_state> const& state_in,
        const tcp::endpoint &endpoint_in);

    websocket_session(net::io_context &ioc,
                      std::shared_ptr<shared_state> const& state_in,        
                      const std::string &serverAddress_in,
                      unsigned short serverPort_in) ;


    ~websocket_session();


    void sendAsync(const void* msgPtr, size_t msgSize, shared_state::CompletionHandlerT completionHandler, bool force_send=false, size_t max_queue_size=std::numeric_limits<size_t>::max());

    // void replaceSendAsync(const void* msgPtr, size_t msgSize, shared_state::CompletionHandlerT &&completionHandler);


  // Resolver and socket require an io_context
  void RunClient();




};

//------------------------------------------------------------------------------

// Handles a plain WebSocket connection
class plain_websocket_session
    : public websocket_session<plain_websocket_session>
    , public std::enable_shared_from_this<plain_websocket_session>
{
    websocket::stream<beast::tcp_stream> ws_;

public:
    // Create the session
    explicit
    plain_websocket_session(
        beast::tcp_stream&& stream,
        std::shared_ptr<shared_state> const& state,
        const tcp::endpoint &endpoint
        );

    explicit
    plain_websocket_session(
        beast::tcp_stream&& stream,
        net::io_context &ioc,
        std::shared_ptr<shared_state> const& state,

        const std::string &serverAddress,
        unsigned short serverPort);

    // Called by the base class
    websocket::stream<beast::tcp_stream>&
    ws();




  void on_connect(beast::error_code ec, tcp::resolver::results_type::endpoint_type ep);

};

//------------------------------------------------------------------------------

// Handles an SSL WebSocket connection
class ssl_websocket_session
    : public websocket_session<ssl_websocket_session>
    , public std::enable_shared_from_this<ssl_websocket_session>
{
    websocket::stream<
        beast::ssl_stream<beast::tcp_stream>> ws_;

public:
    // Create the ssl_websocket_session
    explicit
    ssl_websocket_session(
        beast::ssl_stream<beast::tcp_stream>&& stream,
        std::shared_ptr<shared_state> const& state,
        const tcp::endpoint &endpoint);

    explicit
    ssl_websocket_session(
        beast::ssl_stream<beast::tcp_stream>&& stream,
        net::io_context &ioc,
        std::shared_ptr<shared_state> const& state,
        const std::string &serverAddress,
        unsigned short serverPort);

    // Called by the base class
    websocket::stream<beast::ssl_stream<beast::tcp_stream>>&
    ws();


void on_connect(beast::error_code ec, tcp::resolver::results_type::endpoint_type ep);

  void on_ssl_handshake(beast::error_code ec);
    
};




template<class Body, class Allocator>
static void
make_websocket_session_server(
    beast::tcp_stream stream,
        std::shared_ptr<shared_state> const& state,
        const tcp::endpoint &endpoint,
    http::request<Body, http::basic_fields<Allocator>> req)
{
    std::make_shared<plain_websocket_session>(
        std::move(stream), state, endpoint)->runServer(std::move(req));
}

template<class Body, class Allocator>
static void
make_websocket_session_server(
    beast::ssl_stream<beast::tcp_stream> stream,
        std::shared_ptr<shared_state> const& state,
        const tcp::endpoint &endpoint,
    http::request<Body, http::basic_fields<Allocator>> req)
{
    std::make_shared<ssl_websocket_session>(
        std::move(stream), state, endpoint)->runServer(std::move(req));
}





// template<class Body, class Allocator>
static std::shared_ptr<plain_websocket_session>
make_websocket_session_client(net::io_context &ioc,
                              std::shared_ptr<shared_state> const& state,
                              const std::string &serverAddress,
                              unsigned short serverPort)
{
    return std::make_shared<plain_websocket_session>(beast::tcp_stream(net::make_strand(ioc)), ioc, state, serverAddress, serverPort);
}

// template<class Body, class Allocator>
static std::shared_ptr<ssl_websocket_session>
make_websocket_session_client(net::io_context &ioc,
                              ssl::context& ctx,
                              std::shared_ptr<shared_state> const& state,
                              const std::string &serverAddress,
                              unsigned short serverPort)
{
    return std::make_shared<ssl_websocket_session>(beast::ssl_stream<beast::tcp_stream>(net::make_strand(ioc), ctx), ioc, state, serverAddress, serverPort);
}



} // namespace


#endif