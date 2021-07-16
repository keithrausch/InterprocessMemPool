//
// Copyright (c) 2016-2019 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/vinniefalco/CppCon2018
//
// Modified by Keith Rausch

#include "shared_state.h"
#include "websocket_session.h"


shared_state::
    shared_state(const Callbacks &callbacks_in)
    : callbacks(callbacks_in)
{
}

void shared_state::
    join(websocket_session *session)
{
  std::lock_guard<MutexT> lock(mutex_);
  sessions_.insert(session);

  if (callbacks.callbackAccept)
    callbacks.callbackAccept(session->endpoint);
}

void shared_state::
    leave(websocket_session *session)
{
  std::lock_guard<MutexT> lock(mutex_);
  sessions_.erase(session);

  if (callbacks.callbackClose)
    callbacks.callbackClose(session->endpoint);
}


// Broadcast a message to all websocket client sessions
void shared_state::
    sendAsync(void* msgPtr, size_t msgSize, CompletionHandlerT &&completionHandler)
{
  if (nullptr == msgPtr || 0 == msgSize)
    return;

  // Make a local list of all the weak pointers representing
  // the sessions, so we can do the actual sending without
  // holding the mutex:
  std::vector<std::weak_ptr<websocket_session>> v;
  {
    std::lock_guard<MutexT> lock(mutex_);
    v.reserve(sessions_.size());
    for (auto p : sessions_)
      v.emplace_back(p->weak_from_this());
  }

  // For each session in our local list, try to acquire a strong
  // pointer. If successful, then send the message on that session.
  for (auto const &wp : v)
    if (auto sp = wp.lock())
      sp->sendAsync(msgPtr, msgSize, std::forward<CompletionHandlerT>(completionHandler));
}

// Broadcast a message to all websocket client sessions
void shared_state::
    sendAsync(const boost::asio::ip::tcp::endpoint &endpoint, void* msgPtr, size_t msgSize, CompletionHandlerT &&completionHandler)
{
  if (nullptr == msgPtr || 0 == msgSize)
    return;

  // Make a local list of all the weak pointers representing
  // the sessions, so we can do the actual sending without
  // holding the mutex:
  std::vector<std::weak_ptr<websocket_session>> v;
  {
    std::lock_guard<MutexT> lock(mutex_);
    v.reserve(sessions_.size());
    for (auto p : sessions_)
      v.emplace_back(p->weak_from_this());
  }

  // For each session in our local list, try to acquire a strong
  // pointer. If successful, then send the message on that session.
  for (auto const &wp : v)
    if (auto sp = wp.lock())
    {
      if (sp->endpoint == endpoint)
        sp->sendAsync(msgPtr, msgSize, std::forward<CompletionHandlerT>(completionHandler));
    }
}


void shared_state::sendAsync(const std::string &str)
{
    std::shared_ptr<std::string> strPtr = std::make_shared<std::string>(str);

    sendAsync((void*)(strPtr->data()), strPtr->length(), [strPtr](beast::error_code, size_t){});
}

void shared_state::sendAsync(const boost::asio::ip::tcp::endpoint &endpoint, const std::string &str)
{
    std::shared_ptr<std::string> strPtr = std::make_shared<std::string>(str);
    sendAsync(endpoint, (void*)(strPtr->data()), strPtr->length(), [strPtr](beast::error_code, size_t){});
}

void shared_state::on_read(const tcp::endpoint &endpoint, void *msgPtr, size_t msgSize)
{
  if (callbacks.callbackRead)
    callbacks.callbackRead(endpoint, msgPtr, msgSize);
}

void shared_state::on_error(const tcp::endpoint &endpoint, beast::error_code ec)
{
  if (callbacks.callbackError)
    callbacks.callbackError(endpoint, ec);
}