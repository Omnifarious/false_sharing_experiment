#include <cstdint>
#include <atomic>
#include <memory>
#include <barrier>
#include <thread>
#include <chrono>
#include <fmt/core.h>

using test_t = ::std::atomic<::std::uint64_t>;
using hrt_time_t = ::std::chrono::high_resolution_clock::time_point;
using time_result_t = ::std::chrono::duration<double>;  // Seconds as double.

class save_times {
 public:
   save_times(hrt_time_t &start, hrt_time_t &finish) :
        start_(start), finish_(finish)
   {}
   void operator()() {
      if (!started_) {
         start_ = ::std::chrono::high_resolution_clock::now();
         started_ = true;
      } else {
         finish_ = ::std::chrono::high_resolution_clock::now();
      }
   }

 private:
   hrt_time_t &start_;
   hrt_time_t &finish_;
   bool started_ = false;
};
using benchmark_barrier = ::std::barrier<save_times>;

void count_non_atomic(
     test_t::value_type volatile &counter,
     test_t::value_type const count_limit
)
{
   // Complicated expressions involving possible multiple accesses to a
   // volatile have been deprecated, so this is the replacement for counter++
   auto add_one_to_counter = [&counter]() {
      auto tmp = counter;
      counter = tmp + 1;
      return tmp;
   };
   while (add_one_to_counter() < count_limit)
      ;
}

void count_atomic(test_t &counter, test_t::value_type const count_limit)
{
   while (counter++ < count_limit)
      ;
}

void count_thread(test_t &counter, const test_t::value_type count_limit, benchmark_barrier &latch)
{
   counter = 0;
   latch.arrive_and_wait();
   count_atomic(counter, count_limit);
   latch.arrive_and_wait();
}

auto find_appropriate_limit()
{
   using ::fmt::print;
   print("Calculating what count will give worthwhile results on your CPU.\n");
   using namespace ::std::literals::chrono_literals;
   using ::std::chrono::duration;
   bool found = false;
   test_t::value_type current_estimate = 1U << 16;
   duration<double> non_atomic_seconds;
   while (!found) {
      hrt_time_t start, finish;
      save_times time_saver{start, finish};
      test_t::value_type counter = 0;
      time_saver();
      count_non_atomic(counter, current_estimate);
      time_saver();
      auto const interval = finish - start;
      non_atomic_seconds = interval;
      print("Count: {} took {:.4f} seconds for ordinary counting.\n",
            current_estimate, non_atomic_seconds.count());
      if (interval < 5ms) {
         if (interval < 1ms) {
            current_estimate *= 100;
         } else {
            current_estimate *= 2;
         }
      } else if (interval > (200ms + 2ms) || interval < (200ms - 2ms)) {
         duration<double> const double_200ms = 200ms;
         auto factor = double_200ms / interval;
         current_estimate = factor * current_estimate;
      } else {
         found = true;
      }
   }
   struct {
      test_t::value_type count_limit;
      time_result_t count_duration;
   } retval = {current_estimate, non_atomic_seconds};
   return retval;
}

time_result_t test_single_thread_atomic(test_t::value_type const count_limit)
{
   using ::fmt::print;
   using ::std::chrono::duration;
   hrt_time_t start, finish;
   save_times time_saver{start, finish};
   test_t counter = 0;
   time_saver();
   count_atomic(counter, count_limit);
   time_saver();
   auto const interval = finish - start;
   duration<double> const interval_in_seconds = interval;
   print("Count: {} took {:.4f} seconds for single-thread atomic counting.\n",
         count_limit, interval_in_seconds.count());
   return interval_in_seconds;
}

time_result_t test_cooperating_threads_same_counter(test_t::value_type const count_limit)
{
   using ::fmt::print;
   test_t counter = 0;
   hrt_time_t start, finish;
   benchmark_barrier timesaver{2, save_times{start, finish}};
   print("\nTesting two threads cooperating on the same count.\n");
   {
      using ::std::ref;
      ::std::jthread t{count_thread, ref(counter), count_limit, ref(timesaver)};
      count_thread(ref(counter), count_limit, timesaver);
   }
   auto interval = finish - start;
   ::std::chrono::duration<double> interval_in_seconds = interval;
   print("Count took {:.4f} seconds to finish.\n", interval_in_seconds.count());
   return interval_in_seconds;
}

int main()
{
   using ::fmt::print;
   auto const [count_limit, normal_time] = find_appropriate_limit();
   auto const atomic_time = test_single_thread_atomic(count_limit);
   print("Atomic is {:.2f} times slow than non-atomic.\n",
         atomic_time / normal_time);
   test_cooperating_threads_same_counter(count_limit);
}
