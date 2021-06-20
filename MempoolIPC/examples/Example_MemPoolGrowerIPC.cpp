// Keith Rausch
//
// This example shows how to use an interprocess-compatible utility to automatically 'resize'
// existing mempools when they are out of available elements.
//
// To prove that this is interprocess capable, the parent process creates the mempool, and a
// child process pulls elements from it - forcing it to resize repeatedly

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
void ChildProcess()
{

  namespace ipc = boost::interprocess;

  // the type to route
  typedef MyClassIPC T;
  typedef MemPoolGrower<MemPoolIPC<T>> PoolT;

  std::cout << "starting child process...\n";

  // ask OS for memory region and map it into address space
  size_t poolSize = 1;
  auto segmentPtr = utils_ipc::open_or_create_mapping("MySharedMemory", 4096 * 16);
  auto poolPtr = utils_ipc::find_or_create_shared_object<PoolT>(segmentPtr, "pool", segmentPtr->get_segment_manager(), poolSize, 3);

  if (!segmentPtr || !poolPtr)
  {
    std::cout << "CRITICAL ERROR - SEGMENT OR POOL COULD NOT BE CONSTRUCTED\n";
    return;
  }

  std::vector<PoolT::sPtrT> buffer;

  for (size_t i = 0; i < poolSize * 10; ++i)
  {
    // make up some data
    auto ptr = poolPtr->make_pooled(i);

    buffer.push_back(ptr);
  }

  size_t nOutstanding;
  size_t nAvailable;
  poolPtr->pool.Statistics(nOutstanding, nAvailable);
  std::printf("\nshared_ptr pool has %zu outstanding and %zu available. the pool was reconstructed %zu times\n\n", nOutstanding, nAvailable, poolPtr->nReconstructions);

  std::cout << "ending child process...\n";
}

int main(int argc, char *argv[])
{
  if (argc == 1)
  { //Parent process

    utils_ipc::RemoveSharedMemoryNuclear("MySharedMemory");

    // create all the objects in one process

    // ask OS for memory region and map it into address space

    typedef MyClassIPC T;
    typedef MemPoolGrower<MemPoolIPC<T>> PoolT;
    size_t poolSize = 1;
    auto segmentPtr = utils_ipc::open_or_create_mapping("MySharedMemory", 4096 * 16);
    auto poolPtr = utils_ipc::find_or_create_shared_object<PoolT>(segmentPtr, "pool", segmentPtr->get_segment_manager(), poolSize, 3);

    if (!segmentPtr || !poolPtr)
    {
      std::cout << "CRITICAL ERROR - SEGMENT OR POOL COULD NOT BE CONSTRUCTED\n";
      return 1;
    }

    // start a new process - but do it from another thread so we dont block
    std::string procName(argv[0]);
    procName += " child ";
    std::function<void()> runOtherProcess = [procName] {
      if (0 != std::system(procName.c_str()))
        std::cerr << "OTHER PROCESS RETURNED ERROR\n";
    };
    std::thread otherProcess(runOtherProcess);

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