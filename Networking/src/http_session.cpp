

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
        remote_endpoint(remote_endpoint_in)
        , queue_(*this)
        , buffer_(std::move(buffer))
    {
    }

    template<class Derived>
    void http_session<Derived>::do_read()
    {
        // Construct a new parser for each message
        parser_.emplace();

        // Apply a reasonable limit to the allowed size
        // of the body in bytes to prevent abuse.
        parser_->body_limit(10000);

        // Set the timeout.
        beast::get_lowest_layer(
            derived().stream()).expires_after(std::chrono::seconds(30));

        // Read a request using the parser-oriented interface
        http::async_read(
            derived().stream(),
            buffer_,
            *parser_,
            beast::bind_front_handler(
                &http_session::on_read,
                derived().shared_from_this()));
    }

    template<class Derived>
    void http_session<Derived>::on_read(beast::error_code ec, std::size_t bytes_transferred)
    {
        boost::ignore_unused(bytes_transferred);

        // This means they closed the connection
        if(ec == http::error::end_of_stream)
            return derived().do_eof();

        if(ec)
            return fail(ec, "read");

        // See if it is a WebSocket Upgrade
        if(websocket::is_upgrade(parser_->get()))
        {
            // Disable the timeout.
            // The websocket::stream uses its own timeout settings.
            beast::get_lowest_layer(derived().stream()).expires_never();

            // Create a websocket session, transferring ownership
            // of both the socket and the HTTP request.
            return make_websocket_session_server(
                derived().release_stream(), state_, remote_endpoint,
                parser_->release());
        }

        // Send the response
        // handle_request(*doc_root_, parser_->release(), queue_);
        handle_request(state_->doc_root(), parser_->release(), queue_);

        // If we aren't at the queue limit, try to pipeline another request
        if(! queue_.is_full())
            do_read();
    }

    template<class Derived>
    void http_session<Derived>::on_write(bool close, beast::error_code ec, std::size_t bytes_transferred)
    {
        boost::ignore_unused(bytes_transferred);

        if(ec)
            return fail(ec, "write");

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

    // Start the session
    void plain_http_session::run()
    {
        this->do_read();
    }

    // Called by the base class
    beast::tcp_stream& plain_http_session::stream()
    {
        return stream_;
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

    // Start the session
    void ssl_http_session::run()
    {
        // Set the timeout.
        beast::get_lowest_layer(stream_).expires_after(std::chrono::seconds(30));

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

    // Called by the base class
    beast::ssl_stream<beast::tcp_stream> ssl_http_session::release_stream()
    {
        return std::move(stream_);
    }

    // Called by the base class
    void ssl_http_session::do_eof()
    {
        // Set the timeout.
        beast::get_lowest_layer(stream_).expires_after(std::chrono::seconds(30));

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
            return fail(ec, "handshake");

        // Consume the portion of the buffer used by the handshake
        buffer_.consume(bytes_used);

        do_read();
    }

    void ssl_http_session::on_shutdown(beast::error_code ec)
    {
        if(ec)
            return fail(ec, "shutdown");

        // At this point the connection is closed gracefully
    }


//------------------------------------------------------------------------------


    detect_session::detect_session(
        tcp::socket&& socket,
        ssl::context& ctx,
        std::shared_ptr<shared_state> const& state
        // std::shared_ptr<std::string const> const& doc_root
        )
        : stream_(std::move(socket))
        , ctx_(ctx)
        // , doc_root_(doc_root)
        , state_(state)
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
        stream_.expires_after(std::chrono::seconds(30));

        beast::async_detect_ssl(
            stream_,
            buffer_,
            beast::bind_front_handler(
                &detect_session::on_detect,
                this->shared_from_this()));
    }

    void detect_session::on_detect(beast::error_code ec, bool result)
    {
        if(ec)
            return fail(ec, "detect");

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
