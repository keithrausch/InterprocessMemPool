// Modified by Keith Rausch

#ifndef MULTI_CLIENT_RECEIVER_H
#define MULTI_CLIENT_RECEIVER_H

#include "UtilsASIO.h"
#include "websocket_client_async.h"
#include <regex>
#include <unordered_map>

struct MultiClientReceiverArgs
{
  unsigned short broadcastRcvPort = 8081;
  size_t maxMessageLength = 500;
  double timeout_seconds = 3;
  bool permitLoopback = true;
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
      std::regex_search(str, matches, std::regex(R"delim(time:(\d+),)delim"));
      time = (int64_t)(matches.size() == 2) ? std::stoull(matches[1]) : 0;

      return hadError;
    }
  };

  typedef std::unordered_map<std::string, SessionClient::Callbacks> TopicCallbacksT;

  boost::asio::io_context &io_context;
  TopicCallbacksT topicCallbacks;
  std::unordered_map<std::string, std::weak_ptr<SessionClient>> clients; // TODO this assumes that we cant get the same topic from two different places
  std::shared_ptr<utils_asio::UDPReceiver> udpReceiverPtr;
  MultiClientReceiverArgs args;

  std::uint_fast64_t uniqueInstanceID;

  MultiClientReceiver(boost::asio::io_context &io_context_in, const TopicCallbacksT &topicCallbacks_in, const MultiClientReceiverArgs &args_in, std::uint_fast64_t uniqueInstanceID_in = 0)
      : io_context(io_context_in), topicCallbacks(topicCallbacks_in), args(args_in), uniqueInstanceID(uniqueInstanceID_in)
  {
  }

  static std::string EndpointToString(const boost::asio::ip::udp::endpoint &endpoint)
  {
    return endpoint.address().to_string() + ":" + std::to_string(endpoint.port());
  }

  void ProcessBroadcast(const boost::asio::ip::udp::endpoint &endpoint, void *msgPtr, size_t msgSize)
  {
    using namespace std::chrono;
    int64_t time_ms = duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();

    std::string msg((char *)msgPtr, msgSize);
    std::string endpointString = EndpointToString(endpoint);
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
    std::cout << "delta time between software send & receive (ms)" << time_ms - characteristics.time << std::endl;

    // bind the topic (if we have callbacks for it)

    if (!args.permitLoopback)
    {
      if (characteristics.uniqueHandleID == uniqueInstanceID && uniqueInstanceID != 0)
      {
        std::cout << "MultiClientReceiver::ProcessBroadcast() - received matching instance ID's and loopback disabled\n";
        return;
      }
    }

    // we have to have callbacks for this topic
    if (topicCallbacks.count(topic) == 0)
    {
      std::printf("MultiClientReceiver::ProcessBroadcast() - received a broadcast for topic \"%s\", but no callbacks for it were provided...\n", topic.c_str());
      return;
    }
    auto &callbacks = topicCallbacks[topic];

    // we have to not already have an active session
    std::shared_ptr<SessionClient> client;
    client = clients[topic].lock();
    if (client)
      return; // this session is still alive and kicking, leave it

    // this client does not already exist, we get to create a new one
    client = std::make_shared<SessionClient>(io_context,
                                             serverEndpoint.address().to_string(),
                                             serverEndpoint.port(),
                                             callbacks);

    if (client)
    {
      client->run();
      clients[topic] = client;
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
};

#endif