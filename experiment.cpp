#include <atomic>
#include <cstdint>
#include <tuple>
#include <memory>
#include <array>
#include <barrier>
#include <thread>
#include <chrono>
#include <iostream>

using test_t = ::std::atomic<::std::uint32_t>;
const test_t::value_type count_limit = 429490176U; // 2**32 - 2**16

::std::array<test_t, 128> counters;
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

void count_thread(test_t &counter, benchmark_barrier &latch)
{
    latch.arrive_and_wait();
    while (++counter < count_limit)
       ;
    latch.arrive_and_wait();
}

int main()
{
    hrt_time_t start, finish;
    benchmark_barrier timesaver{1, save_times{start, finish}};   
    test_t counter = 0;
    count_thread(counter, timesaver);
    auto interval = finish - start;
    ::std::chrono::duration<double> interval_in_seconds = interval;
    ::std::cout << "Count took " << interval_in_seconds << " to finish.\n";
}
