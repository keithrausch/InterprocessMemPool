
#ifndef BEASTWEBSERVERFLEXIBLE_CLIENT_HPP
#define BEASTWEBSERVERFLEXIBLE_CLIENT_HPP

#include <boost/asio/ssl.hpp>
#include <boost/beast.hpp>
#include <boost/beast/core.hpp>


namespace BeastNetworking
{

template <class Derived>
class TCPClientBase
{
public:

  struct Callbacks
  {
    typedef boost::asio::ip::tcp::endpoint endpointT;
    typedef std::function<void(const endpointT &endpoint, const void *msgPtr, size_t msgSize)> CallbackReadT;
    typedef std::function<void(const endpointT &endpoint)> CallbackSocketAcceptT;
    typedef std::function<void(const endpointT &endpoint)> CallbackSocketCloseT;
    typedef std::function<void(const endpointT &endpoint, const boost::system::error_code &ec)> CallbackErrorT;

    CallbackReadT callbackRead;
    CallbackSocketAcceptT callbackAccept;
    CallbackSocketCloseT callbackClose;
    CallbackErrorT callbackError;
  };

  Callbacks callbacks;

  Derived& derived()
  {
      return static_cast<Derived&>(*this);
  }

  TCPClientBase(boost::asio::io_context& io_context_in, const std::string server_address_in, uint16_t port_in, size_t max_message_length, const Callbacks &callbacks_in)
    : callbacks(callbacks_in),
      resolver_(io_context_in),
      server_address(server_address_in),
      port(port_in),
      connected_(false),
      io_context_(io_context_in),
      // socket_(boost::asio::make_strand(io_context_in), context),
      read_buffer(max_message_length)
  {
  }

  ~TCPClientBase()
  {
    handle_close();
  }

  void write(const std::string& msg)
  {
    // rely on stranding to add this message to the write queue so we can remain lock-free
    boost::asio::post(derived().socket().get_executor(), boost::beast::bind_front_handler(&TCPClientBase::schedule_write, derived().shared_from_this(), msg));
  }

  void close()
  {
    boost::asio::post(derived().socket().get_executor(), boost::beast::bind_front_handler( &TCPClientBase::handle_close, derived().shared_from_this()));
  }

  bool connected()
  {
    return connected_;
  }

  void run()
  {
    // Look up the domain name
    resolver_.async_resolve( server_address, std::to_string(port),
        boost::beast::bind_front_handler( &TCPClientBase::on_resolve, derived().shared_from_this()));
  }

  void on_finish_connect_or_handshake(boost::beast::error_code ec)
  {

    if (! ec)
    {
      connected_ = true;

      if (callbacks.callbackAccept)
      {
        callbacks.callbackAccept(endpoint);
      }

      do_read();
    }
    else
    {
      handle_error(ec);
      handle_close();
    }
  }



  // void wait()
  // {
  //   boost::system::error_code ec;
  //   socket_.wait(boost::asio::ip::tcp::socket::wait_error, ec);
  //   socket_.wait(boost::asio::ip::tcp::socket::wait_write, ec);
  //   socket_.wait(boost::asio::ip::tcp::socket::wait_read, ec);
  //   std::cout << ec << "\n";
  // }

protected:

  void on_resolve(boost::beast::error_code ec, boost::asio::ip::tcp::resolver::results_type results)
  {
    if (ec)
      return handle_error(ec);

    endpoint = *results.begin();
    // Make the connection on the IP address we get from a lookup

    derived().lowest_layer().close(); // reuse error var

    derived().lowest_layer().async_connect(
        endpoint,
        boost::beast::bind_front_handler( &Derived::on_connect, derived().shared_from_this()));
  }



  void do_read()
  {
    derived().socket().async_read_some(
    // boost::asio::async_read_some(socket_,
        boost::asio::buffer(read_buffer.data(), read_buffer.size()), 
        boost::beast::bind_front_handler(&TCPClientBase::on_read, derived().shared_from_this()));
  }

  void schedule_write(const std::string &msg)
  {
    bool write_in_progress = !write_msgs_.empty();
    write_msgs_.push_back(msg);
    if (!write_in_progress)
    {
      do_write();
    }
  }

  void do_write()
  {
    boost::asio::async_write(derived().socket(),
        boost::asio::buffer(write_msgs_.front().data(), write_msgs_.front().length()),
        boost::beast::bind_front_handler(&TCPClientBase::on_write, derived().shared_from_this()));
  }

  void on_write(boost::system::error_code ec, std::size_t /*length*/)
  {
    if (!ec)
    {
      write_msgs_.pop_front();
      if (!write_msgs_.empty())
      {
        do_write();
      }
    }
    else
    {
      write_msgs_.clear(); // probably a mistake..... NOTE lookie here when you have errors in the future
      handle_error(ec);
      handle_close();
    }
  }

  void on_read(boost::system::error_code ec, std::size_t length)
  {
    if (!ec)
    {
      if (callbacks.callbackRead)
      {
        callbacks.callbackRead(endpoint, read_buffer.data(), length);
      }
      do_read();
    }
    else
    {
      handle_error(ec);
      handle_close();
    }
  }

  void handle_error(const boost::system::error_code &ec)
  {

    if (callbacks.callbackError)
    {
      callbacks.callbackError(endpoint, ec);
    }
  }

  void handle_close()
  {
    if (connected_)
    {
      boost::system::error_code ec;
      derived().lowest_layer().close(ec);

      if (callbacks.callbackClose)
      {
        callbacks.callbackClose(endpoint);
      }

      connected_ = false;
    }
  }

protected:
  boost::asio::ip::tcp::resolver resolver_;
  std::string server_address;
  uint16_t port;
  boost::asio::ip::tcp::endpoint endpoint;
  std::atomic<bool> connected_;
  boost::asio::io_context& io_context_;
  // boost::asio::ip::tcp::socket socket_;
  // boost::asio::ssl::stream<boost::asio::ip::tcp::socket> socket_;
  std::vector<char> read_buffer;
  std::deque<std::string> write_msgs_;
};



// Handles an plain TPL connection
class PlainTCPClient
    : public TCPClientBase<PlainTCPClient>
    , public std::enable_shared_from_this<PlainTCPClient>
{
  boost::asio::ip::tcp::socket socket_;
  // boost::beast::tcp_stream socket_;

public:

    explicit
    PlainTCPClient(boost::asio::io_context& io_context_in, const std::string server_address_in, uint16_t port_in, size_t max_message_length, const Callbacks &callbacks_in) 
    : TCPClientBase<PlainTCPClient>(io_context_in, server_address_in, port_in, max_message_length, callbacks_in),
    socket_(boost::asio::make_strand(io_context_in))
    {
    }

    // Called by the base class
    boost::asio::ip::tcp::socket& socket()
    {
      return socket_;
    }

    boost::asio::ip::tcp::socket& lowest_layer()
    {
      return socket_;
    }


  void on_connect(boost::beast::error_code ec)
  {
    on_finish_connect_or_handshake(ec);
  }

};




// Handles an SSL TPL connection
class SSLTCPClient
    : public TCPClientBase<SSLTCPClient>
    , public std::enable_shared_from_this<SSLTCPClient>
{
  boost::asio::ssl::stream<boost::asio::ip::tcp::socket> socket_;

public:

    explicit
    SSLTCPClient(boost::asio::io_context& io_context_in, boost::asio::ssl::context& ssl_context, const std::string server_address_in, uint16_t port_in, size_t max_message_length, const Callbacks &callbacks_in) 
    : TCPClientBase<SSLTCPClient>(io_context_in, server_address_in, port_in, max_message_length, callbacks_in), 
    socket_(boost::asio::make_strand(io_context_in), ssl_context)
    {
    }

    bool set_tlsext_host_name()
    {
      // and this https://stackoverflow.com/questions/59224873/boost-beast-handshake-sslv3-alert-handshake-failure-error
      // see this https://github.com/boostorg/beast/blob/master/example/http/client/sync-ssl/http_client_sync_ssl.cpp
      bool had_error = (! SSL_set_tlsext_host_name(socket_.native_handle(), server_address.c_str()));
      
      // beast::error_code ec{static_cast<int>(::ERR_get_error()), net::error::get_ssl_category()};

      return had_error;
    }

    // Called by the base class
    boost::asio::ssl::stream<boost::asio::ip::tcp::socket>& socket()
    {
      return socket_;
    }

    decltype(socket_.lowest_layer()) lowest_layer()
    {
      return socket_.lowest_layer();
    }


  void on_connect(boost::beast::error_code ec)
  {
    if (! ec)
    {
      socket_.async_handshake(boost::asio::ssl::stream_base::client, 
      boost::beast::bind_front_handler( &TCPClientBase::on_finish_connect_or_handshake, shared_from_this()));
    }
    else
    {
      handle_error(ec);
      handle_close();
    }
  }

};





template class TCPClientBase<PlainTCPClient>;
template class TCPClientBase<SSLTCPClient>;

}

#endif