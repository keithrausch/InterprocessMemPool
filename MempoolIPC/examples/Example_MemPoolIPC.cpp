// Keith Rausch
//
// This example shows how to use an interprocess-compatible, allocator-based mempool
// to get shared_ptr's without newing/deleting the memory for the shared_ptr's control block.
// An interprocess-compatible router / pipe mechanism is also demoed here.
//
// Two processes (parent and child) are created. The parent fetches elements from the mempool
// and adds them to the pipe. The child process receives the elements on the other side of the
// pipe. Either process may create the mempool/router/pipe/elements, it doesnt matter who is the first
// or last to use anything. Either process may start or stop at any time, and the elements, pool, pipes,
// and anything else are kept in scope until everyone (nomatter what process they are) is done using them.
// For example, the parent could send all elements to the child and then shutdown. The pipe and child will
// keep the elements and their parent mempool alive until either/both pipe and child nolonger have references
// to them.
//

#include <iostream>
#include <cstdlib> //std::system
#include <thread>
#include "UtilsIPC.h"
#include "RouterIPC.h"

#include "MemPoolIPC.h"

struct MyClassIPC
{
  short x;
  double i, j, k, l, m, n, o, p, q, r, s, t, u, v, w;
  MyClassIPC() : x(0) { std::cout << "MyClassIPC()" << std::endl; }
  MyClassIPC(int x_in) : x(x_in) { std::cout << "MyClassIPC(" << x << ")\n"; }
  ~MyClassIPC() { std::cout << "~MyClassIPC(" << x << ")\n"; }
};

//
// parent process. generates data
//
void ParentProcess()
{
  namespace ipc = boost::interprocess;

  // the type to route
  typedef MyClassIPC T;
  typedef MemPoolIPC<T> PoolT;
  typedef RouterIPC<T, PoolT::sPtrT> RouterT;

  std::cout << "starting parent process...\n";

  // ask OS for memory region and map it into address space
  size_t poolSize = 10;
  auto segmentPtr = utils_ipc::open_or_create_mapping("MySharedMemory", 4096 * 16);
  auto poolPtr = utils_ipc::find_or_create_shared_object<PoolT>(segmentPtr, "pool", segmentPtr->get_segment_manager(), poolSize);
  auto routerPtr = utils_ipc::find_or_create_shared_object<RouterT>(segmentPtr, "Router", segmentPtr->get_segment_manager()); //, pipeSize, *segmentPtr);

  if (!segmentPtr || !routerPtr || !poolPtr)
  {
    std::cout << "CRITICAL ERROR - SEGMENT, PIPE AND/OR POOL COULD NOT BE CONSTRUCTED\n";
    return;
  }

  // simple way of waiting for consumer to come up. completely up to the user
  for (size_t i = 0; i < 5; ++i)
  {
    if (routerPtr->NumReceivers() > 0)
      break;
    std::this_thread::sleep_for(std::chrono::duration<double>(1.0));
  }

  for (size_t i = 0; i < poolSize * 2; ++i)
  {
    // make up some data
    auto ptr = poolPtr->make_pooled(i);

    // route it
    routerPtr->Send(ptr, RouterT::ENQUEUE_MODE::WAIT_IF_FULL, 1);
  }

  size_t nOutstanding;
  size_t nAvailable;
  poolPtr->Statistics(nOutstanding, nAvailable);
  std::printf("\nshared_ptr pool has %zu outstanding and %zu available\n\n", nOutstanding, nAvailable);

  // send shutdown signal. completely optional. destroying this pipe also shuts it down
  // std::this_thread::sleep_for(std::chrono::duration<double>(0.1));
  routerPtr->Shutdown();

  std::cout << "ending parent process...\n";
}

//
// child process. consumes data
//
void ChildProcess()
{
  namespace ipc = boost::interprocess;

  // the type to route
  typedef MyClassIPC T;
  typedef MemPoolIPC<T> PoolT;
  typedef RouterIPC<T, PoolT::sPtrT> RouterT;

  std::cout << "starting child process...\n";

  size_t pipeSize = 5;
  auto segmentPtr = utils_ipc::open_or_create_mapping("MySharedMemory", 4096 * 16);
  auto routerPtr = utils_ipc::find_or_create_shared_object<RouterT>(segmentPtr, "Router", segmentPtr->get_segment_manager());
  if (!segmentPtr || !routerPtr)
  {
    std::cout << "CRITICAL ERROR - SEGMENT AND/OR ROUTER COULD NOT BE CONSTRUCTED\n";
    return;
  }

  auto pipePtr = routerPtr->RegisterAsReceiver(pipeSize, *segmentPtr);
  if (!pipePtr)
  {
    std::cout << "CRITICAL ERROR - PIPE COULD NOT BE CONSTRUCTED\n";
    return;
  }

  // receive data
  bool timedout = false;
  while (*pipePtr && !timedout)
  {
    RouterT::sPtrT ptr;
    timedout = pipePtr->Receive(ptr, 5); // timed_wait just for dev purposes
    std::cout << "got ptr... use_count = " << ptr.use_count() << std::endl;
  }

  std::cout << "ending child proces...\n";
}

int main(int argc, char *argv[])
{
  if (argc == 1)
  { //Parent process

    utils_ipc::RemoveSharedMemoryNuclear("MySharedMemory");

    // immediately start the child process
    // this way, both processes will be "starting at the same time" and
    // asking the system for the same resources
    //
    // start a new process - but do it from another thread so we dont block
    std::string procName(argv[0]);
    procName += " child ";
    std::function<void()> runOtherProcess = [procName] {
      if (0 != std::system(procName.c_str()))
        std::cerr << "OTHER PROCESS RETURNED ERROR\n";
    };
    std::thread otherProcess(runOtherProcess);

    try
    {
      // try-catch guarantees stack unwinding, so the interprocess objects will be handleded properly, even with thrown exceptions
      ParentProcess(); // now actually start the parent
    }
    catch (const std::exception &e)
    {
      std::cerr << "PARENT  ENCOUNTERED AN ERROR: " << e.what() << '\n';
    }

    otherProcess.join();
  }
  else
  {
    try
    {
      // try-catch guarantees stack unwinding, so the interprocess objects will be handleded properly, even with thrown exceptions
      ChildProcess();
    }
    catch (const std::exception &e)
    {
      std::cerr << "CHILD  ENCOUNTERED AN ERROR: " << e.what() << '\n';
    }
  }

  return 0;
}