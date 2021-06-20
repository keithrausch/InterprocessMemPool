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

shared_state::SharedSerializedAndReturnedT shared_state::RunAndPackage(const CallbackSendSerializerT &serializer)
{
  SerializedAndReturnedT packaged = SerializedAndReturnedT(serializer, SerializerReturnT()); // RIGHT HERE. we copied the lambda right here
  auto const serializedPtr = std::make_shared<SerializedAndReturnedT>(std::move(packaged));

  // do this AFTER call, else the data that the pointer points to may have been invalidated by the lambdas copy constructor
  if (serializedPtr)
    serializedPtr->second = serializedPtr->first();
    
  return serializedPtr;
}

// Broadcast a message to all websocket client sessions
void shared_state::
    sendAsync(const CallbackSendSerializerT &serializer)
{
  if (!serializer)
    return;

  // Put the message in a shared pointer so we can re-use it for each client
  auto serializedPtr = RunAndPackage(serializer);
  if (nullptr == serializedPtr || nullptr == serializedPtr->second.first || 0 == serializedPtr->second.second)
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
      sp->sendAsync(serializedPtr);
}

// Broadcast a message to all websocket client sessions
void shared_state::
    sendAsync(const boost::asio::ip::tcp::endpoint &endpoint, const CallbackSendSerializerT &serializer)
{
  if (!serializer)
    return;

  // Put the message in a shared pointer so we can re-use it for each client
  auto serializedPtr = RunAndPackage(serializer);
  if (nullptr == serializedPtr || nullptr == serializedPtr->second.first || 0 == serializedPtr->second.second)
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
        sp->sendAsync(serializedPtr);
    }
}

shared_state::CallbackSendSerializerT shared_state::StringToCallback(const std::string &str)
{

  // scope capture - BAM
  CallbackSendSerializerT serializer = [str]() -> std::pair<void *, size_t> {
    void *msgPtr = (void *)(str.data());
    size_t msgSize = str.length();
    return std::pair<void *, size_t>(msgPtr, msgSize);
  };

  return serializer;
}

void shared_state::sendAsync(const std::string &str)
{
    sendAsync(StringToCallback(str));
}

void shared_state::sendAsync(const boost::asio::ip::tcp::endpoint &endpoint, const std::string &str)
{
    sendAsync(endpoint, StringToCallback(str));
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