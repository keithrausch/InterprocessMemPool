// Keith Rausch

#ifndef MULTI_CLIENT_SENDER_H
#define MULTI_CLIENT_SENDER_H

#include "UtilsASIO.h"
#include "listener.h"
#include "shared_state.h"

struct MultiClientSenderArgs
{
  std::string broadcastDestination = "255.255.255.255"; // send broadcast to all listeners
  unsigned short broadcastSendPort = 0;                 // send broadcast on any port
  unsigned short broadcastReceiverPort = 8081;          // change me - broadcast receiver port
  float heartbeatPeriod_seconds = 0.5;                  // seconds between heartbeats

  std::string serverBindAddress = "0.0.0.0"; // bind server to any address
  unsigned short serverBindPort = 0;         // bind server to any port
};

struct MultiClientSender
{
  boost::asio::io_context &ioc;
  std::string topic;                         // topic name
  std::shared_ptr<shared_state> sharedState; // for sending data
  unsigned short boundServerPort;
  MultiClientSenderArgs args;

  std::uint_fast64_t uniqueInstanceID;

  // establish server and get its bound addres and port
  MultiClientSender(boost::asio::io_context &ioc_in, const std::string &topic_in, const MultiClientSenderArgs &args_in, std::uint_fast64_t uniqueInstanceID_in = 0)
      : ioc(ioc_in), topic(topic_in), boundServerPort(0), args(args_in), uniqueInstanceID(uniqueInstanceID_in)
  {

    shared_state::Callbacks callbacks;

    callbacks.callbackRead = [](const tcp::endpoint &endpoint, void *msgPtr, size_t msgSize) {
      std::stringstream ss;
      ss << endpoint;
      std::string endpointString = ss.str();

      std::string msg(static_cast<const char *>(msgPtr), msgSize);

      std::printf("PRODUCER-READ - endpoint: %s\n%s\n", endpointString.c_str(), msg.c_str());
    };

    callbacks.callbackAccept = [](const tcp::endpoint &endpoint) { std::cout << "PRODUCER-ACCEPT - endpoint: " << endpoint << std::endl; };
    callbacks.callbackClose = [](const tcp::endpoint &endpoint) { std::cout << "PRODUCER-CLOSE - endpoint: " << endpoint << std::endl; };

    sharedState = std::make_shared<shared_state>(callbacks);

    // bind to any address, any port
    tcp::endpoint endpoint(net::ip::make_address(args.serverBindAddress), args.serverBindPort);
    auto listenerPtr = std::make_shared<listener>(ioc, endpoint, sharedState);
    if (listenerPtr)
    {
      listenerPtr->run();
      boundServerPort = listenerPtr->localEndpoint.port();
    }
  }

  void StartHeartbeat()
  {
    if (nullptr == sharedState)
    {
      std::cout << "NOT SENDING BROADCAST, SERVER IS DOWN\n";
      return;
    }

    // this is a lambda because we update the time on every send
    auto broadcastMsgCreator = [this]() {
      using namespace std::chrono;

      std::stringstream ss;
      ss << "topic:" << topic
         << ",port:" << boundServerPort
         << ",id:" << uniqueInstanceID
         << ",endian:" << (uint16_t)1
         << ",time:" << duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count()
         << ",";
      return ss.str();
    };

    // send broadcast
    auto heartbeatPtr = std::make_shared<utils_asio::Heartbeat>(ioc,
                                                                broadcastMsgCreator,
                                                                args.broadcastSendPort,
                                                                args.broadcastDestination,
                                                                args.broadcastReceiverPort,
                                                                args.heartbeatPeriod_seconds);

    if (heartbeatPtr)
      heartbeatPtr->run();
  }

  void SendAsync(void * msgPtr, size_t msgSize, shared_state::CompletionHandlerT && completionHandler = shared_state::CompletionHandlerT())
  {
    if (sharedState)
      sharedState->sendAsync(msgPtr, msgSize, std::forward<shared_state::CompletionHandlerT>(completionHandler));
  }

  // // A DANGEROUS CONVENIENCE FUNCTION THAT WILL COPY YOUR ARGS AND ASSUME THAT THE POINTER AND SIZE
  // // PROVIDED ARE STILL VALID. for example, if you send(string.data(), string.length(), ast string), 
  // // the the original string will get copied and fall out of scope, and the pointer you gave will 
  // // be invalidated / not updated to point to the copy of the string
  // // works fine if what youre capturing is a shared pointer
  // template <class ... Args>
  // void SendAsync( void* msgPtr, size_t msgSize, Args&&... args)
  // {
  //     sharedState->sendAsync(msgPtr, msgSize, std::forward<Args>(args)...);
  // }

  // // A DANGEROUS CONVENIENCE FUNCTION THAT WILL COPY YOUR ARGS AND ASSUME THAT THE POINTER AND SIZE
  // // PROVIDED ARE STILL VALID. for example, if you send(string.data(), string.length(), ast string), 
  // // the the original string will get copied and fall out of scope, and the pointer you gave will 
  // // be invalidated / not updated to point to the copy of the string
  // // works fine if what youre capturing is a shared pointer
  // template <class ... Args>
  // void SendAsync(const boost::asio::ip::tcp::endpoint &endpoint, void* msgPtr, size_t msgSize, Args&&... args)
  // {
  //     sharedState->sendAsync(endpoint, msgPtr, msgSize, std::forward<Args>(args)...);
  // }
};

#endif