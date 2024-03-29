//
// Copyright (c) 2016-2019 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/vinniefalco/CppCon2018
//
// Modified by Keith Rausch

#include "websocket_session.hpp"
#include <iostream>


namespace BeastNetworking
{


template<class Derived>
    Derived& websocket_session<Derived>::derived()
    {
        return static_cast<Derived&>(*this);
    }


template<class Derived>
    void websocket_session<Derived>::on_error(beast::error_code ec)
    {
        // Don't report these
        if (ec == net::error::operation_aborted || ec == websocket::error::closed)
            return;

        if (state_)
            state_->on_error(endpoint, ec);
    }






template<class Derived>
  void websocket_session<Derived>::on_resolve(beast::error_code ec, tcp::resolver::results_type results)
  {
    if (ec)
      return on_error(ec);

    // Set the timeout for the operation
    beast::get_lowest_layer(derived().ws()).expires_after(std::chrono::seconds(30));

    // Make the connection on the IP address we get from a lookup
    beast::get_lowest_layer(derived().ws()).async_connect(
        results,
        beast::bind_front_handler(
            &Derived::on_connect,
            derived().shared_from_this()));
  }




template<class Derived>
    void websocket_session<Derived>::on_accept(beast::error_code ec)
    {
        // Handle the error, if any
        if (ec)
            return on_error(ec);

        // Add this session to the list of active sessions
        if (state_ && !upgraded.exchange(true))
            state_->upgrade(&derived());

        // Read a message
        derived().ws().async_read(
            buffer_,
            beast::bind_front_handler(&websocket_session::on_read, derived().shared_from_this()));
    }


template<class Derived>
    void websocket_session<Derived>::on_read( beast::error_code ec, std::size_t bytes_transferred)
    {
        // Handle the error, if any
        if (ec)
            return on_error(ec);

        // Send to all connections
        if (state_)
            state_->on_ws_read(endpoint, buffer_.data().data(), bytes_transferred);

        // Clear the buffer
        buffer_.consume(buffer_.size());

        // Read another message
        derived().ws().async_read(
            buffer_,
            beast::bind_front_handler(&websocket_session::on_read, derived().shared_from_this()));
    }


template<class Derived>
    void websocket_session<Derived>::on_send(const void* msgPtr, size_t msgSize, shared_state::CompletionHandlerT &&completionHandler, bool force_send, size_t max_queue_size)
    {
        // update hypothetical send stats here
        rate_enforcer.update_hypothetical_rate(msgSize);

        // if we have more messages in the queue than allowed, we need to start blowing them away
        // BUT we cant blow away messages marked as 'must_send' nomatter what
        // se we are basically doing our best to honor the max_queue_size, but will exceed it if we need to
        // in order to not drop important messages
        max_queue_size = std::max(max_queue_size, (size_t)2); // the 1st element is being sent right now, cant replace it

        for (size_t i = 2; i < queue_.size() && queue_.size() >= max_queue_size ; ++i)
        {
            auto & msg = queue_[i];
            auto & callback = std::get<2>(msg);
            auto force = std::get<3>(msg);
            if ( ! force)
            {
                if (callback)
                {
                    callback(boost::asio::error::operation_aborted, 0, endpoint);
                    callback = shared_state::CompletionHandlerT(); // not sure if this avoids move-sideeffects later
                }

                // queue_.pop_back();
                queue_.erase(queue_.begin() + i);
                // queue size recorded further down
                --i;
            }
        }


        queue_.emplace_back(msgPtr, msgSize, std::move(completionHandler), force_send);
        queue_size_ = queue_.size();

        // Are we already writing?
        if (queue_.size() > 1)
            return;

        bool may_send = rate_enforcer.should_send(msgSize) || force_send;
        may_send &= (msgPtr!=nullptr && msgSize>0);

        if ( may_send )
        {
            // We are not currently writing, so send this immediately
            derived().ws().async_write(
                boost::asio::buffer(msgPtr, msgSize),
                beast::bind_front_handler(&websocket_session::on_write, derived().shared_from_this()));
        }
        else
        {
            net::post(derived().ws().get_executor(), 
                beast::bind_front_handler(&websocket_session::on_write, derived().shared_from_this(), boost::asio::error::operation_aborted, 0)
                );
        }

    }


template<class Derived>
    void websocket_session<Derived>::on_write( beast::error_code ec, std::size_t bytes_transferred)
    {
        // call the user's completion handler
        auto& handler = std::get<2>(queue_.front());
        if (handler)
            handler(ec, bytes_transferred, endpoint);

        // Remove the string from the queue
        queue_.pop_front();
        queue_size_ = queue_.size();

        // Handle the error, if any
        if (ec)
        {
            on_error(ec);
        }

        // Send the next message if any
        if (!queue_.empty())
        {
            auto &next = queue_.front();
            const void *msgPtr = std::get<0>(next);
            size_t msgSize = std::get<1>(next);
            bool force = std::get<3>(next);

            bool may_send = rate_enforcer.should_send(msgSize) || force;
            may_send &= (msgPtr!=nullptr && msgSize>0);

            if (may_send)
            {
                derived().ws().async_write(
                    boost::asio::buffer(msgPtr, msgSize),
                    beast::bind_front_handler(&websocket_session::on_write, derived().shared_from_this()));
            }
            else
            {
                net::post(derived().ws().get_executor(), 
                    beast::bind_front_handler(&websocket_session::on_write, derived().shared_from_this(), boost::asio::error::operation_aborted, 0)
                 );
            }
        }
    }


template<class Derived>
  void websocket_session<Derived>::on_handshake(beast::error_code ec)
  {
    //
    // this function is only accessed when the user creates a websocket client 
    // which goes through the RunClient, on_resolve, on_connect, on_handshake chain
    //
    // when creating a connection this way, the normal call to state_->upgrade(&derived()); 
    // does not get executed, so we will call it here

    if (ec)
      return on_error(ec);

    derived().ws().binary(true);
    // https://stackoverflow.com/questions/7730260/binary-vs-string-transfer-over-a-stream
    // means bytes sent are bytes received, no UTF-8 text encode/decode

    // set max message length
    if (state_ && state_->read_message_max() > 0)
    {
        // NOTE: the following logic appeard to set the hint to 1536 despite what the documentation says
        // boost.org/doc/libs/develop/libs/beast/doc/html/beast/ref/boost__beast__websocket__stream/read_size_hint/overload1.html
        // uint64_t hint = (0 == ) ? derived().ws().read_size_hint() : state_->read_message_max();
        // derived().ws().read_message_max(hint);


        derived().ws().read_message_max(state_->read_message_max());
    }

    // Add this session to the list of active sessions
    if (state_ && ! upgraded.exchange(true))
        state_->upgrade(&derived());

    // Send the message
    derived().ws().async_read(
        buffer_,
        beast::bind_front_handler(&websocket_session::on_read, derived().shared_from_this()));
  }


template<class Derived>
    websocket_session<Derived>::websocket_session(
        std::shared_ptr<shared_state> const& state_in,
        const tcp::endpoint &endpoint_in) 
        : state_(state_in), 
        rate_enforcer(state_in && state_in->rate_tracker ? state_in->rate_tracker->make_enforcer(state_in->rate_enforcer_args) : RateLimiting::RateEnforcer()),
        serverPort(0), 
        endpoint(endpoint_in),
        upgraded(false), queue_size_(0)
    {
    }

template<class Derived>
    websocket_session<Derived>::websocket_session(net::io_context &ioc,
                      std::shared_ptr<shared_state> const& state_in,        
                      const std::string &serverAddress_in,
                      unsigned short serverPort_in) 
                      : state_(state_in), 
                        resolver_(std::make_shared<tcp::resolver>(net::make_strand(ioc))), 
                        rate_enforcer(state_in && state_in->rate_tracker ? state_in->rate_tracker->make_enforcer(state_in->rate_enforcer_args) : RateLimiting::RateEnforcer()),
                        serverAddress(serverAddress_in),
                        serverPort(serverPort_in),
                        endpoint(),
                        upgraded(false), queue_size_(0)
    {
    }


template<class Derived>
    websocket_session<Derived>::~websocket_session()
    {
        {
            // std::lock_guard<std::mutex> guard(mutex_);
            while (queue_.size() > 0)
            {
                auto & callback = std::get<2>(queue_.front());
                if (callback)
                    callback(boost::asio::error::operation_aborted, 0, endpoint);
                queue_.pop_front();
                queue_size_ = queue_.size();
            }
        }

        // Remove this session from the list of active sessions
        if (state_ && upgraded.exchange(false))
        {
            state_->downgrade(&derived());
        }
    }


template<class Derived>
    void websocket_session<Derived>::sendAsync(const void* msgPtr, size_t msgSize, shared_state::CompletionHandlerT completionHandler /*mae copy*/, bool force_send, size_t max_queue_size)
    {
    // Post our work to the strand, this ensures
    // that the members of `this` will not be
    // accessed concurrently.

    net::post(
        derived().ws().get_executor(),
        beast::bind_front_handler(&websocket_session::on_send, derived().shared_from_this(), msgPtr, msgSize, std::move(completionHandler), force_send, max_queue_size));
    }



  // Resolver and socket require an io_context
template<class Derived>
  void websocket_session<Derived>::RunClient()
  {
    // Look up the domain name
    resolver_->async_resolve(
        serverAddress,
        std::to_string(serverPort),
        beast::bind_front_handler(
            &websocket_session::on_resolve,
            derived().shared_from_this()));
  }


template class websocket_session<plain_websocket_session>;
template class websocket_session<ssl_websocket_session>;






//------------------------------------------------------------------------------

    plain_websocket_session::plain_websocket_session(
        beast::tcp_stream&& stream,
        std::shared_ptr<shared_state> const& state,
        const tcp::endpoint &endpoint
        )
        :  websocket_session<plain_websocket_session>(state, endpoint), ws_(std::move(stream))
    {
    }

    plain_websocket_session::plain_websocket_session(
        beast::tcp_stream&& stream,
        net::io_context &ioc,
        std::shared_ptr<shared_state> const& state,

        const std::string &serverAddress,
        unsigned short serverPort)
        :  websocket_session<plain_websocket_session>(ioc, state, serverAddress, serverPort), ws_(std::move(stream))
    {
    }

    // Called by the base class
    websocket::stream<beast::tcp_stream>&
    plain_websocket_session::ws()
    {
        return ws_;
    }




  void plain_websocket_session::on_connect(beast::error_code ec, tcp::resolver::results_type::endpoint_type ep)
  {
    endpoint = ep;

    if (ec)
      return websocket_session<plain_websocket_session>::on_error(ec);

    //
    // TODO look at boost.org/doc/libs/1_75_0/libs/beast/example/websocket/server/fast/websocket_server_fast.cpp
    // for improved performance. It looks like they play with compression rations, etc
    //
    //

    // Turn off the timeout on the tcp_stream, because
    // the websocket stream has its own timeout system.
    beast::get_lowest_layer(ws_).expires_never();

    // Set suggested timeout settings for the websocket
    ws_.set_option(get_timeout_settings_for_client());

    // Set a decorator to change the User-Agent of the handshake
    ws_.set_option(websocket::stream_base::decorator(
        [](websocket::request_type &req) {
          req.set(http::field::user_agent,
                  std::string(BOOST_BEAST_VERSION_STRING) +
                      " websocket-client-async");
        }));

    // Update the host_ string. This will provide the value of the
    // Host HTTP header during the WebSocket handshake.
    // See https://tools.ietf.org/html/rfc7230#section-5.4
    std::string host_ = serverAddress;
    host_ += ':' + std::to_string(ep.port());

    // Perform the websocket handshake
    ws_.async_handshake(host_, "/",
                        beast::bind_front_handler(
                            &websocket_session::on_handshake,
                            shared_from_this()));
  }


//------------------------------------------------------------------------------
ssl_websocket_session::ssl_websocket_session(
        beast::ssl_stream<beast::tcp_stream>&& stream,
        std::shared_ptr<shared_state> const& state,
        const tcp::endpoint &endpoint)
        : websocket_session<ssl_websocket_session>(state, endpoint), ws_(std::move(stream))
    {
    }

    
    ssl_websocket_session::ssl_websocket_session(
        beast::ssl_stream<beast::tcp_stream>&& stream,
        net::io_context &ioc,
        std::shared_ptr<shared_state> const& state,
        const std::string &serverAddress,
        unsigned short serverPort)
        : websocket_session<ssl_websocket_session>(ioc, state, serverAddress, serverPort), ws_(std::move(stream))
    {
    }

    // Called by the base class
    websocket::stream<beast::ssl_stream<beast::tcp_stream>>&
    ssl_websocket_session::ws()
    {
        return ws_;
    }


void ssl_websocket_session::on_connect(beast::error_code ec, tcp::resolver::results_type::endpoint_type ep)
{
    endpoint = ep;

    if(ec)
    {
        // fail(ec, "on_connect");
        return; 
    }

    // Update the host_ string. This will provide the value of the
    // Host HTTP header during the WebSocket handshake.
    // See https://tools.ietf.org/html/rfc7230#section-5.4
    // host_ += ':' + std::to_string(ep.port());

    std::string host_ = serverAddress;
    host_ += ':' + std::to_string(ep.port());

    // Set a timeout on the operation
    beast::get_lowest_layer(ws_).expires_after(std::chrono::seconds(30));

    // Set SNI Hostname (many hosts need this to handshake successfully)
    if(! SSL_set_tlsext_host_name(
            ws_.next_layer().native_handle(),
            host_.c_str()))
    {
        ec = beast::error_code(static_cast<int>(::ERR_get_error()), net::error::get_ssl_category());
        // fail(ec, "on_connect");
        return; 
    }

    // Perform the SSL handshake
    ws_.next_layer().async_handshake(
        ssl::stream_base::client,
        beast::bind_front_handler(
            &ssl_websocket_session::on_ssl_handshake,
            shared_from_this()));
}

  void ssl_websocket_session::on_ssl_handshake(beast::error_code ec)
  {

    if (ec)
      return on_error(ec);

    //
    // TODO look at boost.org/doc/libs/1_75_0/libs/beast/example/websocket/server/fast/websocket_server_fast.cpp
    // for improved performance. It looks like they play with compression rations, etc
    //
    //

    // Turn off the timeout on the tcp_stream, because
    // the websocket stream has its own timeout system.
    beast::get_lowest_layer(ws_).expires_never();

    // Set suggested timeout settings for the websocket
    ws_.set_option(get_timeout_settings_for_client());

    // Set a decorator to change the User-Agent of the handshake
    ws_.set_option(websocket::stream_base::decorator(
        [](websocket::request_type &req) {
          req.set(http::field::user_agent,
                  std::string(BOOST_BEAST_VERSION_STRING) +
                      " websocket-client-async");
        }));

    // Update the host_ string. This will provide the value of the
    // Host HTTP header during the WebSocket handshake.
    // See https://tools.ietf.org/html/rfc7230#section-5.4
    std::string host_ = serverAddress;
    host_ += ':' + std::to_string(serverPort); // std::to_string(ep.port());

    // Perform the websocket handshake
    ws_.async_handshake( host_ , "/",
                        beast::bind_front_handler(
                            &websocket_session::on_handshake,
                            shared_from_this()));
  }
    





} // namespace