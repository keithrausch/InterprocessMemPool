//
// Copyright (c) 2016-2019 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/boostorg/beast
//
// modified by Keith Rausch

//------------------------------------------------------------------------------
//
// Example: Advanced server, flex (plain + SSL)
//
//------------------------------------------------------------------------------

#include "shared_state.hpp"
#include "websocket_session.hpp"
#include "http_session.hpp"

namespace BeastNetworking
{

shared_state::shared_state()
    : doc_root_(), 
    server_is_tcp_instead_of_http_(false), 
    callbacks()
{
}

shared_state::shared_state(std::string doc_root )
    : doc_root_(std::move(doc_root)), 
    server_is_tcp_instead_of_http_(false), 
    nonhttp_header_size_(0),
    callbacks()
{
}

shared_state::shared_state(const Callbacks &callbacks_in)
    : doc_root_(), 
    server_is_tcp_instead_of_http_(false), 
    nonhttp_header_size_(0), 
    callbacks(callbacks_in)
{
}

shared_state::shared_state(std::string doc_root, const Callbacks &callbacks_in, bool server_is_tcp_instead_of_http_in, size_t nonhttp_header_size_in)
    : doc_root_(std::move(doc_root)), 
    server_is_tcp_instead_of_http_(server_is_tcp_instead_of_http_in), 
    nonhttp_header_size_(nonhttp_header_size_in), 
    callbacks(callbacks_in)
{
}

std::string const& shared_state::doc_root() const noexcept
{
    return doc_root_;
}

uint64_t shared_state::read_message_max()
{
  return read_message_max_;
}

void shared_state::read_message_max(uint64_t size)
{
  read_message_max_ = size;
}

void shared_state::check_doc_root()
{
  if (callbacks.callbackPrint)
  {
    if ( ! std::filesystem::exists(doc_root_))
    {
      callbacks.callbackPrint("root directory does not exist \"" + doc_root_ + "\"");
    }
    else
    {
      callbacks.callbackPrint("starting server at root \"" + doc_root_ + "\"");
    }
  }
}

size_t shared_state::nSessions(size_t &insecure, size_t &secure) const
{
  std::lock_guard<MutexT> lock(mutex_);
  insecure = ws_sessions_.size() + http_sessions_.size();
  secure = wss_sessions_.size() + https_sessions_.size();
  return insecure + secure;
}

size_t shared_state::queue_sizes_summed() const
{
  std::lock_guard<MutexT> lock(mutex_);

  size_t queue_sizes_summed = 0;
  
  for (auto p : ws_sessions_)
  {
    auto strong = p->weak_from_this().lock();
    if (strong)
      queue_sizes_summed += strong->queue_size();
  }

  for (auto p : wss_sessions_)
  {
    auto strong = p->weak_from_this().lock();
    if (strong)
      queue_sizes_summed += strong->queue_size();
  }

  for (auto p : http_sessions_)
  {
    auto strong = p->weak_from_this().lock();
    if (strong)
      queue_sizes_summed += strong->queue_size();
  }

  for (auto p : https_sessions_)
  {
    auto strong = p->weak_from_this().lock();
    if (strong)
      queue_sizes_summed += strong->queue_size();
  }
  
  return queue_sizes_summed;
}

bool shared_state::server_is_tcp_instead_of_http() const
{
  return server_is_tcp_instead_of_http_;
}

size_t shared_state::nonhttp_header_size() const
{
  return nonhttp_header_size_;
}

void shared_state::upgrade(plain_websocket_session *ws_session)
{
  { // callback functions may attempt a sendAsync which locks the mutex
    std::lock_guard<MutexT> lock(mutex_);
    ws_sessions_.insert(ws_session);
  }

  if (callbacks.callbackUpgrade)
    callbacks.callbackUpgrade(ws_session->endpoint);
}

void shared_state::downgrade(plain_websocket_session *ws_session)
{
  { // callback functions may attempt a sendAsync which locks the mutex
    std::lock_guard<MutexT> lock(mutex_);
    ws_sessions_.erase(ws_session);
  }

  if (callbacks.callbackDowngrade)
    callbacks.callbackDowngrade(ws_session->endpoint);
}

void shared_state::upgrade(ssl_websocket_session *wss_session)
{
  { // callback functions may attempt a sendAsync which locks the mutex
    std::lock_guard<MutexT> lock(mutex_);
    wss_sessions_.insert(wss_session);
  }

  if (callbacks.callbackUpgrade)
    callbacks.callbackUpgrade(wss_session->endpoint);
}

void shared_state::downgrade(ssl_websocket_session *wss_session)
{
  { // callback functions may attempt a sendAsync which locks the mutex
    std::lock_guard<MutexT> lock(mutex_);
    wss_sessions_.erase(wss_session);
  }

  if (callbacks.callbackDowngrade)
    callbacks.callbackDowngrade(wss_session->endpoint);
}



void shared_state::join(plain_http_session *session)
{
  { // callback functions may attempt a sendAsync which locks the mutex
    std::lock_guard<MutexT> lock(mutex_);
    http_sessions_.insert(session);
  }

  if (callbacks.callbackAccept)
    callbacks.callbackAccept(session->endpoint);
}

void shared_state::leave(plain_http_session *session)
{
  { // callback functions may attempt a sendAsync which locks the mutex
    std::lock_guard<MutexT> lock(mutex_);
    http_sessions_.erase(session);
  }

  if (callbacks.callbackClose)
    callbacks.callbackClose(session->endpoint);
}

void shared_state::join(ssl_http_session *session)
{
  { // callback functions may attempt a sendAsync which locks the mutex
    std::lock_guard<MutexT> lock(mutex_);
    https_sessions_.insert(session);
  }

  if (callbacks.callbackAccept)
    callbacks.callbackAccept(session->endpoint);
}

void shared_state::leave(ssl_http_session *session)
{
  { // callback functions may attempt a sendAsync which locks the mutex
    std::lock_guard<MutexT> lock(mutex_);
    https_sessions_.erase(session);
  }

  if (callbacks.callbackClose)
    callbacks.callbackClose(session->endpoint);
}







// Broadcast a message to all websocket client sessions
void shared_state::
    sendAsync(const void* msgPtr, size_t msgSize, const CompletionHandlerT &completionHandler, bool force_send, size_t max_queue_size, bool to_ws, bool to_tcp)
{
    // checks for null msgPtr and 0 length are done lower down
    // if either of those things happen, call the completion handler with an abort token
    
    bool sent_to_any = false;

    // lock mutex here and pool all the different types of sessions we have
    // then unlock the mutex and send

    // Make a local list of all the weak pointers representing
    // the sessions, so we can do the actual sending without
    // holding the mutex:
    {
      std::lock_guard<MutexT> lock(mutex_);

      ws_sessionPointerPool.clear();
      wss_sessionPointerPool.clear();
      if (to_ws)
      {
        ws_sessionPointerPool.reserve(ws_sessions_.size());
        for (auto p : ws_sessions_)
          ws_sessionPointerPool.emplace_back(p->weak_from_this());

        wss_sessionPointerPool.reserve(wss_sessions_.size());
        for (auto p : wss_sessions_)
          wss_sessionPointerPool.emplace_back(p->weak_from_this());
      }

      http_sessionPointerPool.clear();
      https_sessionPointerPool.clear();
      if (to_tcp)
      {
        http_sessionPointerPool.reserve(http_sessions_.size());
        for (auto p : http_sessions_)
          http_sessionPointerPool.emplace_back(p->weak_from_this());

        https_sessionPointerPool.reserve(https_sessions_.size());
        for (auto p : https_sessions_)
          https_sessionPointerPool.emplace_back(p->weak_from_this());
      }
    }

    // WS
    for (auto const &wp : ws_sessionPointerPool)
    {
        if (auto sp = wp.lock())
        {
          sp->sendAsync(msgPtr, msgSize, completionHandler, force_send, max_queue_size);
          sent_to_any = true;
        }
    }

    // WSS
    for (auto const &wp : wss_sessionPointerPool)
    {
        if (auto sp = wp.lock())
        {
          sp->sendAsync(msgPtr, msgSize, completionHandler, force_send, max_queue_size);
          sent_to_any = true;
        }
    }

    // HTTP
    for (auto const &wp : http_sessionPointerPool)
    {
        if (auto sp = wp.lock())
        {
          sp->sendAsync(msgPtr, msgSize, completionHandler, force_send, max_queue_size);
          sent_to_any = true;
        }
    }

    // HTTPS
    for (auto const &wp : https_sessionPointerPool)
    {
        if (auto sp = wp.lock())
        {
          sp->sendAsync(msgPtr, msgSize, completionHandler, force_send, max_queue_size);
          sent_to_any = true;
        }
    }

    if ( ! sent_to_any && completionHandler)
    {
      completionHandler(boost::asio::error::not_connected, 0, endpointT());
    }
}

// Broadcast a message to all websocket client sessions
void shared_state::
    sendAsync(const boost::asio::ip::tcp::endpoint &endpoint, const void* msgPtr, size_t msgSize, const CompletionHandlerT &completionHandler, bool force_send, size_t max_queue_size)
  {
    // checks for null msgPtr and 0 length are done lower down
    // if either of those things happen, call the completion handler with an abort token

    bool sent_to_any = false;

    // lock mutex here and pool all the different types of sessions we have
    // then unlock the mutex and send

    // Make a local list of all the weak pointers representing
    // the sessions, so we can do the actual sending without
    // holding the mutex:
    {
      std::lock_guard<MutexT> lock(mutex_);

      ws_sessionPointerPool.clear();
      wss_sessionPointerPool.clear();

      ws_sessionPointerPool.reserve(ws_sessions_.size());
      for (auto p : ws_sessions_)
        ws_sessionPointerPool.emplace_back(p->weak_from_this());

      wss_sessionPointerPool.reserve(wss_sessions_.size());
      for (auto p : wss_sessions_)
        wss_sessionPointerPool.emplace_back(p->weak_from_this());
      

      http_sessionPointerPool.clear();
      https_sessionPointerPool.clear();
      
      http_sessionPointerPool.reserve(http_sessions_.size());
      for (auto p : http_sessions_)
        http_sessionPointerPool.emplace_back(p->weak_from_this());

      https_sessionPointerPool.reserve(https_sessions_.size());
      for (auto p : https_sessions_)
        https_sessionPointerPool.emplace_back(p->weak_from_this());
      
    }


    // For each session in our local list, try to acquire a strong
    // pointer. If successful, then send the message on that session.

    // WS
    for (auto const &wp : ws_sessionPointerPool)
    {
      if (auto sp = wp.lock())
      {
        if (sp->endpoint == endpoint)
        {
            sp->sendAsync(msgPtr, msgSize, completionHandler, force_send, max_queue_size);
            sent_to_any = true;
        }
      }
    }


    // WSS
    for (auto const &wp : wss_sessionPointerPool)
    {
      if (auto sp = wp.lock())
      {
        if (sp->endpoint == endpoint)
        {
            sp->sendAsync(msgPtr, msgSize, completionHandler, force_send, max_queue_size);
            sent_to_any = true;
        }
      }
    }

    // HTTP
    for (auto const &wp : http_sessionPointerPool)
    {
      if (auto sp = wp.lock())
      {
        if (sp->endpoint == endpoint)
        {
            sp->sendAsync(msgPtr, msgSize, completionHandler, force_send, max_queue_size);
            sent_to_any = true;
        }
      }
    }
    

    // HTTPS
    for (auto const &wp : https_sessionPointerPool)
    {
      if (auto sp = wp.lock())
      {
        if (sp->endpoint == endpoint)
        {
            sp->sendAsync(msgPtr, msgSize, completionHandler, force_send, max_queue_size);
            sent_to_any = true;
        }
      }
    }

  if ( ! sent_to_any && completionHandler)
  {
    completionHandler(boost::asio::error::not_connected, 0, endpointT());
  }
}


void shared_state::sendAsync(const std::string &str, bool force_send, size_t max_queue_size, bool to_ws, bool to_tcp)
{
    std::shared_ptr<std::string> strPtr = std::make_shared<std::string>(str);

    sendAsync((void*)(strPtr->data()), strPtr->length(), [strPtr](beast::error_code, size_t, const endpointT &){}, force_send, max_queue_size, to_ws, to_tcp);
}

void shared_state::sendAsync(const boost::asio::ip::tcp::endpoint &endpoint, const std::string &str, bool force_send, size_t max_queue_size)
{
    std::shared_ptr<std::string> strPtr = std::make_shared<std::string>(str);
    sendAsync(endpoint, (void*)(strPtr->data()), strPtr->length(), [strPtr](beast::error_code, size_t, const endpointT &){}, force_send, max_queue_size);
}

void shared_state::on_ws_read(const tcp::endpoint &endpoint, const void *msgPtr, size_t msgSize)
{
  if (callbacks.callbackWSRead)
    callbacks.callbackWSRead(endpoint, msgPtr, msgSize);
}

void shared_state::on_http_read(const tcp::endpoint &endpoint, const void *msgPtr, size_t msgSize)
{
  if (callbacks.callbackHTTPRead)
    callbacks.callbackHTTPRead(endpoint, msgPtr, msgSize);
}

void shared_state::on_error(const tcp::endpoint &endpoint, beast::error_code ec)
{
  if (callbacks.callbackError)
    callbacks.callbackError(endpoint, ec);
}

std::vector<boost::asio::ip::tcp::endpoint> shared_state::get_endpoints()
{
  std::vector<boost::asio::ip::tcp::endpoint> ret;

    std::lock_guard<MutexT> lock(mutex_);

    size_t nSessions = ws_sessions_.size() + wss_sessions_.size() + http_sessions_.size() + https_sessions_.size();

    ret.reserve(nSessions);

    for (auto p : ws_sessions_)
      ret.emplace_back(p->endpoint);

    for (auto p : wss_sessions_)
      ret.emplace_back(p->endpoint);
    
    for (auto p : http_sessions_)
      ret.emplace_back(p->endpoint);

    for (auto p : https_sessions_)
      ret.emplace_back(p->endpoint);
      
    return ret;
}

} // namespace
