#include <chrono>
#include <iostream>

#include "tbb/tbb.h"

using namespace std;

#include "spoton-tree/sptree.h"

void multithreaded (char** argv) {
    std::cout << "multi threaded:" << std::endl;

    uint64_t n = std::atoll (argv[1]);
    uint64_t* keys = new uint64_t[n];

    // Generate keys
    for (uint64_t i = 0; i < n; i++)
        // dense, sorted
        keys[i] = i + 1;
    if (atoi (argv[2]) == 1)
        // dense, random
        std::random_shuffle (keys, keys + n);
    if (atoi (argv[2]) == 2)
        // "pseudo-sparse" (the most-significant leaf bit gets lost)
        for (uint64_t i = 0; i < n; i++)
            keys[i] = (static_cast<uint64_t> (rand ()) << 30) | static_cast<uint32_t> (rand ());

    printf ("operation,n,ops/s\n");
    spoton::SPTree* tree = spoton::SPTree::CreateSPTree (true, false, false);

    // Build tree
    {
        auto starttime = std::chrono::system_clock::now ();
        tbb::parallel_for (tbb::blocked_range<uint64_t> (0, n),
                           [&] (const tbb::blocked_range<uint64_t>& range) {
                               for (uint64_t i = range.begin (); i != range.end (); i++) {
                                   tree->insert (keys[i], keys[i]);
                               }
                           });
        auto duration = std::chrono::duration_cast<std::chrono::microseconds> (
            std::chrono::system_clock::now () - starttime);
        printf ("insert,%ld,%f\n", n, (n * 1.0) / duration.count ());
    }

    {
        // Lookup
        auto starttime = std::chrono::system_clock::now ();
        tbb::parallel_for (tbb::blocked_range<uint64_t> (0, n),
                           [&] (const tbb::blocked_range<uint64_t>& range) {
                               for (uint64_t i = range.begin (); i != range.end (); i++) {
                                   auto val = tree->lookup (keys[i]);
                                   if (val != keys[i]) {
                                       val = tree->lookup (keys[i]);
                                       std::cout << "wrong key read: " << val
                                                 << " expected:" << keys[i] << std::endl;
                                   }
                               }
                           });
        auto duration = std::chrono::duration_cast<std::chrono::microseconds> (
            std::chrono::system_clock::now () - starttime);
        printf ("lookup,%ld,%f\n", n, (n * 1.0) / duration.count ());
    }

    {
        auto starttime = std::chrono::system_clock::now ();

        tbb::parallel_for (tbb::blocked_range<uint64_t> (0, n),
                           [&] (const tbb::blocked_range<uint64_t>& range) {
                               for (uint64_t i = range.begin (); i != range.end (); i++) {
                                   tree->remove (keys[i]);
                               }
                           });
        auto duration = std::chrono::duration_cast<std::chrono::microseconds> (
            std::chrono::system_clock::now () - starttime);
        printf ("remove,%ld,%f\n", n, (n * 1.0) / duration.count ());
    }
    delete[] keys;
}

int main (int argc, char** argv) {
    if (argc != 3) {
        printf (
            "usage: %s n 0|1|2\nn: number of keys\n0: sorted keys\n1: dense keys\n2: sparse keys\n",
            argv[0]);
        return 1;
    }
    // singlethreaded (argv);
    multithreaded (argv);

    return 0;
}