// Keith Rausch

#ifndef BEASTWEBSERVERFLEXIBLE_UTILS_ASIO_HPP
#define BEASTWEBSERVERFLEXIBLE_UTILS_ASIO_HPP

#include <boost/enable_shared_from_this.hpp>
#include <boost/beast.hpp>
#include <boost/asio.hpp>
#include <iostream>
#include <string>
#include <boost/asio/steady_timer.hpp>
#include <boost/beast/core/bind_handler.hpp>

namespace utils_asio
{

  using boost::asio::ip::address;
  using boost::asio::ip::tcp;
  using boost::asio::ip::udp;

  //
  // MUST USE THIS CLASS INSIDE A std::shared_ptr via std::make_shared
  //
  class UDPReceiver : public std::enable_shared_from_this<UDPReceiver>
  {

    udp::socket socket;
    udp::endpoint remoteEndpoint;
    std::vector<char> recvBuffer;

    boost::asio::steady_timer timeout;
    double timeout_period_seconds; // only has millisecond precision
    std::atomic<bool> timedout;

  public:
    struct Callbacks
    {
      typedef std::function<void(const udp::endpoint &, void *msgPtr, size_t msgSize)> CallbackReadT;
      typedef std::function<void(const udp::endpoint &, boost::system::error_code)> CallbackErrorT;
      typedef std::function<void()> CallbackTimeoutT;

      CallbackReadT callbackRead;
      CallbackErrorT callbackError;
      CallbackTimeoutT callbackTimeout;
    };

    Callbacks callbacks;

    explicit UDPReceiver(boost::asio::io_context &io_context,
                         unsigned short portNumber,
                         size_t maxMessageLength,
                         double timeout_period_seconds_in,
                         const Callbacks &callbacks_in = Callbacks())
        : socket(io_context, udp::endpoint(udp::v4(), portNumber)),
          recvBuffer(maxMessageLength),
          timeout(io_context),
          timeout_period_seconds(timeout_period_seconds_in),
          callbacks(callbacks_in)
    {
      socket.set_option(boost::asio::ip::udp::socket::reuse_address(true));
      // DO NOT CALL start_recieve(); here. shared_from_this() needs a fully constructed
      // shared_ptr to work, and calling start_receive here enevitably calls shared_from_this()
      // before the constructor is finished
      // https://stackoverflow.com/questions/43261673/stdbad-weak-ptr-while-shared-from-this
    }

    // DO NOT CALL START IN CONSTRUCTOR. no shared pointer exits yet, enabled_shared_from_this will break
    // https://stackoverflow.com/questions/43261673/stdbad-weak-ptr-while-shared-from-this
    void run()
    {
      timedout = false;
      timeout.async_wait(boost::beast::bind_front_handler(&UDPReceiver::check_timeout, shared_from_this()));
      start_recieve();
    }

  private:
    void on_error(boost::system::error_code ec)
    {
      if (callbacks.callbackError && !timedout)
        callbacks.callbackError(remoteEndpoint, ec);

      // if (callbacks.callbackClose)
      //     callbacks.callbackClose(remoteEndpoint, ec);
    }

    void on_recieve(boost::system::error_code ec, std::size_t bytes_transferred)
    {
      if (ec)
      {
        on_error(ec);
        return;
      }

      if (callbacks.callbackRead)
        callbacks.callbackRead(remoteEndpoint, (void *)&*recvBuffer.begin(), bytes_transferred);

      start_recieve();
    }

    void start_recieve()
    {
      // Set a deadline for the read operation.
      timedout = false;
      if (timeout_period_seconds <= 0)
        timeout.expires_at(boost::asio::steady_timer::time_point::max());
      else
      {
        size_t timeout_period_ms = timeout_period_seconds * 1000 ;
        timeout.expires_after(std::chrono::milliseconds(timeout_period_ms)); // cancels the timer and resets it
      }

      socket.async_receive_from(boost::asio::buffer(recvBuffer),
                                remoteEndpoint,
                                boost::beast::bind_front_handler(&UDPReceiver::on_recieve, shared_from_this()));
    }

    //
    // This function is called when the timer expires, OR when it is reset/ cancelled (like with steady_timer::expires_after())
    //
    // This handler calls itself. Every time the timer is reset, this handler will get called with an
    // error (but we dont care about the error). that means that whenever this function is called, a
    // timer either just expired, or was reset and needs to be used to time a new function. It doesnt
    // actually matter which scenario called this handler to fire, we just need to know how long to sleep
    // the timer for, and if the callbackTimeout() member should be called
    //
    void check_timeout(boost::system::error_code ec)
    {
      if (ec && ec != boost::asio::error::operation_aborted)
        on_error(ec);

      // Check whether the deadline has passed. We compare the deadline against
      // the current time since a new asynchronous operation may have moved the
      // deadline before this actor had a chance to run.
      if (timeout.expiry() <= boost::asio::steady_timer::clock_type::now())
      {
        timedout = true;

        if (callbacks.callbackTimeout)
          callbacks.callbackTimeout();

        // The deadline has passed. The socket is closed so that any outstanding
        // asynchronous operations are cancelled.
        socket.close();

        // There is no longer an active deadline. The expiry is set to the
        // maximum time point so that the actor takes no action until a new
        // deadline is set.
        timeout.expires_at(boost::asio::steady_timer::time_point::max());
      }

      // Put the actor back to sleep.
      timeout.async_wait(boost::beast::bind_front_handler(&UDPReceiver::check_timeout, shared_from_this()));
    }
  };

  /*
  static void SendBroadcast(boost::asio::io_service &io_service, const std::string &msg, unsigned short portSender, const std::string &destination, unsigned short portReceiver)
  {
    namespace ba = boost::asio;
    namespace bs = boost::system;
    using ba::ip::udp;

    udp::socket sock(io_service, udp::endpoint(udp::v4(), portSender));
    sock.set_option(ba::ip::udp::socket::reuse_address(true));
    sock.set_option(ba::socket_base::broadcast(true));

    udp::endpoint sender_endpoint(ba::ip::address_v4::from_string(destination), portReceiver);

    auto handler = [](const boost::system::error_code &, std::size_t) {};
    sock.async_send_to(boost::asio::buffer(msg.c_str(), msg.length()), sender_endpoint, handler);
  }
  */

  //
  // MUST USE THIS CLASS INSIDE A std::shared_ptr via std::make_shared
  //
  class Heartbeat : public std::enable_shared_from_this<Heartbeat>
  {
    // namespace ba = boost::asio;
    // namespace bs = boost::system;
    // using boost::asio::ip::udp;

    boost::asio::io_context &io_context;
    udp::socket socket;
    udp::endpoint receiver_endpoint;
    boost::asio::steady_timer timer;
    float period_seconds;

    typedef std::function<std::string(void)> MsgCreatorT;
    MsgCreatorT msgCreator;
    std::string msg;
    bool running;

  public:
    Heartbeat(boost::asio::io_context &io_context_in, const MsgCreatorT &msgCreator_in, unsigned short portSender, const std::string &destination, unsigned short portReceiver, float period_seconds_in)
        : io_context(io_context_in),
          socket(io_context, udp::v4()),
          receiver_endpoint(boost::asio::ip::address_v4::from_string(destination), portReceiver),
          timer(io_context),
          period_seconds(period_seconds_in),
          msgCreator(msgCreator_in),
          msg(),
          running(false)
    {
      socket.set_option(udp::socket::reuse_address(true));
      socket.set_option(boost::asio::socket_base::broadcast(true));
      socket.bind(udp::endpoint(udp::v4(), portSender));
    }

    void run()
    {
      // calling run multiple times could stomp on the message before its sent
      if (running)
        return;

      running = true;
      start_send();
    }

  private:
    void on_wait(const boost::system::error_code &error)
    {
      if (error)
      {
        return;
      }

      start_send();
    }

    void on_send(const boost::system::error_code &/*error*/, std::size_t /*nBytes*/)
    {
      timer.expires_after(boost::asio::chrono::milliseconds((size_t)(period_seconds * 1000))); // cancels the timer and resets it
      timer.async_wait(boost::beast::bind_front_handler(&Heartbeat::on_wait, shared_from_this()));
    }

    void start_send()
    {
      if (msgCreator)
        msg = msgCreator();

      socket.async_send_to(boost::asio::buffer(msg.c_str(), msg.length()),
                           receiver_endpoint,
                           boost::beast::bind_front_handler(&Heartbeat::on_send, shared_from_this()));
      // when the socket is destroped, all pending ops are cancelled, os msg cant go out of scope before its used
    }
  };

  //
  // A utility class for getting an io_context and then protecting it with an executor_work_guard.
  // This is useful because calling io_context::run() without any pending handlers will shutdown the
  // executor inside io_context immediately. Furthermore, When the io_context runs out of handlers to
  // run, it will shut down the execuctor, and the calls to ::run() will stop blocking. This class keeps
  // that from happening (by using boost::asio::executor_work_guard)
  //
  // This class goes one step further and sets up handlers for SIGINT and SIGTERM and shutsdown the io_context
  // when they are received. This means that we can cntrl+c our program and it will shut down gracefully. sick.
  //
  class GuardedContext
  {
    boost::asio::io_context io_context;

    using work_guard_type = boost::asio::executor_work_guard<boost::asio::io_context::executor_type>;
    work_guard_type work_guard; // keeps io_context::run() from returning if all jobs are finished

    std::vector<std::thread> threads; // threads that call io_context::run()

    boost::asio::signal_set signals;
    std::mutex mutex;

    typedef std::function<void(const boost::system::error_code &, int)> SignalCallbackT;
    typedef std::function<void()> ShutdownCallbackT;
    SignalCallbackT signalCallback;
    ShutdownCallbackT shutdownCallback;

    void on_sigevent(const boost::system::error_code &error, int signal_number)
    {
      if (error)
        std::cout << "GuardedContext::on_sigevent() - CAUGHT SIGEVENT (" + std::to_string(signal_number) + "). BUT HAD ERROR: " << error << std::endl;

      if (error)
        return;

      std::cout << "GuardedContext::on_sigevent() - CAUGHT SIGEVENT (" + std::to_string(signal_number) + "). STOPPING IOCONTEXT.\n";

      io_context.stop();
      // do not call Shutdown() here.
      // that would mean that the thread handling this handler would be trying to join itself
      // and that would deadlock.
      // https://stackoverflow.com/questions/64039374/c-terminate-called-after-throwing-an-instance-of-stdsystem-error-what-r

      if (signalCallback)
      {
        signalCallback(error, signal_number);
        signalCallback = 0; // good practice, i think
      }
    }

  public:
    GuardedContext()
        : io_context(), work_guard(io_context.get_executor()), signals(io_context)
    {
    }

    GuardedContext(size_t nThreads)
        : io_context(), work_guard(io_context.get_executor()), signals(io_context)
    {
      Run(nThreads);
    }

    void SetSignalCallback(const SignalCallbackT & signalCallback_in)
    {
      signalCallback = signalCallback_in;
    }

    void SetShutdownCallback(const ShutdownCallbackT & shutdownCallback_in)
    {
      shutdownCallback = shutdownCallback_in;
    }

    ~GuardedContext()
    {
      Shutdown();
    }

    boost::asio::io_context &GetIOContext()
    {
      return io_context;
    }

    void Run(size_t nThreads)
    {
      // Do not run if already called.
      // This is a design choice since caling Run() multiple times wouldnt truly hurt much.
      // This function could be so much more complicated.
      if (threads.size() > 0)
      {
        std::cout << "GuardedContext::Run() - YOU MAY NOT CALL RUN MULTIPLE TIMES\n";
        return;
      }

      // Run the I/O service on the requested number of threads
      threads.reserve(nThreads);
      for (size_t i = 0; i < nThreads; ++i)
        threads.emplace_back([this] { io_context.run(); });

      // set handlers for sigint, sigterm, etccc

      // Construct a signal set registered for process termination.
      // Start an asynchronous wait for one of the signals to occur.
      signals.add(SIGINT);
      signals.add(SIGTERM);
      signals.async_wait(boost::beast::bind_front_handler(&GuardedContext::on_sigevent, this));
    }

    void Shutdown()
    {
      std::lock_guard<std::mutex> guard(mutex);

      if (shutdownCallback)
      {
        shutdownCallback();
        shutdownCallback = 0; // prevent it from being called twice
      }
      
      signals.cancel(); // cancel the callbacks. this frees up the scope of the lambdas. do this before stopping the service

      if (!io_context.stopped())
        io_context.stop(); // required because we have a executor_work_guard

      // Block until all the threads exit
      for (auto &thread : threads)
      {
        if (thread.joinable())
          thread.join();
      }
    }
  };

} // namespace utils_asio

#endif