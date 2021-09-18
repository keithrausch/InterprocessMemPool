#ifndef UTILS_IPC_H
#define UTILS_IPC_H

#include <iostream>

#include <boost/interprocess/managed_shared_memory.hpp>
#include <boost/interprocess/sync/interprocess_mutex.hpp>
#include <boost/interprocess/sync/scoped_lock.hpp>

#include <random>                          // random number gen
#include <boost/filesystem/operations.hpp> // ::remove()

// TODO send error messages to std::cerr

namespace utils_ipc
{

  namespace ipc = boost::interprocess; // convenience

  // wrapped these functions in the own namespace in an attempt to
  // make them private. not sure if this is a good idea or not. i like
  // giving the user options.
  namespace
  {
    static std::string GetObjectMutexName(const std::string &name)
    {
      return "__" + name + "_object_mutex__";
    }
    static std::string GetObjectCounterName(const std::string &name)
    {
      return "__" + name + "_object_couter__";
    }
    static std::string GetObjectName(const std::string &name)
    {
      return "__" + name + "_object__";
    }
    static std::string GetMappingMutexName(const std::string &name)
    {
      return "__" + name + "_mapping_mutex__";
    }
    static std::string GetMappingCounterName(const std::string &name)
    {
      return "__" + name + "_mapping_count__";
    }
    static std::string GetMappingName(const std::string &name)
    {
      return "__" + name + "_mapping__";
    }

    //
    // destroy an object by name in a shared segment
    //
    template <typename T>
    static void destroy_shared_object(const std::shared_ptr<ipc::managed_shared_memory> &segmentPtr, const std::string &name)
    {
      if (!segmentPtr)
        return;

      std::string name_mutex = GetObjectMutexName(name);
      std::string name_counter = GetObjectCounterName(name);
      std::string name_object = GetObjectName(name);

      // find_or_create the mutex (should just find it)
      auto mutexPtr = segmentPtr->find_or_construct<ipc::interprocess_mutex>(name_mutex.c_str(), std::nothrow)();
      if (!mutexPtr)
      {
        std::printf("SOMETHING HAS GONE CRITICALLY WRONG IN LOOKING FOR \"%s\" TO DESTROY \"%s\"\n", name_mutex.c_str(), name.c_str());
        return;
      }
      auto &mutex = *mutexPtr;

      // lock the mutex
      ipc::scoped_lock<ipc::interprocess_mutex> guard(mutex);

      // find or create the reference counter (should just find it)
      auto counterPtr = segmentPtr->find_or_construct<std::size_t>(name_counter.c_str(), std::nothrow)(0);
      if (!counterPtr)
      {
        std::printf("SOMETHING HAS GONE CRITICALLY WRONG IN LOOKING FOR \"%s\" TO DESTROY \"%s\"\n", name_counter.c_str(), name.c_str());
        return;
      }
      auto &counter = *counterPtr;

      // decrement the reference counter
      auto counterOld = counter;
      if (counter > 0)
        --counter;

      // destroy the object if appropriate
      if (1 == counterOld)
      {
        std::printf("\"%s\" reference count was %zu, now %zu. DELETING...\n", name.c_str(), counterOld, counter);

        // try-catch JUST IN CASE. guarantees stack unwinding.
        try
        {
          segmentPtr->destroy<T>(name_object.c_str());
        }
        catch (const std::exception &e)
        {
          std::printf("ERROR WHILE DESTROYING OBJECT \"%s\". Error is: %s\n", name_object.c_str(), e.what());
        }
      }
      else
        std::printf("\"%s\" reference count was %zu, now %zu - not deleting.\n", name.c_str(), counterOld, counter);
    }

    //
    // remove a shared segment by name
    //
    static void remove_mapping(ipc::managed_shared_memory *segmentPtr, const std::string &name, bool &shouldRemove)
    {
      shouldRemove = false;

      if (!segmentPtr)
        return;

      std::string name_mutex = GetMappingMutexName(name);
      std::string name_counter = GetMappingCounterName(name);
      std::string name_mapping = GetMappingName(name);

      // find_or_create the mutex (should just find it)
      auto mutexPtr = segmentPtr->find_or_construct<ipc::interprocess_mutex>(name_mutex.c_str(), std::nothrow)();
      if (!mutexPtr)
        return; // this should never happen
      auto &mutex = *mutexPtr;

      // lock the mutex
      ipc::scoped_lock<ipc::interprocess_mutex> guard(mutex);

      // find or create the reference counter (should just find it)
      auto counterPtr = segmentPtr->find_or_construct<std::size_t>(name_counter.c_str(), std::nothrow)(0);
      if (!counterPtr)
        return; // this should never happen
      auto &counter = *counterPtr;

      // decrement the reference counter
      auto counterOld = counter;
      if (counter > 0)
        --counter;

      // remove the mapping from the OS if appropriate
      shouldRemove = 1 == counterOld;

      std::printf("\"%s\" reference count was %zu, now %zu.\n", name.c_str(), counterOld, counter);
    }

  } // anonymous namespace

  //
  // Creates an object in the mapped region and returns it wrapped in a shared_ptr. Using this function lets
  // us track its use count, so the last reference to that object (among ALL processes) will automatically
  // destroy it. The object's deleter function holds a copy of the segment pointer, so the object will keep
  // the segment in scope. no worries
  //
  template <typename T, typename... Args>
  static std::shared_ptr<T> find_or_create_shared_object(const std::shared_ptr<ipc::managed_shared_memory> &segmentPtr, size_t nProcessMax, const std::string &name, Args &&...args)
  {
    //
    // in the future, when we support variadic labda's, wrap most of this code in a labda and call the SegmentManager::atomic_func()
    // this will let us crate 3 objects atomically... it will also let us do the same in destroy_shared_object(), where we will be able
    // to destroy three objects atomically and not worry about race conditions affecting the object count.
    // the main reason for this is so that we can get the names of the objects not-yet destroyed in the segment
    // before we remove the segment. that's just a nice check.
    //

    if (!segmentPtr)
      return nullptr;

    std::string name_mutex = GetObjectMutexName(name);
    std::string name_counter = GetObjectCounterName(name);
    std::string name_object = GetObjectName(name);

    // find_or_create the mutex
    ipc::interprocess_mutex *mutexPtr = segmentPtr->find_or_construct<ipc::interprocess_mutex>(name_mutex.c_str(), std::nothrow)();
    if (!mutexPtr)
      return nullptr;
    auto &mutex = *mutexPtr;

    // lock the mutex
    ipc::scoped_lock<ipc::interprocess_mutex> guard(mutex);

    // find or create the reference counter
    std::size_t *counterPtr = segmentPtr->find_or_construct<std::size_t>(name_counter.c_str(), std::nothrow)(0);
    if (!counterPtr)
      return nullptr;
    auto &counter = *counterPtr;

    if (counter >= nProcessMax)
      return nullptr;

    // find_or_create the object
    T *objectPtr = nullptr;
    try
    {
      objectPtr = segmentPtr->find_or_construct<T>(name_object.c_str(), std::nothrow)(std::forward<Args>(args)...);
      // std::nothrow returns nullptr if there was not enough memory. if there was an exception during construction, it
      // wont handle that, so we wrap this in a try-catch just in case.

      // increment the reference counter
      ++counter; // should this be inside or outside, idk. if an object failed to construct, did it really construct?
                 // destructors arent called if an error is thrown during construction...
    }
    catch (const std::exception &e)
    {
      objectPtr = nullptr;
      std::printf("ERROR WHILE CONSTRUCTING OBJECT \"%s\". Error is: %s\n", name_object.c_str(), e.what());
    }

    // wrap the object in a shared pointer with a deleter that will handle cleanup
    std::function<void(T *)> deleter = [segmentPtr, name](T *) {
      destroy_shared_object<T>(segmentPtr, name);
    };
    std::shared_ptr<T> shared_ip_object(objectPtr, deleter);

    std::printf("\"%s\" reference count was %zu, now %zu. There are %zu bytes free in the shared segment\n", name.c_str(), objectPtr ? counter - 1 : counter, counter, segmentPtr->get_free_memory());

    return shared_ip_object;
  }

  template <typename T, typename... Args>
  static std::shared_ptr<T> find_or_create_shared_object(const std::shared_ptr<ipc::managed_shared_memory> &segmentPtr, const std::string &name, Args &&...args)
  {
    return find_or_create_shared_object<T>(segmentPtr, std::numeric_limits<size_t>::max(), name, std::forward<Args>(args)...);
  }

  //
  // This function is more of a convenience than a threadsafe guarantee of corruption
  // it is impossible (as far as i can tell) to prevent the race condition of one thread removing (unmapping)
  // the shared memory the moment after another thread opens/creates it. In such a case, remove() will throw
  // BUT the resource will still be removed in name, and only when all handles to it are destroyed will
  // the OS destroy and destroy the mapping itself.
  //
  // see: https://www.boost.org/doc/libs/1_74_0/doc/html/interprocess/sharedmemorybetweenprocesses.html#interprocess.sharedmemorybetweenprocesses.sharedmemory.removing
  //
  // If one or more references to the shared memory object exist when its unlinked, the name will be removed
  // before the function returns, but the removal of the memory object contents will be postponed until all
  // open and map references to the shared memory object have been removed.
  //
  static std::shared_ptr<ipc::managed_shared_memory> open_or_create_mapping(const std::string &name, size_t size)
  {
    std::string name_mutex = GetMappingMutexName(name);
    std::string name_counter = GetMappingCounterName(name);
    std::string name_mapping = GetMappingName(name);

    auto deleter = [name](ipc::managed_shared_memory *ptr) {
      // written this way because it seems like the proper order of operations.
      // 1 - decrement counters
      // 2 - deleter segment manager (the object in this process. typically itll be on the stack anyways)
      // 3 - remove the mapping
      bool shouldRemove;
      remove_mapping(ptr, name, shouldRemove);

      // Uncomment this block in the future when we support c++14. That will let us use variadic lambda's
      // That will be possible when we move 99% of the body of find_or_create_shared_object() into a lambda and call.
      // That will let us atomically create reference counted objects. and the mutex and counter objects associated
      // with them will also be deleted atomically when we move 99% of the body of destroy_shared_object() into a
      // labda to mirror it.
      //
      // auto remainingChecker = [ptr, name]()
      // {
      //     size_t nNamed = ptr->get_num_named_objects();
      //     size_t nUnique = ptr->get_num_unique_objects();
      //     std::printf("There are %zu named and %zu unnamed objects remaining in the segment %s", nNamed, nUnique, name.c_str());
      //
      //     typedef ipc::managed_shared_memory::const_named_iterator const_named_it;
      //     const_named_it named_beg = ptr->named_begin();
      //     const_named_it named_end = ptr->named_end();
      //
      //     typedef ipc::managed_shared_memory::const_unique_iterator const_unique_it;
      //     const_unique_it unique_beg = ptr->unique_begin();
      //     const_unique_it unique_end = ptr->unique_end();
      //
      //     for(; named_beg != named_end; ++named_beg)
      //         std::printf("%s\n", named_beg->name());
      //
      //     for(; unique_beg != unique_end; ++unique_beg)
      //         std::printf("%s\n", unique_beg->name());
      // };
      //
      // if (shouldRemove)
      //     ptr->atomic_func(remainingChecker);

      delete ptr;

      if (shouldRemove)
      {
        std::printf("REMOVING \"%s\"\n", name.c_str());

        std::string name_mapping = GetMappingName(name);
        bool hadError = !ipc::shared_memory_object::remove(name_mapping.c_str());
        if (hadError)
        {
          std::cerr << "GOT ERROR WHILE TRYING TO REMOVE \""
                    << name
                    << "\". "
                       "THIS CAN HAPPEN IF THERE ARE OUTSTANDING HANDLES AT THE TIME OF REMOVE. "
                       "ANOTHER PROCESS MAY HAVE OPENED A HANDLE IN A RACE CONDITION. "
                       "REGARDLESS, THE MAPPING HAS BEEN REMOVED IN NAME AND WILL BE "
                       "FULLY REMOVED/DESTROYED BY THE KERNEL WHEN ALL OPEN REFERENCES "
                       "TO IT ARE CLOSED. THIS MEANS THAT THIS PARTICULAR MAPPING IS "
                       "BASICALLY A GHOST NOW AND CANNOT BE FOUND BY ANYONE LOOKING FOR IT. "
                       "YOU ARE ABOUT TO HAVE A BAD TIME.\n";
        }
      }
      else
      {
        std::printf("NOT REMOVING \"%s\"\n", name.c_str());
      }
    };

    // open or create the mapping first
    // i dont LOVE creating the ipc::managed_shared_memory object like this, but this is the only way
    // we can easily specify a custom deleter for automatic cleanup, so i feel like thats worth it.
    // its also not really any different than what make_shared would do, so what's it matter...
    // i fully recognize that im convincing myself here more than you
    std::shared_ptr<ipc::managed_shared_memory> segmentPtr(new ipc::managed_shared_memory(ipc::open_or_create, name_mapping.c_str(), size), deleter);
    // when you really think about things, the fact that im new-ing a segment_manager here should
    // make your brain hurt, and that is not the kind of thing i would normally say lightly

    if (!segmentPtr)
      return nullptr;

    // find_or_create the mutex
    auto mutexPtr = segmentPtr->find_or_construct<ipc::interprocess_mutex>(name_mutex.c_str(), std::nothrow)();
    if (!mutexPtr)
      return nullptr;
    auto &mutex = *mutexPtr;

    // lock the mutex
    ipc::scoped_lock<ipc::interprocess_mutex> guard(mutex);

    // find or create the reference counter
    auto counterPtr = segmentPtr->find_or_construct<std::size_t>(name_counter.c_str(), std::nothrow)(0);
    if (!counterPtr)
      return nullptr;
    auto &counter = *counterPtr;

    // increment the reference counter
    ++counter;

    std::printf("\"%s\" reference count was %zu, now %zu.\n", name.c_str(), counter - 1, counter);

    return segmentPtr;
  }

  //
  // get a random number to uniquely identify an instance of shared memory across all computers and time. its far from perfect.
  //
  static std::uint_fast64_t GenerateUniqueID()
  {
    // there are much better ways in do this. The different answers here
    // https://stackoverflow.com/questions/24334012/best-way-to-seed-mt19937-64-for-monte-carlo-simulations
    // are pretty good ideas.

    // also interesting
    // https://stackoverflow.com/questions/8500677/what-is-uint-fast32-t-and-why-should-it-be-used-instead-of-the-regular-int-and-u

    std::mt19937_64 prng;
    std::random_device device;
    std::seed_seq seq{device(), device(), device(), device()};
    prng.seed(seq);
    std::uint_fast64_t number = prng();

    return number;
  }

  // This is the nuclear option for removing shared memory segments.
  // ipc::shared_memory_object::remove() fails if there is still a reference to the shared memory.
  // This typically takes the form of an undestructed segment manager... which can happen if a
  // process crashed or didnt shutdown properly
  //
  // the solution (on linux) is to just remove the shared memory segment from /dev/shm. Since posix
  // shared memory segments are basically just "files in disguise" (their words), and since
  // boost::interprocess appears to use /dev/shm, we can just delete the shared memory region as though it were a file
  //
  // NOTE: TODO I have not implemented this function for windows
  //
  // https://stackoverflow.com/questions/56021045/i-cannot-see-the-shared-memory-created-by-the-boostinterprocess-via-shell-comm
  // https://stackoverflow.com/questions/36629713/shm-unlink-from-the-shell
  //
  //
  // unrelated, but i know this will bite me one day:
  // https://superuser.com/questions/1117764/why-are-the-contents-of-dev-shm-is-being-removed-automatically
  static bool RemoveSharedMemoryNuclear(const std::string &name)
  {
    bool hadError = true;

    std::string name_mapping = GetMappingName(name);

#ifdef _WIN32
    std::cout << "utils_ipc::RemoveSharedMemoryNuclear() - attempting to forcibly wipe any existing shared memory, but i havent written this code yet\n";
    hadError = true;
    return hadError;
#endif

#ifdef __linux__
    hadError = false;
    std::string fullPath = "/dev/shm/" + name_mapping;
    // race condition here, but this was a nuclear option anyways
    if (boost::filesystem::exists(fullPath))
    {
      std::printf("utils_ipc::RemoveSharedMemoryNuclear() - found a shared segment by the name of \"%s\". Attempting to forcibly remove it. If there is a process still using this region, it is about to have a bad time.\n", name.c_str());
      hadError = !boost::filesystem::remove(fullPath);
    }
    return hadError;
#endif

    // if we got here, we dont support this OS
    hadError = true;
    return hadError;
  }

} // namespace utils_ipc

#endif