//
// Copyright (c) 2016-2019 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/vinniefalco/CppCon2018
//
// Modified by Keith Rausch

#ifndef BEASTWEBSERVERFLEXIBLE_SHARED_STATE_HPP
#define BEASTWEBSERVERFLEXIBLE_SHARED_STATE_HPP


// #include <boost/beast/core.hpp>
// #include <boost/beast/http.hpp>
// #include <boost/beast/ssl.hpp>
#include "net.hpp"


// stuff for shared_state
#include <memory>
#include <mutex>
#include <string>
#include <unordered_set>
#include <functional>
// #include "beast.h"
#include <queue>


namespace BeastNetworking
{

namespace
{
    namespace beast = boost::beast;                 // from <boost/beast.hpp>
    namespace http = beast::http;                   // from <boost/beast/http.hpp>
    namespace websocket = beast::websocket;         // from <boost/beast/websocket.hpp>
    namespace net = boost::asio;                    // from <boost/asio.hpp>
    namespace ssl = boost::asio::ssl;               // from <boost/asio/ssl.hpp>
    using tcp = boost::asio::ip::tcp;               // from <boost/asio/ip/tcp.hpp>
}



// Forward declaration
class plain_websocket_session;
class ssl_websocket_session;

// Represents the shared server state
class shared_state
{
  std::string /*const*/ doc_root_;

  // This mutex synchronizes all access to sessions_
  typedef std::recursive_mutex MutexT;
  MutexT mutex_;

  // Keep a list of all the connected clients
  std::unordered_set<plain_websocket_session *> sessions_;
  std::unordered_set<ssl_websocket_session *> sessionsSSL_;
  std::vector<std::weak_ptr<plain_websocket_session>> sessionPointerPool; // this is an attempt to find a slow-down in the code. consider removing
  std::vector<std::weak_ptr<ssl_websocket_session>> sessionPointerPoolSSL; // this is an attempt to find a slow-down in the code. consider removing

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

  shared_state();
  shared_state(std::string doc_root);
  shared_state(const Callbacks &callbacks_in);
  shared_state(std::string doc_root, const Callbacks &callbacks_in);

    std::string const&
    doc_root() const noexcept
    {
        return doc_root_;
    }

  size_t nSessions();

  void join(plain_websocket_session *session);
  void leave(plain_websocket_session *session);
  void join(ssl_websocket_session *session);
  void leave(ssl_websocket_session *session);
  void sendAsync(const void * msgPtr, size_t msgSize, CompletionHandlerT &&completionHandler = CompletionHandlerT(), bool overwrite = false);
  void sendAsync(const boost::asio::ip::tcp::endpoint &endpoint, const void * msgPtr, size_t msgSize, CompletionHandlerT &&completionHandler = CompletionHandlerT(), bool overwrite = false);
  void on_read(const boost::asio::ip::tcp::endpoint &endpoint, const void * msgPtr, size_t msgSize);
  void on_error(const boost::asio::ip::tcp::endpoint &endpoint, beast::error_code ec);


  void sendAsync(const std::string &str);
  void sendAsync(const boost::asio::ip::tcp::endpoint &endpoint, const std::string &str);

  void replaceSendAsync(const void * msgPtr, size_t msgSize, CompletionHandlerT &&completionHandler = CompletionHandlerT());
  void replaceSendAsync(const boost::asio::ip::tcp::endpoint &endpoint, const void * msgPtr, size_t msgSize, CompletionHandlerT &&completionHandler = CompletionHandlerT());
};

} // namespace


#endif