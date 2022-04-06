#include "thread_pool.hpp"

int main () {
    thread_pool pool (10);
    uint32_t squares[100];
    pool.parallelize_loop (0, 100, [&squares] (const uint32_t& a, const uint32_t& b) {
        for (uint32_t i = a; i < b; i++) squares[i] = i * i;
    });
    std::cout << "16^2 = " << squares[16] << '\n';
    std::cout << "32^2 = " << squares[32] << '\n';
    return 0;
}