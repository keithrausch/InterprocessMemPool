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

public:
  struct Callbacks
  {
    typedef boost::asio::ip::tcp::endpoint endpointT;
    typedef std::function<void(const endpointT &endpoint, void *msgPtr, size_t msgSize)> CallbackReadT;
    typedef std::function<void(const endpointT &endpoint)> CallbackSocketAcceptT;
    typedef std::function<void(const endpointT &endpoint)> CallbackSocketCloseT;
    typedef std::function<void(const endpointT &endpoint, const beast::error_code &ec)> CallbackErrorT;

    CallbackReadT callbackRead;
    CallbackSocketAcceptT callbackAccept;
    CallbackSocketCloseT callbackClose;
    CallbackErrorT callbackError;
  };

  Callbacks callbacks;

  typedef std::pair<void *, size_t> SerializerReturnT;
  typedef std::function<SerializerReturnT()> CallbackSendSerializerT;
  typedef std::pair<CallbackSendSerializerT, SerializerReturnT> SerializedAndReturnedT;
  typedef std::shared_ptr<SerializedAndReturnedT> SharedSerializedAndReturnedT;
  // std::vector<shared_serialized_and_returned_T> queue_; // woah this is a vector, its even a vector in the beast example. weird.

  explicit shared_state(const Callbacks &callbacks_in);

  void join(websocket_session *session);
  void leave(websocket_session *session);
  void sendAsync(const CallbackSendSerializerT &serializer);
  void sendAsync(const boost::asio::ip::tcp::endpoint &endpoint, const CallbackSendSerializerT &serializer);
  void on_read(const boost::asio::ip::tcp::endpoint &endpoint, void *msgPtr, size_t msgSize);
  void on_error(const boost::asio::ip::tcp::endpoint &endpoint, beast::error_code ec);


  void sendAsync(const std::string &str);
  void sendAsync(const boost::asio::ip::tcp::endpoint &endpoint, const std::string &str);
  static CallbackSendSerializerT StringToCallback(const std::string &str);

  template <class ... Args>
  static CallbackSendSerializerT ObjToCallback(void* msgPtr, size_t msgSize, Args&&... args)
  {
    // scope capture BY COPY :
    CallbackSendSerializerT serializer = [msgPtr, msgSize, args...]() -> std::pair<void *, size_t> {
      return std::pair<void *, size_t>(msgPtr, msgSize);
    };

    return serializer;
  }

  SharedSerializedAndReturnedT RunAndPackage(const CallbackSendSerializerT &serializer);

  // A DANGEROUS CONVENIENCE FUNCTION THAT WILL COPY YOUR ARGS AND ASSUME THAT THE POINTER AND SIZE
  // PROVIDED ARE STILL VALID. for example, if you send(string.data(), string.length(), ast string), 
  // the the original string will get copied and fall out of scope, and the pointer you gave will 
  // be invalidated / not updated to point to the copy of the string.
  // works fine if what youre capturing is a shared pointer
  template <class ... Args>
  void sendAsync( void* msgPtr, size_t msgSize, Args&&... args)
  {
      sendAsync(ObjToCallback(msgPtr, msgSize, std::forward<Args>(args)...));
  }

  // A DANGEROUS CONVENIENCE FUNCTION THAT WILL COPY YOUR ARGS AND ASSUME THAT THE POINTER AND SIZE
  // PROVIDED ARE STILL VALID. for example, if you send(string.data(), string.length(), ast string), 
  // the the original string will get copied and fall out of scope, and the pointer you gave will 
  // be invalidated / not updated to point to the copy of the string
  // works fine if what youre capturing is a shared pointer
  template <class ... Args>
  void sendAsync(const boost::asio::ip::tcp::endpoint &endpoint, void* msgPtr, size_t msgSize, Args&&... args)
  {
      sendAsync(endpoint, ObjToCallback(msgPtr, msgSize, std::forward<Args>(args)...));
  }

};

#endif