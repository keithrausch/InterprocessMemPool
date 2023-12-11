#ifndef MEMPOOL_HEAP_H
#define MEMPOOL_HEAP_H

//
// Keith Rausch
//

#include <iostream>   // cout, printf
#include <mutex>      // mutex
#include <memory>     // std::align
#include <cstddef>    // std::max_align_t
#include <functional> // std::function
#include <type_traits>

namespace IPC
{

//
// A utility for getting raw memory of a certain number of bytes and alignment
// this object owns the memory it allocates, so that memory will be freed when
// this object is destructed. Alloctor rebinding is supported
//
template <template<typename> class AllocatorT = std::allocator>
class RawMemory
{
  uint8_t* raw;                   // pointer to raw memory
  size_t rawSize;                 // number of bytes in raw
  size_t nElementsTotal;          // number of elements intended to be stored in raw
  uint8_t* rawAligned;            // a pointer inside raw that is aligned according to 'alignment'
  AllocatorT<uint8_t> allocator;  // allocator used to get raw memory

  public:

  // simpple constructor. for the total number of elements to hold.
  // We suport allocator rebinding
  template <typename U> 
  RawMemory(size_t nElementsTotal_in, const AllocatorT<U> &allocator_in = AllocatorT<uint8_t>()) 
  : raw(nullptr), rawSize(0), nElementsTotal(nElementsTotal_in), rawAligned(nullptr), allocator(allocator_in)
  {}

  // function to get an aligned pointer
  uint8_t* Allocate(size_t sizeT, size_t alignT)
  {

    // use the provided allocator to get the memory that will serve as our pool
    size_t nBytesTotal = sizeT*nElementsTotal + alignof(std::max_align_t);
    try
    {
      raw = allocator.allocate(nBytesTotal); // std::allocator::allocate() MUST return a pointer to valid memory per the standard, else it throws
    }
    catch (const std::bad_alloc &e)
    {
      std::printf("RAWMEMORY - ATTEMPTED TO ALLOCATE %llu OF MEMORY, BUT THREW std::bad_alloc\n", (long long unsigned int)nBytesTotal);
    }

    if (nullptr == raw)
    {
      // throw std::bad_alloc();
      rawSize = 0;
      return nullptr;
    }
    else
      rawSize = nBytesTotal;

    size_t rawSizeAfterAlignment = rawSize;
    void* rawAligned_ = raw; // needs a type of void*&.. seems like bad function design to me, but what do i know
    rawAligned = (uint8_t*)std::align(alignT, nElementsTotal*sizeT, rawAligned_, rawSizeAfterAlignment);
    if (nullptr == rawAligned || rawSizeAfterAlignment < sizeT*nElementsTotal)
    {
      // throw std::bad_alloc();
      return nullptr;
    }
    
    return rawAligned;
  }

  // destructor will free memory
  ~RawMemory()
  {
    if (raw)
    {
      allocator.deallocate(raw, rawSize);
      rawSize = 0;
    }
  }
};




//
// The premiere mempool utility. This allocates memory for an array 
// objects of a given size and alignment. It then records pointers into that
// memory that it can hand out. This is intended to be used as the shared 
// internal state for a mempool allocator
//
template <template<typename> class AllocatorT = std::allocator>
class AllocatorState
{
  typedef std::mutex MutexT;
  typedef std::lock_guard<MutexT> GuardT;

  AllocatorT<uint8_t> allocator;

  MutexT mutex;
  RawMemory<AllocatorT> memoryRaw;
  RawMemory<AllocatorT> memoryPtrsToRaw;
  uint8_t* rawAligned;
  uint8_t** ptrsAligned;
  size_t nElementsTotal;         // number of elements in pool (element is a whole array in when in array mode)
  size_t nAvailable;             // number of elements currently loaned out
  size_t historic_min_available; // lowest number of elements available in history of the pool
  bool initialized;              // pool is allocated and setup
  
  size_t sizeSharedT;            // total size of each block in this pool
  size_t alignSharedT;           // alignment of each block in this pool
  std::function<void(size_t)> destructorCallback;

  public:

  // constructor for a mempool of size nElements_in. Use / rebind a given allocator
  // this constructor does not actually allocate the pool
  template <typename U>
  AllocatorState(size_t nElements_in, const AllocatorT<U>& allocator_in = AllocatorT<uint8_t>())
    : allocator(allocator_in), 
    memoryRaw(nElements_in, allocator),
    memoryPtrsToRaw(nElements_in, allocator), 
    rawAligned(nullptr),
    ptrsAligned(nullptr),
    nElementsTotal(nElements_in), nAvailable(nElements_in),
    historic_min_available(nElements_in),
    initialized(false),
    sizeSharedT(0),
    alignSharedT(0), 
    destructorCallback(nullptr)
  {}

  // frees all memory
  ~AllocatorState()
  {
    // memoryRaw and momoryPtrsToRaw are destroyed
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

    rawAligned = (uint8_t*)memoryRaw.Allocate(sizeSharedT, alignSharedT);
    ptrsAligned = (uint8_t**)memoryPtrsToRaw.Allocate(sizeof(uint8_t*), alignof(uint8_t*));


    // get the pointers into the raw pool
    if (rawAligned && ptrsAligned)
    {
      for (size_t i = 0; i < nElementsTotal; ++i)
        ptrsAligned[i] = rawAligned + (sizeSharedT * i); // TODO CHECK ALIGNMENT
      initialized = true;
    }
    else
    {
      nAvailable = 0;
      initialized = false;
    }
    
  }

  // hand out a pointer to the pool as if to allocate
  void* Loan()
  {
    GuardT guard(mutex);

    if (nAvailable > 0)
    {
      --nAvailable;
      historic_min_available = std::min(historic_min_available, nAvailable); // record this point if its a new low in our history
      return ptrsAligned[nAvailable];
    }
    else
    {
      // std::cout << "ALLOCATORSTATE - RAN OUT OF ELEMENTS TO LOAN. THE POOL IS TOO SMALL.\n";
      throw std::bad_alloc(); // this MUST happen
      // return nullptr;
    }
  }

  // take a pointer back as if to deallocate that memory
  // call the destructor with the appropriate elementIndex
  void Accept(uint8_t* p) noexcept
  {
    // check for null, just in case someone deallocated a pointer without checking their allocation
    if (nullptr == p)
      return;

    // This is deliberately before the guard. Whatever this destructor does, 
    // it shouldnt hold up this mempool from operating
    if (destructorCallback)
    {
      size_t elementIndex = GetElementNumber((uint8_t*)p);
      destructorCallback(elementIndex);
    }
   
    GuardT guard(mutex);

    ptrsAligned[nAvailable++] = p; // TODO wouldnt hurt to check if this is a valid address for this pool
  }

  // set the destructor that is fired for each Accept()
  void SetDestructor(const std::function<void(size_t)> &func)
  {
    GuardT guard(mutex); // guard just in case. 
    destructorCallback = func;
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

  size_t Capacity()
  {
    return nElementsTotal;
  }

  // get number of currently loaned out elements and number of available elements
  void Statistics(size_t &nOutstanding_out, size_t &nAvailable_out, size_t &historic_min_available_out)
  {
    GuardT guard(mutex);
    nAvailable_out = nAvailable;
    nOutstanding_out = nElementsTotal - nAvailable;
    historic_min_available_out = historic_min_available;
  }

  // given a pointer to SOMEWHERE in a block of memory, figure out which block it belongs to and return that index
  size_t GetElementNumber(uint8_t* ptr)
  {
    return (ptr - rawAligned) / sizeSharedT; // integer division intended; it rounds down here which is what we want
  }
};



//
// An allocator that wraps a shared_ptr to an AllocatorState.  This is mostly compliant to the C++ Allocator Requirements. 
// This should support everything we need for basic use, but this allocator can only deallocate elements that /it/ allocated.
// This works because std::make_shared and std::allocate_shared (along with everything else that use allocators) make a copy
// of the allocator, and that copy lives with the object. This allocator is little more than a wrapper and a shared pointer, 
// so that shared_ptr gets copied everywhere this allocator goes. it's pretty straight forward in that respect.
//
// What is NOT straight forward is how I size the memory pools at initialization time. Because shared_ptr's have a control block
// that manages the resource count and object pointer (and other things i imagine), I need to know how big that control block is.
// To complicate things, std::make_shared and std::allocate_shared rebind the provided allocator to the type of a struct with 
// 2 members: a control_block and an object T (bascially), and then they do a single allocation of the size that fits both of 
// those things. What's even more painful is that the control block is not a publically exposed type, and it's compiler dependent. 
//
// To get around this, At the time of construction, I create /another/ SharedPointerAllocator (with mempool size of 1), and I call
// std::allocate_shared which calls the class's copy constructor. Inside that copy constructor, i check the size and alignment of that
// now combined type. I dynamically allocate enough room to make std::allocate_shared happy, and then that's it. I know the size and 
// alignment of that type, and i can use that information to size the mempool inside *this and finally finish construction. I know
// this isnt ideal, but i really dont think there's a better way. Furthermore, You have to actually construct an oject just to throw
// it away, so i provided a variadic parameter pack in case your object doesnt have a default constructor.
//
// This behavior is written this way because a copy of the allocator is made for each shared pointer control block, so things this way
// makes it very difficult to mis-detect the size/alignment of the total {control_block, T, allocator} object. We are literally 
// getting the size and alignment of /exactly/ what we we need an array of, and any future class member additions will automatically
// be included in the size/alignment detection (future proof)
//
template <class T, bool verbose = false, template<typename> class AllocatorT = std::allocator>
class SharedPointerAllocator
{

public:
  typedef std::size_t size_type;
  typedef std::ptrdiff_t difference_type;
  typedef T* pointer;
  typedef const T* const_pointer;
  typedef T& reference;
  typedef const T& const_reference;
  typedef T value_type;
  typedef std::shared_ptr<T>  sPtrT;
  
  typedef AllocatorState<AllocatorT> StateT; //convenience

  // this should really be the only member in this class, since allocators get copied everywhere they go
  std::shared_ptr<StateT> state;

  private:

  // this constructor is private and will only be called by the other constructor to create a temporary object
  // to figure out size/alignment
  template <typename U, typename ... Args>
  SharedPointerAllocator(const AllocatorT<U> &allocator, Args&&... args)
    : state(std::allocate_shared<StateT>(AllocatorT<StateT>(allocator), 1, allocator))
  {
    
    static_assert(std::is_default_constructible<U>::value, "ERROR IPC::SharedPointerAllocator REQUIRES TYPES TO BE DEFAULT CONSTRUCTIBLE DUE TO THE WAY IT DETERMINES SIZE AND ALIGNMENT INFO FOR THE SHARED POINTER CONTROL BLOCK");

    if (!state)
    {
      // throw std::bad_alloc();    
      std::printf("ALLOCATOR - SOMETHING WEIRD HAS HAPPENED. THE POOL STATE IS NULL\n");
    }  
  }

  public:

  // default constructor just in case
  SharedPointerAllocator() {}

  // any "copy" of this SharedPointerAllocator is a shallow copy of the shared state
  template <class U, template<typename> class AllocatorU, bool VerboseU> 
  SharedPointerAllocator(SharedPointerAllocator<U, VerboseU, AllocatorU> const& other) noexcept 
  : state(other.state)
  {
    if (nullptr == state)
      std::printf("ALLOCATOR - SOMETHING WEIRD HAS HAPPENED. THE POOL STATE IS NULL\n");
    
    if (state->Initialized())
    {
      if (state->TotalElementSize() < sizeof(T))
      {
        std::printf("ALLOCATOR - SOMETHING WEIRD HAS HAPPENED. THE POOL IS MISS-SIZED. TOTAL ELEMENT SIZE = %zu AND SIZEOF(T)= %zu\n", state ? state->TotalElementSize() : 0, sizeof(T));
        // throw std::bad_alloc(); // cannot throw, noexcept
        state = nullptr;
      }
    }
    else
    {
      state->Allocate(sizeof(T), alignof(T));
    }
  }

  template <typename U=T, typename ... Args>
  SharedPointerAllocator(size_t nElements, const AllocatorT<U> &allocator = AllocatorT<U>(), Args&&... args)
    : state(std::allocate_shared<StateT>(AllocatorT<StateT>(allocator), nElements, allocator))
  {
    if (!state)
    {
      std::printf("SharedPointerAllocator - SOMETHING WEIRD HAS HAPPENED. THE POOL STATE IS NULL\n");
      // throw std::bad_alloc();
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
    SharedPointerAllocator<T, verbose, AllocatorT> oneOffAllocator(allocator, std::forward<Args>(args)...);
    std::allocate_shared<T>(oneOffAllocator, std::forward<Args>(args)...); // throw away object. TODO what if this errors out?

    // std::cout << "control block size and alignment detected\n";

    // now allocate the memory we need, now that we know the proper size
    size_t sizeSharedT;
    size_t alignSharedT;
    oneOffAllocator.state->GetSizeAndAlignment(sizeSharedT, alignSharedT);
    state->Allocate(sizeSharedT, alignSharedT);

    if (state->Initialized())
    {
      if (verbose)
      {
        std::printf("Constructing Allocator<T> with sizeof(T)=%zu but an extra %zu bytes are needed for the shared_ptr's control block. Total size = %zu, total alignment = %zu\n", sizeof(T), sizeSharedT - sizeof(T), sizeSharedT, alignSharedT);
      }
    }
    else
    {
      if (verbose)
      {
        std::printf("SharedPointerAllocator - SOMETHING WEIRD HAS HAPPENED. THE POOL STATE COULD NOT INITIALIZE\n");
      }
      state = nullptr;
    }
  }

  // rebind function. extremely important. and im not even gonna explain it :P
  template<class U>
  struct rebind {
    typedef SharedPointerAllocator<U, verbose, AllocatorT> other;
  };

  // allocate pointer to element of mempool
  value_type* allocate(std::size_t n)
  {
    if (!state || n > 1) // we can only allocate one element at a time. this is how shared_ptr's work
      return nullptr; // consider throwing

    return (T*)state->Loan();
  }

  // deallocate
  void deallocate(value_type* p, std::size_t) noexcept  // Use pointer if pointer is not a value_type*
  {
    if (state)
      state->Accept((uint8_t*)p);
  }

  // pass through to set the destructor that will be called when deallocating objects.
  // this destructor does nothing for this class alone, but the array version of this class 
  // can set it to destruct non-arithmetic types (non-integral and non floating_point)
  void SetDestructor(const std::function<void(size_t)> &func)
  {
    if (state)
      state->SetDestructor(func);
  }

  // pass through to get number of outstandini and available elements in the pool
  void statistics(size_t &nOutstanding, size_t &nAvailable, size_t &historic_min_available)
  {
    if (state)
      state->Statistics(nOutstanding, nAvailable, historic_min_available);
    else
    {
      nOutstanding = 0;
      nAvailable = 0;
      historic_min_available = 0;
    }
  }

  size_t element_size()
  {
    return state ? state->Capacity() : 0;
  }

  size_t capacity()
  {
    return state ? state->Capacity() : 0;
  }

  template <typename ...Args>
  std::shared_ptr<T> allocate_shared(Args&& ...args)
  {
    if ( ! state )
      return nullptr;

    // this try-catch is required because std::allocate_shared does not check if it got a nullptr.
    // per the standard, an allocator MUST return a pointer to valid memory or throw
    try
    {
      return std::allocate_shared<T>(*this, std::forward<Args>(args)...);
    }
    catch (const std::bad_alloc &e)
    {
      return nullptr;
    }
  }

  // NOTE: since c++11, the destroy() allocator trait is optional and std::allocator_traits<T> falls back to calling p->~T() for you.
  // wasn't that nice of the standards committe.
  // see https://en.cppreference.com/w/cpp/memory/allocator_traits/destroy
  // see https://en.cppreference.com/w/cpp/memory/allocator_traits

  // NOTE:
  // it might be cool to add construct() and destruct() functions to this mempool so users can implement their own
  // utilities for object lifetime analysis, etc
};

// obligatory operators
template <class T, class U, template<typename> class AllocatorT, template<typename> class AllocatorU, bool VerboseT, bool VerboseU>
bool operator==(SharedPointerAllocator<T, VerboseT, AllocatorT> const& x, SharedPointerAllocator<U, VerboseU, AllocatorU> const& y) noexcept
{
    return x.state.get() == y.state.get();
}

// obligatory operators
template <class T, class U, template<typename> class AllocatorT, template<typename> class AllocatorU, bool VerboseT, bool VerboseU>
bool operator!=(SharedPointerAllocator<T, VerboseT, AllocatorT> const& x, SharedPointerAllocator<U, VerboseU, AllocatorU> const& y) noexcept
{
    return !(x == y);
}



//
// A utility that gives you shared pointers to arrays, where both the shared_ptr<T*> 
// and the T[] that the shared_ptr's pointer points to are mempooled. (wow)
// If T is non-arithmetic (not integral or floating_point), then the memory will be 
// constructed (placement-new'd) when you ask for a shared_ptr. 
// That memory will also be destructed (~T()) when that
// shared pointer goes out of scope. The block of memory for T[] is managed via its own
// shared_ptr, and that memory cannot go out of scope until the SharedPointerAllocator<T*> 
// goes out of scope
//
template <class T, bool verbose = false, template<typename> class AllocatorT = std::allocator>
class SharedPointerArrayAllocator// : public SharedPointerAllocator<T*, AllocatorT>
{
  public:

  typedef RawMemory<AllocatorT> RawMemoryT; 

  SharedPointerAllocator<T*, verbose, AllocatorT> sharedPointerAllocator; // mempool of pointers
  std::shared_ptr<RawMemoryT> rawObjects; // memory block of all arrays. scope is kept alive by destructor lambdas
  size_t count; // array size per pool element
  uint8_t* rawAligned;

  // default constructor just in case
  SharedPointerArrayAllocator() : sharedPointerAllocator(), rawObjects(), count(0), rawAligned(nullptr) {}

  // simple constructor 
  template <typename U=T>
  SharedPointerArrayAllocator(size_t nElements, size_t count_in, const AllocatorT<U> &allocator = AllocatorT<U>()) 
  : sharedPointerAllocator(nElements, AllocatorT<T*>(allocator)), 
    rawObjects(std::allocate_shared<RawMemoryT>(AllocatorT<RawMemoryT>(allocator), nElements * count_in, allocator)),
    count(count_in), rawAligned(nullptr)
  {
    if (nullptr == rawObjects)
    {
      // throw std::bad_alloc();
      std::printf("SharedPointerArrayAllocator - SOMETHING WEIRD HAS HAPPENED. THE POOL STATE IS NULL\n");
    }

    // allocate the T[] memory
    rawAligned = rawObjects->Allocate(sizeof(T), alignof(T));

    // set the destructor callback and give it a shared pointer to the T[] data so it cant go out of scope
    auto rawObjectsPtr = rawObjects; // one pool keeps the other in scope
    auto countCopy = count;
    auto rawAlignedCopy = rawAligned;
    auto destructorCallback = [rawObjectsPtr, countCopy, rawAlignedCopy](size_t elementIndex)
    {
        // start and stop elements in the pool
        T* first = (T*)rawAlignedCopy + (elementIndex * countCopy);
        T* last = first + countCopy;

        // only ~T() if T is NOT an arithmetic type
        if (!std::is_arithmetic<T>::value)
        {
            for (T* ptr = first; ptr < last; ++ptr)
                ptr->~T();            
        }
    };

    sharedPointerAllocator.SetDestructor(destructorCallback);
  }

  // 
  // get a shared_ptr<T*> where that shared pointer's object is a pointer to an array of memory. 
  // This function will only construct in place if T is a non-arithmetic type (or forced ??)
  //
  template <typename ...Args>
  std::shared_ptr<T*> allocate_raw(Args&& ...args )
  {
    if (!rawObjects || ! sharedPointerAllocator.state)
      return nullptr; // could throw too

    T* tempPtr = nullptr;    
    auto ret = sharedPointerAllocator.allocate_shared(tempPtr); // constructed this way so that this shared_ptr owns an object instead of just being null
    if (! ret)
      return nullptr;

    size_t index = sharedPointerAllocator.state->GetElementNumber((uint8_t*)ret.get());
    //std::cout << "constructing at index: " << index << std::endl;

    // start and stop positions in the pool
    T* first = (T*)rawAligned + (index * count);
    T* last = first + count;

    // now point this shared pointer to the actual memory it should be pointing to
    *ret = first;

    // only placement new if T is an integral type
    if (!std::is_arithmetic<T>::value)
    {
      // iterate over each element in this block and construct in place an element of type T with perfect forwarding
      for (T* ptr = first; ptr < last; ++ptr)
        new(ptr) T(std::forward<Args>(args)... );
    }

    return ret;
  }

  // pass through to get number of outstanding and available elements in the pool
  void Statistics(size_t &nOutstanding, size_t &nAvailable, size_t &historic_min_available)
  {
    sharedPointerAllocator.statistics(nOutstanding, nAvailable, historic_min_available);
  }

};




// a simple allocator that wraps std::allocator and prints when it 
// allocates and deletes memory. really nothing else to it. this was helpful for debugging. 
// other people may find it useful
// TODO i should really use rebinding here, and in the rest of this code
template <class T>
class Passthrough_Allocator
{

public:
  typedef std::size_t size_type;
  typedef std::ptrdiff_t difference_type;
  typedef T* pointer;
  typedef const T* const_pointer;
  typedef T& reference;
  typedef const T& const_reference;
  typedef T value_type;
  
  std::allocator<T> allocator;


  Passthrough_Allocator() : allocator(std::allocator<T>())
  {
    // std::printf("PASSTHROUGH ALLOCATOR - default constructed\n");
  }

  template <class U> 
  Passthrough_Allocator(Passthrough_Allocator<U> const& other) noexcept 
  : allocator(other.allocator)
  {
    // std::printf("PASSTHROUGH ALLOCATOR - copy constructed\n");
  }

  template<class U>
  struct rebind {
    typedef Passthrough_Allocator<U> other;
  };

  value_type* allocate(std::size_t n)
  {
    std::printf("PASSTHROUGH ALLOCATOR - allocated %zu elements of %zu bytes each for a total of %zu bytes\n", n, sizeof(T), n*sizeof(T));
    value_type* raw = allocator.allocate(n);
    if (nullptr == raw)
    {
      // throw std::bad_alloc();
      std::printf("SharedPointerArrayAllocator - SOMETHING WEIRD HAS HAPPENED. COULD NOT ALLOCATE\n");
    }

    return raw;
  }

  void deallocate(value_type* p, std::size_t n) noexcept  // Use pointer if pointer is not a value_type*
  {
    std::printf("PASSTHROUGH ALLOCATOR - deallocated %zu elements of %zu bytes each for a total of %zu bytes\n", n, sizeof(T), n*sizeof(T));
    allocator.deallocate(p, n);
  }
};

template <class T, class U>
bool operator==(Passthrough_Allocator<T> const& x, 
                Passthrough_Allocator<U> const& y) noexcept
{
  return true;
}

template <class T, class U>
bool operator!=(Passthrough_Allocator<T> const& x, 
                Passthrough_Allocator<U> const& y) noexcept
{
  return !(x == y);
}


} // namespace


#endif