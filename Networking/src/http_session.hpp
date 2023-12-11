
#ifndef BEASTWEBSERVERFLEXIBLE_HTTP_SESSION_HPP
#define BEASTWEBSERVERFLEXIBLE_HTTP_SESSION_HPP

#include "net.hpp"

#include <boost/beast/websocket.hpp>
#include "websocket_session.hpp"

namespace BeastNetworking
{



// Return a reasonable mime type based on the extension of a file.
static beast::string_view mime_type(beast::string_view path)
{
    using beast::iequals;
    auto const ext = [&path]
    {
        auto const pos = path.rfind(".");
        if(pos == beast::string_view::npos)
            return beast::string_view{};
        return path.substr(pos);
    }();
    if(iequals(ext, ".htm"))  return "text/html";
    if(iequals(ext, ".html")) return "text/html";
    if(iequals(ext, ".php"))  return "text/html";
    if(iequals(ext, ".css"))  return "text/css";
    if(iequals(ext, ".txt"))  return "text/plain";
    if(iequals(ext, ".js"))   return "application/javascript";
    if(iequals(ext, ".json")) return "application/json";
    if(iequals(ext, ".xml"))  return "application/xml";
    if(iequals(ext, ".swf"))  return "application/x-shockwave-flash";
    if(iequals(ext, ".flv"))  return "video/x-flv";
    if(iequals(ext, ".png"))  return "image/png";
    if(iequals(ext, ".jpe"))  return "image/jpeg";
    if(iequals(ext, ".jpeg")) return "image/jpeg";
    if(iequals(ext, ".jpg"))  return "image/jpeg";
    if(iequals(ext, ".gif"))  return "image/gif";
    if(iequals(ext, ".bmp"))  return "image/bmp";
    if(iequals(ext, ".ico"))  return "image/vnd.microsoft.icon";
    if(iequals(ext, ".tiff")) return "image/tiff";
    if(iequals(ext, ".tif"))  return "image/tiff";
    if(iequals(ext, ".svg"))  return "image/svg+xml";
    if(iequals(ext, ".svgz")) return "image/svg+xml";
    return "application/text";
}

// Append an HTTP rel-path to a local filesystem path.
// The returned path is normalized for the platform.
static std::string path_cat( beast::string_view base, beast::string_view path)
{
    if(base.empty())
        return std::string(path);
    std::string result(base);
#ifdef BOOST_MSVC
    char constexpr path_separator = '\\';
    if(result.back() == path_separator)
        result.resize(result.size() - 1);
    result.append(path.data(), path.size());
    for(auto& c : result)
        if(c == '/')
            c = path_separator;
#else
    char constexpr path_separator = '/';
    if(result.back() == path_separator)
        result.resize(result.size() - 1);
    result.append(path.data(), path.size());
#endif
    return result;
}

// This function produces an HTTP response for the given
// request. The type of the response object depends on the
// contents of the request, so the interface requires the
// caller to pass a generic lambda for receiving the response.
template< class Body, class Allocator, class Send, class OnPostCallback>
void handle_request( beast::string_view doc_root, http::request<Body, http::basic_fields<Allocator>>&& req, Send&& send, const OnPostCallback &on_post)
{
    // Returns a bad request response
    auto const bad_request =
    [&req](beast::string_view why)
    {
        http::response<http::string_body> res{http::status::bad_request, req.version()};
        res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
        res.set(http::field::content_type, "text/html");
        res.keep_alive(req.keep_alive());
        res.body() = std::string(why);
        res.prepare_payload();
        return res;
    };

    // Returns a not found response
    auto const not_found =
    [&req](beast::string_view target)
    {
        http::response<http::string_body> res{http::status::not_found, req.version()};
        res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
        res.set(http::field::content_type, "text/html");
        res.keep_alive(req.keep_alive());
        res.body() = "The resource '" + std::string(target) + "' was not found.";
        res.prepare_payload();
        return res;
    };

    // Returns a server error response
    auto const server_error =
    [&req](beast::string_view what)
    {
        http::response<http::string_body> res{http::status::internal_server_error, req.version()};
        res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
        res.set(http::field::content_type, "text/html");
        res.keep_alive(req.keep_alive());
        res.body() = "An error occurred: '" + std::string(what) + "'";
        res.prepare_payload();
        return res;
    };

    if (req.method() == http::verb::post && req.method() != http::verb::head)
    {
            on_post(req.body().data(), req.body().size());
    }

    // Make sure we can handle the method
    if( req.method() != http::verb::get &&
        req.method() != http::verb::head)
        {
        return send(bad_request("Unknown HTTP-method"));
        }

    // Request path must be absolute and not contain "..".
    if( req.target().empty() ||
        req.target()[0] != '/' ||
        req.target().find("..") != beast::string_view::npos)
        {
        return send(bad_request("Illegal request-target"));
        }

    // Build the path to the requested file
    std::string path = path_cat(doc_root, req.target());
    path = path.substr(0, path.find('?')); // scrip off question mark and data after it
    if(path.back() == '/')
        path.append("index.html");

    // Attempt to open the file
    beast::error_code ec;
    http::file_body::value_type body;
    body.open(path.c_str(), beast::file_mode::scan, ec);

    // Handle the case where the file doesn't exist
    if(ec == beast::errc::no_such_file_or_directory)
        return send(not_found(req.target()));

    // Handle an unknown error
    if(ec)
        return send(server_error(ec.message()));

    // Cache the size since we need it after the move
    auto const size = body.size();

    // Respond to HEAD request
    if(req.method() == http::verb::head)
    {
        http::response<http::empty_body> res{http::status::ok, req.version()};
        res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
        res.set(http::field::content_type, mime_type(path));
        res.content_length(size);
        res.keep_alive(req.keep_alive());
        return send(std::move(res));
    }

    // Respond to GET request
    http::response<http::file_body> res{
        std::piecewise_construct,
        std::make_tuple(std::move(body)),
        std::make_tuple(http::status::ok, req.version())};
    res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
    res.set(http::field::content_type, mime_type(path));
    res.content_length(size);
    res.keep_alive(req.keep_alive());
    return send(std::move(res));
}



//------------------------------------------------------------------------------

// Handles an HTTP server connection.
// This uses the Curiously Recurring Template Pattern so that
// the same code works with both SSL streams and regular sockets.
template<class Derived>
class http_session
{
    // Access the derived class, this is part of
    // the Curiously Recurring Template Pattern idiom.
    Derived& derived();

    // This queue is used for HTTP pipelining.
    class queue
    {
        enum
        {
            // Maximum number of responses we will queue
            limit = 8
        };

        // The type-erased, saved work item
        struct work
        {
            virtual ~work() = default;
            virtual void operator()() = 0;
        };

        http_session& self_;
        std::vector<std::unique_ptr<work>> items_;

    public:
        explicit
        queue(http_session& self)
            : self_(self)
        {
            static_assert(limit > 0, "queue limit must be positive");
            items_.reserve(limit);
        }

        // Returns `true` if we have reached the queue limit
        bool
        is_full() const
        {
            return items_.size() >= limit;
        }

        // Called when a message finishes sending
        // Returns `true` if the caller should initiate a read
        bool
        on_write()
        {
            BOOST_ASSERT(! items_.empty());
            auto const was_full = is_full();
            items_.erase(items_.begin());
            if(! items_.empty())
                (*items_.front())();
            return was_full;
        }

        // TODO REMOVE ME. THIS IS SO HACKY. NOT EVEN THREAD SAFE
        size_t size()
        {
            return items_.size();
        }

        // Called by the HTTP handler to send a response.
        template<bool isRequest, class Body, class Fields>
        void
        operator()(http::message<isRequest, Body, Fields>&& msg)
        {
            // This holds a work item
            struct work_impl : work
            {
                http_session& self_;
                http::message<isRequest, Body, Fields> msg_;

                work_impl(
                    http_session& self,
                    http::message<isRequest, Body, Fields>&& msg)
                    : self_(self)
                    , msg_(std::move(msg))
                {
                }

                void
                operator()()
                {
                    http::async_write(
                        self_.derived().stream(),
                        msg_,
                        beast::bind_front_handler(
                            &http_session::on_write,
                            self_.derived().shared_from_this(),
                            msg_.need_eof()));
                }
            };

            // Allocate and store the work
            items_.push_back(
                boost::make_unique<work_impl>(self_, std::move(msg)));

            // If there was no previous work, start this one
            if(items_.size() == 1)
                (*items_.front())();
        }
    };

    // std::shared_ptr<std::string const> doc_root_;
    queue queue_; // part of the example, for get/post/etc
    std::deque<shared_state::SpanAndHandlerT> send_queue_; // no mutex needed, only ever modified inside handlers, which are in a strand

    // The parser is stored in an optional container so we can
    // construct it from scratch it at the beginning of each new message.
    boost::optional<http::request_parser<http::string_body>> parser_;
    std::atomic<size_t> queue_size_;

protected:
    beast::flat_buffer buffer_;
    std::shared_ptr<shared_state> state_;
    std::vector<uint8_t> nonhttp_buffer_;
    size_t nonhttp_buffer_size_{0}; // so we dont have to keep resizing our vector

public:
    tcp::endpoint endpoint;

    // Construct the session
    http_session(
        beast::flat_buffer buffer,
        std::shared_ptr<shared_state> const& state,
        // std::shared_ptr<std::string const> const& doc_root,
        tcp::endpoint endpoint_in
        );

    void do_read();

    void on_nonhttp_read_header(const boost::system::error_code& error, size_t bytes_transferred);
    void on_nonhttp_read_body(const boost::system::error_code& error, size_t bytes_transferred);

    void on_read(beast::error_code ec, std::size_t bytes_transferred);

    void on_write(bool close, beast::error_code ec, std::size_t bytes_transferred);

    void sendAsync(const void* msgPtr, size_t msgSize, shared_state::CompletionHandlerT completionHandler, bool force_send, size_t max_queue_size );

    void on_send(const void* msgPtr, size_t msgSize, shared_state::CompletionHandlerT &&completionHandler, bool force_send=false, size_t max_queue_size=std::numeric_limits<size_t>::max());

    void on_write2( beast::error_code ec, std::size_t bytes_transferred);

    void on_error(beast::error_code ec);

    size_t queue_size() const
    {
        return queue_size_;
    }
};

//------------------------------------------------------------------------------

// Handles a plain HTTP connection
class plain_http_session
    : public http_session<plain_http_session>
    , public std::enable_shared_from_this<plain_http_session>
{
    beast::tcp_stream stream_;

public:
    // Create the session
    plain_http_session(
        beast::tcp_stream&& stream,
        beast::flat_buffer&& buffer,
        std::shared_ptr<shared_state> const& state
        //std::shared_ptr<std::string const> const& doc_root
        );

    ~plain_http_session();

    // Start the session
    void run();

    // Called by the base class
    beast::tcp_stream& stream();
    boost::asio::ip::tcp::socket& socket();

    // Called by the base class
    beast::tcp_stream release_stream();

    // Called by the base class
    void do_eof();
};

//------------------------------------------------------------------------------

// Handles an SSL HTTP connection
class ssl_http_session
    : public http_session<ssl_http_session>
    , public std::enable_shared_from_this<ssl_http_session>
{
    beast::ssl_stream<beast::tcp_stream> stream_;

public:
    // Create the http_session
    ssl_http_session(
        beast::tcp_stream&& stream,
        ssl::context& ctx,
        beast::flat_buffer&& buffer,
        std::shared_ptr<shared_state> const& state
        //std::shared_ptr<std::string const> const& doc_root
        );

    ~ssl_http_session();

    // Start the session
    void run();

    // Called by the base class
    beast::ssl_stream<beast::tcp_stream>& stream();
    boost::asio::ip::tcp::socket& socket();

    // Called by the base class
    beast::ssl_stream<beast::tcp_stream> release_stream();

    // Called by the base class
    void do_eof();

private:
    void on_handshake(
        beast::error_code ec,
        std::size_t bytes_used);

    void on_shutdown(beast::error_code ec);
};

//------------------------------------------------------------------------------

// Detects SSL handshakes
class detect_session : public std::enable_shared_from_this<detect_session>
{
    beast::tcp_stream stream_;
    ssl::context& ctx_;
    // std::shared_ptr<std::string const> doc_root_;
    std::shared_ptr<shared_state> state_;
    beast::flat_buffer buffer_;
    Security security_;

public:
    explicit
    detect_session(
        tcp::socket&& socket,
        ssl::context& ctx,
        std::shared_ptr<shared_state> const& state,
        Security security
        // std::shared_ptr<std::string const> const& doc_root
        );

    // Launch the detector
    void run();

    void on_run();

    void on_detect(beast::error_code ec, bool result);
};


} // namespace

#endif