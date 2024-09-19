#include <cstdint>
#include <atomic>
#include <memory>
#include <array>
#include <barrier>
#include <thread>
#include <chrono>
#include <iostream>

using test_t = ::std::atomic<::std::uint64_t>;

alignas(64) ::std::array<test_t, 128> counters;
using hrt_time_t = ::std::chrono::high_resolution_clock::time_point;


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

test_t::value_type find_appropriate_limit()
{
   ::std::cout << "Calculating what count will give worthwhile results on "
                  "your CPU.\n";
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
      ::std::cout << "Count: " << current_estimate
                  << " took " << non_atomic_seconds.count()
                  << " seconds for ordinary counting.\n";
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
   {
      hrt_time_t start, finish;
      save_times time_saver{start, finish};
      test_t counter = 0;
      time_saver();
      count_atomic(counter, current_estimate);
      time_saver();
      auto const interval = finish - start;
      duration<double> const interval_in_seconds = interval;
      ::std::cout << "Count: " << current_estimate
                  << " took " << interval_in_seconds.count()
                  << " seconds for single-thread atomic counting.\n";
      ::std::cout << "Atomic is " << interval_in_seconds / non_atomic_seconds
                  << " times slower than non-atomic.\n";
   }
   return current_estimate;
}

void test_cooperating_threads_same_counter(test_t::value_type const count_limit)
{
   test_t counter = 0;
   hrt_time_t start, finish;
   benchmark_barrier timesaver{2, save_times{start, finish}};
   ::std::cout << "\nTesting two threads cooperating on the same count.\n";
   {
      using ::std::ref;
      ::std::jthread t{count_thread, ref(counter), count_limit, ref(timesaver)};
      count_thread(ref(counter), count_limit, timesaver);
   }
   auto interval = finish - start;
   ::std::chrono::duration<double> interval_in_seconds = interval;
   ::std::cout << "Count took " << interval_in_seconds.count()
               << " seconds to finish.\n";
}

int main()
{
   auto const count_limit = find_appropriate_limit();
   test_cooperating_threads_same_counter(count_limit);
}
