// Keith Rausch
//
// This example shows how to use an allocator-based mempool to get shared_ptr's without
// repeatedly newing/deleting the memory for the shared_ptr's control block.
// An array version is also available and demoed here.
//

#include "MemPoolHeap.h"

#include <vector>
#include <iostream>
#include <memory> // mutex, etc

using namespace IPC; // dont crucify me

struct MyClass
{
  // just some random variables
  short x;
  char someChars[1];

  MyClass() : x(0) { std::cout << "MyClass()" << std::endl; }
  MyClass(int x_in) : x(x_in) { std::cout << "MyClass(" << x << ")\n"; }
  ~MyClass() { std::cout << "~MyClass(" << x << ")\n"; }
};

void test_static_pool()
{
  //
  // backend allocators are optional, will default to std::allocator.
  // PassthroughAllocator is an option that prints its allocations/ deallocations for you to see
  //

  size_t poolSize = 5;
  size_t pixelsPerImage = 3;
  SharedPointerAllocator<MyClass> myAllocator(poolSize);
  SharedPointerArrayAllocator<MyClass> myArrayAllocator(poolSize, pixelsPerImage);

  std::cout << " ---------- ALL MEMORY SHOULD BE ALLOCATED BY THIS POINT ---------- \n";

  std::cout << " ---------- make some shared pointers to MyClass objects ---------- \n";

  auto pA1 = myAllocator.allocate_shared(1);
  auto pA2 = pA1;
  pA1 = nullptr;

  auto pB1 = myAllocator.allocate_shared(2);
  auto pB2 = pB1;
  pB1 = nullptr;

  auto pC1 = myAllocator.allocate_shared(3);
  auto pC2 = pC1;
  pC1 = nullptr;

  // print some statistics
  size_t nOutstanding;
  size_t nAvailable;
  size_t historic_min_available;
  myAllocator.statistics(nOutstanding, nAvailable, historic_min_available);
  std::printf("\nshared_ptr pool has %zu outstanding, %zu available, %zu is historic-low in availability\n\n", nOutstanding, nAvailable, historic_min_available);

  std::cout << " ---------- make some shared pointers to T[] objects- THESE OBJECTS ARE NOT ALLOCATED, BUT THEY ARE CONSTRUCTED IF NON-ARITHMETIC ---------- \n";

  // allocate a bunch of different arrays
  // fill the arrays with data
  std::vector<std::shared_ptr<MyClass *>> pointerVec;
  for (size_t i = 0; i < poolSize; ++i)
  {
    pointerVec.push_back(myArrayAllocator.allocate_raw(i));
  }

  // print the data in those arrays
  size_t frameIndex = 0;
  for (auto &rawPtr : pointerVec)
  {
    for (size_t pixelIndex = 0; pixelIndex < pixelsPerImage; ++pixelIndex)
    {
      std::printf("frame[%zu], pixel[%zu] = ", frameIndex, pixelIndex);
      auto &obj = *(*rawPtr + pixelIndex);
      std::cout << obj.x << std::endl;
    }
    frameIndex++;
  }

  // print some
  myArrayAllocator.Statistics(nOutstanding, nAvailable, historic_min_available);
  std::printf("\narray has %zu outstanding, %zu available, %zu is historic low in availability\n\n", nOutstanding, nAvailable, historic_min_available);
}




int main(int, char *[])
{
  std::cout << " ---------- testing static pool ---------- \n";
  test_static_pool();
  std::cout << " ---------- end of main() ---------- \n";

  return 0;
}