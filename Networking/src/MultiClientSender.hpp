// Keith Rausch

#ifndef BEASTWEBSERVERFLEXIBLE_MULTI_CLIENT_SENDER_HPP
#define BEASTWEBSERVERFLEXIBLE_MULTI_CLIENT_SENDER_HPP

#include "UtilsASIO.hpp"
#include "shared_state.hpp"
#include "listener.hpp"


namespace BeastNetworking
{


struct MultiClientSender
{

  struct Args
  {
    std::string broadcastDestination = "255.255.255.255"; // send broadcast to all listeners
    unsigned short broadcastSendPort = 0;                 // send broadcast on any port
    unsigned short broadcastReceiverPort = 8081;          // change me - broadcast receiver port
    float heartbeatPeriod_seconds = 0.5;                  // seconds between heartbeats

    std::string serverBindAddress = "0.0.0.0"; // bind server to any address
    unsigned short serverBindPort = 0;         // bind server to any port
  };

  boost::asio::io_context &ioc;
  boost::asio::ssl::context &ssl_context;
  std::string topic;                         // topic name
  std::shared_ptr<shared_state> sharedState; // for sending data
  unsigned short boundServerPort;
  Args args;

  std::uint_fast64_t uniqueInstanceID;

  // establish server and get its bound addres and port
  MultiClientSender(boost::asio::io_context &ioc_in, boost::asio::ssl::context &ssl_context_in, const std::string &topic_in, const Args &args_in, std::uint_fast64_t uniqueInstanceID_in = 0, const std::shared_ptr<RateLimiting::RateTracker> &rate_tracker_in=nullptr)
      : ioc(ioc_in), ssl_context(ssl_context_in), topic(topic_in), boundServerPort(0), args(args_in), uniqueInstanceID(uniqueInstanceID_in)
  {

    shared_state::Callbacks callbacks;

    callbacks.callbackRead = [](const tcp::endpoint &endpoint, const void *msgPtr, size_t msgSize) {
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
    auto listenerPtr = std::make_shared<listener>(ioc, ssl_context, endpoint, sharedState);
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
         << ",system_time:" << duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count()
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

  void SendAsync(const void * msgPtr, size_t msgSize, shared_state::CompletionHandlerT && completionHandler = shared_state::CompletionHandlerT(), bool force_send=false, size_t max_queue_size = std::numeric_limits<size_t>::max())
  {
    if (sharedState)
      sharedState->sendAsync(msgPtr, msgSize, std::forward<shared_state::CompletionHandlerT>(completionHandler), force_send, max_queue_size);
  }
};

} // namespace

#endif