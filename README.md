## About and Features
This library offers a set of c++ tools for creating memory pools in interprocess-shared memory as well as a few utilities for facilitating interprocess communication and automatic device discovery. It's built on top of `Boost::IPC` and `Boost::asio` and is tested on Linux (primarily) and Windows. 

All IPC objects are encapsulated by shared pointers that track the usage counter not just in your app, but inside all other processes as well. The result is a smart pointer that that keeps its owned object _and its dependencies_ alive until references in every process naturally fall out of scope, just like a traditional `std::shared_ptr<T>` would. Memory allocation is handled at the `Allocator` level, meaning that _even the shared pointer's control block is accounted for and included in the pool size_  - no dynamic allocation happens when creating new shared pointers (yeah, that was not fun). The Interprocess router supports timeouts for reading / writing, and memory pools support automatic growth when exhausted. 

The network transmission utilities use broadcast/ multicast for topic announcement (not required) and websockets for two-way data transmission in one-to-many and many-to-one configurations. Serialization is handled by the user, and callback functions / completion handlers can be set for message receipt, broadcast receipt, connection open / close, and much more.  

c++11 

## Examples

#### IPC Memory Pool:
```cpp
//
// process A (producer)
//
{
  typedef MyClass T; // type to pool and route
  typedef MemPoolIPC<T> PoolT;
  typedef utils_ipc::RouterIPC<T, PoolT::sPtrT> RouterT;

  // ask OS for memory region and map it into address space
  auto segmentPtr = utils_ipc::open_or_create_mapping("MySharedMemory", nBytes);
  // get pool and router types. each hold references to segmentPtr to keep it in scope
  auto poolPtr = utils_ipc::find_or_create_shared_object<PoolT>(segmentPtr, "pool", segmentPtr->get_segment_manager(), poolSize);
  auto routerPtr = utils_ipc::find_or_create_shared_object<RouterT>(segmentPtr, "Router", segmentPtr->get_segment_manager());


  // send data
  for (/**/)
  {
    // get element from the pool
    auto elementPtr = poolPtr->make_pooled(/* constructor args */); // holds poolPtr in scope
    // route it
    routerPtr->Send(elementPtr, RouterT::ENQUEUE_MODE::WAIT_IF_FULL, 1); 
  }
  
  // when process exits, counters for elements, pool, router, and segment will decrement and 
  // destruct if and only if all processes are done with them
}

```

```cpp
//
// process B (consumer)
//
{
  typedef MyClass T; // type to pool and route
  typedef MemPoolIPC<T> PoolT;
  typedef utils_ipc::RouterIPC<T, PoolT::sPtrT> RouterT;

  // ask OS for memory region and map it into address space
  auto segmentPtr = utils_ipc::open_or_create_mapping("MySharedMemory", nBytes);
  // get pool and router types. each hold references to segmentPtr to keep it in scope
  auto routerPtr = utils_ipc::find_or_create_shared_object<RouterT>(segmentPtr, "Router", segmentPtr->get_segment_manager());
  

  // receive data
  bool timedout = false;
  while (*pipePtr && !timedout)
  {
    RouterT::sPtrT elementPtr;
    timedout = pipePtr->Receive(elementPtr, 5); // timed_wait because we can
    if (timedout || ! elementPtr) 
      continue;
      
  // do something with elementPtr
  }
 
  // when process exits, counters for elements, pool, router, and segment will decrement and 
  // destruct if and only if all processes are done with them  
}

```
#### Automatic Network Discovery and Transmission:
```cpp
//
// process A (producer / sender)
//
{
  // the type to route
  typedef MyClassIPC T;
  typedef MemPoolIPC<T> PoolT;

  // ask OS for memory region and map it into address space
  auto segmentPtr = utils_ipc::open_or_create_mapping("MySharedMemory", nBytes);
  auto poolPtr = utils_ipc::find_or_create_shared_object<PoolT>(segmentPtr, "pool", segmentPtr->get_segment_manager(), poolSize);

  // create io_context (work_guarded)
  utils_asio::GuardedContext guarded_context(nThreads);

  // the magic is right here
  MultiClientSenderArgs args;
  MultiClientSender sender(guarded_context.GetIOContext(), "myTopic", args, poolPtr->UniqueInstanceID());
  
  // start heartbeat (UDP breadcast of topic name and other misc info). also starts sending server
  sender.StartHeartbeat();
  
  for (/**/)
  {
    // make up some data
    auto ptr = poolPtr->make_pooled(/* constructor args */);
    // send it. serialize it somehow, maybe it's a plain structor and we can just send (&*ptr) and sizeof()
    sender.SendAsync(msgPtr, msgSize, [ptr](boost::beast::error_code, size_t){});  // callback handler keeps element and its pool in scope
  }
}

```
```cpp
//
// process B (consumer / listener / receiver)
//
{
  // the type to route
  typedef MyClassIPC T;
  typedef MemPoolIPC<T> PoolT;
  typedef utils_ipc::RouterIPC<T, PoolT::sPtrT> RouterT;

  auto segmentPtr = utils_ipc::open_or_create_mapping("MySharedMemory", nBytes);
  auto poolPtr = utils_ipc::find_or_create_shared_object<PoolT>(segmentPtr, "pool", segmentPtr->get_segment_manager(), poolSize);
  auto routerPtr = utils_ipc::find_or_create_shared_object<RouterT>(segmentPtr, "Router", segmentPtr->get_segment_manager());

  //
  // setup all the callbacks for each topic. whenever we receive a message for a topic, fire one of these
  //
  MultiClientReceiver::TopicCallbacksT receivableTopics;

  auto callbackRead = [poolPtr, routerPtr](const tcp::endpoint &endpoint, void *msgPtr, size_t msgSize)
  {
    // do something with the recieved element. or just route it to everyone else on your system
    auto elementPtr = poolPtr->make_pooled(msgPtr, msgSize);
    size_t nPipesHadError = routerPtr->Send(elementPtr, RouterT::ENQUEUE_MODE::SOFT_FAIL_IF_FULL);
  }; // lambda keeps pool and router in scope and therefore segment

  receivableTopics["myTopic"].callbackRead = callbackRead;

  // if you had more topics, you would add them here
  // receivableTopics["someOtherTopic"] = ...

  // create io_context (work_guarded)
  utils_asio::GuardedContext guarded_context(nThreads - 1); // use the last thread to block

  MultiClientReceiverArgs args;
  MultiClientReceiver receiver(guarded_context.GetIOContext(), receivableTopics, args, poolPtr->UniqueInstanceID());
  receiver.ListenForTopics(); // start listening server running

  guarded_context.GetIOContext().run(); // block until the receiver timeout out
}

```

## Other Utilities
- Memory Pool for local-process only usage (don`t need IPC if there's only one process)
- Network utilities for automatic device discovery. Topic names are broadcast (or multicast), and a receiver will initiate a websocket connection to the sender if the topic name matches
- Automatically grow memory pools if they are exhausted. You'll eat the dynamic memory allocation, but the old pool will self-destruct once all references to it fall out of scope
- Array versions of the memory pools so that you can get batches of elements at once (think array of pixels for an image) 


## TODO
 - Change some prints to `cerr`
 - General cleanup
 - Support multicast instead of only broadcast