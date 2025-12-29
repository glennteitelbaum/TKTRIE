#include <chrono>
#include <iostream>
#include <vector>
#include <string>
#include <thread>
#include <atomic>
#include <map>
#include <unordered_map>
#include <shared_mutex>
#include "tktrie.h"

const std::vector<std::string> WORDS = {
    "the", "be", "to", "of", "and", "a", "in", "that", "have", "I",
    "it", "for", "not", "on", "with", "he", "as", "you", "do", "at",
    "this", "but", "his", "by", "from", "they", "we", "say", "her", "she",
    "or", "an", "will", "my", "one", "all", "would", "there", "their", "what",
    "so", "up", "out", "if", "about", "who", "get", "which", "go", "me"
};

template<typename M>
class guarded_map {
    M data;
    mutable std::shared_mutex mtx;
public:
    bool contains(const std::string& key) const {
        std::shared_lock lock(mtx);
        return data.find(key) != data.end();
    }
    bool insert(const std::pair<std::string, int>& kv) {
        std::unique_lock lock(mtx);
        return data.insert(kv).second;
    }
    bool erase(const std::string& key) {
        std::unique_lock lock(mtx);
        return data.erase(key) > 0;
    }
};

using locked_map = guarded_map<std::map<std::string, int>>;
using locked_umap = guarded_map<std::unordered_map<std::string, int>>;

std::atomic<bool> stop{false};

// Separate benchmarks for each operation type
template<typename Container>
double bench_find(Container& c, int threads, int ms) {
    std::atomic<long long> ops{0};
    stop = false;
    std::vector<std::thread> workers;
    
    for (int t = 0; t < threads; t++) {
        workers.emplace_back([&]() {
            long long local = 0;
            while (!stop) {
                for (const auto& w : WORDS) { c.contains(w); local++; }
            }
            ops += local;
        });
    }
    
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
    stop = true;
    for (auto& w : workers) w.join();
    return ops * 1000.0 / ms;
}

template<typename Container>
double bench_insert(Container& c, int threads, int ms) {
    std::atomic<long long> ops{0};
    stop = false;
    std::vector<std::thread> workers;
    
    for (int t = 0; t < threads; t++) {
        workers.emplace_back([&, t]() {
            long long local = 0;
            int i = 0;
            while (!stop) {
                for (const auto& w : WORDS) { c.insert({w, t*10000 + i++}); local++; }
            }
            ops += local;
        });
    }
    
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
    stop = true;
    for (auto& w : workers) w.join();
    return ops * 1000.0 / ms;
}

template<typename Container>
double bench_erase(Container& c, int threads, int ms) {
    std::atomic<long long> ops{0};
    stop = false;
    std::vector<std::thread> workers;
    
    for (int t = 0; t < threads; t++) {
        workers.emplace_back([&]() {
            long long local = 0;
            while (!stop) {
                for (const auto& w : WORDS) { c.erase(w); local++; }
            }
            ops += local;
        });
    }
    
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
    stop = true;
    for (auto& w : workers) w.join();
    return ops * 1000.0 / ms;
}

template<typename Container>
double bench_mixed_find(int find_threads, int write_threads, int ms) {
    Container c;
    for (size_t i = 0; i < WORDS.size(); i++) c.insert({WORDS[i], (int)i});
    
    std::atomic<long long> find_ops{0}, write_ops{0};
    stop = false;
    std::vector<std::thread> workers;
    
    // Find threads
    for (int t = 0; t < find_threads; t++) {
        workers.emplace_back([&]() {
            long long local = 0;
            while (!stop) {
                for (const auto& w : WORDS) { c.contains(w); local++; }
            }
            find_ops += local;
        });
    }
    
    // Write threads (50% insert, 50% erase)
    for (int t = 0; t < write_threads; t++) {
        workers.emplace_back([&, t]() {
            long long local = 0;
            int i = 0;
            while (!stop) {
                for (size_t j = 0; j < WORDS.size(); j++) {
                    if (j % 2 == 0) c.insert({WORDS[j], t*10000 + i++});
                    else c.erase(WORDS[j]);
                    local++;
                }
            }
            write_ops += local;
        });
    }
    
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
    stop = true;
    for (auto& w : workers) w.join();
    return find_ops * 1000.0 / ms;
}

int main() {
    constexpr int MS = 500;
    
    std::cout << "=== Per-Operation Benchmark: RCU Trie vs Guarded map/umap ===\n\n";
    
    // FIND benchmark
    std::cout << "FIND (contains) - ops/sec:\n";
    std::cout << "Threads |   RCU Trie |   std::map |  std::umap | RCU/map | RCU/umap\n";
    std::cout << "--------|------------|------------|------------|---------|----------\n";
    
    for (int threads : {1, 2, 4, 8}) {
        gteitelbaum::tktrie<std::string, int> rcu_t;
        locked_map lm;
        locked_umap lu;
        for (size_t i = 0; i < WORDS.size(); i++) {
            rcu_t.insert({WORDS[i], (int)i});
            lm.insert({WORDS[i], (int)i});
            lu.insert({WORDS[i], (int)i});
        }
        
        double rcu = bench_find(rcu_t, threads, MS);
        double map = bench_find(lm, threads, MS);
        double umap = bench_find(lu, threads, MS);
        printf("%7d | %10.1fM | %10.1fM | %10.1fM | %7.1fx | %8.1fx\n",
               threads, rcu/1e6, map/1e6, umap/1e6, rcu/map, rcu/umap);
    }
    
    // INSERT benchmark
    std::cout << "\nINSERT - ops/sec:\n";
    std::cout << "Threads |   RCU Trie |   std::map |  std::umap | RCU/map | RCU/umap\n";
    std::cout << "--------|------------|------------|------------|---------|----------\n";
    
    for (int threads : {1, 2, 4, 8}) {
        gteitelbaum::tktrie<std::string, int> rcu_t;
        locked_map lm;
        locked_umap lu;
        
        double rcu = bench_insert(rcu_t, threads, MS);
        double map = bench_insert(lm, threads, MS);
        double umap = bench_insert(lu, threads, MS);
        printf("%7d | %10.1fM | %10.1fM | %10.1fM | %7.1fx | %8.1fx\n",
               threads, rcu/1e6, map/1e6, umap/1e6, rcu/map, rcu/umap);
    }
    
    // ERASE benchmark
    std::cout << "\nERASE - ops/sec:\n";
    std::cout << "Threads |   RCU Trie |   std::map |  std::umap | RCU/map | RCU/umap\n";
    std::cout << "--------|------------|------------|------------|---------|----------\n";
    
    for (int threads : {1, 2, 4, 8}) {
        gteitelbaum::tktrie<std::string, int> rcu_t;
        locked_map lm;
        locked_umap lu;
        for (size_t i = 0; i < WORDS.size(); i++) {
            rcu_t.insert({WORDS[i], (int)i});
            lm.insert({WORDS[i], (int)i});
            lu.insert({WORDS[i], (int)i});
        }
        
        double rcu = bench_erase(rcu_t, threads, MS);
        double map = bench_erase(lm, threads, MS);
        double umap = bench_erase(lu, threads, MS);
        printf("%7d | %10.1fM | %10.1fM | %10.1fM | %7.1fx | %8.1fx\n",
               threads, rcu/1e6, map/1e6, umap/1e6, rcu/map, rcu/umap);
    }
    
    // FIND with concurrent writes
    std::cout << "\nFIND with concurrent WRITERS (insert+erase) - find ops/sec:\n";
    std::cout << "Find/Write |   RCU Trie |   std::map |  std::umap | RCU/map | RCU/umap\n";
    std::cout << "-----------|------------|------------|------------|---------|----------\n";
    
    for (auto [f, w] : std::vector<std::pair<int,int>>{{4,0}, {4,1}, {4,2}, {4,4}, {8,0}, {8,2}, {8,4}}) {
        double rcu = bench_mixed_find<gteitelbaum::tktrie<std::string, int>>(f, w, MS);
        double map = bench_mixed_find<locked_map>(f, w, MS);
        double umap = bench_mixed_find<locked_umap>(f, w, MS);
        printf("    %d / %d | %10.1fM | %10.1fM | %10.1fM | %7.1fx | %8.1fx\n",
               f, w, rcu/1e6, map/1e6, umap/1e6, rcu/map, rcu/umap);
    }
    
    return 0;
}#include <chrono>
#include <iostream>
#include <vector>
#include <string>
#include <thread>
#include <atomic>
#include <map>
#include <unordered_map>
#include <shared_mutex>
#include "tktrie.h"

const std::vector<std::string> WORDS = {
    "the", "be", "to", "of", "and", "a", "in", "that", "have", "I",
    "it", "for", "not", "on", "with", "he", "as", "you", "do", "at",
    "this", "but", "his", "by", "from", "they", "we", "say", "her", "she",
    "or", "an", "will", "my", "one", "all", "would", "there", "their", "what",
    "so", "up", "out", "if", "about", "who", "get", "which", "go", "me"
};

template<typename M>
class guarded_map {
    M data;
    mutable std::shared_mutex mtx;
public:
    bool contains(const std::string& key) const {
        std::shared_lock lock(mtx);
        return data.find(key) != data.end();
    }
    bool insert(const std::pair<std::string, int>& kv) {
        std::unique_lock lock(mtx);
        return data.insert(kv).second;
    }
    bool erase(const std::string& key) {
        std::unique_lock lock(mtx);
        return data.erase(key) > 0;
    }
};

using locked_map = guarded_map<std::map<std::string, int>>;
using locked_umap = guarded_map<std::unordered_map<std::string, int>>;

std::atomic<bool> stop{false};

// Separate benchmarks for each operation type
template<typename Container>
double bench_find(Container& c, int threads, int ms) {
    std::atomic<long long> ops{0};
    stop = false;
    std::vector<std::thread> workers;
    
    for (int t = 0; t < threads; t++) {
        workers.emplace_back([&]() {
            long long local = 0;
            while (!stop) {
                for (const auto& w : WORDS) { c.contains(w); local++; }
            }
            ops += local;
        });
    }
    
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
    stop = true;
    for (auto& w : workers) w.join();
    return ops * 1000.0 / ms;
}

template<typename Container>
double bench_insert(Container& c, int threads, int ms) {
    std::atomic<long long> ops{0};
    stop = false;
    std::vector<std::thread> workers;
    
    for (int t = 0; t < threads; t++) {
        workers.emplace_back([&, t]() {
            long long local = 0;
            int i = 0;
            while (!stop) {
                for (const auto& w : WORDS) { c.insert({w, t*10000 + i++}); local++; }
            }
            ops += local;
        });
    }
    
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
    stop = true;
    for (auto& w : workers) w.join();
    return ops * 1000.0 / ms;
}

template<typename Container>
double bench_erase(Container& c, int threads, int ms) {
    std::atomic<long long> ops{0};
    stop = false;
    std::vector<std::thread> workers;
    
    for (int t = 0; t < threads; t++) {
        workers.emplace_back([&]() {
            long long local = 0;
            while (!stop) {
                for (const auto& w : WORDS) { c.erase(w); local++; }
            }
            ops += local;
        });
    }
    
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
    stop = true;
    for (auto& w : workers) w.join();
    return ops * 1000.0 / ms;
}

template<typename Container>
double bench_mixed_find(int find_threads, int write_threads, int ms) {
    Container c;
    for (size_t i = 0; i < WORDS.size(); i++) c.insert({WORDS[i], (int)i});
    
    std::atomic<long long> find_ops{0}, write_ops{0};
    stop = false;
    std::vector<std::thread> workers;
    
    // Find threads
    for (int t = 0; t < find_threads; t++) {
        workers.emplace_back([&]() {
            long long local = 0;
            while (!stop) {
                for (const auto& w : WORDS) { c.contains(w); local++; }
            }
            find_ops += local;
        });
    }
    
    // Write threads (50% insert, 50% erase)
    for (int t = 0; t < write_threads; t++) {
        workers.emplace_back([&, t]() {
            long long local = 0;
            int i = 0;
            while (!stop) {
                for (size_t j = 0; j < WORDS.size(); j++) {
                    if (j % 2 == 0) c.insert({WORDS[j], t*10000 + i++});
                    else c.erase(WORDS[j]);
                    local++;
                }
            }
            write_ops += local;
        });
    }
    
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
    stop = true;
    for (auto& w : workers) w.join();
    return find_ops * 1000.0 / ms;
}

int main() {
    constexpr int MS = 500;
    
    std::cout << "=== Per-Operation Benchmark: RCU Trie vs Guarded map/umap ===\n\n";
    
    // FIND benchmark
    std::cout << "FIND (contains) - ops/sec:\n";
    std::cout << "Threads |   RCU Trie |   std::map |  std::umap | RCU/map | RCU/umap\n";
    std::cout << "--------|------------|------------|------------|---------|----------\n";
    
    for (int threads : {1, 2, 4, 8}) {
        gteitelbaum::tktrie<std::string, int> rcu_t;
        locked_map lm;
        locked_umap lu;
        for (size_t i = 0; i < WORDS.size(); i++) {
            rcu_t.insert({WORDS[i], (int)i});
            lm.insert({WORDS[i], (int)i});
            lu.insert({WORDS[i], (int)i});
        }
        
        double rcu = bench_find(rcu_t, threads, MS);
        double map = bench_find(lm, threads, MS);
        double umap = bench_find(lu, threads, MS);
        printf("%7d | %10.1fM | %10.1fM | %10.1fM | %7.1fx | %8.1fx\n",
               threads, rcu/1e6, map/1e6, umap/1e6, rcu/map, rcu/umap);
    }
    
    // INSERT benchmark
    std::cout << "\nINSERT - ops/sec:\n";
    std::cout << "Threads |   RCU Trie |   std::map |  std::umap | RCU/map | RCU/umap\n";
    std::cout << "--------|------------|------------|------------|---------|----------\n";
    
    for (int threads : {1, 2, 4, 8}) {
        gteitelbaum::tktrie<std::string, int> rcu_t;
        locked_map lm;
        locked_umap lu;
        
        double rcu = bench_insert(rcu_t, threads, MS);
        double map = bench_insert(lm, threads, MS);
        double umap = bench_insert(lu, threads, MS);
        printf("%7d | %10.1fM | %10.1fM | %10.1fM | %7.1fx | %8.1fx\n",
               threads, rcu/1e6, map/1e6, umap/1e6, rcu/map, rcu/umap);
    }
    
    // ERASE benchmark
    std::cout << "\nERASE - ops/sec:\n";
    std::cout << "Threads |   RCU Trie |   std::map |  std::umap | RCU/map | RCU/umap\n";
    std::cout << "--------|------------|------------|------------|---------|----------\n";
    
    for (int threads : {1, 2, 4, 8}) {
        gteitelbaum::tktrie<std::string, int> rcu_t;
        locked_map lm;
        locked_umap lu;
        for (size_t i = 0; i < WORDS.size(); i++) {
            rcu_t.insert({WORDS[i], (int)i});
            lm.insert({WORDS[i], (int)i});
            lu.insert({WORDS[i], (int)i});
        }
        
        double rcu = bench_erase(rcu_t, threads, MS);
        double map = bench_erase(lm, threads, MS);
        double umap = bench_erase(lu, threads, MS);
        printf("%7d | %10.1fM | %10.1fM | %10.1fM | %7.1fx | %8.1fx\n",
               threads, rcu/1e6, map/1e6, umap/1e6, rcu/map, rcu/umap);
    }
    
    // FIND with concurrent writes
    std::cout << "\nFIND with concurrent WRITERS (insert+erase) - find ops/sec:\n";
    std::cout << "Find/Write |   RCU Trie |   std::map |  std::umap | RCU/map | RCU/umap\n";
    std::cout << "-----------|------------|------------|------------|---------|----------\n";
    
    for (auto [f, w] : std::vector<std::pair<int,int>>{{4,0}, {4,1}, {4,2}, {4,4}, {8,0}, {8,2}, {8,4}}) {
        double rcu = bench_mixed_find<gteitelbaum::tktrie<std::string, int>>(f, w, MS);
        double map = bench_mixed_find<locked_map>(f, w, MS);
        double umap = bench_mixed_find<locked_umap>(f, w, MS);
        printf("    %d / %d | %10.1fM | %10.1fM | %10.1fM | %7.1fx | %8.1fx\n",
               f, w, rcu/1e6, map/1e6, umap/1e6, rcu/map, rcu/umap);
    }
    
    return 0;
}
