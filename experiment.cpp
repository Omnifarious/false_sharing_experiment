#include <cstdint>
#include <atomic>
#include <memory>
#include <barrier>
#include <thread>
#include <chrono>
#include <array>
#include <fmt/core.h>

using test_t = ::std::atomic<::std::uint64_t>;
using hrt_time_t = ::std::chrono::high_resolution_clock::time_point;
using time_result_t = ::std::chrono::duration<double>;  // Seconds as double.

template <typename T>
constexpr T highest_bit(T val)
{
   T top_bit = 1;
   while (top_bit <= val) {
      top_bit <<= 1;
   }
   return (val == 0) ? 1 : top_bit >> 1;
}

template <::std::size_t Size>
struct alignas(highest_bit(Size) * 8) counter_array : public ::std::array<test_t, Size>
{};

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

void count_thread(
        test_t &counter,
        const test_t::value_type count_limit,
        benchmark_barrier &latch
)
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
            current_estimate *= 128;
         } else {
            current_estimate *= 2;
         }
      } else if (interval > 202ms || interval < 198ms) {
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

time_result_t test_cooperating_threads_same_counter(
        test_t::value_type const count_limit
)
{
   using ::fmt::print;
   test_t counter = 0;
   hrt_time_t start, finish;
   benchmark_barrier timesaver{2, save_times{start, finish}};
   print("\nTesting two threads cooperating on the same count.\n");
   {
      using ::std::ref;
      ::std::jthread t{count_thread, ref(counter), count_limit * 2, ref(timesaver)};
      count_thread(ref(counter), count_limit * 2, timesaver);
   }
   auto interval = finish - start;
   ::std::chrono::duration<double> interval_in_seconds = interval;
   print("Count took {:.4f} seconds to finish.\n", interval_in_seconds.count());
   return interval_in_seconds;
}

time_result_t test_two_threads_adjacent_counters(
        test_t::value_type const count_limit
)
{
   using ::fmt::print;
   // --------------------------------
   //                 ++++++++++++++++
   //                 1111111122222222
   counter_array<2> counters;
   test_t &counter_one = counters[0];
   test_t &counter_two = counters[counters.size() - 1];
   hrt_time_t start, finish;
   benchmark_barrier timesaver{2, save_times{start, finish}};
   print("\nTesting two threads each incrementing to {} on adjacent counters.\n", count_limit);
   {
      void *raw_counter_one_addr = &counter_one;
      void *raw_counter_two_addr = &counter_two;
      print("Address of 1st counter is {}\n", raw_counter_one_addr);
      print("Address of 2nd counter is {}\n", raw_counter_two_addr);
      print(
              "They are {} bytes apart.\n",
              static_cast<char *>(raw_counter_two_addr) - static_cast<char *>(raw_counter_one_addr)
      );
   }
   {
      using ::std::ref;
      ::std::jthread t{count_thread, ref(counter_one), count_limit, ref(timesaver)};
      count_thread(ref(counter_two), count_limit, timesaver);
   }
   auto interval = finish - start;
   ::std::chrono::duration<double> interval_in_seconds = interval;
   print("Count took {:.4f} seconds to finish.\n", interval_in_seconds.count());
   return interval_in_seconds;
}

time_result_t test_two_threads_spaced_counters(
        test_t::value_type const count_limit
)
{
   using ::fmt::print;
   counter_array<64> counters;
   test_t &counter_one = counters[0];
   test_t &counter_two = counters[counters.size() - 1];
   hrt_time_t start, finish;
   benchmark_barrier timesaver{2, save_times{start, finish}};
   print("\nTesting two threads each incrementing to {} on spaced apart counters.\n", count_limit);
   {
      void *raw_counter_one_addr = &counter_one;
      void *raw_counter_two_addr = &counter_two;
      print("Address of 1st counter is {}\n", raw_counter_one_addr);
      print("Address of 2nd counter is {}\n", raw_counter_two_addr);
      print(
              "They are {} bytes apart.\n",
              static_cast<char *>(raw_counter_two_addr) - static_cast<char *>(raw_counter_one_addr)
      );
   }
   {
      using ::std::ref;
      ::std::jthread t{count_thread, ref(counter_one), count_limit, ref(timesaver)};
      count_thread(ref(counter_two), count_limit, timesaver);
   }
   auto interval = finish - start;
   ::std::chrono::duration<double> interval_in_seconds = interval;
   print("Count took {:.4f} seconds to finish.\n", interval_in_seconds.count());
   return interval_in_seconds;
}

auto guess_cache_line_size(
        test_t::value_type const count_limit,
        time_result_t const adjacent_time
)
{
   using ::fmt::print;
   counter_array<128> counters;
   test_t &counter_one = counters[0];
   auto spacing = counters.size() / 2;
   while (spacing > 1) {
      test_t &counter_two = counters[spacing];
      hrt_time_t start, finish;
      benchmark_barrier timesaver{2, save_times{start, finish}};
      print("\nTesting with a spacing of {}\n", spacing);
      {
         using ::std::ref;
         ::std::jthread t{count_thread, ref(counter_one), count_limit, ref(timesaver)};
         count_thread(ref(counter_two), count_limit, timesaver);
      }
      auto interval = finish - start;
      ::std::chrono::duration<double> interval_in_seconds = interval;
      print("Count took {:.4f} seconds to finish.\n", interval_in_seconds.count());
      if (interval_in_seconds / adjacent_time > 0.7) {
         print("Guessing that cache line size is {}\n", spacing * 2 * sizeof(test_t::value_type));
         return spacing * 2;
      } else {
         spacing /= 2;
      }
   }
   print("Maybe your cache line size is less than {}?\n", sizeof(test_t::value_type));
   return decltype(spacing){0};
}

int main()
{
   using ::fmt::print;
   auto const [count_limit, normal_time] = find_appropriate_limit();
   auto const atomic_time = test_single_thread_atomic(count_limit);
   print("Atomic is {:.2f} times slower than non-atomic.\n",
         atomic_time / normal_time);
   test_cooperating_threads_same_counter(count_limit);
   auto adjacent_time = test_two_threads_adjacent_counters(count_limit);
   test_two_threads_spaced_counters(count_limit);
   guess_cache_line_size(count_limit, adjacent_time);
}
