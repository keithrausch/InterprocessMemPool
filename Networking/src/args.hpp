
#ifndef BEASTWEBSERVERFLEXIBLE_ARGS_HPP
#define BEASTWEBSERVERFLEXIBLE_ARGS_HPP

#include <string>

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


  struct MultiClientSenderArgs
  {
    std::string broadcastDestination = "255.255.255.255"; // send broadcast to all listeners
    unsigned short broadcastSendPort = 0;                 // send broadcast on any port
    unsigned short broadcastReceiverPort = 8081;          // change me - broadcast receiver port
    float heartbeatPeriod_seconds = 0.5;                  // seconds between heartbeats

    std::string serverBindAddress = "0.0.0.0"; // bind server to any address
    unsigned short serverBindPort = 0;         // bind server to any port
    bool verbose = false;
  };



}

#endif