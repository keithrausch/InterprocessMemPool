// //
// // Copyright (c) 2016-2019 Vinnie Falco (vinnie dot falco at gmail dot com)
// //
// // Distributed under the Boost Software License, Version 1.0. (See accompanying
// // file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
// //
// // Official repository: https://github.com/boostorg/beast
// //
// // modified by Keith Rausch

// //------------------------------------------------------------------------------
// //
// // Example: Advanced server, flex (plain + SSL)
// //
// //------------------------------------------------------------------------------


// #ifndef BEASTWEBSERVER_ADVANCED_SERVER_FLEXIBLE_HPP
// #define BEASTWEBSERVER_ADVANCED_SERVER_FLEXIBLE_HPP

// // #include "example/common/server_certificate.hpp"
// // #include "server_certificate.hpp"

// #include "net.hpp"
// #include "listener.hpp"

// #include <boost/beast/core.hpp>
// #include <boost/beast/http.hpp>
// #include <boost/beast/ssl.hpp>
// #include <boost/beast/websocket.hpp>
// #include <boost/beast/version.hpp>
// #include <boost/asio/bind_executor.hpp>
// #include <boost/asio/dispatch.hpp>
// #include <boost/asio/signal_set.hpp>
// #include <boost/asio/steady_timer.hpp>
// #include <boost/asio/strand.hpp>
// #include <boost/make_unique.hpp>
// #include <boost/optional.hpp>
// #include <algorithm>
// #include <cstdlib>
// #include <functional>
// #include <iostream>
// #include <memory>
// #include <string>
// #include <thread>
// #include <vector>
// #include <filesystem>
// #include "shared_state.hpp"


// namespace BeastNetworking
// {

// namespace
// {
//     namespace beast = boost::beast;                 // from <boost/beast.hpp>
//     namespace http = beast::http;                   // from <boost/beast/http.hpp>
//     namespace websocket = beast::websocket;         // from <boost/beast/websocket.hpp>
//     namespace net = boost::asio;                    // from <boost/asio.hpp>
//     namespace ssl = boost::asio::ssl;               // from <boost/asio/ssl.hpp>
//     using tcp = boost::asio::ip::tcp;               // from <boost/asio/ip/tcp.hpp>
// }

// // static bool load_server_certificate(boost::asio::ssl::context& ctx, const std::string &cert_chain_file, const std::string &private_key_file, const std::string &tmp_dh_file )
// // {

// //     ctx.set_default_verify_paths();
// //     ctx.set_options(
// //         boost::asio::ssl::context::default_workarounds
// //         | boost::asio::ssl::context::no_sslv2
// //         | boost::asio::ssl::context::no_sslv3 // https://stackoverflow.com/questions/43117638/boost-asio-get-with-client-certificate-sslv3-hand-shake-failed
// //         | boost::asio::ssl::context::single_dh_use
// //         );

// //     // check these files exist

// //     bool hadError = false;

// //     if ( ! std::filesystem::exists(cert_chain_file))
// //     {
// //         std::cerr << "BeastWebServer - cert_chain_file does not exist \"" + cert_chain_file + "\"\n";
// //         hadError = true;
// //     }
// //     if ( ! std::filesystem::exists(private_key_file))
// //     {
// //         std::cerr << "BeastWebServer - private_key_file does not exist \"" + private_key_file + "\"\n";
// //         hadError = true;
// //     }
// //     if ( ! std::filesystem::exists(tmp_dh_file))
// //     {
// //         std::cerr << "BeastWebServer - cert_chain_file does not exist \"" + tmp_dh_file + "\"\n";
// //         hadError = true;
// //     }

// //     // // ssl_context.set_password_callback(std::bind(&server::get_password, this));
// //     ctx.use_certificate_chain_file(cert_chain_file);
// //     ctx.use_private_key_file(private_key_file, boost::asio::ssl::context::pem);
// //     ctx.use_tmp_dh_file(tmp_dh_file);

// //     return hadError;
// // }



// // // Return a reasonable mime type based on the extension of a file.
// // static beast::string_view mime_type(beast::string_view path)
// // {
// //     using beast::iequals;
// //     auto const ext = [&path]
// //     {
// //         auto const pos = path.rfind(".");
// //         if(pos == beast::string_view::npos)
// //             return beast::string_view{};
// //         return path.substr(pos);
// //     }();
// //     if(iequals(ext, ".htm"))  return "text/html";
// //     if(iequals(ext, ".html")) return "text/html";
// //     if(iequals(ext, ".php"))  return "text/html";
// //     if(iequals(ext, ".css"))  return "text/css";
// //     if(iequals(ext, ".txt"))  return "text/plain";
// //     if(iequals(ext, ".js"))   return "application/javascript";
// //     if(iequals(ext, ".json")) return "application/json";
// //     if(iequals(ext, ".xml"))  return "application/xml";
// //     if(iequals(ext, ".swf"))  return "application/x-shockwave-flash";
// //     if(iequals(ext, ".flv"))  return "video/x-flv";
// //     if(iequals(ext, ".png"))  return "image/png";
// //     if(iequals(ext, ".jpe"))  return "image/jpeg";
// //     if(iequals(ext, ".jpeg")) return "image/jpeg";
// //     if(iequals(ext, ".jpg"))  return "image/jpeg";
// //     if(iequals(ext, ".gif"))  return "image/gif";
// //     if(iequals(ext, ".bmp"))  return "image/bmp";
// //     if(iequals(ext, ".ico"))  return "image/vnd.microsoft.icon";
// //     if(iequals(ext, ".tiff")) return "image/tiff";
// //     if(iequals(ext, ".tif"))  return "image/tiff";
// //     if(iequals(ext, ".svg"))  return "image/svg+xml";
// //     if(iequals(ext, ".svgz")) return "image/svg+xml";
// //     return "application/text";
// // }

// // // Append an HTTP rel-path to a local filesystem path.
// // // The returned path is normalized for the platform.
// // static std::string path_cat( beast::string_view base, beast::string_view path)
// // {
// //     if(base.empty())
// //         return std::string(path);
// //     std::string result(base);
// // #ifdef BOOST_MSVC
// //     char constexpr path_separator = '\\';
// //     if(result.back() == path_separator)
// //         result.resize(result.size() - 1);
// //     result.append(path.data(), path.size());
// //     for(auto& c : result)
// //         if(c == '/')
// //             c = path_separator;
// // #else
// //     char constexpr path_separator = '/';
// //     if(result.back() == path_separator)
// //         result.resize(result.size() - 1);
// //     result.append(path.data(), path.size());
// // #endif
// //     return result;
// // }

// // // This function produces an HTTP response for the given
// // // request. The type of the response object depends on the
// // // contents of the request, so the interface requires the
// // // caller to pass a generic lambda for receiving the response.
// // template< class Body, class Allocator, class Send>
// // void handle_request( beast::string_view doc_root, http::request<Body, http::basic_fields<Allocator>>&& req, Send&& send)
// // {
// //     // Returns a bad request response
// //     auto const bad_request =
// //     [&req](beast::string_view why)
// //     {
// //         http::response<http::string_body> res{http::status::bad_request, req.version()};
// //         res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
// //         res.set(http::field::content_type, "text/html");
// //         res.keep_alive(req.keep_alive());
// //         res.body() = std::string(why);
// //         res.prepare_payload();
// //         return res;
// //     };

// //     // Returns a not found response
// //     auto const not_found =
// //     [&req](beast::string_view target)
// //     {
// //         http::response<http::string_body> res{http::status::not_found, req.version()};
// //         res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
// //         res.set(http::field::content_type, "text/html");
// //         res.keep_alive(req.keep_alive());
// //         res.body() = "The resource '" + std::string(target) + "' was not found.";
// //         res.prepare_payload();
// //         return res;
// //     };

// //     // Returns a server error response
// //     auto const server_error =
// //     [&req](beast::string_view what)
// //     {
// //         http::response<http::string_body> res{http::status::internal_server_error, req.version()};
// //         res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
// //         res.set(http::field::content_type, "text/html");
// //         res.keep_alive(req.keep_alive());
// //         res.body() = "An error occurred: '" + std::string(what) + "'";
// //         res.prepare_payload();
// //         return res;
// //     };

// //     // Make sure we can handle the method
// //     if( req.method() != http::verb::get &&
// //         req.method() != http::verb::head)
// //         return send(bad_request("Unknown HTTP-method"));

// //     // Request path must be absolute and not contain "..".
// //     if( req.target().empty() ||
// //         req.target()[0] != '/' ||
// //         req.target().find("..") != beast::string_view::npos)
// //         return send(bad_request("Illegal request-target"));

// //     // Build the path to the requested file
// //     std::string path = path_cat(doc_root, req.target());
// //     if(req.target().back() == '/')
// //         path.append("index.html");

// //     // Attempt to open the file
// //     beast::error_code ec;
// //     http::file_body::value_type body;
// //     body.open(path.c_str(), beast::file_mode::scan, ec);

// //     // Handle the case where the file doesn't exist
// //     if(ec == beast::errc::no_such_file_or_directory)
// //         return send(not_found(req.target()));

// //     // Handle an unknown error
// //     if(ec)
// //         return send(server_error(ec.message()));

// //     // Cache the size since we need it after the move
// //     auto const size = body.size();

// //     // Respond to HEAD request
// //     if(req.method() == http::verb::head)
// //     {
// //         http::response<http::empty_body> res{http::status::ok, req.version()};
// //         res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
// //         res.set(http::field::content_type, mime_type(path));
// //         res.content_length(size);
// //         res.keep_alive(req.keep_alive());
// //         return send(std::move(res));
// //     }

// //     // Respond to GET request
// //     http::response<http::file_body> res{
// //         std::piecewise_construct,
// //         std::make_tuple(std::move(body)),
// //         std::make_tuple(http::status::ok, req.version())};
// //     res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
// //     res.set(http::field::content_type, mime_type(path));
// //     res.content_length(size);
// //     res.keep_alive(req.keep_alive());
// //     return send(std::move(res));
// // }

// //------------------------------------------------------------------------------

// // // Report a failure
// // static void fail(beast::error_code ec, char const* what)
// // {
// //     // ssl::error::stream_truncated, also known as an SSL "short read",
// //     // indicates the peer closed the connection without performing the
// //     // required closing handshake (for example, Google does this to
// //     // improve performance). Generally this can be a security issue,
// //     // but if your communication protocol is self-terminated (as
// //     // it is with both HTTP and WebSocket) then you may simply
// //     // ignore the lack of close_notify.
// //     //
// //     // https://github.com/boostorg/beast/issues/38
// //     //
// //     // https://security.stackexchange.com/questions/91435/how-to-handle-a-malicious-ssl-tls-shutdown
// //     //
// //     // When a short read would cut off the end of an HTTP message,
// //     // Beast returns the error beast::http::error::partial_message.
// //     // Therefore, if we see a short read here, it has occurred
// //     // after the message has been completed, so it is safe to ignore it.

// //     if(ec == net::ssl::error::stream_truncated)
// //         return;

// //     std::cerr << what << ": " << ec.message() << "\n";
// // }

// //------------------------------------------------------------------------------

// // // Echoes back all received WebSocket messages.
// // // This uses the Curiously Recurring Template Pattern so that
// // // the same code works with both SSL streams and regular sockets.
// // template<class Derived>
// // class websocket_session
// // {
// //     // Access the derived class, this is part of
// //     // the Curiously Recurring Template Pattern idiom.
// //     Derived&
// //     derived()
// //     {
// //         return static_cast<Derived&>(*this);
// //     }

// //     beast::flat_buffer buffer_;
// //     std::shared_ptr<shared_state> state_;
// //     std::queue<shared_state::SpanAndHandlerT> queue_; // no mutex needed, only ever modified inside handlers, which are in a strand


// //     std::shared_ptr<tcp::resolver> resolver_;



// // protected:
// //     void on_error(beast::error_code ec)
// //     {
// //         // Don't report these
// //         if (ec == net::error::operation_aborted || ec == websocket::error::closed)
// //             return;

// //         state_->on_error(endpoint, ec);
// //     }






// //   void on_resolve(beast::error_code ec, tcp::resolver::results_type results)
// //   {
// //     if (ec)
// //       return on_error(ec);

// //     // Set the timeout for the operation
// //     beast::get_lowest_layer(derived().ws()).expires_after(std::chrono::seconds(30));

// //     // Make the connection on the IP address we get from a lookup
// //     beast::get_lowest_layer(derived().ws()).async_connect(
// //         results,
// //         beast::bind_front_handler(
// //             &Derived::on_connect,
// //             derived().shared_from_this()));
// //   }




// //     void
// //     on_accept(beast::error_code ec)
// //     {
// //         // Handle the error, if any
// //         if (ec)
// //             return on_error(ec);

// //         // Add this session to the list of active sessions
// //         state_->join(&derived());

// //         // Read a message
// //         derived().ws().async_read(
// //             buffer_,
// //             beast::bind_front_handler(&websocket_session::on_read, derived().shared_from_this()));
// //     }


// //     void
// //     on_read( beast::error_code ec, std::size_t bytes_transferred)
// //     {
// //         // Handle the error, if any
// //         if (ec)
// //             return on_error(ec);

// //         // Send to all connections
// //         state_->on_read(endpoint, buffer_.data().data(), bytes_transferred);

// //         // Clear the buffer
// //         buffer_.consume(buffer_.size());

// //         // Read another message
// //         derived().ws().async_read(
// //             buffer_,
// //             beast::bind_front_handler(&websocket_session::on_read, derived().shared_from_this()));
// //     }


// //     void on_send(const void* msgPtr, size_t msgSize, shared_state::CompletionHandlerT &&completionHandler, bool overwrite)
// //     {
// //     if (overwrite && queue_.size() > 1)
// //     {
// //         // std::cout << "overwriting\n";
// //         auto & back = queue_.back();
// //         auto & callback = std::get<2>(back);
// //         if (callback)
// //         {
// //         callback(boost::asio::error::operation_aborted, 0);
// //         callback = shared_state::CompletionHandlerT(); // not sure if this avoids move-sideeffects later
// //         }
        
// //         std::get<0>(back) = msgPtr;
// //         std::get<1>(back) = msgSize;
// //         std::get<2>(back) = std::move(completionHandler);

// //         return;
// //     }
// //     // std::cout << "NOT overwriting\n";

// //     queue_.emplace(msgPtr, msgSize, std::move(completionHandler));

// //     // Are we already writing?
// //     if (queue_.size() > 1)
// //         return;

// //     // We are not currently writing, so send this immediately
// //     derived().ws().async_write(
// //         boost::asio::buffer(msgPtr, msgSize),
// //         beast::bind_front_handler(&websocket_session::on_write, derived().shared_from_this()));
// //     }

// //     void
// //     on_write( beast::error_code ec, std::size_t bytes_transferred)
// //     {
// //         // call the user's completion handler
// //         auto& handler = std::get<2>(queue_.front());
// //         if (handler)
// //             handler(ec, bytes_transferred);

// //         // Remove the string from the queue
// //         queue_.pop();

// //         // Handle the error, if any
// //         if (ec)
// //             return on_error(ec);

// //         // Send the next message if any
// //         if (!queue_.empty())
// //         {
// //             auto &next = queue_.front();
// //             const void *msgPtr = std::get<0>(next);
// //             size_t msgSize = std::get<1>(next);

// //             derived().ws().async_write(
// //                 boost::asio::buffer(msgPtr, msgSize),
// //                 beast::bind_front_handler(&websocket_session::on_write, derived().shared_from_this()));
// //         }
// //     }

// // public:
// //     std::string serverAddress;
// //     unsigned short serverPort;

// //     tcp::endpoint endpoint;



// //   void on_handshake(beast::error_code ec)
// //   {
// //     if (ec)
// //       return on_error(ec);

// //     // Send the message
// //     derived().ws().async_read(
// //         buffer_,
// //         beast::bind_front_handler(&websocket_session::on_read, derived().shared_from_this()));
// //   }

// //     // Start the asynchronous operation
// //     template<class Body, class Allocator>
// //     void
// //     runServer(http::request<Body, http::basic_fields<Allocator>> req)
// //     {
// //         // // Accept the WebSocket upgrade request
// //         // do_accept(std::move(req));

// //         // Set suggested timeout settings for the websocket
// //         derived().ws().set_option(
// //             websocket::stream_base::timeout::suggested(
// //                 beast::role_type::server));

// //         // Set a decorator to change the Server of the handshake
// //         derived().ws().set_option(websocket::stream_base::decorator(
// //             [](websocket::response_type& res)
// //             {
// //                 res.set(http::field::server,
// //                     std::string(BOOST_BEAST_VERSION_STRING) +
// //                         " websocket-chat-multi");
// //             }));

// //         // Accept the websocket handshake
// //         derived().ws().async_accept(
// //             req,
// //             beast::bind_front_handler(
// //                 &websocket_session::on_accept,
// //                 derived().shared_from_this()));

        
// //         derived().ws().binary(true); 
// //         // https://stackoverflow.com/questions/7730260/binary-vs-string-transfer-over-a-stream
// //         // means bytes sent are bytes received, no UTF-8 text encode/decode
// //     }

// //     websocket_session(
// //         std::shared_ptr<shared_state> const& state_in,
// //         const tcp::endpoint &endpoint_in) : state_(state_in), serverPort(0), endpoint(endpoint_in)
// //     {
// //         //derived().ws().binary(true); 
// //         // https://stackoverflow.com/questions/7730260/binary-vs-string-transfer-over-a-stream
// //         // means bytes sent are bytes received, no UTF-8 text encode/decode
// //     }

// //     websocket_session(net::io_context &ioc,
// //                       std::shared_ptr<shared_state> const& state_in,        
// //                       const std::string &serverAddress_in,
// //                       unsigned short serverPort_in) 
// //                       : state_(state_in), 
// //                       resolver_(std::make_shared<tcp::resolver>(net::make_strand(ioc))), 
// //                         serverAddress(serverAddress_in),
// //                         serverPort(serverPort_in),
// //                         endpoint()
// //     {
// //         //derived().ws().binary(true); 
// //         // https://stackoverflow.com/questions/7730260/binary-vs-string-transfer-over-a-stream
// //         // means bytes sent are bytes received, no UTF-8 text encode/decode
// //     }


// //     ~websocket_session()
// //     {
// //         {
// //             // std::lock_guard<std::mutex> guard(mutex_);
// //             while (queue_.size() > 0)
// //             {
// //             auto & callback = std::get<2>(queue_.front());
// //             if (callback)
// //                 callback(boost::asio::error::operation_aborted, 0);
// //             queue_.pop();
// //             }
// //         }

// //         // Remove this session from the list of active sessions
// //         state_->leave(&derived());
// //     }


// //     void sendAsync(const void* msgPtr, size_t msgSize, shared_state::CompletionHandlerT &&completionHandler, bool overwrite)
// //     {
// //     // Post our work to the strand, this ensures
// //     // that the members of `this` will not be
// //     // accessed concurrently.

// //     net::post(
// //         derived().ws().get_executor(),
// //         beast::bind_front_handler(&websocket_session::on_send, derived().shared_from_this(), msgPtr, msgSize, std::forward<shared_state::CompletionHandlerT>(completionHandler), overwrite));
// //     }

// //     void replaceSendAsync(const void* msgPtr, size_t msgSize, shared_state::CompletionHandlerT &&completionHandler)
// //     {
// //         sendAsync(msgPtr, msgSize, std::move(completionHandler), true);
// //     }


// //   // Resolver and socket require an io_context
// //   void RunClient()
// //   {
// //     // Look up the domain name
// //     resolver_->async_resolve(
// //         serverAddress,
// //         std::to_string(serverPort),
// //         beast::bind_front_handler(
// //             &websocket_session::on_resolve,
// //             derived().shared_from_this()));
// //   }




// // };

// // //------------------------------------------------------------------------------

// // // Handles a plain WebSocket connection
// // class plain_websocket_session
// //     : public websocket_session<plain_websocket_session>
// //     , public std::enable_shared_from_this<plain_websocket_session>
// // {
// //     websocket::stream<beast::tcp_stream> ws_;

// // public:
// //     // Create the session
// //     explicit
// //     plain_websocket_session(
// //         beast::tcp_stream&& stream,
// //         std::shared_ptr<shared_state> const& state,
// //         const tcp::endpoint &endpoint
// //         )
// //         :  websocket_session<plain_websocket_session>(state, endpoint), ws_(std::move(stream))
// //     {
// //     }

// //     explicit
// //     plain_websocket_session(
// //         beast::tcp_stream&& stream,
// //         net::io_context &ioc,
// //         std::shared_ptr<shared_state> const& state,

// //         const std::string &serverAddress,
// //         unsigned short serverPort)
// //         :  websocket_session<plain_websocket_session>(ioc, state, serverAddress, serverPort), ws_(std::move(stream))
// //     {
// //     }

// //     // Called by the base class
// //     websocket::stream<beast::tcp_stream>&
// //     ws()
// //     {
// //         return ws_;
// //     }




// //   void
// //   on_connect(beast::error_code ec, tcp::resolver::results_type::endpoint_type ep)
// //   {
// //     endpoint = ep;

// //     if (ec)
// //       return websocket_session<plain_websocket_session>::on_error(ec);

// //     //
// //     // TODO look at boost.org/doc/libs/1_75_0/libs/beast/example/websocket/server/fast/websocket_server_fast.cpp
// //     // for improved performance. It looks like they play with compression rations, etc
// //     //
// //     //

// //     // Turn off the timeout on the tcp_stream, because
// //     // the websocket stream has its own timeout system.
// //     beast::get_lowest_layer(ws_).expires_never();

// //     // Set suggested timeout settings for the websocket
// //     ws_.set_option(
// //         websocket::stream_base::timeout::suggested(
// //             beast::role_type::client));

// //     // Set a decorator to change the User-Agent of the handshake
// //     ws_.set_option(websocket::stream_base::decorator(
// //         [](websocket::request_type &req) {
// //           req.set(http::field::user_agent,
// //                   std::string(BOOST_BEAST_VERSION_STRING) +
// //                       " websocket-client-async");
// //         }));

// //     // Update the host_ string. This will provide the value of the
// //     // Host HTTP header during the WebSocket handshake.
// //     // See https://tools.ietf.org/html/rfc7230#section-5.4
// //     std::string host_ = serverAddress;
// //     host_ += ':' + std::to_string(ep.port());

// //     // Perform the websocket handshake
// //     ws_.async_handshake(host_, "/",
// //                         beast::bind_front_handler(
// //                             &websocket_session::on_handshake,
// //                             shared_from_this()));
// //   }

// // };

// // //------------------------------------------------------------------------------

// // // Handles an SSL WebSocket connection
// // class ssl_websocket_session
// //     : public websocket_session<ssl_websocket_session>
// //     , public std::enable_shared_from_this<ssl_websocket_session>
// // {
// //     websocket::stream<
// //         beast::ssl_stream<beast::tcp_stream>> ws_;

// // public:
// //     // Create the ssl_websocket_session
// //     explicit
// //     ssl_websocket_session(
// //         beast::ssl_stream<beast::tcp_stream>&& stream,
// //         std::shared_ptr<shared_state> const& state,
// //         const tcp::endpoint &endpoint)
// //         : websocket_session<ssl_websocket_session>(state, endpoint), ws_(std::move(stream))
// //     {
// //     }

// //     explicit
// //     ssl_websocket_session(
// //         beast::ssl_stream<beast::tcp_stream>&& stream,
// //         net::io_context &ioc,
// //         std::shared_ptr<shared_state> const& state,
// //         const std::string &serverAddress,
// //         unsigned short serverPort)
// //         : websocket_session<ssl_websocket_session>(ioc, state, serverAddress, serverPort), ws_(std::move(stream))
// //     {
// //     }

// //     // Called by the base class
// //     websocket::stream<
// //         beast::ssl_stream<beast::tcp_stream>>&
// //     ws()
// //     {
// //         return ws_;
// //     }


// // void on_connect(beast::error_code ec, tcp::resolver::results_type::endpoint_type ep)
// // {
// //     endpoint = ep;

// //     if(ec)
// //         return fail(ec, "connect");

// //     // Update the host_ string. This will provide the value of the
// //     // Host HTTP header during the WebSocket handshake.
// //     // See https://tools.ietf.org/html/rfc7230#section-5.4
// //     // host_ += ':' + std::to_string(ep.port());

// //     std::string host_ = serverAddress;
// //     host_ += ':' + std::to_string(ep.port());

// //     // Set a timeout on the operation
// //     beast::get_lowest_layer(ws_).expires_after(std::chrono::seconds(30));

// //     // Set SNI Hostname (many hosts need this to handshake successfully)
// //     if(! SSL_set_tlsext_host_name(
// //             ws_.next_layer().native_handle(),
// //             host_.c_str()))
// //     {
// //         ec = beast::error_code(static_cast<int>(::ERR_get_error()),
// //             net::error::get_ssl_category());
// //         return fail(ec, "connect");
// //     }

// //     // Perform the SSL handshake
// //     ws_.next_layer().async_handshake(
// //         ssl::stream_base::client,
// //         beast::bind_front_handler(
// //             &ssl_websocket_session::on_ssl_handshake,
// //             shared_from_this()));
// // }

// //   void on_ssl_handshake(beast::error_code ec)
// //   {

// //     if (ec)
// //       return on_error(ec);

// //     //
// //     // TODO look at boost.org/doc/libs/1_75_0/libs/beast/example/websocket/server/fast/websocket_server_fast.cpp
// //     // for improved performance. It looks like they play with compression rations, etc
// //     //
// //     //

// //     // Turn off the timeout on the tcp_stream, because
// //     // the websocket stream has its own timeout system.
// //     beast::get_lowest_layer(ws_).expires_never();

// //     // Set suggested timeout settings for the websocket
// //     ws_.set_option(
// //         websocket::stream_base::timeout::suggested(
// //             beast::role_type::client));

// //     // Set a decorator to change the User-Agent of the handshake
// //     ws_.set_option(websocket::stream_base::decorator(
// //         [](websocket::request_type &req) {
// //           req.set(http::field::user_agent,
// //                   std::string(BOOST_BEAST_VERSION_STRING) +
// //                       " websocket-client-async");
// //         }));

// //     // Update the host_ string. This will provide the value of the
// //     // Host HTTP header during the WebSocket handshake.
// //     // See https://tools.ietf.org/html/rfc7230#section-5.4
// //     std::string host_ = serverAddress;
// //     host_ += ':' + std::to_string(serverPort); // std::to_string(ep.port());

// //     // Perform the websocket handshake
// //     ws_.async_handshake( host_ , "/",
// //                         beast::bind_front_handler(
// //                             &websocket_session::on_handshake,
// //                             shared_from_this()));
// //   }
    
// // };


// // // ------------------------------------------------------------------------------

// // template<class Body, class Allocator>
// // void
// // make_websocket_session_server(
// //     beast::tcp_stream stream,
// //         std::shared_ptr<shared_state> const& state,
// //         const tcp::endpoint &endpoint,
// //     http::request<Body, http::basic_fields<Allocator>> req)
// // {
// //     std::make_shared<plain_websocket_session>(
// //         std::move(stream), state, endpoint)->runServer(std::move(req));
// // }

// // template<class Body, class Allocator>
// // void
// // make_websocket_session_server(
// //     beast::ssl_stream<beast::tcp_stream> stream,
// //         std::shared_ptr<shared_state> const& state,
// //         const tcp::endpoint &endpoint,
// //     http::request<Body, http::basic_fields<Allocator>> req)
// // {
// //     std::make_shared<ssl_websocket_session>(
// //         std::move(stream), state, endpoint)->runServer(std::move(req));
// // }





// // // template<class Body, class Allocator>
// // static std::shared_ptr<plain_websocket_session>
// // make_websocket_session_client(net::io_context &ioc,
// //                               std::shared_ptr<shared_state> const& state,
// //                               const std::string &serverAddress,
// //                               unsigned short serverPort)
// // {
// //     return std::make_shared<plain_websocket_session>(beast::tcp_stream(net::make_strand(ioc)), ioc, state, serverAddress, serverPort);
// // }

// // // template<class Body, class Allocator>
// // static std::shared_ptr<ssl_websocket_session>
// // make_websocket_session_client(net::io_context &ioc,
// //                               ssl::context& ctx,
// //                               std::shared_ptr<shared_state> const& state,
// //                               const std::string &serverAddress,
// //                               unsigned short serverPort)
// // {
// //     return std::make_shared<ssl_websocket_session>(beast::ssl_stream<beast::tcp_stream>(net::make_strand(ioc), ctx), ioc, state, serverAddress, serverPort);
// // }



// //   // Start the asynchronous operation
// //   static void runClient(net::io_context &ioc,
// //                          const std::string &serverAddress,
// //                          unsigned short serverPort)
// //   {
// //     // Save these for later
// //     // host_ = serverAddress;

// //   }





// //------------------------------------------------------------------------------

// // Handles an HTTP server connection.
// // This uses the Curiously Recurring Template Pattern so that
// // the same code works with both SSL streams and regular sockets.
// template<class Derived>
// class http_session
// {
//     // Access the derived class, this is part of
//     // the Curiously Recurring Template Pattern idiom.
//     Derived&
//     derived()
//     {
//         return static_cast<Derived&>(*this);
//     }

//     // This queue is used for HTTP pipelining.
//     class queue
//     {
//         enum
//         {
//             // Maximum number of responses we will queue
//             limit = 8
//         };

//         // The type-erased, saved work item
//         struct work
//         {
//             virtual ~work() = default;
//             virtual void operator()() = 0;
//         };

//         http_session& self_;
//         std::vector<std::unique_ptr<work>> items_;

//     public:
//         explicit
//         queue(http_session& self)
//             : self_(self)
//         {
//             static_assert(limit > 0, "queue limit must be positive");
//             items_.reserve(limit);
//         }

//         // Returns `true` if we have reached the queue limit
//         bool
//         is_full() const
//         {
//             return items_.size() >= limit;
//         }

//         // Called when a message finishes sending
//         // Returns `true` if the caller should initiate a read
//         bool
//         on_write()
//         {
//             BOOST_ASSERT(! items_.empty());
//             auto const was_full = is_full();
//             items_.erase(items_.begin());
//             if(! items_.empty())
//                 (*items_.front())();
//             return was_full;
//         }

//         // Called by the HTTP handler to send a response.
//         template<bool isRequest, class Body, class Fields>
//         void
//         operator()(http::message<isRequest, Body, Fields>&& msg)
//         {
//             // This holds a work item
//             struct work_impl : work
//             {
//                 http_session& self_;
//                 http::message<isRequest, Body, Fields> msg_;

//                 work_impl(
//                     http_session& self,
//                     http::message<isRequest, Body, Fields>&& msg)
//                     : self_(self)
//                     , msg_(std::move(msg))
//                 {
//                 }

//                 void
//                 operator()()
//                 {
//                     http::async_write(
//                         self_.derived().stream(),
//                         msg_,
//                         beast::bind_front_handler(
//                             &http_session::on_write,
//                             self_.derived().shared_from_this(),
//                             msg_.need_eof()));
//                 }
//             };

//             // Allocate and store the work
//             items_.push_back(
//                 boost::make_unique<work_impl>(self_, std::move(msg)));

//             // If there was no previous work, start this one
//             if(items_.size() == 1)
//                 (*items_.front())();
//         }
//     };

//     // std::shared_ptr<std::string const> doc_root_;
//     std::shared_ptr<shared_state> state_;
//     queue queue_;
//     tcp::endpoint remote_endpoint;

//     // The parser is stored in an optional container so we can
//     // construct it from scratch it at the beginning of each new message.
//     boost::optional<http::request_parser<http::string_body>> parser_;

// protected:
//     beast::flat_buffer buffer_;

// public:
//     // Construct the session
//     http_session(
//         beast::flat_buffer buffer,
//         std::shared_ptr<shared_state> const& state,
//         // std::shared_ptr<std::string const> const& doc_root,
//         tcp::endpoint remote_endpoint_in
//         )
//         : state_(state),
//         // doc_root_(doc_root), 
//         remote_endpoint(remote_endpoint_in)
//         , queue_(*this)
//         , buffer_(std::move(buffer))
//     {
//     }

//     void
//     do_read()
//     {
//         // Construct a new parser for each message
//         parser_.emplace();

//         // Apply a reasonable limit to the allowed size
//         // of the body in bytes to prevent abuse.
//         parser_->body_limit(10000);

//         // Set the timeout.
//         beast::get_lowest_layer(
//             derived().stream()).expires_after(std::chrono::seconds(30));

//         // Read a request using the parser-oriented interface
//         http::async_read(
//             derived().stream(),
//             buffer_,
//             *parser_,
//             beast::bind_front_handler(
//                 &http_session::on_read,
//                 derived().shared_from_this()));
//     }

//     void
//     on_read(beast::error_code ec, std::size_t bytes_transferred)
//     {
//         boost::ignore_unused(bytes_transferred);

//         // This means they closed the connection
//         if(ec == http::error::end_of_stream)
//             return derived().do_eof();

//         if(ec)
//             return fail(ec, "read");

//         // See if it is a WebSocket Upgrade
//         if(websocket::is_upgrade(parser_->get()))
//         {
//             // Disable the timeout.
//             // The websocket::stream uses its own timeout settings.
//             beast::get_lowest_layer(derived().stream()).expires_never();

//             // Create a websocket session, transferring ownership
//             // of both the socket and the HTTP request.
//             return make_websocket_session_server(
//                 derived().release_stream(), state_, remote_endpoint,
//                 parser_->release());
//         }

//         // Send the response
//         // handle_request(*doc_root_, parser_->release(), queue_);
//         handle_request(state_->doc_root(), parser_->release(), queue_);

//         // If we aren't at the queue limit, try to pipeline another request
//         if(! queue_.is_full())
//             do_read();
//     }

//     void
//     on_write(bool close, beast::error_code ec, std::size_t bytes_transferred)
//     {
//         boost::ignore_unused(bytes_transferred);

//         if(ec)
//             return fail(ec, "write");

//         if(close)
//         {
//             // This means we should close the connection, usually because
//             // the response indicated the "Connection: close" semantic.
//             return derived().do_eof();
//         }

//         // Inform the queue that a write completed
//         if(queue_.on_write())
//         {
//             // Read another request
//             do_read();
//         }
//     }
// };

// //------------------------------------------------------------------------------

// // Handles a plain HTTP connection
// class plain_http_session
//     : public http_session<plain_http_session>
//     , public std::enable_shared_from_this<plain_http_session>
// {
//     beast::tcp_stream stream_;

// public:
//     // Create the session
//     plain_http_session(
//         beast::tcp_stream&& stream,
//         beast::flat_buffer&& buffer,
//         std::shared_ptr<shared_state> const& state
//         //std::shared_ptr<std::string const> const& doc_root
//         )
//         : http_session<plain_http_session>(
//             std::move(buffer),
//             state,
//             stream.socket().remote_endpoint())
//         , stream_(std::move(stream))
//     {
//     }

//     // Start the session
//     void
//     run()
//     {
//         this->do_read();
//     }

//     // Called by the base class
//     beast::tcp_stream&
//     stream()
//     {
//         return stream_;
//     }

//     // Called by the base class
//     beast::tcp_stream
//     release_stream()
//     {
//         return std::move(stream_);
//     }

//     // Called by the base class
//     void
//     do_eof()
//     {
//         // Send a TCP shutdown
//         beast::error_code ec;
//         stream_.socket().shutdown(tcp::socket::shutdown_send, ec);

//         // At this point the connection is closed gracefully
//     }
// };

// //------------------------------------------------------------------------------

// // Handles an SSL HTTP connection
// class ssl_http_session
//     : public http_session<ssl_http_session>
//     , public std::enable_shared_from_this<ssl_http_session>
// {
//     beast::ssl_stream<beast::tcp_stream> stream_;

// public:
//     // Create the http_session
//     ssl_http_session(
//         beast::tcp_stream&& stream,
//         ssl::context& ctx,
//         beast::flat_buffer&& buffer,
//         std::shared_ptr<shared_state> const& state
//         //std::shared_ptr<std::string const> const& doc_root
//         )
//         : http_session<ssl_http_session>(
//             std::move(buffer),
//             state,
//             stream.socket().remote_endpoint())
//         , stream_(std::move(stream), ctx)
//     {
//     }

//     // Start the session
//     void
//     run()
//     {
//         // Set the timeout.
//         beast::get_lowest_layer(stream_).expires_after(std::chrono::seconds(30));

//         // Perform the SSL handshake
//         // Note, this is the buffered version of the handshake.
//         stream_.async_handshake(
//             ssl::stream_base::server,
//             buffer_.data(),
//             beast::bind_front_handler(
//                 &ssl_http_session::on_handshake,
//                 shared_from_this()));
//     }

//     // Called by the base class
//     beast::ssl_stream<beast::tcp_stream>&
//     stream()
//     {
//         return stream_;
//     }

//     // Called by the base class
//     beast::ssl_stream<beast::tcp_stream>
//     release_stream()
//     {
//         return std::move(stream_);
//     }

//     // Called by the base class
//     void
//     do_eof()
//     {
//         // Set the timeout.
//         beast::get_lowest_layer(stream_).expires_after(std::chrono::seconds(30));

//         // Perform the SSL shutdown
//         stream_.async_shutdown(
//             beast::bind_front_handler(
//                 &ssl_http_session::on_shutdown,
//                 shared_from_this()));
//     }

// private:
//     void
//     on_handshake(
//         beast::error_code ec,
//         std::size_t bytes_used)
//     {
//         if(ec)
//             return fail(ec, "handshake");

//         // Consume the portion of the buffer used by the handshake
//         buffer_.consume(bytes_used);

//         do_read();
//     }

//     void
//     on_shutdown(beast::error_code ec)
//     {
//         if(ec)
//             return fail(ec, "shutdown");

//         // At this point the connection is closed gracefully
//     }
// };

// //------------------------------------------------------------------------------

// // Detects SSL handshakes
// class detect_session : public std::enable_shared_from_this<detect_session>
// {
//     beast::tcp_stream stream_;
//     ssl::context& ctx_;
//     // std::shared_ptr<std::string const> doc_root_;
//     std::shared_ptr<shared_state> state_;
//     beast::flat_buffer buffer_;

// public:
//     explicit
//     detect_session(
//         tcp::socket&& socket,
//         ssl::context& ctx,
//         std::shared_ptr<shared_state> const& state
//         // std::shared_ptr<std::string const> const& doc_root
//         )
//         : stream_(std::move(socket))
//         , ctx_(ctx)
//         // , doc_root_(doc_root)
//         , state_(state)
//     {
//     }

//     // Launch the detector
//     void
//     run()
//     {
//         // We need to be executing within a strand to perform async operations
//         // on the I/O objects in this session. Although not strictly necessary
//         // for single-threaded contexts, this example code is written to be
//         // thread-safe by default.
//         net::dispatch(
//             stream_.get_executor(),
//             beast::bind_front_handler(
//                 &detect_session::on_run,
//                 this->shared_from_this()));
//     }

//     void
//     on_run()
//     {
//         // Set the timeout.
//         stream_.expires_after(std::chrono::seconds(30));

//         beast::async_detect_ssl(
//             stream_,
//             buffer_,
//             beast::bind_front_handler(
//                 &detect_session::on_detect,
//                 this->shared_from_this()));
//     }

//     void
//     on_detect(beast::error_code ec, bool result)
//     {
//         if(ec)
//             return fail(ec, "detect");

//         if(result)
//         {
//             // Launch SSL session
//             std::make_shared<ssl_http_session>(
//                 std::move(stream_),
//                 ctx_,
//                 std::move(buffer_),
//                 state_)->run();
//             return;
//         }

//         // Launch plain session
//         std::make_shared<plain_http_session>(
//             std::move(stream_),
//             std::move(buffer_),
//             state_)->run();
//     }
// };

// // // Accepts incoming connections and launches the sessions
// // class listener : public std::enable_shared_from_this<listener>
// // {
// //     net::io_context& ioc_;
// //     ssl::context& ctx_;
// //     tcp::acceptor acceptor_;
// //     // std::shared_ptr<std::string const> doc_root_;
// //     std::shared_ptr<shared_state> state_;

// // public:
// //     tcp::endpoint localEndpoint;

// //     listener(
// //         net::io_context& ioc,
// //         ssl::context& ctx,
// //         tcp::endpoint endpoint,
// //         std::shared_ptr<shared_state> const& state)
// //         : ioc_(ioc)
// //         , ctx_(ctx)
// //         , acceptor_(net::make_strand(ioc))
// //         , state_(state)
// //     {
// //         beast::error_code ec;

// //         // Open the acceptor
// //         acceptor_.open(endpoint.protocol(), ec);
// //         if(ec)
// //         {
// //             fail(ec, "open");
// //             return;
// //         }

// //         // Allow address reuse
// //         acceptor_.set_option(net::socket_base::reuse_address(true), ec);
// //         if(ec)
// //         {
// //             fail(ec, "set_option");
// //             return;
// //         }

// //         // Bind to the server address
// //         acceptor_.bind(endpoint, ec);
// //         if(ec)
// //         {
// //             fail(ec, "bind");
// //             return;
// //         }

// //         localEndpoint = acceptor_.local_endpoint();

// //         // Start listening for connections
// //         acceptor_.listen(
// //             net::socket_base::max_listen_connections, ec);
// //         if(ec)
// //         {
// //             fail(ec, "listen");
// //             return;
// //         }
// //     }

// //     // Start accepting incoming connections
// //     void
// //     run()
// //     {
// //         do_accept();
// //     }

// // private:
// //     void
// //     do_accept()
// //     {
// //         // The new connection gets its own strand
// //         acceptor_.async_accept(
// //             net::make_strand(ioc_),
// //             beast::bind_front_handler(
// //                 &listener::on_accept,
// //                 shared_from_this()));
// //     }

// //     void
// //     on_accept(beast::error_code ec, tcp::socket socket)
// //     {
// //         if(ec)
// //         {
// //             fail(ec, "accept");
// //         }
// //         else
// //         {
// //             // Create the detector http_session and run it
// //             std::make_shared<detect_session>(
// //                 std::move(socket),
// //                 ctx_,
// //                 state_)->run();
// //         }

// //         // Accept another connection
// //         do_accept();
// //     }
// // };



// } // namespace

// #endif
