#include <chrono>
#include <iostream>
#include <vector>
#include <string>
#include <thread>
#include <atomic>
#include <random>
#include <map>
#include <unordered_map>
#include <shared_mutex>
#include <bit>
#include <cstring>
#include "tktrie.h"

// Convert uint64_t to 8-byte big-endian string (no allocation, reuses buffer)
inline void uint64_to_key(uint64_t v, char* buf) {
    uint64_t be = __builtin_bswap64(v);
    std::memcpy(buf, &be, 8);
}

// Generate random uint64 keys as 8-byte strings
std::vector<std::string> generate_int_keys(size_t count) {
    std::vector<std::string> keys;
    keys.reserve(count);
    std::mt19937_64 rng(42);
    std::uniform_int_distribution<uint64_t> dist(0, UINT64_MAX);
    
    char buf[8];
    for (size_t i = 0; i < count; i++) {
        uint64_to_key(dist(rng), buf);
        keys.emplace_back(buf, 8);
    }
    return keys;
}

const std::vector<std::string> INT_KEYS = generate_int_keys(10000);

template<typename M>
class guarded_map {
    M data;
    mutable std::shared_mutex mtx;
public:
    bool contains(const typename M::key_type& key) const {
        std::shared_lock lock(mtx);
        return data.find(key) != data.end();
    }
    bool insert(const std::pair<typename M::key_type, typename M::mapped_type>& kv) {
        std::unique_lock lock(mtx);
        return data.insert(kv).second;
    }
    bool erase(const typename M::key_type& key) {
        std::unique_lock lock(mtx);
        return data.erase(key) > 0;
    }
};

template<typename K, typename V>
using locked_map = guarded_map<std::map<K, V>>;
template<typename K, typename V>
using locked_umap = guarded_map<std::unordered_map<K, V>>;

std::atomic<bool> stop{false};

template<typename Container, typename Keys>
double bench_find(Container& c, const Keys& keys, int threads, int ms) {
    std::atomic<long long> ops{0};
    stop = false;
    std::vector<std::thread> workers;
    
    for (int t = 0; t < threads; t++) {
        workers.emplace_back([&]() {
            long long local = 0;
            while (!stop) {
                for (const auto& k : keys) { c.contains(k); local++; }
            }
            ops += local;
        });
    }
    
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
    stop = true;
    for (auto& w : workers) w.join();
    return ops * 1000.0 / ms;
}

template<typename Container, typename Keys, typename V>
double bench_insert(Container& c, const Keys& keys, int threads, int ms) {
    std::atomic<long long> ops{0};
    stop = false;
    std::vector<std::thread> workers;
    
    for (int t = 0; t < threads; t++) {
        workers.emplace_back([&, t]() {
            long long local = 0;
            int i = 0;
            while (!stop) {
                for (const auto& k : keys) { c.insert({k, (V)(t*10000 + i++)}); local++; }
            }
            ops += local;
        });
    }
    
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
    stop = true;
    for (auto& w : workers) w.join();
    return ops * 1000.0 / ms;
}

template<typename Container, typename Keys>
double bench_erase(Container& c, const Keys& keys, int threads, int ms) {
    std::atomic<long long> ops{0};
    stop = false;
    std::vector<std::thread> workers;
    
    for (int t = 0; t < threads; t++) {
        workers.emplace_back([&]() {
            long long local = 0;
            while (!stop) {
                for (const auto& k : keys) { c.erase(k); local++; }
            }
            ops += local;
        });
    }
    
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
    stop = true;
    for (auto& w : workers) w.join();
    return ops * 1000.0 / ms;
}

template<typename Container, typename Keys, typename V>
double bench_mixed_find(const Keys& keys, int find_threads, int write_threads, int ms) {
    Container c;
    for (size_t i = 0; i < keys.size(); i++) c.insert({keys[i], (V)i});
    
    std::atomic<long long> find_ops{0};
    stop = false;
    std::vector<std::thread> workers;
    
    for (int t = 0; t < find_threads; t++) {
        workers.emplace_back([&]() {
            long long local = 0;
            while (!stop) {
                for (const auto& k : keys) { c.contains(k); local++; }
            }
            find_ops += local;
        });
    }
    
    for (int t = 0; t < write_threads; t++) {
        workers.emplace_back([&, t]() {
            int i = 0;
            while (!stop) {
                for (size_t j = 0; j < keys.size(); j++) {
                    if (j % 2 == 0) c.insert({keys[j], (V)(t*10000 + i++)});
                    else c.erase(keys[j]);
                }
            }
        });
    }
    
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
    stop = true;
    for (auto& w : workers) w.join();
    return find_ops * 1000.0 / ms;
}

int main() {
    constexpr int MS = 500;
    
    std::cout << "=== Per-Operation Benchmark: RCU Trie vs Guarded map/umap ===\n";
    std::cout << "Int keys (as 8-byte big-endian strings): " << INT_KEYS.size() << "\n";
    std::cout << "Range: 0 to UINT64_MAX (full 64-bit random)\n\n";
    
    std::cout << "FIND (contains) - ops/sec:\n";
    std::cout << "Threads |   RCU Trie |   std::map |  std::umap | RCU/map | RCU/umap\n";
    std::cout << "--------|------------|------------|------------|---------|----------\n";
    
    for (int threads : {1, 2, 4, 8, 12, 16}) {
        gteitelbaum::tktrie<std::string, int> rcu_t;
        locked_map<std::string, int> lm;
        locked_umap<std::string, int> lu;
        for (size_t i = 0; i < INT_KEYS.size(); i++) {
            rcu_t.insert({INT_KEYS[i], (int)i});
            lm.insert({INT_KEYS[i], (int)i});
            lu.insert({INT_KEYS[i], (int)i});
        }
        
        double rcu = bench_find(rcu_t, INT_KEYS, threads, MS);
        double map = bench_find(lm, INT_KEYS, threads, MS);
        double umap = bench_find(lu, INT_KEYS, threads, MS);
        printf("%7d | %10.1fM | %10.1fM | %10.1fM | %7.1fx | %8.1fx\n",
               threads, rcu/1e6, map/1e6, umap/1e6, rcu/map, rcu/umap);
    }
    
    std::cout << "\nINSERT - ops/sec:\n";
    std::cout << "Threads |   RCU Trie |   std::map |  std::umap | RCU/map | RCU/umap\n";
    std::cout << "--------|------------|------------|------------|---------|----------\n";
    
    for (int threads : {1, 2, 4, 8, 12, 16}) {
        gteitelbaum::tktrie<std::string, int> rcu_t;
        locked_map<std::string, int> lm;
        locked_umap<std::string, int> lu;
        
        double rcu = bench_insert<decltype(rcu_t), decltype(INT_KEYS), int>(rcu_t, INT_KEYS, threads, MS);
        double map = bench_insert<decltype(lm), decltype(INT_KEYS), int>(lm, INT_KEYS, threads, MS);
        double umap = bench_insert<decltype(lu), decltype(INT_KEYS), int>(lu, INT_KEYS, threads, MS);
        printf("%7d | %10.1fM | %10.1fM | %10.1fM | %7.1fx | %8.1fx\n",
               threads, rcu/1e6, map/1e6, umap/1e6, rcu/map, rcu/umap);
    }
    
    std::cout << "\nERASE - ops/sec:\n";
    std::cout << "Threads |   RCU Trie |   std::map |  std::umap | RCU/map | RCU/umap\n";
    std::cout << "--------|------------|------------|------------|---------|----------\n";
    
    for (int threads : {1, 2, 4, 8, 12, 16}) {
        gteitelbaum::tktrie<std::string, int> rcu_t;
        locked_map<std::string, int> lm;
        locked_umap<std::string, int> lu;
        for (size_t i = 0; i < INT_KEYS.size(); i++) {
            rcu_t.insert({INT_KEYS[i], (int)i});
            lm.insert({INT_KEYS[i], (int)i});
            lu.insert({INT_KEYS[i], (int)i});
        }
        
        double rcu = bench_erase(rcu_t, INT_KEYS, threads, MS);
        double map = bench_erase(lm, INT_KEYS, threads, MS);
        double umap = bench_erase(lu, INT_KEYS, threads, MS);
        printf("%7d | %10.1fM | %10.1fM | %10.1fM | %7.1fx | %8.1fx\n",
               threads, rcu/1e6, map/1e6, umap/1e6, rcu/map, rcu/umap);
    }
    
    std::cout << "\nFIND with concurrent WRITERS - find ops/sec:\n";
    std::cout << "Find/Write |   RCU Trie |   std::map |  std::umap | RCU/map | RCU/umap\n";
    std::cout << "-----------|------------|------------|------------|---------|----------\n";
    
    for (auto [f, w] : std::vector<std::pair<int,int>>{{4,0}, {4,1}, {4,2}, {4,4}, {8,0}, {8,2}, {8,4}, {12,0}, {12,4}}) {
        double rcu = bench_mixed_find<gteitelbaum::tktrie<std::string, int>, decltype(INT_KEYS), int>(INT_KEYS, f, w, MS);
        double map = bench_mixed_find<locked_map<std::string, int>, decltype(INT_KEYS), int>(INT_KEYS, f, w, MS);
        double umap = bench_mixed_find<locked_umap<std::string, int>, decltype(INT_KEYS), int>(INT_KEYS, f, w, MS);
        printf("   %2d / %d | %10.1fM | %10.1fM | %10.1fM | %7.1fx | %8.1fx\n",
               f, w, rcu/1e6, map/1e6, umap/1e6, rcu/map, rcu/umap);
    }
    
    return 0;
}
