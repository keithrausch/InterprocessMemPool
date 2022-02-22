// Keith Rausch

#include "rate_limiter.hpp"
#include <iostream>

namespace BeastNetworking
{
  namespace RateLimiting
  {

    RateTracker::RateTracker() : max_byterate(std::numeric_limits<double>::max())
    {}

    RateTracker::RateTracker(double max_byterate_in) : max_byterate(max_byterate_in)
    {}

    void RateTracker::set_max_byterate(double max_byterate_in)
    {
      max_byterate = max_byterate_in < 0 ? std::numeric_limits<double>::max() : max_byterate_in;
    }

    double RateTracker::get_max_byterate()
    {
      return max_byterate;
    }


    size_t RateTracker::update_and_get_allowable(size_t new_rate, size_t id )
    {
      double total = 0;

      { // lock guard scope
        std::shared_lock<std::shared_mutex> lock(shared_mutex);

        if (id < true_rates.size())
        {
          true_rates[id] = new_rate;
        }

        for (auto& rate : true_rates)
        {
          total += rate.load();
        }
      }

      return (new_rate / total) * std::min(total, max_byterate.load());
    }
    
    void RateTracker::unregister(size_t id)
    {
      { // lock guard scope
        std::lock_guard<std::shared_mutex> lock(shared_mutex);

        if (id < true_rates.size())
        {
          true_rates[id] = true_rates.back().load();
          true_rates.pop_back();
        }
      }
    }

    RateEnforcer RateTracker::make_enforcer(const RateEnforcer::Args &args)
    {
        std::shared_ptr<RateTracker> this_block = shared_from_this();

        size_t id = 0;

        { // lock guard scope
          std::lock_guard<std::shared_mutex> lock(shared_mutex);

          id = true_rates.size();
          true_rates.emplace_back(0);
        }

        return RateEnforcer(this_block, args, id);
    }

    RateEnforcer::RateEnforcer() : unlimited_rate(0), allowed_rate(0), actual_rate(0), id(0)
    {}

    RateEnforcer::RateEnforcer( const std::shared_ptr<RateTracker> & rate_tracker_in, const Args &args_in, size_t id_in) : unlimited_rate(0), allowed_rate(0), actual_rate(0), rate_tracker(rate_tracker_in), args(args_in), id(id_in)
    {}

    RateEnforcer::~RateEnforcer()
    {
      if (rate_tracker)
      {
        rate_tracker->unregister(id);
      }
    }

    void RateEnforcer::update_hypothetical_rate(size_t nBytes)
    {
      double last_unlimited_send = unlimited_timer.elapsed(); // seconds since last hypothetical send
      double instantaneous_unlimited_rate = nBytes / ( last_unlimited_send + std::numeric_limits<double>::epsilon());
      unlimited_timer.reset();

      // this COULD be a race condition is accessed in a multi threaded context but thats unlikely
      unlimited_rate = unlimited_rate * (1.0-args.alpha) + args.alpha * instantaneous_unlimited_rate;

      if (rate_tracker)
      {
        allowed_rate = rate_tracker->update_and_get_allowable(unlimited_rate, id);
        // std::cout << "allowed_rate = "+std::to_string(allowed_rate)+", unlimited_rate = "+std::to_string(unlimited_rate)+"\n";
      }
    }

    bool RateEnforcer::should_send(size_t nBytes)
    {
      bool may_send = true; // default in case we return early

      if ( ! rate_tracker)
      {
        may_send = true;
        return may_send;
      }

      double last_actual_send = actual_timer.elapsed(); // seconds since last actual send
      double instantaneous_actual_rate = nBytes / ( last_actual_send + std::numeric_limits<double>::epsilon());

      double thresh = allowed_rate.load(); // snap the atomic

      if (actual_rate > thresh * args.rate_recalculation_trigger) // the allowed rate has suddenly dropped, lets re compute our moving average
      {
        actual_rate = 0;
      }

      double temp_actual_rate = actual_rate * (1.0-args.alpha) + args.alpha * instantaneous_actual_rate;


      // get permissible rate
      may_send = (temp_actual_rate <= thresh) || (!args.limitable);

      if (may_send)
      {
        actual_timer.reset();
        actual_rate = temp_actual_rate;
      }

      // std::cout << "actual_rate = " + std::to_string(actual_rate) + " last send time = " + std::to_string(last_actual_send)+ "\n";

      return may_send;
    }


} // namespace
} // namespace
