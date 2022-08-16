// Keith Rausch

#ifndef BEASTWEBSERVERFLEXIBLE_RATE_LIMITER_HPP
#define BEASTWEBSERVERFLEXIBLE_RATE_LIMITER_HPP

#include <atomic>
#include <memory>
#include <vector>
#include <shared_mutex>


namespace BeastNetworking
{


namespace RateLimiting
{


    template <typename T>
    struct CopyableAtomic
    {
        std::atomic<T> atomic;

        CopyableAtomic() : atomic(T())
        {}

        CopyableAtomic(const CopyableAtomic &other) : atomic(other.atomic.load())
        {}

        CopyableAtomic(const T &other) : atomic(other)
        {}

        CopyableAtomic<T> operator=( const CopyableAtomic &other )
        {
            atomic = other.atomic.load();
            return *this;
        }

        T operator=( const T &other )
        {
            atomic = other;
            return other;
        }

        T load()
        {
            return atomic.load();
        }
    };

    class RateTracker;

    class RateEnforcer
    {

        public:

        struct Args
        {
            double alpha = 0.1; // exponential moving average. alpha is weighting of newest measurement. 1-alpha is weighting of previous knowledge
            // bool included_in_total_rate = true;
            bool limitable = true;
            double rate_recalculation_trigger = 1.1; // if the current byte rate is this multiple above the allowable rate, reset the byte rate back to 0
        };

        private:

        class Timer
        {
            typedef std::chrono::steady_clock Clock;
            typedef std::chrono::duration<double, std::ratio<1>> Second;
            std::chrono::time_point<Clock> start;

        public:
            Timer() : start(Clock::now()) {}
            void reset() { start = Clock::now(); }
            double elapsed() const
            {
                return std::chrono::duration_cast<Second>(Clock::now() - start).count();
            }

            static double since_epoch()
            {
                using namespace std::chrono;
                return duration_cast<milliseconds>(Clock::now().time_since_epoch()).count() / 1000.0;
            }
        };

        private:
        Timer unlimited_timer;
        double unlimited_rate;
        CopyableAtomic<size_t> allowed_rate;
        Timer actual_timer;
        double actual_rate;
        std::shared_ptr<RateTracker> rate_tracker;
        Args args;
        size_t id;

        friend class RateTracker;

        RateEnforcer( const std::shared_ptr<RateTracker> & control_block_in, const Args &args_in, size_t id_in);

        public:

        RateEnforcer();
        RateEnforcer(RateEnforcer&& rate_enforcer) = default;
        ~RateEnforcer();
        void update_hypothetical_rate(size_t nBytes);
        bool should_send(size_t nBytes);
    };

    class RateTracker : public std::enable_shared_from_this<RateTracker>
    {
        friend class RateEnforcer;

        std::vector<CopyableAtomic<size_t>> true_rates;
        std::shared_mutex shared_mutex;
        std::atomic<double> max_byterate;

        size_t update_and_get_allowable(size_t new_rate, size_t id );
        void unregister(size_t id);

        public:

        RateTracker();
        RateTracker(double max_byterate);
        void set_max_byterate(double byterate);
        double get_max_byterate();

        RateEnforcer make_enforcer( const RateEnforcer::Args &args);
    };

    typedef std::shared_ptr<RateLimiting::RateTracker> sRateTracker;




} // namespace
} // namespace


#endif