// Modified by Keith Rausch

#ifndef MULTI_CLIENT_RECEIVER_H
#define MULTI_CLIENT_RECEIVER_H

#include "UtilsASIO.h"
// #include "websocket_client_async.h"
#include <regex>
#include <unordered_map>
// #include "advanced_server_flex.hpp"
#include "listener.hpp"
#include "http_session.hpp"


namespace BeastNetworking
{

struct MultiClientReceiverArgs
{
  unsigned short broadcastRcvPort = 8081;
  size_t maxMessageLength = 500;
  double timeout_seconds = 3;
  bool permitLoopback = true;
  bool verbose = false;
  bool useSSL = true;
};

struct MultiClientReceiver
{
  struct SenderCharacteristics
  {
    std::string topic;
    unsigned short port;
    std::uint_fast64_t uniqueHandleID;
    uint16_t endianness;
    int64_t time; // not all that precise. this isnt ntp afterall

    SenderCharacteristics() : topic(), port(0), uniqueHandleID(0), endianness(0), time(0)
    {
    }

    bool Parse(const std::string &str)
    {
      bool hadError = false;

      std::smatch matches;

      // topic
      std::regex_search(str, matches, std::regex(R"delim(topic:(\w+),)delim"));
      topic = (matches.size() == 2) ? std::string(matches[1]) : std::string("");
      hadError |= matches.size() != 2; // THIS IS AN ERROR CONDITION

      // port
      std::regex_search(str, matches, std::regex(R"delim(port:(\d+),)delim"));
      port = (unsigned short)(matches.size() == 2) ? std::stoull(matches[1]) : 0;

      // uniqueHandleID
      std::regex_search(str, matches, std::regex(R"delim(id:(\d+),)delim"));
      uniqueHandleID = (std::uint_fast64_t)(matches.size() == 2) ? std::stoull(matches[1]) : 0;

      // endianness
      std::regex_search(str, matches, std::regex(R"delim(endian:(\d+),)delim"));
      endianness = (uint16_t)(matches.size() == 2) ? std::stoull(matches[1]) : 0;

      // time
      std::regex_search(str, matches, std::regex(R"delim(system_time:(\d+),)delim"));
      time = (int64_t)(matches.size() == 2) ? std::stoull(matches[1]) : 0;

      return hadError;
    }
  };

  typedef std::unordered_map<std::string, std::shared_ptr<shared_state>> TopicStatesT;

  boost::asio::io_context &io_context;
  boost::asio::ssl::context &ssl_context;
  TopicStatesT topicStates;
  std::unordered_map<std::string, std::weak_ptr<plain_websocket_session>> clients; // TODO this assumes that we cant get the same topic from two different places
  std::unordered_map<std::string, std::weak_ptr<ssl_websocket_session>> clientsSSL; // TODO this assumes that we cant get the same topic from two different places
  std::mutex clientsMutex;
  std::shared_ptr<utils_asio::UDPReceiver> udpReceiverPtr;
  MultiClientReceiverArgs args;

  std::uint_fast64_t uniqueInstanceID;

  template <typename EndpointT>
  std::string UniqueClientName(const std::string &topic, const EndpointT &endpoint)
  {
    return "__" + topic + "__" + EndpointToString(endpoint);
  }

  MultiClientReceiver(boost::asio::io_context &io_context_in, boost::asio::ssl::context &ssl_context_in, const TopicStatesT &topicStates_in, const MultiClientReceiverArgs &args_in, std::uint_fast64_t uniqueInstanceID_in = 0)
      : io_context(io_context_in), ssl_context(ssl_context_in), topicStates(topicStates_in), args(args_in), uniqueInstanceID(uniqueInstanceID_in)
  {
  }

  template <typename EndpointT>
  static std::string EndpointToString(const /*boost::asio::ip::udp::endpoint*/ EndpointT &endpoint)
  {
    return endpoint.address().to_string() + ":" + std::to_string(endpoint.port());
  }

  void ProcessBroadcast(const boost::asio::ip::udp::endpoint &endpoint, const void *msgPtr, size_t msgSize)
  {
    using namespace std::chrono;
    int64_t time_ms = duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();

    std::string msg((char *)msgPtr, msgSize);
    std::string endpointString = EndpointToString(endpoint);
    if (args.verbose)
      std::printf("RECEIVED BROADCAST FROM: %s\n%s\n", endpointString.c_str(), msg.c_str());

    // search the broadcast for the server's claimed address and port
    std::string serverAddress;
    std::string serverPort;
    SenderCharacteristics characteristics;
    bool hadError = characteristics.Parse(msg);

    if (hadError)
      return;

    auto topic = characteristics.topic;
    // auto serverEndpoint = characteristics.claimedServerAddress;
    auto serverEndpoint = boost::asio::ip::tcp::endpoint(endpoint.address(), characteristics.port);
    if (args.verbose)
      std::cout << "delta time between software send & receive (ms)" << time_ms - characteristics.time << std::endl;

    // bind the topic (if we have callbacks for it)

    if (!args.permitLoopback)
    {
      if (characteristics.uniqueHandleID == uniqueInstanceID && uniqueInstanceID != 0)
      {
        if (args.verbose)
          std::cout << "MultiClientReceiver::ProcessBroadcast() - received matching instance ID's and loopback disabled\n";
        return;
      }
    }

    // we have to have callbacks for this topic
    if (topicStates.count(topic) == 0)
    {
      if (args.verbose)
        std::printf("MultiClientReceiver::ProcessBroadcast() - received a broadcast for topic \"%s\", but no callbacks for it were provided...\n", topic.c_str());
      return;
    }
    auto &state = topicStates[topic];

    auto uniqueClientName = UniqueClientName(topic, serverEndpoint);

    if (args.useSSL)
    {
      // we have to not already have an active session
      std::shared_ptr<ssl_websocket_session> client;
      
      std::lock_guard<std::mutex> lock(clientsMutex); // leave this locked, we access clients again below
      client = clientsSSL[uniqueClientName].lock();
      
      if (client)
        return; // this session is still alive and kicking, leave it

      // this client does not already exist, we get to create a new one
      // client = std::make_shared<WebSocketSessionClient>(io_context,
      //                                         ssl_context,
      //                                         state,
      //                                         serverEndpoint.address().to_string(),
      //                                         serverEndpoint.port(),
      //                                         args.useSSL);
      client = BeastNetworking::make_websocket_session_client(io_context, 
                                                              ssl_context, 
                                                              state, 
                                                              serverEndpoint.address().to_string(),
                                                              serverEndpoint.port());

      if (client)
      {
        client->RunClient();
        clientsSSL[uniqueClientName] = client;
      }
    }
    else
    {
      // we have to not already have an active session
      std::shared_ptr<plain_websocket_session> client;
      
      std::lock_guard<std::mutex> lock(clientsMutex); // leave this locked, we access clients again below
      client = clients[uniqueClientName].lock();
      
      if (client)
        return; // this session is still alive and kicking, leave it

      // this client does not already exist, we get to create a new one
      // client = std::make_shared<plain_websocket_session>(io_context,
      //                                         ssl_context,
      //                                         state,
      //                                         serverEndpoint.address().to_string(),
      //                                         serverEndpoint.port(),
      //                                         args.useSSL);
      client = BeastNetworking::make_websocket_session_client(io_context, 
                                                              state, 
                                                              serverEndpoint.address().to_string(),
                                                              serverEndpoint.port());

      if (client)
      {
        client->RunClient();
        clients[uniqueClientName] = client;
      }
    }

  }

  void ListenForTopics()
  {
    utils_asio::UDPReceiver::Callbacks callbacks;

    auto callbackError = [](const boost::asio::ip::udp::endpoint &, boost::system::error_code ec) { std::cout << "CONSUMER-ERROR CALLBACK WAS CALLED: " << ec.message() << ec.value() << std::endl; };
    callbacks.callbackError = callbackError;

    auto callbackTimeout = [this]() {
      std::cout << "MULTICLIENTRECEIVER::CALLBACK_TIMEOUT() - STOPPING IO_CONTEXT\n";
      io_context.stop();
    };
    callbacks.callbackTimeout = callbackTimeout;

    callbacks.callbackRead = std::bind(&MultiClientReceiver::ProcessBroadcast, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3);

    udpReceiverPtr = std::make_shared<utils_asio::UDPReceiver>(io_context,
                                                               args.broadcastRcvPort,
                                                               args.maxMessageLength,
                                                               args.timeout_seconds,
                                                               callbacks);
    udpReceiverPtr->run();
  }

  template <typename EndpointT>
  void SendAsync(const std::string &topic, const EndpointT &endpoint, void* msgPtr, size_t msgSize, BeastNetworking::shared_state::CompletionHandlerT &&completionHandler = BeastNetworking::shared_state::CompletionHandlerT(), bool overwrite = false)
  {
    auto uniqueClientName = UniqueClientName(topic, endpoint);
    std::lock_guard<std::mutex> lock(clientsMutex);

    bool found = false;

    {
      auto client = clients[uniqueClientName].lock();
      if (client)
      {
        client->sendAsync(msgPtr, msgSize, std::move(completionHandler), overwrite);
        found = true;
      }
    }
    {
      auto client = clientsSSL[uniqueClientName].lock();
      if (client)
      {
        client->sendAsync(msgPtr, msgSize, std::move(completionHandler), overwrite);
        found = true;
      }
    }
    
    if ( ! found)
      completionHandler(boost::asio::error::operation_aborted, 0);
      
  }

  template <typename EndpointT>
  void ReplaceSendAsync(const std::string &topic, const EndpointT &endpoint, void* msgPtr, size_t msgSize, BeastNetworking::shared_state::CompletionHandlerT &&completionHandler = BeastNetworking::shared_state::CompletionHandlerT())
  {
      SendAsync(topic, endpoint, msgPtr, msgSize, std::forward<BeastNetworking::shared_state::CompletionHandlerT>(completionHandler), true);
  }

  template <typename EndpointT>
  void SendAsync(const std::string &topic, const EndpointT &endpoint, const std::string &str, bool overwrite = false)
  {
    std::shared_ptr<std::string> strPtr = std::make_shared<std::string>(str);
    auto uniqueClientName = UniqueClientName(topic, endpoint);
    std::lock_guard<std::mutex> lock(clientsMutex);
    
    {
      auto client = clients[uniqueClientName].lock();
      if (client)
        client->sendAsync((*strPtr).data(), (*strPtr).length(), [strPtr](boost::beast::error_code, size_t){}, overwrite);
    }
    {
      auto client = clientsSSL[uniqueClientName].lock();
      if (client)
        client->sendAsync((*strPtr).data(), (*strPtr).length(), [strPtr](boost::beast::error_code, size_t){}, overwrite);
    }
  }
};

} // namespace

#endif