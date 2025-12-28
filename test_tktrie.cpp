#include "tktrie.h"
#include <map>
#include <shared_mutex>
#include <thread>
#include <vector>
#include <random>
#include <chrono>
#include <iostream>
#include <algorithm>
#include <atomic>
#include <iomanip>

// Word list for testing
const std::vector<std::string> WORDS = {
    "apple", "application", "apply", "banana", "band", "bandana", "cat", "catalog",
    "catch", "catcher", "dog", "dodge", "dome", "domestic", "elephant", "elevator",
    "elite", "fish", "fishing", "fisherman", "goal", "goat", "going", "gold",
    "hello", "help", "helper", "helicopter", "ice", "icy", "icon", "idea",
    "jump", "jumper", "jumping", "just", "justice", "kite", "kitchen", "kitten",
    "lamp", "land", "landing", "language", "mango", "man", "manual", "many",
    "night", "nine", "ninja", "noble", "ocean", "octopus", "office", "often",
    "paint", "painter", "painting", "paper", "quest", "question", "quick", "quiet",
    "rain", "rainbow", "random", "range", "sand", "sandwich", "santa", "save",
    "table", "tablet", "tail", "take", "umbrella", "uncle", "under", "unit",
    "vase", "vast", "vector", "very", "water", "wave", "way", "weak",
    "xenon", "xerox", "xray", "yacht", "yard", "year", "yellow", "zero",
    "zone", "zoo", "zoom", "zebra", "alpha", "beta", "gamma", "delta"
};

constexpr int ITERATIONS = 100;  // Repeat the word list this many times

// Locked std::map wrapper
template<typename K, typename V>
class locked_map {
    std::map<K, V> data;
    mutable std::shared_mutex mtx;
public:
    auto find(const K& key) {
        std::shared_lock lock(mtx);
        return data.find(key);
    }
    
    auto end() { return data.end(); }
    
    void insert(const std::pair<K, V>& kv) {
        std::unique_lock lock(mtx);
        data.insert(kv);
    }
    
    size_t erase(const K& key) {
        std::unique_lock lock(mtx);
        return data.erase(key);
    }
    
    size_t size() {
        std::shared_lock lock(mtx);
        return data.size();
    }
};

std::atomic<long long> total_ops{0};

template<typename Container>
void worker(Container& c, int thread_id, unsigned seed) {
    std::vector<std::string> words = WORDS;
    std::mt19937 rng(seed);
    std::shuffle(words.begin(), words.end(), rng);
    
    long long ops = 0;
    
    for (int iter = 0; iter < ITERATIONS; ++iter) {
        for (const auto& word : words) {
            int value = thread_id * 10000 + ops;
            
            // find
            auto it = c.find(word);
            (void)it;
            ops++;
            
            // insert
            c.insert({word, value});
            ops++;
            
            // find
            it = c.find(word);
            (void)it;
            ops++;
            
            // erase
            c.erase(word);
            ops++;
            
            // find
            it = c.find(word);
            (void)it;
            ops++;
            
            // insert again
            c.insert({word, value + 1});
            ops++;
            
            // find
            it = c.find(word);
            (void)it;
            ops++;
        }
    }
    
    total_ops += ops;
}

template<typename Container>
double benchmark(const std::string& name, int num_threads) {
    Container c;
    total_ops = 0;
    
    std::vector<std::thread> threads;
    
    auto start = std::chrono::high_resolution_clock::now();
    
    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back(worker<Container>, std::ref(c), i, i * 12345);
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    
    double ops_per_sec = total_ops.load() * 1000000.0 / duration.count();
    
    std::cout << name << " (" << num_threads << " threads):\n";
    std::cout << "  Total ops: " << total_ops.load() << "\n";
    std::cout << "  Time: " << duration.count() / 1000.0 << " ms\n";
    std::cout << std::fixed << std::setprecision(0);
    std::cout << "  Ops/sec: " << ops_per_sec << "\n";
    std::cout << "  Final size: " << c.size() << "\n\n";
    
    return ops_per_sec;
}

int main() {
    std::cout << "=== Concurrent Performance Benchmark ===\n";
    std::cout << "Words: " << WORDS.size() << "\n";
    std::cout << "Iterations: " << ITERATIONS << "\n";
    std::cout << "Ops per thread: " << WORDS.size() * 7 * ITERATIONS << "\n\n";
    
    // Warm up
    std::cout << "--- Warm-up run ---\n";
    benchmark<tktrie<std::string, int>>("tktrie (warmup)", 4);
    benchmark<locked_map<std::string, int>>("locked_map (warmup)", 4);
    
    std::cout << "--- Single-threaded ---\n";
    double trie_1 = benchmark<tktrie<std::string, int>>("tktrie", 1);
    double map_1 = benchmark<locked_map<std::string, int>>("locked_map", 1);
    std::cout << "Ratio (trie/map): " << std::fixed << std::setprecision(2) << trie_1 / map_1 << "x\n\n";
    
    std::cout << "--- 4 threads ---\n";
    double trie_4 = benchmark<tktrie<std::string, int>>("tktrie", 4);
    double map_4 = benchmark<locked_map<std::string, int>>("locked_map", 4);
    std::cout << "Ratio (trie/map): " << trie_4 / map_4 << "x\n\n";
    
    std::cout << "--- 8 threads ---\n";
    double trie_8 = benchmark<tktrie<std::string, int>>("tktrie", 8);
    double map_8 = benchmark<locked_map<std::string, int>>("locked_map", 8);
    std::cout << "Ratio (trie/map): " << trie_8 / map_8 << "x\n\n";
    
    std::cout << "--- 16 threads ---\n";
    double trie_16 = benchmark<tktrie<std::string, int>>("tktrie", 16);
    double map_16 = benchmark<locked_map<std::string, int>>("locked_map", 16);
    std::cout << "Ratio (trie/map): " << trie_16 / map_16 << "x\n\n";
    
    std::cout << "=== Summary ===\n";
    std::cout << std::fixed << std::setprecision(0);
    std::cout << "Threads | tktrie ops/s   | locked_map ops/s | Ratio\n";
    std::cout << "--------|----------------|------------------|------\n";
    std::cout << std::setprecision(2);
    std::cout << "   1    | " << std::setw(14) << (long)trie_1 << " | " << std::setw(16) << (long)map_1 << " | " << trie_1/map_1 << "x\n";
    std::cout << "   4    | " << std::setw(14) << (long)trie_4 << " | " << std::setw(16) << (long)map_4 << " | " << trie_4/map_4 << "x\n";
    std::cout << "   8    | " << std::setw(14) << (long)trie_8 << " | " << std::setw(16) << (long)map_8 << " | " << trie_8/map_8 << "x\n";
    std::cout << "  16    | " << std::setw(14) << (long)trie_16 << " | " << std::setw(16) << (long)map_16 << " | " << trie_16/map_16 << "x\n";
    
    return 0;
}
