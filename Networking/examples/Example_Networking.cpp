// Keith Rausch
//
// This example shows how to use the networking utilities to send data in loopback.
// two processes are created (parent and child. One sends out a heartbeat interprocess-compatible, allocator-based mempool
// to get shared_ptr's without newing/deleting the memory for the shared_ptr's control block.
// An interprocess-compatible router / pipe mechanism is also demoed here.
//
// Two processes (parent and child) are created. One pulls elements from a mempool and sends them out
// over the network. The second (child) process receives the elements and adds them to a pipe to be send out to
// anyone else on the same receiving computer. The sender (parent) advertises its topic at 1Hz. The receiver (child)
// listens for those UDP broadcasts and (when it hears a matching topic), connects to the sender via a WebSocket.
//
// The parent and child processes may start / stop at any time. If the recevier hears the 1Hz broadcast, itll automatically
// reconnect to the sender. In this particular example, the sender is told to wait until it has at least one recevier
// connected to it.
//
// A real implementation of this multi-device, pub-sub architecture would be to put one receiver on each computer. That
// receiver would be responsible for receiving all incoming messages for all topics and adding them to the routers/pipes
// on that computer. Computers can have any number of senders. i guess its a mesh network of many-to-one's.
//
// if a receiving computer determines it has the same unique identifier for a mempool, then it wont bother to route messages
// over the network.... those messages should already be put in the router on that computer by the original sender. of course,
// this is optional
//
// Its important to understand that this networking code is completely independant of the IPC code. The callbacks for
// broadcast-recevier, socket-receive, socket-accept, socket-close, socket-error, etc. are all user provided.
// You should be able to use this code to make any two comptures on a network talk super easily. it was meant to be super
// flexible and reusable. The downside is that you have to handle your own serialization logic. The library should let
// you know the endianess of the sender, but you still have to deal with it.
//

#include <iostream>
#include <cstdlib> //std::system
#include <thread>
#include <chrono>

#include "MemPoolIPC.h"
#include "RouterIPC.h"
#include "MultiClientSender.h"
#include "MultiClientReceiver.h"

using namespace IPC; // dont crucify me

struct MyClassIPC
{
  short x;
  double i, j, k, l, m, n, o, p, q, r, s, t, u, v, w;
  MyClassIPC() : x(0) { std::cout << "MyClassIPC()" << std::endl; }
  MyClassIPC(int x_in) : x(x_in) { std::cout << "MyClassIPC(" << x << ")\n"; }
  MyClassIPC(const void *msgPtr, size_t msgSize)
  {
    if (msgSize != sizeof(MyClassIPC))
      return;
    memcpy(this, msgPtr, msgSize);
    std::printf("MyClassIPC(%p, %zu), basically MyClassIPC(%d)\n", msgPtr, msgSize, (int)x);
  }
  ~MyClassIPC() { std::cout << "~MyClassIPC(" << x << ")\n"; }
};

//
// parent process. generates data
//
void ProducerProcess()
{
  namespace ipc = boost::interprocess;

  // the type to route
  typedef MyClassIPC T;
  typedef MemPoolIPC<T> PoolT;

  std::cout << "starting producer process...\n";

  // ask OS for memory region and map it into address space
  size_t poolSize = 50;
  auto segmentPtr = utils_ipc::open_or_create_mapping("MySharedMemory", 4096 * 16);
  auto poolPtr = utils_ipc::find_or_create_shared_object<PoolT>(segmentPtr, "pool", segmentPtr->get_segment_manager(), poolSize);
  if (!segmentPtr || !poolPtr)
  {
    std::cout << "CRITICAL ERROR - SEGMENT AND/OR POOL COULD NOT BE CONSTRUCTED\n";
    return;
  }

  // create io_context (work_guarded)
  size_t nThreads = 1;
  utils_asio::GuardedContext guarded_context(nThreads);

  MultiClientSenderArgs args;
  MultiClientSender sender(guarded_context.GetIOContext(), "myTopic", args, poolPtr->UniqueInstanceID());
  sender.StartHeartbeat();

  std::this_thread::sleep_for(std::chrono::duration<double>(1.5));

  for (size_t i = 0; i < 50; ++i)
  {
    // make up some data
    auto ptr = poolPtr->make_pooled(i);
    if (!ptr)
    {
      std::cout << "pool depleted!\n";
      std::this_thread::sleep_for(std::chrono::duration<double>(0.1));
      continue;
    }

    // just one of many ways to send data. 
    // the std::function method is the most generic, 
    // but this is a convenience function for most simple cases
    sender.SendAsync((void*)(&*ptr), sizeof(*ptr), [ptr](boost::beast::error_code, size_t){}); 
  }

  std::this_thread::sleep_for(std::chrono::duration<double>(1.0)); // give the sender some time to finish up

  // shutdown while objects associated with async functions are still in scope. not necessary here, wouldnt hurt.
  // guarded_context.Shutdown();

  std::cout << "ending producer process...\n";
}

//
// child process. consumes data
//
void ConsumerProcess()
{
  namespace ipc = boost::interprocess;

  // the type to route
  typedef MyClassIPC T;
  typedef MemPoolIPC<T> PoolT;
  typedef utils_ipc::RouterIPC<T, PoolT::sPtrT> RouterT;

  std::cout << "starting consumer process...\n";
  size_t poolSize = 50;
  auto segmentPtr = utils_ipc::open_or_create_mapping("MySharedMemory", 4096 * 16);
  auto poolPtr = utils_ipc::find_or_create_shared_object<PoolT>(segmentPtr, "pool", segmentPtr->get_segment_manager(), poolSize);
  auto routerPtr = utils_ipc::find_or_create_shared_object<RouterT>(segmentPtr, "Router", segmentPtr->get_segment_manager());
  if (!segmentPtr)
  {
    std::cout << "CRITICAL ERROR - SEGMENT COULD NOT BE CONSTRUCTED\n";
    return;
  }

  //
  // setup all the callbacks for each topic
  //
  MultiClientReceiver::TopicCallbacksT receivableTopics;

  auto callbackRead = [poolPtr, routerPtr](const tcp::endpoint &endpoint, const void *msgPtr, size_t msgSize) {
    // std::string msg(static_cast<const char*>(msgPtr), msgSize);

    auto elementPtr = poolPtr->make_pooled(msgPtr, msgSize);
    size_t nPipesHadError = routerPtr->Send(elementPtr, RouterT::ENQUEUE_MODE::SOFT_FAIL_IF_FULL);
    if (nPipesHadError > 0)
      std::printf("MultiClientReceiver::TopicCallbacksT::CallbackRead() - Could not route element on topic \"%s\". Router had %zu pipes with errors and set to SOFT_FAIL_IF_FULL", "myTopic", nPipesHadError);

    std::stringstream ss;
    ss << endpoint;
    std::string endpointString = ss.str();
    std::printf("CONSUMER-READ - endpoint: %s\n", endpointString.c_str());
  };

  receivableTopics["myTopic"].callbackRead = callbackRead;

  // if you had more topics, you would add them here
  // receivableTopics["someOtherTopic"] = ...

  // create io_context (work_guarded)
  size_t nThreads = 2;
  utils_asio::GuardedContext guarded_context(nThreads - 1); // use the last thread to block

  MultiClientReceiverArgs args;
  args.permitLoopback = true;
  args.verbose = true;
  MultiClientReceiver receiver(guarded_context.GetIOContext(), receivableTopics, args, poolPtr->UniqueInstanceID());
  receiver.ListenForTopics();

  guarded_context.GetIOContext().run(); // block until the receiver timeout out
  

  std::cout << "ending consumer proces...\n";
}

int main(int argc, char *argv[])
{

  if (argc == 1)
  { //Producer process

    utils_ipc::RemoveSharedMemoryNuclear("MySharedMemory");

    // start producer, but do it from another thread so we dont block
    std::string procName(argv[0]);
    procName += " child ";
    std::function<void()> runOtherProcess = [procName] {
      if (0 != std::system(procName.c_str()))
        std::cerr << "OTHER PROCESS RETURNED ERROR\n";
    };
    std::thread otherProcess(runOtherProcess);

    // immediately start the producer process
    try
    {
      // try-catch guarantees stack unwinding, so the interprocess objects will be handleded properly, even with thrown exceptions
      ProducerProcess(); // now actually start the parent
    }
    catch (const std::exception &e)
    {
      std::cerr << "PRODUCER ENCOUNTERED AN ERROR: " << e.what() << '\n';
    }

    otherProcess.join();
  }
  else
  {
    try
    {
      // try-catch guarantees stack unwinding, so the interprocess objects will be handleded properly, even with thrown exceptions
      ConsumerProcess();
    }
    catch (const std::exception &e)
    {
      std::cerr << "CONSUMER ENCOUNTERED AN ERROR: " << e.what() << '\n';
    }
  }

  return 0;
}