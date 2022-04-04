#include <string>
#include <unordered_set>
#include <vector>

#include "gtest/gtest.h"
#include "spoton-tree/middleLayer.h"

TEST (BloomFilter32, SetTest) {
    spoton::BloomFilterFix32 bloomfilter;

    std::vector<std::string> words = {"1234", "asdf", "Michale", "Jony", "Cindy", "Naga"};
    std::vector<std::string> nowords = {"65e645", "qqqweq", "cidnfa", "dad", "haha", "Nice"};

    for (auto& w : words) {
        bloomfilter.set (w.data (), w.size ());
    }

    for (auto& w : words) {
        ASSERT_TRUE (bloomfilter.couldExist (w.data (), w.size ()));
    }

    for (auto& w : nowords) {
        ASSERT_FALSE (bloomfilter.couldExist (w.data (), w.size ()));
    }

    // printf ("%s\n", bloomfilter.ToStringBinary ().c_str ());
}

TEST (BloomFilter32, FalsePositiveRate) {
    spoton::BloomFilterFix32 bloomfilter;

    std::unordered_set<std::string> words;
    for (int i = 0; i < 32; i++) {
        auto w = "num" + std::to_string (random ());
        words.insert (w);
    }

    for (auto& w : words) {
        bloomfilter.set (w.data (), w.size ());
    }
    for (auto& w : words) {
        ASSERT_TRUE (bloomfilter.couldExist (w.data (), w.size ()));
    }
    // printf ("%s\n", bloomfilter.ToStringBinary ().c_str ());
    int NUM = 100000;
    int false_positive_count = 0;
    for (int i = 0; i < NUM; i++) {
        auto w = std::to_string (random ());
        if (bloomfilter.couldExist (w.data (), w.size ())) {
            false_positive_count++;
        }
    }

    printf ("fpr: %f\n", false_positive_count / (double)(NUM));
}

TEST (BloomFilter64, SetTest) {
    spoton::BloomFilterFix64 bloomfilter;

    std::vector<std::string> words = {"1234", "asdf", "Michale", "Jony", "Cindy", "Naga"};
    std::vector<std::string> nowords = {"65e645", "qqqweq", "cidnfa", "dad", "haha", "Nice"};

    for (auto& w : words) {
        bloomfilter.set (w.data (), w.size ());
    }

    for (auto& w : words) {
        ASSERT_TRUE (bloomfilter.couldExist (w.data (), w.size ()));
    }

    for (auto& w : nowords) {
        ASSERT_FALSE (bloomfilter.couldExist (w.data (), w.size ()));
    }
}

TEST (BloomFilter64, FalsePositiveRate) {
    spoton::BloomFilterFix64 bloomfilter;

    std::unordered_set<std::string> words;
    for (int i = 0; i < 64; i++) {
        auto w = "num" + std::to_string (random ());
        words.insert (w);
    }

    for (auto& w : words) {
        bloomfilter.set (w.data (), w.size ());
    }
    for (auto& w : words) {
        ASSERT_TRUE (bloomfilter.couldExist (w.data (), w.size ()));
    }
    // printf ("%s\n", bloomfilter.ToStringBinary ().c_str ());
    int NUM = 100000;
    int false_positive_count = 0;
    for (int i = 0; i < NUM; i++) {
        auto w = std::to_string (random ());
        if (bloomfilter.couldExist (w.data (), w.size ())) {
            false_positive_count++;
        }
    }

    printf ("fpr: %f\n", false_positive_count / (double)(NUM));
}

int main (int argc, char* argv[]) {
    ::testing::InitGoogleTest (&argc, argv);
    return RUN_ALL_TESTS ();
}
