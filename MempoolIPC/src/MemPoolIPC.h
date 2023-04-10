#ifndef SHARED_POINTER_MEM_POOL_IPC_H
#define SHARED_POINTER_MEM_POOL_IPC_H

#define DEBUG_PRINT_MEM_POOL_IPC false

// all of these includes were coppied from boost/interprocess/allocators/allocator.hpp
// they need to be weeded through

#include <boost/interprocess/detail/config_begin.hpp>
#include <boost/interprocess/detail/workaround.hpp>

#include <boost/intrusive/pointer_traits.hpp>

#include <boost/interprocess/interprocess_fwd.hpp>
#include <boost/interprocess/containers/allocation_type.hpp>
#include <boost/container/detail/multiallocation_chain.hpp>
#include <boost/interprocess/allocators/detail/allocator_common.hpp>
#include <boost/interprocess/detail/utilities.hpp>
#include <boost/interprocess/containers/version_type.hpp>
#include <boost/interprocess/exceptions.hpp>
#include <boost/assert.hpp>
#include <boost/utility/addressof.hpp>
#include <boost/interprocess/detail/type_traits.hpp>
#include <boost/container/detail/placement_new.hpp>

#include <cstddef>
#include <stdexcept>

// TODO clean all these includes
#include <boost/interprocess/managed_shared_memory.hpp>
#include <boost/interprocess/smart_ptr/shared_ptr.hpp>
#include <boost/interprocess/allocators/allocator.hpp>
#include <boost/interprocess/smart_ptr/deleter.hpp>

#include "UtilsIPC.h" // random number generator

namespace IPC
{

// TODO remove this from global scope
namespace ipc = boost::interprocess; // convenience

//
// A utility for getting raw memory of a certain number of bytes and alignment
// this object owns the memory it allocates, so that memory will be freed when
// this object is destructed. Alloctor rebinding is supported
//
// this class must be instantiated and then told how much memory to hold in a separate call
// this behavior is completely fueled by the way the AllocatorState cant know the size/alignment
// of memory it needs until it attempts to allocate a shared pointer
//
template <class SegmentManager>
class RawMemoryIPC
{
  //Segment manager
  typedef SegmentManager segment_manager;
  typedef typename SegmentManager::void_pointer void_pointer;

private:
  //  //Self type
  //  typedef allocator<T, SegmentManager>   self_t;

  //Pointer to void
  typedef typename segment_manager::void_pointer aux_pointer_t;

  //Typedef to const void pointer
  typedef typename boost::intrusive::
      pointer_traits<aux_pointer_t>::template rebind_pointer<const void>::type cvoid_ptr;

  //Pointer to the allocator
  typedef typename boost::intrusive::
      pointer_traits<cvoid_ptr>::template rebind_pointer<segment_manager>::type alloc_ptr_t;

  ipc::offset_ptr<uint8_t> raw;        // pointer to raw memory
  size_t rawSize;                      // number of bytes in raw
  size_t nElementsTotal;               // number of elements intended to be stored in raw
  alloc_ptr_t allocator; //Pointer to the allocator

public:
  // simple constructor. for the total number of elements to hold.
  // We suport allocator rebinding
  RawMemoryIPC(size_t nElementsTotal_in, segment_manager *segment_mngr)
      : raw(nullptr), rawSize(0), nElementsTotal(nElementsTotal_in), allocator(segment_mngr)
  {
  }

  //
  // function to get an aligned pointer to nElementsTotal_in*sizeT worth of bytes, aligned to alignT
  // 
  uint8_t *Allocate(size_t sizeT, size_t alignT)
  {

    // use the provided allocator to get the memory that will serve as our pool
    size_t nBytesTotal = sizeT * nElementsTotal;
    uint8_t *ptrAbsolute = static_cast<uint8_t *>(allocator->allocate_aligned(nBytesTotal, alignT, std::nothrow)); // dont use allocator->get_free_memory() because it could be a race condition
    raw = ptrAbsolute;
    if (! raw && nBytesTotal > 0)
    {
      std::printf("RawMemoryIPC::Allocate() - COUNT NOT ALLOCATE ENOUGH MEMORY WHILE REQUESTING %zu BYTES. YOU NEED TO INCREASE YOUR MAPPING SIZE\n", nBytesTotal);
      throw std::bad_alloc();
    }
    else
      rawSize = nBytesTotal;

    return ptrAbsolute;
  }

  // destructor will free memory
  ~RawMemoryIPC()
  {
    if (raw)
    {
      allocator->deallocate((void *)ipc::ipcdetail::to_raw_pointer(raw));
      rawSize = 0;
    }
  }
};

//
// The premiere mempool utility. This allocates memory for an array of
// objects of a given size and alignment. It then records pointers into that
// memory that it can hand out. This is intended to be used as the shared
// internal state for a mempool allocator.
//
//template <template<typename> class AllocatorT = std::allocator>
template <class SegmentManager>
class AllocatorState
{
  //Segment manager
  typedef SegmentManager segment_manager;
  typedef typename SegmentManager::void_pointer void_pointer;

private:
  //  //Self type
  //  typedef allocator<T, SegmentManager>   self_t;

  //Pointer to void
  typedef typename segment_manager::void_pointer aux_pointer_t;

  //Typedef to const void pointer
  typedef typename boost::intrusive::
      pointer_traits<aux_pointer_t>::template rebind_pointer<const void>::type cvoid_ptr;

  //Pointer to the allocator
  typedef typename boost::intrusive::
      pointer_traits<cvoid_ptr>::template rebind_pointer<segment_manager>::type alloc_ptr_t;

  typedef ipc::mutex_family::mutex_type MutexT;
  typedef ipc::scoped_lock<MutexT> GuardT;

  //AllocatorT<uint8_t> allocator;
  alloc_ptr_t allocator; //Pointer to the allocator

  MutexT mutex;
  RawMemoryIPC<SegmentManager> objectsBuffer;     // memory for the objects themselves 
  RawMemoryIPC<SegmentManager> pointersBuffer;    // memory for the pointers to the objects
  ipc::offset_ptr<uint8_t> objects_alignedPtr;    // the aligned pointer into object buffer
  ipc::offset_ptr<uintptr_t> pointers_alignedPtr; // the aligned pointer into the pointer Buffer
  size_t nElementsTotal; // number of elements in pool (element is a whole array when in array mode)
  size_t nAvailable;     // number of elements currently loaned out
  bool initialized;      // pool is allocated and setup

  size_t sizeSharedT;     // total size of each block in this pool
  size_t alignSharedT;    // alignment of each block in this pool
  size_t countPerElement; // every count elements is a group. for when we allocate arrays of arrays

public:
  // constructor for a mempool of size nElements_in * count_in. Use / rebind a given allocator
  // this constructor does not actually allocate the pool
  // when NOT in array mode, just leave countPerElement_in as 1. When in array mode, set that to any value (>0 obvi)
  AllocatorState(size_t nElements_in, segment_manager *segment_mngr, size_t countPerElement_in = 1)
      : allocator(segment_mngr),
        objectsBuffer(nElements_in * countPerElement_in, segment_mngr),
        pointersBuffer(nElements_in, segment_mngr),
        objects_alignedPtr(nullptr),
        pointers_alignedPtr(nullptr),
        nElementsTotal(nElements_in), nAvailable(nElements_in),
        initialized(false),
        sizeSharedT(0),
        alignSharedT(0),
        countPerElement(countPerElement_in)
  {
  }

  // frees all memory
  ~AllocatorState()
  {
    if (DEBUG_PRINT_MEM_POOL_IPC)
      std::cout << "~AllocatorState()\n";
    // objectsBuffer and pointersBuffer scope ends and are therefore destroyed
  }

  //
  // function for allocating all memory and creating a set of
  // pointers into that memory.
  //
  void Allocate(size_t sizeSharedT_in, size_t alignSharedT_in)
  {

    GuardT guard(mutex);

    if (initialized)
      return;

    sizeSharedT = sizeSharedT_in;
    alignSharedT = alignSharedT_in;

    objects_alignedPtr = objectsBuffer.Allocate(sizeSharedT, alignSharedT);
    pointers_alignedPtr = (uintptr_t *)pointersBuffer.Allocate(sizeof(uintptr_t), alignof(uintptr_t));

    // get the pointers into the raw pool
    // these pointers are actually encoded by their index into the pool, not their absolue address
    for (size_t i = 0; i < nElementsTotal; ++i)
      pointers_alignedPtr[i] = (sizeSharedT * countPerElement * i);

    initialized = true;
  }

  // hand out a pointer to the pool as if to allocate
  ipc::offset_ptr<void> Loan()
  {
    GuardT guard(mutex);

    if (nAvailable > 0 && countPerElement > 0)
    {
      return objects_alignedPtr + pointers_alignedPtr[--nAvailable];
    }
    else
    {
      std::cout << "ALLOCATORSTATE - RAN OUT OF ELEMENTS TO LOAN. THE POOL SIZE IS TOO SMALL OR YOU SPECIFIED 0 ARRAY ELEMENTS PER POOL ELEMENT.\n";
      //throw std::bad_alloc();
      return nullptr;
    }
  }

  // take a pointer back as if to deallocate that memory
  // call the destructor with the appropriate elementIndex
  void Accept(ipc::offset_ptr<uint8_t> p) noexcept
  {
    // check for null, just in case someone deallocated a pointer without checking their allocation
    if (nullptr == p)
      return;

    GuardT guard(mutex);

    pointers_alignedPtr[nAvailable++] = p - objects_alignedPtr; // TODO wouldnt hurt to check if this is a valid address for this pool
  }

  // get the size and alignment that this pool is configured for
  void GetSizeAndAlignment(size_t &sizeSharedT_out, size_t &alignSharedT_out)
  {
    sizeSharedT_out = sizeSharedT;
    alignSharedT_out = alignSharedT;
  }

  // check if already initialized
  bool Initialized()
  {
    // this is set once behind a guard soon after construction time.
    // It's safe enough to not guard the access here when used with SharedPointerAllocator
    return initialized;
  }

  // get total number of elements that this pool can hold
  size_t TotalElementSize()
  {
    // this is set once behind a guard soon after construction time.
    // It's safe enough to not guard the access here when used with SharedPointerAllocator
    return sizeSharedT;
  }

  // get number of currently loaned out elements and number of available elements
  void Statistics(size_t &nOutstanding_out, size_t &nAvailable_out)
  {
    GuardT guard(mutex);
    nAvailable_out = nAvailable;
    nOutstanding_out = nElementsTotal - nAvailable;
  }

  // given a pointer to SOMEWHERE in a block of memory, figure out which block it belongs to and return that index
  // size_t GetElementNumber(ipc::offset_ptr<uint8_t> ptr)
  // {
  //   return (ptr - objects_alignedPtr) / sizeSharedT; // integer division intended; it rounds down here which is what we want.
  // }
};

//
// This is an Allocator for objects of type T. That means its basically a mempool of just objects.
// The real mempool will wrap this and ask it for objects. 
//
// This holds a shared pointer to the AllocatorState that gets passed around and coppied wherever needed
//
// This class is templated on 'sizingMode' because, at construction, when it needs to figure out the size(T) and alignment(T),
// it has to construct a temporary object of type T. To avoid this construction, i just cast a pointer to T* and use that.
// The trick is that letting this class destroy that object like any other element it allocates would be bad.
//
// TLDR; This is basically the destructor type that a shared_ptr uses, as well as the object allocator
//
template <class T, class SegmentManager, bool sizingMode = false>
class ObjectAllocator
{
public:
  typedef typename boost::intrusive::
      pointer_traits<typename SegmentManager::void_pointer>::template rebind_pointer<T>::type pointer;

private:
  typedef typename boost::intrusive::
      pointer_traits<pointer>::template rebind_pointer<SegmentManager>::type segment_manager_pointer;

public:
  typedef AllocatorState<SegmentManager> StateT; //convenience
  typedef ipc::allocator<void, SegmentManager> void_allocator_type;
  typedef ipc::deleter<StateT, SegmentManager> StateDeleter_T;
  typedef ipc::shared_ptr<StateT, void_allocator_type, StateDeleter_T> sStateT;

  // this should really be the only member in this class, since allocators get copied everywhere they go
  size_t countPerElement;
  sStateT state;
  //
  // this class does not hold a std::shared_ptr to the segment nor the pool because these objects
  // may be shipped around to different processes, so they cant control the lifetime of any object
  // that is specific to one process. it relies on the mempool, or the shared_ptr that holds the 
  // mempool to hold that object and keep it in scope
  //

  ObjectAllocator(size_t nElements, SegmentManager *segment_mngr, size_t countPerElement_in)
      : countPerElement(countPerElement_in),
        state(segment_mngr->template construct<StateT>(ipc::anonymous_instance, std::nothrow)(nElements, segment_mngr, countPerElement_in),
              void_allocator_type(segment_mngr),
              StateDeleter_T(segment_mngr))
  {
    if (!state)
    {
      std::printf("ObjectAllocator::ObjectAllocator() - COULD NOT ALLOCATE AN AllocatorState. YOU MAY NEED TO INCREASE YOUR MAPPING SIZE\n");
      throw std::bad_alloc();
    }

    if (state)
      state->Allocate(sizeof(T), alignof(T));
  }

  T *allocate()
  {
    if (!state)
      return nullptr;

    T *ptrAbsolute = (T *)state->Loan().get();
    if (DEBUG_PRINT_MEM_POOL_IPC)
      std::cout << "ObjectAllocator::allocating = " << (void *)ptrAbsolute << std::endl;

    return ptrAbsolute;
  }

  void operator()(const pointer &p)
  {
    // the segment manager would just call this and be done. 
    // this is just an example of how a minimum-effort, passthrough class would be written
    // mp_mngr->destroy_ptr(ipc::ipcdetail::to_raw_pointer(p));

    if (sizingMode)
    {
      if (DEBUG_PRINT_MEM_POOL_IPC)
        std::cout << "ObjectAllocator::deleting ptr BUT IN SIZING MODE\n";
      return;
    }

    if (DEBUG_PRINT_MEM_POOL_IPC && p)
      std::cout << "ObjectAllocator::deleting ptr = " << (void *)p.get() << std::endl;

    // start and stop positions in the pool
    T *first = p.get();
    T *last = first + countPerElement;

    if (!std::is_arithmetic<T>::value)
    {
      // iterate over each element in this block and destruct
      for (T *ptr = last-1; ptr >= first; --ptr) // destruct in reverse order. this is supposedly good practice
        ptr->~T();
    }

    if (state && p)
      state->Accept(ipc::offset_ptr<uint8_t>((uint8_t *)p.get()));
  }
};

//
// This is an Allocator for ipc::shared_ptr<T>'s control block objects. 
// That means its basically a mempool of the objects that a shared_ptr's control block.
// The real mempool will wrap this and ask it for objects. 
//
// This holds a shared pointer to the AllocatorState that gets passed around and coppied wherever needed
//
// This class is templated on 'DeleterT' because, at construction, when the parent mempool needs to figure out 
// the size(T) and alignment(T), it has to construct a temporary object of type T. To avoid this construction,
// I just cast a pointer to T* and use that. The trick is that letting this class destroy that object like any 
// other element its given would be bad.
//
// TLDR: this is an allocator/ mempool of just shared_ptr<T>'s control block. its destructor is templated because
// boost::interprocess::shared_ptr<T> functions slightly differently than std::shared_ptr. The latter's ::make_shared()
// will construct an object for you, but the former will ONLY takes in a T* (pointer to an existing T). The former ALSO
// forces you to specify a deleter, and i decided to get clever and change how i delete things, so i needed to template 
// that too 
//
// a lot of this code is coppied from https://www.boost.org/doc/libs/1_75_0/boost/interprocess/allocators/allocator.hpp
template <class T, class SegmentManager, template <typename, typename, bool> class DeleterT = ObjectAllocator>
class ControlBlockAllocator
{
public:
  //Segment manager
  typedef SegmentManager segment_manager;
  typedef typename SegmentManager::void_pointer void_pointer;

private:
  //Self type
  typedef ControlBlockAllocator<T, SegmentManager, DeleterT> self_t;

  //Pointer to void
  typedef typename segment_manager::void_pointer aux_pointer_t;

  //Typedef to const void pointer
  typedef typename boost::intrusive::
      pointer_traits<aux_pointer_t>::template rebind_pointer<const void>::type cvoid_ptr;

  //Pointer to the allocator
  typedef typename boost::intrusive::
      pointer_traits<cvoid_ptr>::template rebind_pointer<segment_manager>::type alloc_ptr_t;

  //  //Not assignable from related allocatorStateT
  //  template<class T2, class SegmentManager2>
  //  ControlBlockAllocator& operator=(const ControlBlockAllocator<T2, SegmentManager2, DeleterT>&);

  //  //Not assignable from other allocator
  //  ControlBlockAllocator& operator=(const ControlBlockAllocator&);

  typedef AllocatorState<SegmentManager> StateT; //convenience
  typedef ipc::allocator<void, SegmentManager> void_allocator_type;
  typedef ipc::deleter<StateT, SegmentManager> StateDeleter_T;
  typedef ipc::shared_ptr<StateT, void_allocator_type, StateDeleter_T> sStateT;

public:
  // this should really be the only member in this class, since allocators get copied everywhere they go
  sStateT state;
  //
  // this class does not hold a std::shared_ptr to the segment nor the pool because these objects
  // may be shipped around to different processes, so they cant control the lifetime of any object
  // that is specific to one process. it relies on the mempool, or the shared_ptr that holds the 
  // mempool to hold that object and keep it in scope
  //

  typedef T value_type;
  typedef typename boost::intrusive::
      pointer_traits<cvoid_ptr>::template rebind_pointer<T>::type pointer;
  typedef typename boost::intrusive::
      pointer_traits<pointer>::template rebind_pointer<const T>::type const_pointer;
  typedef typename ipc::ipcdetail::add_reference<value_type>::type reference;
  typedef typename ipc::ipcdetail::add_reference<const value_type>::type const_reference;
  typedef typename segment_manager::size_type size_type;
  typedef typename segment_manager::difference_type difference_type;

  typedef boost::interprocess::version_type<ControlBlockAllocator, 2> version;

// #if !defined(BOOST_INTERPROCESS_DOXYGEN_INVOKED)

//   //Experimental. Don't use.
//   typedef boost::container::dtl::transform_multiallocation_chain<typename SegmentManager::multiallocation_chain, T> multiallocation_chain;
// #endif //#ifndef BOOST_INTERPROCESS_DOXYGEN_INVOKED

  ControlBlockAllocator<T, SegmentManager, DeleterT> &operator=(const ControlBlockAllocator<T, SegmentManager, DeleterT> &) = default;

  template <class T2>
  struct rebind
  {
    typedef ControlBlockAllocator<T2, SegmentManager, DeleterT> other;
  };

public: //private:
  // this constructor is private and will only be called by the other constructor to create a temporary object
  // to figure out size/alignment
  ControlBlockAllocator(segment_manager *segment_mngr)
      : state(segment_mngr->template construct<StateT>(ipc::anonymous_instance, std::nothrow)(1, segment_mngr, 1),
              void_allocator_type(segment_mngr),
              StateDeleter_T(segment_mngr))
  {
    if (!state)
    {
      std::printf("ControlBlockAllocator::ControlBlockAllocator() - COULD NOT ALLOCATE AN AllocatorState. YOU MAY NEED TO INCREASE YOUR MAPPING SIZE\n");
      throw std::bad_alloc();
    }
  }

public:
  // the constructor the user should use. It creates, sizes, and allocates nElements of whatever type a shared pointer's control block needs
  ControlBlockAllocator(size_t nElements, segment_manager *segment_mngr)
      : state(segment_mngr->template construct<StateT>(ipc::anonymous_instance, std::nothrow)(nElements, segment_mngr, 1),
              void_allocator_type(segment_mngr),
              StateDeleter_T(segment_mngr))
  {
    if (!state)
    {
      std::printf("ControlBlockAllocator::ControlBlockAllocator() - COULD NOT ALLOCATE AN AllocatorState. YOU MAY NEED TO INCREASE YOUR MAPPING SIZE\n");
      throw std::bad_alloc();
    }

    // std::cout << "about to create a temporary object for control block size detection\n";

    // because a shared_ptr allocates a control block and object AND copies (rebinds) the allocator in that object,
    // calling this here will let us detect the proper size and alignment of that struct {control_blockt and T} object.
    // The created shared pointer is immediately deleted, and we know how to size and align the mempool now.
    // I know this really isnt ideal, but there truly is no other way to do this since:
    // - The control block is not an exposed type
    // - The control block is platform / compiler dependent
    // - The compiler is free to rearange member variables in memory as it sees fit
    // - The size and alignment of the {control_block and T} struct may be different than either individal size/alignment
    typedef ControlBlockAllocator<T, SegmentManager, DeleterT> OneOffAllocatorT;
    typedef DeleterT<T, SegmentManager, true> OneOffDeleterT;
    OneOffAllocatorT oneOffAllocator(segment_mngr);
    OneOffDeleterT oneOffDeleter(1, segment_mngr, 1);

    double fakeout = -1;
    double *fakeoutPtr = &fakeout;
    T *ptrObject = (T *)fakeoutPtr;
    //T* ptrObject = nullptr; // WE CANT USE A NULL POINTER. ipc::shared_ptr CHECKS FOR IT. rage.
    auto oneOffPtr = ipc::shared_ptr<T, OneOffAllocatorT, OneOffDeleterT>(ptrObject,
                                                                          oneOffAllocator,
                                                                          oneOffDeleter);

    if (DEBUG_PRINT_MEM_POOL_IPC)
      std::cout << "control block size and alignment detected\n";

    // now allocate the memory we need, now that we know the proper size
    size_t sizeSharedT;
    size_t alignSharedT;
    oneOffAllocator.state->GetSizeAndAlignment(sizeSharedT, alignSharedT);
    state->Allocate(sizeSharedT, alignSharedT);

    std::printf("Constructing Allocator<T> with sizeof(T*)=%zu but an extra %zu bytes are needed for the shared_ptr's control block. Total size = %zu, total alignment = %zu\n", sizeof(T *), sizeSharedT - sizeof(T *), sizeSharedT, alignSharedT);
  }

  //!Constructor from other allocator.
  //!Never throws
  ControlBlockAllocator(const ControlBlockAllocator &other)
      : state(other.state)
  {
  }

  // this is the constructor that does all the work in the background. When you called the non-copy constructor, it
  // evenually rebinds this allocator to the type needed by a shared_ptr's control block and creates a copy. 
  // this is that copy.
  // T2 here is the type of the shared_ptr control block. it's here that we finally figure out the size and alignment
  // of that control block. we call Allocate, and then that's it. our work is done.
  template <class T2>
  ControlBlockAllocator(const ControlBlockAllocator<T2, SegmentManager, DeleterT> &other)
      : state(other.state)
  {
    if (nullptr == state)
      std::printf("ALLOCATOR - SOMETHING WEIRD HAS HAPPENED. THE POOL NANE IS NULL\n");

    if (state->Initialized())
    {
      if (state->TotalElementSize() < sizeof(T))
      {
        std::printf("ALLOCATOR - SOMETHING WEIRD HAS HAPPENED. THE POOL IS MISS-SIZED. TOTAL ELEMENT SIZE = %zu AND SIZEOF(T)= %zu\n", state ? state->TotalElementSize() : 0, sizeof(T));
        // throw std::bad_alloc(); // cannot throw, noexcept
        state.reset(); // = nullptr
      }
    }
    else
    {
      state->Allocate(sizeof(T), alignof(T));
    }
  }

  //!Allocates memory for an array of count elements.
  //!Throws boost::interprocess::bad_alloc if there is no enough memory
  pointer allocate(size_type count, cvoid_ptr hint = 0)
  {
    // this code is what a simple passthrough allocator would look like
    // its left here as an example
    //
    // (void)hint;
    // if(ipc::size_overflows<sizeof(T)>(count)){
    //    throw ipc::bad_alloc();
    // }
    // return pointer(static_cast<value_type*>(mp_mngr->allocate(count*sizeof(T))));

    (void)hint; // suppress [-Werror=unused-parameter]

    if (!state || count > 1)  // we can only allocate one element at a time. this is how shared_ptr's work
      throw ipc::bad_alloc(); //return nullptr; // consider throwing

    T *ptrAbsolute = (T *)state->Loan().get();
    if (DEBUG_PRINT_MEM_POOL_IPC)
      std::cout << "ControlBlockAllocator::allocating = " << (void *)ptrAbsolute << std::endl;

    return pointer(ptrAbsolute);
  }

  //!Deallocates memory previously allocated.
  //!Never throws
  void deallocate(const pointer &ptr, size_type)
  {
    // this code is what a simple passthrough allocator would look like
    // its left here as an example
    //
    //mp_mngr->deallocate((void*)ipc::ipcdetail::to_raw_pointer(ptr));

    if (DEBUG_PRINT_MEM_POOL_IPC && ptr)
      std::cout << "ControlBlockAllocator::deallocating = " << (void *)ptr.get() << std::endl;

    if (state && ptr)
      state->Accept(ipc::offset_ptr<uint8_t>((uint8_t *)ptr.get()));
  }

  //!Allocates just one object. Memory allocated with this function
  //!must be deallocated only with deallocate_one().
  //!Throws boost::interprocess::bad_alloc if there is no enough memory
  pointer allocate_one()
  {
    return this->allocate(1);
  }

  //!Deallocates memory previously allocated with allocate_one().
  //!You should never use deallocate_one to deallocate memory allocated
  //!with other functions different from allocate_one(). Never throws
  void deallocate_one(const pointer &p)
  {
    return this->deallocate(p, 1);
  }

  // pass through to get number of outstandin and available elements in the pool
  void statistics(size_t &nOutstanding, size_t &nAvailable)
  {
    if (state)
      state->Statistics(nOutstanding, nAvailable);
    else
    {
      nOutstanding = 0;
      nAvailable = 0;
    }
  }
};

//!Equality test for same type
//!of allocator
template <class T, class SegmentManager, template <typename, typename, bool> class DeleterT>
inline bool operator==(const ControlBlockAllocator<T, SegmentManager, DeleterT> &alloc1,
                       const ControlBlockAllocator<T, SegmentManager, DeleterT> &alloc2)
{
  // return alloc1.get_segment_manager() == alloc2.get_segment_manager();
  return alloc1.state.get() == alloc2.state.get();
}

//!Inequality test for same type
//!of allocator
template <class T, class SegmentManager, template <typename, typename, bool> class DeleterT>
inline bool operator!=(const ControlBlockAllocator<T, SegmentManager, DeleterT> &alloc1,
                       const ControlBlockAllocator<T, SegmentManager, DeleterT> &alloc2)
{
  // return alloc1.get_segment_manager() != alloc2.get_segment_manager();
  return alloc1.state.get() != alloc2.state.get();
}

//
// oh boy, you found it! this is the top level mempool that users will see and use. It
// makes use of ObjectAllocator and ControlBlockAllocator and does nothing more than take
// elements from each of them, put them together, and give them to the user in the form of 
// a shared_ptr. 
//
//
template <typename T, typename SegmentManager=ipc::managed_shared_memory::segment_manager>
struct MemPoolIPC
{
private:
  //Segment manager
  typedef SegmentManager segment_manager;
  typedef typename SegmentManager::void_pointer void_pointer;

  //  //Self type
  //  typedef allocator<T, SegmentManager>   self_t;

  //Pointer to void
  typedef typename segment_manager::void_pointer aux_pointer_t;

  //Typedef to const void pointer
  typedef typename boost::intrusive::
      pointer_traits<aux_pointer_t>::template rebind_pointer<const void>::type cvoid_ptr;

  //Pointer to the allocator
  typedef typename boost::intrusive::
      pointer_traits<cvoid_ptr>::template rebind_pointer<segment_manager>::type alloc_ptr_t;

  std::uint_fast64_t uniqueInstanceID;
  alloc_ptr_t segmentManager; // held so it can construct a copy of itself. itll do

public:
  // the type defs for the allocator and deleter
  typedef ControlBlockAllocator<T, SegmentManager> MyControlBlockAllocator;
  typedef ObjectAllocator<T, SegmentManager> MyObjectAllocator;
  typedef ipc::shared_ptr<T, MyControlBlockAllocator, MyObjectAllocator> sPtrT;

  size_t nElements;
  size_t countPerElement;
  MyControlBlockAllocator myControlBlockAllocator;
  MyObjectAllocator myObjectAllocator;
  // objects that use these allocators make copies of them (and therefore their internal state), so
  // this object and these allocator instances are free to go out of scope while their elements are
  // being used since those elements will keep their shared states alive.
  // In other words, these pools are allowed to go out of scope and their elements will actually
  // keep them alive until the last element is deleted.
  //
  // while the pool cannot be destroyed out from underneath its elements, the segment technically can be...
  // although this is pretty unlikely. The segment is not truly unmapped by the OS untill all handles to it
  // are released, which means, ///if each interprocess stores its handle in the shared pointer made with
  // the proper utility functions, this should not ever happen///. That would mean that each process has
  // properly shutdown and all references to the segment have already been wiped. therefore, nothing is
  // left to even be using this pool anymore. You should be fine since your code was already shutdown 
  // anyways, but if you had to do some resource freeing (hardware or software), then
  // it would matter. 
  //
  // this class doesnt hold a reference to the mapped segment, but the shared pointer that holds class
  // does. if made with the proper utility functions. This class also CANT hold a reference to the mapped 
  // segment because this class is shared by all the different processes.

  MemPoolIPC(SegmentManager *segmentManager_in, size_t nElements_in, size_t countPerElement_in=1)
      : uniqueInstanceID(utils_ipc::GenerateUniqueID()),
        segmentManager(segmentManager_in),
        nElements(nElements_in),
        countPerElement(countPerElement_in),
        myControlBlockAllocator(nElements, segmentManager_in),
        myObjectAllocator(nElements, segmentManager_in, countPerElement_in)
  {
  }

  MemPoolIPC<T, SegmentManager> &operator=(const MemPoolIPC<T, SegmentManager> &rhs) = default;

  template <typename... Args>
  sPtrT make_pooled(Args &&...args)
  {
    auto ptrObject = myObjectAllocator.allocate();
    if (nullptr == ptrObject)
      return sPtrT();

    // start and stop positions in the pool
    T *first = ptrObject;
    T *last = first + countPerElement;

    if (!std::is_arithmetic<T>::value || sizeof...(args) > 0)
    {
      // iterate over each element in this block and construct in place an element of type T with perfect forwarding
      for (T *ptr = first; ptr < last; ++ptr)
        new (ptr) T(std::forward<Args>(args)...);
    }

    //new (ptrObject) T(std::forward<Args>(args)...);
    sPtrT ptr(ptrObject, myControlBlockAllocator, myObjectAllocator);
    // objects that use these allocators make copies of them (and therefore their internal state), so
    // this object and these allocator instances are free to go out of scope while their elements are
    // being used since those elements will keep their shared states alive.
    // In other words, these pooled are allowed to go out of scope and their elements will actually
    // keep them alive until the last element is deleted.
    //
    // while the pool cannot be destroyed out from underneath its elements, the segment, however, can be.
    // see comment block earlier in the class

    return ptr;
  }

  // pass through to get number of outstandin and available elements in the pool
  void Statistics(size_t &nOutstanding, size_t &nAvailable)
  {
    myControlBlockAllocator.statistics(nOutstanding, nAvailable);
  }

  std::uint_fast64_t UniqueInstanceID()
  {
    return uniqueInstanceID;
  }

  // this is a factory function intended to create a brand new object of this type
  // with all the same input arguments except with a different size. 
  // NOTE: this is not a resize(). this is a whole new memory allocation.
  // the idea is to "rebind" the pool for some parent object that needs a pool
  // that is basically identical... just larger
  MemPoolIPC ReConstruct(size_t newSize)
  {
    return MemPoolIPC(segmentManager.get(), newSize, countPerElement);
  }
};

//
// This is a nifty utility class that warps any of the top level mempools and
// resizes them if they appear to be out of available elements.
// TODO as is, this class is functional but could use just a little more love
//
template <typename T>
struct MemPoolGrowerIPC
{
  typedef typename T::sPtrT sPtrT;
  typedef ipc::mutex_family::mutex_type MutexT;
  typedef ipc::scoped_lock<MutexT> GuardT;

  T pool;
  MutexT mutex;
  size_t growthRate; // new size is growthRate*oldSize
  size_t nReconstructions;

  template <typename... Args>
  MemPoolGrowerIPC(Args &&...args) : pool(std::forward<Args>(args)...), growthRate(2), nReconstructions(0)
  {
  }

  //
  // this function needs a little more work.
  // as is, if a pool returns a nullptr for any OTHER reason
  // besides being out of room, this function will grow the pool.
  // If there is a reason besides being out of room that doesnt go away, 
  // itll just keep reallocating without fixing the problem.
  //
  template <typename... Args>
  sPtrT make_pooled(Args &&...args)
  {
    GuardT guard(mutex);

    sPtrT ptr = pool.make_pooled(std::forward<Args>(args)...);

    // element is null, pool is likely empty // TODO this is not the only scenario in which we get a nullptr, so this is actually kinda dangerous
    if (!ptr)
    {
      std::cout << "got a null ptr, the pool appears to be too small. expanding it...\n";

      size_t newSize = pool.nElements == 0 ? 1 : growthRate * pool.nElements;

      pool = pool.ReConstruct(newSize); // the old pool will naturally deallocate itself as its elements are returned
      ++nReconstructions;

      ptr = pool.make_pooled(std::forward<Args>(args)...);
    }

    return ptr;
  }
};

} // namespace

#endif