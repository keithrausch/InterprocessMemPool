// Keith Rausch
//
// This example shows how to use an interprocess-compatible, allocator-based mempool to
// create an fetch arrays of elements.  You can use this to create mempools of raw images,
// for example.
//

#include <iostream>
#include <cstdlib> //std::system
#include <thread>
#include "UtilsIPC.h"

#include "MemPoolIPC.h"

using namespace IPC; // dont crucify me

struct MyClassIPC
{
  short x;
  double i, j, k, l, m, n, o, p, q, r, s, t, u, v, w;
  MyClassIPC() : x(0) { std::cout << "MyClassIPC()" << std::endl; }
  MyClassIPC(int x_in) : x(x_in) { std::cout << "MyClassIPC(" << x << ")\n"; }
  ~MyClassIPC() { std::cout << "~MyClassIPC(" << x << ")\n"; }
};

void Process()
{
  namespace ipc = boost::interprocess;

  // the type to route
  typedef MyClassIPC T;
  typedef MemPoolIPC<T> PoolArrayT;

  std::cout << "starting process...\n";

  size_t poolSize = 10;
  size_t arraySize = 2;
  auto segmentPtr = utils_ipc::open_or_create_mapping("MySharedMemory", 4096 * 16);
  auto poolPtr = utils_ipc::find_or_create_shared_object<PoolArrayT>(segmentPtr, "poolArray", segmentPtr->get_segment_manager(), poolSize, arraySize);
  if (!segmentPtr || !poolPtr)
  {
    std::cout << "CRITICAL ERROR - SEGMENT AND/OR POOLARRAY COULD NOT BE CONSTRUCTED\n";
    return;
  }

  auto elementPtr = poolPtr->make_pooled(123); // smart pointer to an image. for example
  auto &element0 = *elementPtr;                // first pixel in the image
  auto &element1 = elementPtr.get()[1];        // second pixel in the image
  (void)element0;                              // suppress [-Werror=unused-parameter]
  (void)element1;                              // suppress [-Werror=unused-parameter]
  elementPtr.get()[1].x = 456;

  std::cout << "ending process...\n";
}

int main(int, char *[])
{
  utils_ipc::RemoveSharedMemoryNuclear("MySharedMemory");

  try
  {
    // try-catch guarantees stack unwinding, so the interprocess objects will be handleded properly, even with thrown exceptions
    Process();
  }
  catch (const std::exception &e)
  {
    std::cerr << "PROCESS ENCOUNTERED AN ERROR: " << e.what() << '\n';
  }

  return 0;
}