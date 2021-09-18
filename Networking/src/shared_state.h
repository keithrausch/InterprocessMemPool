//
// Copyright (c) 2016-2019 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/vinniefalco/CppCon2018
//
// Modified by Keith Rausch

#ifndef BOOST_BEAST_EXAMPLE_WEBSOCKET_CHAT_MULTI_SHARED_STATE_HPP
#define BOOST_BEAST_EXAMPLE_WEBSOCKET_CHAT_MULTI_SHARED_STATE_HPP

#include <boost/smart_ptr.hpp>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_set>
#include <functional>
#include "beast.h"


namespace IPC
{

// Forward declaration
class websocket_session;

// Represents the shared server state
class shared_state
{


  // This mutex synchronizes all access to sessions_
  typedef std::recursive_mutex MutexT;
  MutexT mutex_;

  // Keep a list of all the connected clients
  std::unordered_set<websocket_session *> sessions_;
  std::vector<std::weak_ptr<websocket_session>> sessionPointerPool; // this is an attempt to find a slow-down in the code. consider removing

public:
  struct Callbacks
  {
    typedef boost::asio::ip::tcp::endpoint endpointT;
    typedef std::function<void(const endpointT &endpoint, const void *msgPtr, size_t msgSize)> CallbackReadT;
    typedef std::function<void(const endpointT &endpoint)> CallbackSocketAcceptT;
    typedef std::function<void(const endpointT &endpoint)> CallbackSocketCloseT;
    typedef std::function<void(const endpointT &endpoint, const beast::error_code &ec)> CallbackErrorT;

    CallbackReadT callbackRead;
    CallbackSocketAcceptT callbackAccept;
    CallbackSocketCloseT callbackClose;
    CallbackErrorT callbackError;
  };

  Callbacks callbacks;


  typedef std::function<void(beast::error_code, size_t)> CompletionHandlerT;
  typedef std::tuple<const void*, size_t, CompletionHandlerT> SpanAndHandlerT;
  
  // std::vector<shared_serialized_and_returned_T> queue_; // woah this is a vector, its even a vector in the beast example. weird.

  explicit shared_state(const Callbacks &callbacks_in);

  void join(websocket_session *session);
  void leave(websocket_session *session);
  void sendAsync(const void * msgPtr, size_t msgSize, CompletionHandlerT &&completionHandler = CompletionHandlerT());
  void sendAsync(const boost::asio::ip::tcp::endpoint &endpoint, const void * msgPtr, size_t msgSize, CompletionHandlerT &&completionHandler = CompletionHandlerT());
  void on_read(const boost::asio::ip::tcp::endpoint &endpoint, const void *msgPtr, size_t msgSize);
  void on_error(const boost::asio::ip::tcp::endpoint &endpoint, beast::error_code ec);


  void sendAsync(const std::string &str);
  void sendAsync(const boost::asio::ip::tcp::endpoint &endpoint, const std::string &str);
};

} // namespace

#endif