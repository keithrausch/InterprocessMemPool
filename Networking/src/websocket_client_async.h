// //
// // Copyright (c) 2016-2019 Vinnie Falco (vinnie dot falco at gmail dot com)
// //
// // Distributed under the Boost Software License, Version 1.0. (See accompanying
// // file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
// //
// // Official repository: https://github.com/boostorg/beast
// //
// // Modified by Keith Rausch

// #ifndef IPC_WEBSOCKET_CLIENT_ASYNC_HPP
// #define IPC_WEBSOCKET_CLIENT_ASYNC_HPP

// #include <boost/beast/core.hpp>
// #include <boost/beast/websocket.hpp>
// #include <boost/asio/strand.hpp>
// #include <cstdlib>
// #include <functional>
// #include <iostream>
// #include <memory>
// #include <string>
// #include <queue>


// namespace IPC
// {

// namespace beast = boost::beast;         // from <boost/beast.hpp>
// namespace http = beast::http;           // from <boost/beast/http.hpp>
// namespace websocket = beast::websocket; // from <boost/beast/websocket.hpp>
// namespace net = boost::asio;            // from <boost/asio.hpp>
// using tcp = boost::asio::ip::tcp;       // from <boost/asio/ip/tcp.hpp>

// // Sends a WebSocket message and prints the response
// class SessionClient : public std::enable_shared_from_this<SessionClient>
// {
// public:
//   typedef std::function<void(beast::error_code, size_t)> CompletionHandlerT;

// private:

//   tcp::resolver resolver_;
//   websocket::stream<beast::tcp_stream> ws_;
//   beast::flat_buffer buffer_;
//   std::string serverAddress;
//   unsigned short serverPort;

//   typedef std::tuple<const void*, size_t, CompletionHandlerT> SpanAndHandlerT;

//   std::queue<SpanAndHandlerT> queue_; // woah this is a vector, its ever a vector in the beast example. weird.

// public:
//   struct Callbacks
//   {
//     typedef boost::asio::ip::tcp::endpoint endpointT;
//     typedef std::function<void(const endpointT &endpoint, const void *msgPtr, size_t msgSize)> CallbackReadT;
//     typedef std::function<void(const endpointT &endpoint, const beast::error_code &ec)> CallbackSocketAcceptT;
//     typedef std::function<void(const endpointT &endpoint, const beast::error_code &ec)> CallbackSocketCloseT;
//     typedef std::function<void(const endpointT &endpoint, const beast::error_code &ec)> CallbackErrorT;

//     CallbackReadT callbackRead;
//     CallbackSocketAcceptT callbackAccept;
//     CallbackSocketCloseT callbackClose;
//     CallbackErrorT callbackError;
//   };

//   Callbacks callbacks;
//   tcp::endpoint endpoint;

//   // Resolver and socket require an io_context
//   explicit SessionClient(net::io_context &ioc,
//                          const std::string &serverAddress_in,
//                          unsigned short serverPort_in,
//                          const Callbacks &callbacks_in = Callbacks())
//       : resolver_(net::make_strand(ioc)),
//         ws_(net::make_strand(ioc)),
//         serverAddress(serverAddress_in),
//         serverPort(serverPort_in),
//         callbacks(callbacks_in)
//   {
//   }

//   // Start the asynchronous operation
//   void
//   run()
//   {
//     // Save these for later
//     // host_ = serverAddress;

//     // Look up the domain name
//     resolver_.async_resolve(
//         serverAddress,
//         std::to_string(serverPort),
//         beast::bind_front_handler(
//             &SessionClient::on_resolve,
//             shared_from_this()));
//   }

//   void on_error(beast::error_code ec)
//   {
//     if (callbacks.callbackError)
//       callbacks.callbackError(endpoint, ec);

//     if (callbacks.callbackClose)
//       callbacks.callbackClose(endpoint, ec);
//   }

//   void
//   on_resolve(beast::error_code ec, tcp::resolver::results_type results)
//   {
//     if (ec)
//       return on_error(ec);

//     // Set the timeout for the operation
//     beast::get_lowest_layer(ws_).expires_after(std::chrono::seconds(30));

//     // Make the connection on the IP address we get from a lookup
//     beast::get_lowest_layer(ws_).async_connect(
//         results,
//         beast::bind_front_handler(
//             &SessionClient::on_connect,
//             shared_from_this()));
//   }

//   void
//   on_connect(beast::error_code ec, tcp::resolver::results_type::endpoint_type ep)
//   {
//     endpoint = ep;

//     if (ec)
//       return on_error(ec);

//     //
//     // TODO look at boost.org/doc/libs/1_75_0/libs/beast/example/websocket/server/fast/websocket_server_fast.cpp
//     // for improved performance. It looks like they play with compression rations, etc
//     //
//     //

//     // Turn off the timeout on the tcp_stream, because
//     // the websocket stream has its own timeout system.
//     beast::get_lowest_layer(ws_).expires_never();

//     // Set suggested timeout settings for the websocket
//     ws_.set_option(
//         websocket::stream_base::timeout::suggested(
//             beast::role_type::client));

//     // Set a decorator to change the User-Agent of the handshake
//     ws_.set_option(websocket::stream_base::decorator(
//         [](websocket::request_type &req) {
//           req.set(http::field::user_agent,
//                   std::string(BOOST_BEAST_VERSION_STRING) +
//                       " websocket-client-async");
//         }));

//     // Update the host_ string. This will provide the value of the
//     // Host HTTP header during the WebSocket handshake.
//     // See https://tools.ietf.org/html/rfc7230#section-5.4
//     std::string host_ = serverAddress;
//     host_ += ':' + std::to_string(ep.port());

//     // Perform the websocket handshake
//     ws_.async_handshake(host_, "/",
//                         beast::bind_front_handler(
//                             &SessionClient::on_handshake,
//                             shared_from_this()));
//   }

//   void
//   on_handshake(beast::error_code ec)
//   {
//     if (ec)
//       return on_error(ec);

//     // Send the message
//     ws_.async_read(
//         buffer_,
//         beast::bind_front_handler(&SessionClient::on_read, shared_from_this()));
//   }

//   void
//   on_read(beast::error_code ec, std::size_t bytes_transferred)
//   {
//     if (ec)
//       return on_error(ec);

//     auto mutableBuffer = buffer_.data();

//     if (callbacks.callbackRead)
//       callbacks.callbackRead(endpoint, mutableBuffer.data(), bytes_transferred);

//     // Clear the buffer
//     buffer_.consume(buffer_.size());

//     // Read another message
//     ws_.async_read(
//         buffer_,
//         beast::bind_front_handler( &SessionClient::on_read, shared_from_this()));
//   }

//   void sendAsync(const void* msgPtr, size_t msgSize, CompletionHandlerT &&completionHandler, bool overwrite)
//   {
//     // Post our work to the strand, this ensures
//     // that the members of `this` will not be
//     // accessed concurrently.

//     net::post(
//         ws_.get_executor(),
//         beast::bind_front_handler(&SessionClient::on_send, shared_from_this(), msgPtr, msgSize, std::forward<CompletionHandlerT>(completionHandler), overwrite));
//   }

//   void replaceSendAsync(const void* msgPtr, size_t msgSize, CompletionHandlerT &&completionHandler)
//   {
//     sendAsync(msgPtr, msgSize, std::move(completionHandler), true);
//   }


//   void on_send(const void* msgPtr, size_t msgSize, CompletionHandlerT &&completionHandler, bool overwrite)
//   {
//     if (overwrite && queue_.size() > 1)
//     {
//       // std::cout << "overwriting\n";
//       auto & back = queue_.back();
//       auto & callback = std::get<2>(back);
//       if (callback)
//       {
//         callback(boost::asio::error::operation_aborted, 0);
//         callback = CompletionHandlerT(); // not sure if this avoids move-sideeffects later
//       }
        
//       std::get<0>(back) = msgPtr;
//       std::get<1>(back) = msgSize;
//       std::get<2>(back) = std::move(completionHandler);

//       return;
//     }
//     // std::cout << "NOT overwriting\n";

//     queue_.emplace(msgPtr, msgSize, std::move(completionHandler));

//     // Are we already writing?
//     if (queue_.size() > 1)
//       return;

//     // We are not currently writing, so send this immediately
//     ws_.async_write(
//         boost::asio::buffer(msgPtr, msgSize),
//         beast::bind_front_handler(&SessionClient::on_write, shared_from_this()));
//   }

//   void on_write(beast::error_code ec, std::size_t writtenLength)
//   {
//     // call the user's completion handler
//     auto& handler = std::get<2>(queue_.front());
//     if (handler)
//       handler(ec, writtenLength);

//     // Remove the string from the queue
//     queue_.pop();

//     // Handle the error, if any
//     if (ec)
//       return on_error(ec);

//     // Send the next message if any
//     if (!queue_.empty())
//     {
//       auto &next = queue_.front();
//       const void *msgPtr = std::get<0>(next);
//       size_t msgSize = std::get<1>(next);

//       ws_.async_write(
//           boost::asio::buffer(msgPtr, msgSize),
//           beast::bind_front_handler(&SessionClient::on_write, shared_from_this()));
//     }
//   }
// };

// } // namespace


// #endif