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

namespace BeastNetworking
{

shared_state::shared_state()
    : doc_root_(), callbacks()
{
}

shared_state::shared_state(std::string doc_root )
    : doc_root_(std::move(doc_root)), callbacks()
{
  if ( ! std::filesystem::exists(doc_root_))
    std::cerr << "BeastWebServer - root directory does not exist \"" + doc_root_ + "\"\n";
  else
    std::cout <<  "BeastWebServer - starting server at root \"" + doc_root_ + "\"\n";
}

shared_state::shared_state(const Callbacks &callbacks_in)
    : doc_root_(), callbacks(callbacks_in)
{
}

shared_state::shared_state(std::string doc_root, const Callbacks &callbacks_in)
    : doc_root_(std::move(doc_root)), callbacks(callbacks_in)
{
  if ( ! std::filesystem::exists(doc_root_))
    std::cerr << "BeastWebServer - root directory does not exist \"" + doc_root_ + "\"\n";
  else
    std::cout <<  "BeastWebServer - starting server at root \"" + doc_root_ + "\"\n";
}

size_t shared_state::nSessions()
{
  std::lock_guard<MutexT> lock(mutex_);
  return sessions_.size();
}

void shared_state::join(plain_websocket_session *session)
{
  std::lock_guard<MutexT> lock(mutex_);
  sessions_.insert(session);

  if (callbacks.callbackAccept)
    callbacks.callbackAccept(session->endpoint);
}

void shared_state::leave(plain_websocket_session *session)
{
  std::lock_guard<MutexT> lock(mutex_);
  sessions_.erase(session);

  if (callbacks.callbackClose)
    callbacks.callbackClose(session->endpoint);
}


void shared_state::join(ssl_websocket_session *session)
{
  std::lock_guard<MutexT> lock(mutex_);
  sessionsSSL_.insert(session);

  if (callbacks.callbackAccept)
    callbacks.callbackAccept(session->endpoint);
}

void shared_state::leave(ssl_websocket_session *session)
{
  std::lock_guard<MutexT> lock(mutex_);
  sessionsSSL_.erase(session);

  if (callbacks.callbackClose)
    callbacks.callbackClose(session->endpoint);
}


// Broadcast a message to all websocket client sessions
void shared_state::
    sendAsync(const void* msgPtr, size_t msgSize, CompletionHandlerT &&completionHandler, bool force_send, size_t max_queue_size)
{
  if (nullptr == msgPtr || 0 == msgSize)
    return;

    bool sent_to_any = false;

    {
        // Make a local list of all the weak pointers representing
        // the sessions, so we can do the actual sending without
        // holding the mutex:
        {
            std::lock_guard<MutexT> lock(mutex_);
            sessionPointerPool.clear();
            sessionPointerPool.reserve(sessions_.size());
            for (auto p : sessions_)
            {
              sessionPointerPool.emplace_back(p->weak_from_this());
            }
        }

        // For each session in our local list, try to acquire a strong
        // pointer. If successful, then send the message on that session.
        for (auto const &wp : sessionPointerPool)
        {
            if (auto sp = wp.lock())
            {
              sp->sendAsync(msgPtr, msgSize, std::forward<CompletionHandlerT>(completionHandler), force_send, max_queue_size);
              sent_to_any = true;
            }
        }
    }

    // NOW DO SSL

    {
        // Make a local list of all the weak pointers representing
        // the sessions, so we can do the actual sending without
        // holding the mutex:
        {
            std::lock_guard<MutexT> lock(mutex_);
            sessionPointerPoolSSL.clear();
            sessionPointerPoolSSL.reserve(sessionsSSL_.size());
            for (auto p : sessionsSSL_)
            {
              sessionPointerPoolSSL.emplace_back(p->weak_from_this());
            }
        }

        // For each session in our local list, try to acquire a strong
        // pointer. If successful, then send the message on that session.
        for (auto const &wp : sessionPointerPoolSSL)
        {
            if (auto sp = wp.lock())
            {
              sp->sendAsync(msgPtr, msgSize, std::forward<CompletionHandlerT>(completionHandler), force_send, max_queue_size);
              sent_to_any = true;
            }
        }
    }

    if ( ! sent_to_any && completionHandler)
    {
      completionHandler(boost::asio::error::not_connected, 0, endpointT());
    }
}

// Broadcast a message to all websocket client sessions
void shared_state::
    sendAsync(const boost::asio::ip::tcp::endpoint &endpoint, const void* msgPtr, size_t msgSize, CompletionHandlerT &&completionHandler, bool force_send, size_t max_queue_size)
{
  if (nullptr == msgPtr || 0 == msgSize)
    return;

    bool sent_to_any = false;

    {
        // Make a local list of all the weak pointers representing
        // the sessions, so we can do the actual sending without
        // holding the mutex:
        {
          std::lock_guard<MutexT> lock(mutex_);
          sessionPointerPool.clear();
          sessionPointerPool.reserve(sessions_.size());
          for (auto p : sessions_)
          {
            sessionPointerPool.emplace_back(p->weak_from_this());
          }
        }

        // For each session in our local list, try to acquire a strong
        // pointer. If successful, then send the message on that session.
        for (auto const &wp : sessionPointerPool)
        {
          if (auto sp = wp.lock())
          {
            if (sp->endpoint == endpoint)
            {
                sp->sendAsync(msgPtr, msgSize, std::forward<CompletionHandlerT>(completionHandler), force_send, max_queue_size);
                sent_to_any = true;
            }
          }
        }
    }

    // NOW DO SSL

    {
        // Make a local list of all the weak pointers representing
        // the sessions, so we can do the actual sending without
        // holding the mutex:
        {
          std::lock_guard<MutexT> lock(mutex_);
          sessionPointerPoolSSL.clear();
          sessionPointerPoolSSL.reserve(sessionsSSL_.size());
          for (auto p : sessionsSSL_)
          {
            sessionPointerPoolSSL.emplace_back(p->weak_from_this());
          }
        }

        // For each session in our local list, try to acquire a strong
        // pointer. If successful, then send the message on that session.
        for (auto const &wp : sessionPointerPoolSSL)
        {
          if (auto sp = wp.lock())
          {
            if (sp->endpoint == endpoint)
            {
                sp->sendAsync(msgPtr, msgSize, std::forward<CompletionHandlerT>(completionHandler), force_send, max_queue_size);
            }
          }
        }
    }


  if ( ! sent_to_any && completionHandler)
  {
    completionHandler(boost::asio::error::not_connected, 0, endpointT());
  }
}


void shared_state::sendAsync(const std::string &str, bool force_send, size_t max_queue_size)
{
    std::shared_ptr<std::string> strPtr = std::make_shared<std::string>(str);

    sendAsync((void*)(strPtr->data()), strPtr->length(), [strPtr](beast::error_code, size_t, const endpointT &){}, force_send, max_queue_size);
}

void shared_state::sendAsync(const boost::asio::ip::tcp::endpoint &endpoint, const std::string &str, bool force_send, size_t max_queue_size)
{
    std::shared_ptr<std::string> strPtr = std::make_shared<std::string>(str);
    sendAsync(endpoint, (void*)(strPtr->data()), strPtr->length(), [strPtr](beast::error_code, size_t, const endpointT &){}, force_send, max_queue_size);
}

void shared_state::on_read(const tcp::endpoint &endpoint, const void *msgPtr, size_t msgSize)
{
  if (callbacks.callbackRead)
    callbacks.callbackRead(endpoint, msgPtr, msgSize);
}

void shared_state::on_error(const tcp::endpoint &endpoint, beast::error_code ec)
{
  if (callbacks.callbackError)
    callbacks.callbackError(endpoint, ec);
}

} // namespace
