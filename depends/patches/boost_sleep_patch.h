// boost_sleep_patch.h
#ifndef BOOST_SLEEP_PATCH_H
#define BOOST_SLEEP_PATCH_H

// Define a replacement for Boost sleep implementation if it's not available
#include <thread>
namespace boost {
    namespace this_thread {
        inline void sleep_for(std::chrono::milliseconds ms) {
            std::this_thread::sleep_for(ms);
        }
        inline void sleep_until(std::chrono::steady_clock::time_point time) {
            std::this_thread::sleep_until(time);
        }
    }
}
#endif // BOOST_SLEEP_PATCH_H
