//
// Copyright (c) 2016-2019 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/vinniefalco/CppCon2018
//
// Modified by Keith Rausch

#ifndef BEASTWEBSERVERFLEXIBLE_NET_HPP
#define BEASTWEBSERVERFLEXIBLE_NET_HPP

#include <boost/asio.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/ssl.hpp>
#include <iostream>

#include <filesystem>

namespace BeastNetworking
{

    enum class Security { INSECURE, SECURE, BOTH };

namespace
{
    namespace beast = boost::beast;                 // from <boost/beast.hpp>
    namespace http = beast::http;                   // from <boost/beast/http.hpp>
    namespace websocket = beast::websocket;         // from <boost/beast/websocket.hpp>
    namespace net = boost::asio;                    // from <boost/asio.hpp>
    namespace ssl = boost::asio::ssl;               // from <boost/asio/ssl.hpp>
    using tcp = boost::asio::ip::tcp;               // from <boost/asio/ip/tcp.hpp>
}

// namespace net = boost::asio;      // from <boost/asio.hpp>
// using tcp = boost::asio::ip::tcp; // from <boost/asio/ip/tcp.hpp>


// Report a failure
static void fail(beast::error_code ec, char const* what)
{
    // ssl::error::stream_truncated, also known as an SSL "short read",
    // indicates the peer closed the connection without performing the
    // required closing handshake (for example, Google does this to
    // improve performance). Generally this can be a security issue,
    // but if your communication protocol is self-terminated (as
    // it is with both HTTP and WebSocket) then you may simply
    // ignore the lack of close_notify.
    //
    // https://github.com/boostorg/beast/issues/38
    //
    // https://security.stackexchange.com/questions/91435/how-to-handle-a-malicious-ssl-tls-shutdown
    //
    // When a short read would cut off the end of an HTTP message,
    // Beast returns the error beast::http::error::partial_message.
    // Therefore, if we see a short read here, it has occurred
    // after the message has been completed, so it is safe to ignore it.

    if(ec == net::ssl::error::stream_truncated)
        return;

    std::cerr << "BeastNetworking::" << what << ": " << ec.message() << "\n";
}


static bool load_server_certificate(boost::asio::ssl::context& ctx, const std::string &cert_chain_file, const std::string &private_key_file, const std::string &tmp_dh_file )
{

    ctx.set_default_verify_paths();
    ctx.set_options(
        boost::asio::ssl::context::default_workarounds
        | boost::asio::ssl::context::no_sslv2
        | boost::asio::ssl::context::no_sslv3 // https://stackoverflow.com/questions/43117638/boost-asio-get-with-client-certificate-sslv3-hand-shake-failed
        | boost::asio::ssl::context::single_dh_use
        );

    // check these files exist


    bool hadError = false;

    std::error_code ec;

    if ( ! std::filesystem::exists(cert_chain_file, ec) || ec /* short circuit*/)
    {
        std::cerr << "BeastWebServer - cert_chain_file does not exist \"" + cert_chain_file + "\"\n";
        std::cerr << ec << std::endl;
        hadError = true;
    }

    ec.clear();
    if ( ! std::filesystem::exists(private_key_file, ec) || ec /* short circuit*/)
    {
        std::cerr << "BeastWebServer - private_key_file does not exist \"" + private_key_file + "\"\n";
        std::cerr << ec << std::endl;
        hadError = true;
    }

    ec.clear();
    if ( ! std::filesystem::exists(tmp_dh_file, ec) || ec /* short circuit*/)
    {
        std::cerr << "BeastWebServer - cert_chain_file does not exist \"" + tmp_dh_file + "\"\n";
        std::cerr << ec << std::endl;
        hadError = true;
    }

    // // ssl_context.set_password_callback(std::bind(&server::get_password, this));
    boost::system::error_code boost_error;
    ctx.use_certificate_chain_file(cert_chain_file, boost_error);
    if (boost_error)
    {
        std::cerr << boost_error << std::endl;
        hadError = true;
    }

    boost_error.clear();
    ctx.use_private_key_file(private_key_file, boost::asio::ssl::context::pem, boost_error);
    if (boost_error)
    {
        std::cerr << boost_error << std::endl;
        hadError = true;
    }

    boost_error.clear();
    ctx.use_tmp_dh_file(tmp_dh_file, boost_error);
    if (boost_error)
    {
        std::cerr << boost_error << std::endl;
        hadError = true;
    }

    // man this function really got away from me
    
    return hadError;
}



} // namespace

#endif