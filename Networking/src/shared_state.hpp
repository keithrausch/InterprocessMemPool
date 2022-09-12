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


#include "net.hpp"
#include "rate_limiter.hpp"


// stuff for shared_state
#include <memory>
#include <mutex>
#include <string>
#include <unordered_set>
#include <functional>
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
class plain_http_session;
class ssl_http_session;

// Represents the shared server state
class shared_state
{
  std::string /*const*/ doc_root_;

  // This mutex synchronizes all access to all sessions
  typedef std::recursive_mutex MutexT;
  MutexT mutex_;

  // Keep a list of all the connected clients
  std::unordered_set<plain_websocket_session *> ws_sessions_;
  std::unordered_set<ssl_websocket_session *> wss_sessions_;
  std::unordered_set<plain_http_session *> http_sessions_;
  std::unordered_set<ssl_http_session *> https_sessions_;
  std::vector<std::weak_ptr<plain_websocket_session>> ws_sessionPointerPool; // this is an attempt to find a slow-down in the code. consider removing
  std::vector<std::weak_ptr<ssl_websocket_session>> wss_sessionPointerPool; // this is an attempt to find a slow-down in the code. consider removing
  std::vector<std::weak_ptr<plain_http_session>> http_sessionPointerPool; // this is an attempt to find a slow-down in the code. consider removing
  std::vector<std::weak_ptr<ssl_http_session>> https_sessionPointerPool; // this is an attempt to find a slow-down in the code. consider removing

public:
  struct Callbacks
  {
    typedef boost::asio::ip::tcp::endpoint endpointT;
    typedef std::function<void(const endpointT &endpoint, const void *msgPtr, size_t msgSize)> CallbackReadT;
    typedef std::function<void(const endpointT &endpoint)> CallbackSocketAcceptT;
    typedef std::function<void(const endpointT &endpoint)> CallbackSocketUpgradeT;
    typedef std::function<void(const endpointT &endpoint)> CallbackSocketDowngradeT;
    typedef std::function<void(const endpointT &endpoint)> CallbackSocketCloseT;
    typedef std::function<void(const endpointT &endpoint, const beast::error_code &ec)> CallbackErrorT;

    CallbackReadT callbackWSRead;
    CallbackReadT callbackHTTPRead;
    CallbackSocketAcceptT callbackAccept;
    CallbackSocketAcceptT callbackUpgrade;
    CallbackSocketAcceptT callbackDowngrade;
    CallbackSocketCloseT callbackClose;
    CallbackErrorT callbackError;
  };

  Callbacks callbacks;
  RateLimiting::RateEnforcer::Args rate_enforcer_args; // default
  RateLimiting::sRateTracker rate_tracker;

  typedef boost::asio::ip::tcp::endpoint endpointT;
  typedef std::function<void(beast::error_code, size_t, const endpointT &)> CompletionHandlerT;
  typedef std::tuple<const void*, size_t, CompletionHandlerT, bool> SpanAndHandlerT;

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

  size_t nSessions(size_t &insecure, size_t &secure);

  void upgrade(plain_websocket_session *ws_session);
  void downgrade(plain_websocket_session *ws_session);
  void upgrade(ssl_websocket_session *wss_session);
  void downgrade(ssl_websocket_session *wss_session);
  void join(plain_http_session *session);
  void leave(plain_http_session *session);
  void join(ssl_http_session *session);
  void leave(ssl_http_session *session);
  void on_ws_read(const boost::asio::ip::tcp::endpoint &endpoint, const void * msgPtr, size_t msgSize);
  void on_http_read(const boost::asio::ip::tcp::endpoint &endpoint, const void * msgPtr, size_t msgSize);
  void on_error(const boost::asio::ip::tcp::endpoint &endpoint, beast::error_code ec);


  void sendAsync(const void * msgPtr, size_t msgSize, const CompletionHandlerT &completionHandler = CompletionHandlerT(), bool force_send=false, size_t max_queue_size=std::numeric_limits<size_t>::max(), bool to_ws=true, bool to_tcp=false);
  void sendAsync(const boost::asio::ip::tcp::endpoint &endpoint, const void * msgPtr, size_t msgSize, const CompletionHandlerT &completionHandler = CompletionHandlerT(), bool force_send=false, size_t max_queue_size=std::numeric_limits<size_t>::max());
  void sendAsync(const std::string &str, bool force_send=false, size_t max_queue_size=std::numeric_limits<size_t>::max(), bool to_ws=true, bool to_tcp=false);
  void sendAsync(const boost::asio::ip::tcp::endpoint &endpoint, const std::string &str, bool force_send=false, size_t max_queue_size=std::numeric_limits<size_t>::max());

  std::vector<boost::asio::ip::tcp::endpoint> get_endpoints();
};

} // namespace


#endif