#ifndef ROUTER_IPC_H
#define ROUTER_IPC_H

#define DEBUG_PRINT_ROUTER_IPC false

#include <boost/interprocess/managed_shared_memory.hpp>
#include <boost/interprocess/smart_ptr/shared_ptr.hpp>
#include <boost/interprocess/allocators/allocator.hpp>
#include <boost/interprocess/smart_ptr/deleter.hpp>
#include <boost/interprocess/sync/interprocess_mutex.hpp>
#include <boost/interprocess/sync/scoped_lock.hpp>
#include <boost/interprocess/sync/interprocess_semaphore.hpp>
#include <boost/date_time.hpp>
#include <atomic>

#include <boost/interprocess/containers/vector.hpp>
#include <scoped_allocator>
#include <boost/interprocess/smart_ptr/weak_ptr.hpp>

namespace utils_ipc
{

namespace ipc = boost::interprocess; // convenience

template <typename T, typename SharedPtr_T>
class PipeIPC
{
private:
  size_t indexPush;
  size_t indexPop;
  size_t capacity;
  std::atomic<size_t> size;

  //Semaphores to protect and synchronize access
  //ipc::interprocess_semaphore mutex; // TODO why did the examples use this innstead of mutex
  ipc::interprocess_mutex mutex;
  ipc::interprocess_semaphore nempty;
  ipc::interprocess_semaphore nstored;

  ipc::allocator<SharedPtr_T, ipc::managed_shared_memory::segment_manager> allocator;

  ipc::offset_ptr<SharedPtr_T> items;
  std::atomic_bool shutdown; // this should be fine to do in a shared space. atomics are cpu instruction based

public:
  typedef SharedPtr_T sPtrT;
  enum ENQUEUE_MODE
  {
    WAIT_IF_FULL,
    SOFT_FAIL_IF_FULL
  };

  PipeIPC(size_t capacity_in, ipc::managed_shared_memory &segment_in)
      : indexPush(0), indexPop(0), capacity(capacity_in), size(0),
        /*mutex(1),*/ nempty(capacity_in), nstored(0),
        allocator(segment_in.get_segment_manager()),
        items(allocator.allocate(capacity)), shutdown(false)
  {
    if (DEBUG_PRINT_ROUTER_IPC)
      std::cout << "PipeIPC()\n";

    if (!items)
      std::cout << "PipeIPC::PipeIPC() - error allocating\n";

    // we have allocated this memory. now we need to construct it.
    // placement-new a bunch of empty pointers in it. if we didnt
    // do this, bad things would happen since wed be assigning to
    // unconstructed memory
    for (size_t i = 0; i < capacity; ++i)
      new (&items[i]) SharedPtr_T();
  }

  ~PipeIPC()
  {
    // this function not mutex protected because there should be no other
    // instances out there if the destructor is being called... "should" being the key word here
    if (DEBUG_PRINT_ROUTER_IPC)
      std::cout << "~Pipe()\n";
    Shutdown(); // just in case, why not

    if (items)
    {
      for (size_t i = 0; i < capacity; ++i)
        items[i].reset(); // destroy
      allocator.deallocate(items, capacity);
      items = nullptr;
    }
  }

  // timeout of 0 will wait indefinitely
  // return hadError
  bool Send(const SharedPtr_T &ptr, const ENQUEUE_MODE &enqueueMode = ENQUEUE_MODE::SOFT_FAIL_IF_FULL, const size_t &timeout_seconds = 0)
  {
    if (shutdown)
    {
      std::cerr << "ROUTER - COULD NOT ROUTE THIS POINTER, ROUTER IS SHUT DOWN\n";
      return false;
    }

    bool success = true; // default
    if (ENQUEUE_MODE::SOFT_FAIL_IF_FULL == enqueueMode)
      success = nempty.try_wait();
    else
    {
      if (0 != timeout_seconds)
      {
        using namespace boost::posix_time;
        ptime abs_time = second_clock::universal_time() + seconds(timeout_seconds);
        success = nempty.timed_wait(abs_time);
      }
      else
      {
        nempty.wait();
      }
    }

    success &= !shutdown; // cant be succussful if we are shutting down

    if (success)
    {
      { // lock_guard scope
        ipc::scoped_lock<ipc::interprocess_mutex> guard(mutex);
        //mutex.wait();
        items[indexPush] = ptr;
        indexPush = (indexPush + 1) % capacity;
        ++size;
        //mutex.post();
      }
      nstored.post();
    }
    else
      std::cerr << "ROUTER - COULD NOT ROUTE THIS POINTER, ROUTER FULL OR SHUTING DOWN\n";

    return !success;
  }

  // timeout of 0 will wait indefinitely
  bool Receive(SharedPtr_T &ptr, const size_t &timeout_seconds = 0)
  {
    ptr.reset();
    bool timedout = false; // default

    if (0 != timeout_seconds)
    {
      using namespace boost::posix_time;
      ptime abs_time = second_clock::universal_time() + seconds(timeout_seconds);
      timedout = !nstored.timed_wait(abs_time);
    }
    else
      nstored.wait();

    if (!timedout)
    {
      { // lock_guard scope
        ipc::scoped_lock<ipc::interprocess_mutex> guard(mutex);
        //mutex.wait();
        ptr = items[indexPop];
        items[indexPop].reset();
        indexPop = (indexPop + 1) % capacity;
        --size;
        //mutex.post();
      }
      nempty.post();
    }
    return timedout;
  }

  void Shutdown()
  {
    shutdown = true; // atomic

    nstored.post(); // notify the receiver

    nempty.post(); // notify any pending senders
  }

  // return true if the pipe in not shutdown OR has queued elements
  operator bool() const
  {
    return !shutdown || size > 0;
  }
};

//
// Utility class for sending data to multiple receivers at once.
// You ask it for a pipe and it'll return a strong pointer while maintaining a weak pointer for itself.
// Calling Send() here will send elements to all known pipes.
//
template <typename T, typename SharedPtr_T, typename SegmentManager = ipc::managed_shared_memory::segment_manager>
class RouterIPC
{
  typedef PipeIPC<T, SharedPtr_T> PipeT;

  typedef ipc::managed_shared_memory::segment_manager segment_manager_type;

  typedef ipc::allocator<void, segment_manager_type> void_allocator_type;
  typedef ipc::deleter<PipeT, segment_manager_type> deleter_type;
public:
  typedef ipc::shared_ptr<PipeT, void_allocator_type, deleter_type> sStrongPipeT;
  typedef ipc::weak_ptr<PipeT, void_allocator_type, deleter_type> sWeakPipeT;
private:
  // ipc::allocator<SharedPtr_T, ipc::managed_shared_memory::segment_manager> allocator;
  typedef std::scoped_allocator_adaptor<ipc::allocator<sWeakPipeT, segment_manager_type>> ShmemAllocator;
  typedef ipc::vector<sWeakPipeT, ShmemAllocator> MyVector;

  ipc::interprocess_mutex mutex;
  MyVector pipes;
  // segment_manager_type *segment_manager;

public:
  typedef SharedPtr_T sPtrT;
  typedef typename PipeT::ENQUEUE_MODE ENQUEUE_MODE;

  RouterIPC(SegmentManager *segmentManager_in)
      : pipes(segmentManager_in)
  {
  }

  //
  // return a strong pointer to the pipe and record a weak pointer internally
  //
  sStrongPipeT RegisterAsReceiver(size_t capacity_in, ipc::managed_shared_memory &segment_in)
  {
    // create a new pipe. return a strong pointer to it, and store a weak pointer to it inside this class

    segment_manager_type *segment_manager = segment_in.get_segment_manager();

    if (!segment_manager)
      return sStrongPipeT();

    sStrongPipeT newPipe(segment_manager->template construct<PipeT>(ipc::anonymous_instance, std::nothrow)(capacity_in, segment_in),
                         void_allocator_type(segment_manager),
                         deleter_type(segment_manager));

    if (nullptr == newPipe)
      return sStrongPipeT();

    sWeakPipeT newPipeWeak = newPipe;

    ipc::scoped_lock<ipc::interprocess_mutex> guard(mutex);
    pipes.push_back(newPipeWeak);

    return newPipe;
  }

  //
  // Send element to all pipes.
  // The Send signature is whatever you want it to be. 
  // Return the number of LIVING pipes that had an errr while sending.
  //
  template <typename... Args>
  size_t Send(Args &&...args)
  {
    size_t nLivingPipesWithSendError = 0;
    ipc::scoped_lock<ipc::interprocess_mutex> guard(mutex);

    // I take this opportunity to filter the vector for dead pointers. if
    // If we encounter a weak_ptr that we couldnt promote, swap it with the element
    // at the end of the vector and try again. then shrink the vector

    for (int i = 0; i < (int)pipes.size(); ++i) // signed number desired
    {
      sStrongPipeT pipePtr = pipes[i].lock();

      if (nullptr == pipePtr)
      {
        std::cout << "RouterIPC::Send() - found bad pipe, swapping and popping\n";
        pipes[i] = pipes.back();
        pipes.pop_back(); // this logic works even if the bad pipe is the last in the vector
        --i;
        continue;
      }

      // this pipe must be good, send the data to it
      if (pipePtr->Send(std::forward<Args>(args)...))
        ++nLivingPipesWithSendError;
    }

    return nLivingPipesWithSendError;
  }

  //
  // Send shutodwn signal to all pipes
  //
  void Shutdown()
  {
    ipc::scoped_lock<ipc::interprocess_mutex> guard(mutex);

    for (auto &pipe : pipes)
    {
      sStrongPipeT pipePtr = pipe.lock();
      if (pipePtr)
      {
        pipePtr->Shutdown();
      }
    }

    // i dont think there's a strong reason to call pipes.clear()
  }

  //
  // get number of living pipes
  //
  size_t NumReceivers()
  {
    // We dont care about dead pipes
    ipc::scoped_lock<ipc::interprocess_mutex> guard(mutex);

    size_t count = 0;

    for (auto &pipe : pipes)
    {
      if (pipe.lock())
        ++count;
    }

    return count;
  }
};

} // namespace

#endif