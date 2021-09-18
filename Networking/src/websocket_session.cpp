//
// Copyright (c) 2016-2019 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/vinniefalco/CppCon2018
//
// Modified by Keith Rausch

#include "websocket_session.h"
#include <iostream>


namespace IPC
{

websocket_session::websocket_session(
    tcp::socket &&socket,
    std::shared_ptr<shared_state> const &state, const tcp::endpoint &endpoint_in)
    : ws_(std::move(socket)), state_(state), endpoint(endpoint_in)
{
  ws_.binary(true); 
  // https://stackoverflow.com/questions/7730260/binary-vs-string-transfer-over-a-stream
  // means bytes sent are bytes received, no UTF-8 text encode/decode
}

websocket_session::~websocket_session()
{
  {
    // std::lock_guard<std::mutex> guard(mutex_);
    while (queue_.size() > 0)
    {
      auto & callback = std::get<2>(queue_.front());
      if (callback)
        callback(boost::asio::error::operation_aborted, 0);
      queue_.pop();
    }
  }

  // Remove this session from the list of active sessions
  state_->leave(this);
}

void websocket_session::on_error(beast::error_code ec)
{
  // Don't report these
  if (ec == net::error::operation_aborted ||
      ec == websocket::error::closed)
    return;

  state_->on_error(endpoint, ec);
}

void websocket_session::on_accept(beast::error_code ec)
{
  // Handle the error, if any
  if (ec)
    return on_error(ec);

  // Add this session to the list of active sessions
  state_->join(this);

  // Read a message
  ws_.async_read(
      buffer_,
      beast::bind_front_handler(&websocket_session::on_read, shared_from_this()));
}

void websocket_session::on_read(beast::error_code ec, std::size_t bytes_transferred)
{
  boost::ignore_unused(bytes_transferred);

  // Handle the error, if any
  if (ec)
    return on_error(ec);

  // Send to all connections
  state_->on_read(endpoint, buffer_.data().data(), bytes_transferred);

  // Clear the buffer
  buffer_.consume(buffer_.size());

  // Read another message
  ws_.async_read(
      buffer_,
      beast::bind_front_handler(&websocket_session::on_read, shared_from_this()));
}

void websocket_session::sendAsync(const void* msgPtr, size_t msgSize, shared_state::CompletionHandlerT &&completionHandler)
{
  // Post our work to the strand, this ensures
  // that the members of `this` will not be
  // accessed concurrently.

  net::post(
      ws_.get_executor(),
      beast::bind_front_handler(&websocket_session::on_send, shared_from_this(), msgPtr, msgSize, std::forward<shared_state::CompletionHandlerT>(completionHandler), false));
}

void websocket_session::replaceSendAsync(const void* msgPtr, size_t msgSize, shared_state::CompletionHandlerT &&completionHandler)
{
  // Post our work to the strand, this ensures
  // that the members of `this` will not be
  // accessed concurrently.

  net::post(
      ws_.get_executor(),
      beast::bind_front_handler(&websocket_session::on_send, shared_from_this(), msgPtr, msgSize, std::forward<shared_state::CompletionHandlerT>(completionHandler), true));
}

void websocket_session::on_send(const void* msgPtr, size_t msgSize, shared_state::CompletionHandlerT &&completionHandler, bool overwrite)
{
  // std::lock_guard<std::mutex> guard(mutex_);

  if (overwrite && queue_.size() > 1)
  {
    auto & back = queue_.back();
    auto & callback = std::get<2>(back);
    if (callback)
    {
      callback(boost::asio::error::operation_aborted, 0);
      callback = shared_state::CompletionHandlerT(); // not sure if this avoids move-sideeffects later
    }
      
    std::get<0>(back) = msgPtr;
    std::get<1>(back) = msgSize;
    std::get<2>(back) = std::move(completionHandler);

    return;
  }

  // Always add to queue
  queue_.emplace(msgPtr, msgSize, std::move(completionHandler));


  // Are we already writing?
  if (queue_.size() > 1)
    return;

  // We are not currently writing, so send this immediately
  ws_.async_write(
      boost::asio::buffer(msgPtr, msgSize),
      beast::bind_front_handler(&websocket_session::on_write, shared_from_this()));
}

void websocket_session::on_write(beast::error_code ec, std::size_t writtenLength)
{
  // std::lock_guard<std::mutex> guard(mutex_);

  // call the user's completion handler
  auto& handler = std::get<2>(queue_.front());
  if (handler)
    handler(ec, writtenLength);

  // Remove the string from the queue
  queue_.pop();

  // Handle the error, if any
  if (ec)
    return on_error(ec);

  // Send the next message if any
  if (!queue_.empty())
  {
    auto &next = queue_.front();
    const void *msgPtr = std::get<0>(next);
    size_t msgSize = std::get<1>(next);

    ws_.async_write(
        boost::asio::buffer(msgPtr, msgSize),
        beast::bind_front_handler(&websocket_session::on_write, shared_from_this()));
  }
}

void websocket_session::run()
{
  // Set suggested timeout settings for the websocket
  ws_.set_option(
      websocket::stream_base::timeout::suggested(
          beast::role_type::server));

  // Set a decorator to change the Server of the handshake
  ws_.set_option(websocket::stream_base::decorator(
      [](websocket::response_type &res) {
        res.set(http::field::server,
                std::string(BOOST_BEAST_VERSION_STRING) +
                    " websocket-chat-multi");
      }));

  // Accept the websocket handshake
  ws_.async_accept(beast::bind_front_handler(&websocket_session::on_accept, shared_from_this()));
}

} // namespace