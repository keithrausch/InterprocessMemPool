

#include "http_session.hpp"

namespace BeastNetworking
{



    // Access the derived class, this is part of
    // the Curiously Recurring Template Pattern idiom.
    template<class Derived>
    Derived& http_session<Derived>::derived()
    {
        return static_cast<Derived&>(*this);
    }

    // Construct the session
    template<class Derived>
    http_session<Derived>::http_session(
        beast::flat_buffer buffer,
        std::shared_ptr<shared_state> const& state,
        // std::shared_ptr<std::string const> const& doc_root,
        tcp::endpoint remote_endpoint_in
        )
        : state_(state),
        // doc_root_(doc_root), 
        endpoint(remote_endpoint_in)
        , queue_(*this)
        , buffer_(std::move(buffer))
        , queue_size_(0)
    {
    }

    template<class Derived>
    void http_session<Derived>::do_read()
    {
        // shoving this branch here so that we can natively support raw tcp sockets instead of 
        // forcing people to use http. sure i should have made another class, but i woke up at 3am
        // and we need this
        if (state_ && state_->server_is_tcp_instead_of_http())
        {
            size_t header_size = state_->nonhttp_header_size();
            if (nonhttp_buffer_size_ < header_size)
            {
                nonhttp_buffer_.resize(header_size);
                nonhttp_buffer_size_ = nonhttp_buffer_.size();
            }

            boost::asio::async_read(derived().socket(),
                boost::asio::buffer(nonhttp_buffer_.data(), header_size),
                beast::bind_front_handler(
                &http_session::on_nonhttp_read_header, derived().shared_from_this()));
        }
        else
        {
            // Construct a new parser for each message
            parser_.emplace();

            // Apply a reasonable limit to the allowed size
            // of the body in bytes to prevent abuse.
            parser_->body_limit(10000);

            // Set the timeout.
            // beast::get_lowest_layer(derived().stream()).expires_after(std::chrono::seconds(30));

            // Read a request using the parser-oriented interface
            http::async_read(
                derived().stream(),
                buffer_,
                *parser_,
                beast::bind_front_handler(
                    &http_session::on_read,
                    derived().shared_from_this()));
        }
    }

  template<class Derived>
  void http_session<Derived>::on_nonhttp_read_header(const boost::system::error_code& error, size_t /*bytes_transferred*/)
  {
    if (error)
    {
        on_error(error);
        return;
    }

    if (state_ && state_->callbacks.callbackGetBodyLengthFromNonHTTPHeader)
    {
        size_t body_length = state_->callbacks.callbackGetBodyLengthFromNonHTTPHeader((void*)nonhttp_buffer_.data(), nonhttp_buffer_.size());
        size_t header_length = state_->nonhttp_header_size();
        size_t total_msg_length = body_length + header_length;

        if (nonhttp_buffer_size_ < total_msg_length)
        {
            nonhttp_buffer_.resize(total_msg_length);
            nonhttp_buffer_size_ = nonhttp_buffer_.size();
        }

        boost::asio::async_read(derived().socket(),
            boost::asio::buffer(nonhttp_buffer_.data()+header_length, body_length),
            beast::bind_front_handler(&http_session::on_nonhttp_read_body, derived().shared_from_this(), header_length, body_length));
    }
  }

  template<class Derived>
  void http_session<Derived>::on_nonhttp_read_body(size_t header_size, size_t body_size, const boost::system::error_code& error, size_t bytes_transferred)
  {
    bool had_error = false;
    size_t expected_byte_count = header_size+body_size;

    had_error |= (body_size != bytes_transferred);
    had_error |= (expected_byte_count > nonhttp_buffer_.size());

    if (error || had_error)
    {
        on_error(error);
        return;
    }
    
    if (state_)
        state_->on_http_read(endpoint, nonhttp_buffer_.data(), expected_byte_count);
    do_read();
  }




    template<class Derived>
    void http_session<Derived>::on_read(beast::error_code ec, std::size_t bytes_transferred)
    {
        boost::ignore_unused(bytes_transferred);

        // This means they closed the connection
        if(ec == http::error::end_of_stream)
            return derived().do_eof();

        if(ec)
            return fail(ec, "on_read");

        // See if it is a WebSocket Upgrade
        if(websocket::is_upgrade(parser_->get()))
        {
            // Disable the timeout.
            // The websocket::stream uses its own timeout settings.
            beast::get_lowest_layer(derived().stream()).expires_never();

            // Create a websocket session, transferring ownership
            // of both the socket and the HTTP request.
            return make_websocket_session_server(
                derived().release_stream(), state_, endpoint,
                parser_->release());
        }

        // Send the response
        auto on_post = [this](const void* ptr, size_t count)
        {
            if (state_)
                state_->on_http_read(endpoint, ptr, count);
        };

        handle_request(state_->doc_root(), parser_->release(), queue_, on_post);

        // If we aren't at the queue limit, try to pipeline another request
        if(! queue_.is_full())
            do_read();
    }

    template<class Derived>
    void http_session<Derived>::on_write(bool close, beast::error_code ec, std::size_t bytes_transferred)
    {
        boost::ignore_unused(bytes_transferred);

        if(ec)
            return fail(ec, "on_write");

        if(close)
        {
            // This means we should close the connection, usually because
            // the response indicated the "Connection: close" semantic.
            return derived().do_eof();
        }

        // Inform the queue that a write completed
        if(queue_.on_write())
        {
            // Read another request
            do_read();
        }
    }

    template<class Derived>
    void http_session<Derived>::sendAsync(const void* msgPtr, size_t msgSize, shared_state::CompletionHandlerT completionHandler, bool force_send, size_t max_queue_size)
    {
    // Post our work to the strand, this ensures
    // that the members of `this` will not be
    // accessed concurrently.

    net::post(
        derived().stream().get_executor(),
        beast::bind_front_handler(&http_session::on_send, derived().shared_from_this(), msgPtr, msgSize, std::move(completionHandler), force_send, max_queue_size));
    }

    template<class Derived>
    void http_session<Derived>::on_send(const void* msgPtr, size_t msgSize, shared_state::CompletionHandlerT &&completionHandler, bool force_send, size_t max_queue_size)
    {
        // update hypothetical send stats here
        // rate_enforcer.update_hypothetical_rate(msgSize);

        // if we have more messages in the queue than allowed, we need to start blowing them away
        // BUT we cant blow away messages marked as 'must_send' nomatter what
        // se we are basically doing our best to honor the max_queue_size, but will exceed it if we need to
        // in order to not drop important messages
        max_queue_size = std::max(max_queue_size, (size_t)2); // the 1st element is being sent right now, cant replace it

        for (size_t i = 2; i < send_queue_.size() && send_queue_.size() >= max_queue_size ; ++i)
        {
            auto & msg = send_queue_[i];
            auto & callback = std::get<2>(msg);
            auto force = std::get<3>(msg);
            if ( ! force)
            {
                if (callback)
                {
                    callback(boost::asio::error::operation_aborted, 0, endpoint);
                    callback = shared_state::CompletionHandlerT(); // not sure if this avoids move-sideeffects later
                }

                // queue_.pop_back();
                send_queue_.erase(send_queue_.begin() + i);
                // queue size recorded further down
                --i;
            }
        }


        send_queue_.emplace_back(msgPtr, msgSize, std::move(completionHandler), force_send);
        queue_size_ = send_queue_.size();

        // Are we already writing?
        if (send_queue_.size() > 1)
            return;

        bool may_send = true;//rate_enforcer.should_send(msgSize) || force_send;
        may_send &= (msgPtr!=nullptr && msgSize>0);

        if ( may_send  && queue_.size() == 0)
        {
            // We are not currently writing, so send this immediately
            derived().socket().async_send(
                boost::asio::buffer(msgPtr, msgSize),
                beast::bind_front_handler(&http_session::on_write2, derived().shared_from_this()));
        }
        else
        {
            net::post(derived().stream().get_executor(), 
                beast::bind_front_handler(&http_session::on_write2, derived().shared_from_this(), boost::asio::error::operation_aborted, 0)
                );
        }
    }

    template<class Derived>
    void http_session<Derived>::on_write2( beast::error_code ec, std::size_t bytes_transferred)
    {
        // call the user's completion handler
        auto& handler = std::get<2>(send_queue_.front());
        if (handler)
            handler(ec, bytes_transferred, endpoint);

        // Remove the string from the queue
        send_queue_.pop_front();
        queue_size_ = send_queue_.size();

        // Handle the error, if any
        if (ec)
        {
            on_error(ec);
        }

        // Send the next message if any
        if (!send_queue_.empty())
        {
            auto &next = send_queue_.front();
            const void *msgPtr = std::get<0>(next);
            size_t msgSize = std::get<1>(next);
            bool force = std::get<3>(next);

            bool may_send = true;//rate_enforcer.should_send(msgSize) || force;
            may_send &= (msgPtr!=nullptr && msgSize>0);

            if (may_send && queue_.size() == 0)
            {
                derived().socket().async_send(
                    boost::asio::buffer(msgPtr, msgSize),
                    beast::bind_front_handler(&http_session::on_write2, derived().shared_from_this()));
            }
            else
            {
                net::post(derived().stream().get_executor(), 
                    beast::bind_front_handler(&http_session::on_write2, derived().shared_from_this(), boost::asio::error::operation_aborted, 0)
                 );
            }
        }
    }


template<class Derived>
    void http_session<Derived>::on_error(beast::error_code ec)
    {
        // Don't report these
        if (ec == net::error::operation_aborted || ec == websocket::error::closed)
            return;

        state_->on_error(endpoint, ec);
    }


template class http_session<plain_http_session>;
template class http_session<ssl_http_session>;

//------------------------------------------------------------------------------


    plain_http_session::plain_http_session(
        beast::tcp_stream&& stream,
        beast::flat_buffer&& buffer,
        std::shared_ptr<shared_state> const& state
        //std::shared_ptr<std::string const> const& doc_root
        )
        : http_session<plain_http_session>(
            std::move(buffer),
            state,
            stream.socket().remote_endpoint())
        , stream_(std::move(stream))
    {
    }

    plain_http_session::~plain_http_session()
    {
        state_->leave(this);
    }

    // Start the session
    void plain_http_session::run()
    {
        state_->join(this); // join the shared state here now that we support tcp comms
        this->do_read();
    }

    // Called by the base class
    beast::tcp_stream& plain_http_session::stream()
    {
        return stream_;
    }

    boost::asio::ip::tcp::socket& plain_http_session::socket()
    {
        return stream_.socket();
    }

    // Called by the base class
    beast::tcp_stream plain_http_session::release_stream()
    {
        return std::move(stream_);
    }

    // Called by the base class
    void plain_http_session::do_eof()
    {
        // Send a TCP shutdown
        beast::error_code ec;
        stream_.socket().shutdown(tcp::socket::shutdown_send, ec);

        // At this point the connection is closed gracefully
    }


//------------------------------------------------------------------------------


    ssl_http_session::ssl_http_session(
        beast::tcp_stream&& stream,
        ssl::context& ctx,
        beast::flat_buffer&& buffer,
        std::shared_ptr<shared_state> const& state
        //std::shared_ptr<std::string const> const& doc_root
        )
        : http_session<ssl_http_session>(
            std::move(buffer),
            state,
            stream.socket().remote_endpoint())
        , stream_(std::move(stream), ctx)
    {
    }

    ssl_http_session::~ssl_http_session()
    {
        state_->leave(this);
    }

    // Start the session
    void ssl_http_session::run()
    {
        // Set the timeout.
        // beast::get_lowest_layer(stream_).expires_after(std::chrono::seconds(30));

        // Perform the SSL handshake
        // Note, this is the buffered version of the handshake.
        stream_.async_handshake(
            ssl::stream_base::server,
            buffer_.data(),
            beast::bind_front_handler(
                &ssl_http_session::on_handshake,
                shared_from_this()));
    }

    // Called by the base class
    beast::ssl_stream<beast::tcp_stream>& ssl_http_session::stream()
    {
        return stream_;
    }

    boost::asio::ip::tcp::socket& ssl_http_session::socket()
    {
        return stream_.next_layer().socket();
    }

    // Called by the base class
    beast::ssl_stream<beast::tcp_stream> ssl_http_session::release_stream()
    {
        return std::move(stream_);
    }

    // Called by the base class
    void ssl_http_session::do_eof()
    {
        // Set the timeout.
        // beast::get_lowest_layer(stream_).expires_after(std::chrono::seconds(30));

        // Perform the SSL shutdown
        stream_.async_shutdown(
            beast::bind_front_handler(
                &ssl_http_session::on_shutdown,
                shared_from_this()));
    }

    void ssl_http_session::on_handshake(
        beast::error_code ec,
        std::size_t bytes_used)
    {
        if(ec)
            return fail(ec, "on_handshake");

        // Consume the portion of the buffer used by the handshake
        buffer_.consume(bytes_used);

        state_->join(this); // join the shared state here now that we support tcp comms
        do_read();
    }

    void ssl_http_session::on_shutdown(beast::error_code ec)
    {
        if(ec)
            return fail(ec, "on_shutdown");

        // At this point the connection is closed gracefully
    }


//------------------------------------------------------------------------------


    detect_session::detect_session(
        tcp::socket&& socket,
        ssl::context& ctx,
        std::shared_ptr<shared_state> const& state,
        // std::shared_ptr<std::string const> const& doc_root
        Security security
        )
        : stream_(std::move(socket))
        , ctx_(ctx)
        // , doc_root_(doc_root)
        , state_(state)
        , security_(security)
    {
    }

    // Launch the detector
    void detect_session::run()
    {
        // We need to be executing within a strand to perform async operations
        // on the I/O objects in this session. Although not strictly necessary
        // for single-threaded contexts, this example code is written to be
        // thread-safe by default.
        net::dispatch(
            stream_.get_executor(),
            beast::bind_front_handler(
                &detect_session::on_run,
                this->shared_from_this()));
    }

    void detect_session::on_run()
    {
        // Set the timeout.
        // stream_.expires_after(std::chrono::seconds(30));

        // if the user wants both http/ws and https/wss on this port, you must detect what the client wants
        if (Security::BOTH == security_)
        {
            beast::async_detect_ssl(
                stream_,
                buffer_,
                beast::bind_front_handler(
                    &detect_session::on_detect,
                    this->shared_from_this()));
        }
        else
        {
            // else pretend the client specified what they want
            on_detect(beast::error_code(), Security::SECURE == security_);
        }
    }

    void detect_session::on_detect(beast::error_code ec, bool result)
    {
        if(ec)
            return fail(ec, "on_detect");

        if(result)
        {
            // Launch SSL session
            std::make_shared<ssl_http_session>(
                std::move(stream_),
                ctx_,
                std::move(buffer_),
                state_)->run();
            return;
        }

        // Launch plain session
        std::make_shared<plain_http_session>(
            std::move(stream_),
            std::move(buffer_),
            state_)->run();
    }


} // namespace
