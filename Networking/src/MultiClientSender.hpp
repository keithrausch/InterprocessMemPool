// Keith Rausch

#ifndef BEASTWEBSERVERFLEXIBLE_MULTI_CLIENT_SENDER_HPP
#define BEASTWEBSERVERFLEXIBLE_MULTI_CLIENT_SENDER_HPP

#include "args.hpp"
#include "UtilsASIO.hpp"
#include "shared_state.hpp"
#include "listener.hpp"


namespace BeastNetworking
{


struct MultiClientSender
{


  boost::asio::io_context &ioc;
  boost::asio::ssl::context &ssl_context;
  std::string topic;                         // topic name
  std::shared_ptr<shared_state> sharedState; // for sending data
  unsigned short boundServerPort_insecure;
  unsigned short boundServerPort_secure;
  MultiClientSenderArgs args;

  std::uint_fast64_t uniqueInstanceID;

  // establish server and get its bound addres and port
  MultiClientSender(boost::asio::io_context &ioc_in, boost::asio::ssl::context &ssl_context_in, const std::string &topic_in, const MultiClientSenderArgs &args_in, std::uint_fast64_t uniqueInstanceID_in = 0/*, const std::shared_ptr<RateLimiting::RateTracker> &rate_tracker_in=nullptr*/)
      : ioc(ioc_in), ssl_context(ssl_context_in), topic(topic_in), boundServerPort_insecure(0), boundServerPort_secure(0), args(args_in), uniqueInstanceID(uniqueInstanceID_in)
  {

    shared_state::Callbacks callbacks;

    callbacks.callbackWSRead = [](const tcp::endpoint &endpoint, const void *msgPtr, size_t msgSize) {
      std::stringstream ss;
      ss << endpoint;
      std::string endpointString = ss.str();

      std::string msg(static_cast<const char *>(msgPtr), msgSize);

      std::printf("PRODUCER-READ - endpoint: %s\n%s\n", endpointString.c_str(), msg.c_str());
    };

    if (args.verbose)
    {
      auto to_string = [](const tcp::endpoint &endpoint){ return endpoint.address().to_string() + ":" + std::to_string(endpoint.port());};
      callbacks.callbackUpgrade = [this, to_string](const tcp::endpoint &endpoint) { std::cout << "InterprocessMemPool::MultiClientSender::on_upgrade() - topic: \""+topic+"\", accepting endpoint: " + to_string(endpoint) + "\n"; };
      callbacks.callbackClose = [this, to_string](const tcp::endpoint &endpoint) { std::cout << "InterprocessMemPool::MultiClientSender::on_close() - topic: \""+topic+"\", closing endpoint: " + to_string(endpoint) + "\n"; };
    }

    sharedState = std::make_shared<shared_state>(callbacks);

    // bind to any address, any port (insecure)
    if (args.serverBindPort_insecure == args.serverBindPort_secure && args.serverBindPort_secure > 0)
    {
        tcp::endpoint our_endpoint(net::ip::make_address(args.serverBindAddress), args.serverBindPort_secure);
        auto listenerPtr = std::make_shared<listener>(ioc, ssl_context, our_endpoint, sharedState, BeastNetworking::Security::BOTH);
        if (listenerPtr)
        {
          listenerPtr->run();
          boundServerPort_insecure = listenerPtr->localEndpoint.port();
          boundServerPort_secure = listenerPtr->localEndpoint.port();
        }
    }
    else
    {
      if (args.serverBindPort_insecure > 0)
      {
        tcp::endpoint our_endpoint(net::ip::make_address(args.serverBindAddress), args.serverBindPort_insecure);
        auto listenerPtr = std::make_shared<listener>(ioc, ssl_context, our_endpoint, sharedState, BeastNetworking::Security::INSECURE);
        if (listenerPtr)
        {
          listenerPtr->run();
          boundServerPort_insecure = listenerPtr->localEndpoint.port();
        }
      }

      // bind to any address, any port (secure)
      if (args.serverBindPort_secure > 0)
      {
        tcp::endpoint our_endpoint(net::ip::make_address(args.serverBindAddress), args.serverBindPort_secure);
        auto listenerPtr = std::make_shared<listener>(ioc, ssl_context, our_endpoint, sharedState, BeastNetworking::Security::BOTH);
        if (listenerPtr)
        {
          listenerPtr->run();
          boundServerPort_secure = listenerPtr->localEndpoint.port();
        }
      }
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
         << ",port_insecure:" << boundServerPort_insecure
         << ",port_secure:" << boundServerPort_secure
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

typedef std::shared_ptr<MultiClientSender> sMultiClientSender;

} // namespace

#endif