//
// Copyright (c) 2016-2019 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/vinniefalco/CppCon2018
//
// Modified by Keith Rausch

#ifndef BEASTWEBSERVERFLEXIBLE_LISTENER_HPP
#define BEASTWEBSERVERFLEXIBLE_LISTENER_HPP

#include "net.hpp"
#include <memory>
#include "shared_state.hpp"

namespace BeastNetworking
{

// Accepts incoming connections and launches the sessions
class listener : public std::enable_shared_from_this<listener>
{
    net::io_context& ioc_;
    ssl::context& ctx_;
    tcp::acceptor acceptor_;
    // std::shared_ptr<std::string const> doc_root_;
    std::shared_ptr<shared_state> state_;
    Security security_;

public:
    tcp::endpoint localEndpoint;

    listener(
        net::io_context& ioc,
        ssl::context& ctx,
        tcp::endpoint endpoint,
        std::shared_ptr<shared_state> const& state, 
        Security security);

    // Start accepting incoming connections
    void run();

private:
    void do_accept();

    void on_accept(beast::error_code ec, tcp::socket socket);
};


} // namespace

#endif