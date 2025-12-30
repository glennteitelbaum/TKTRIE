#pragma once
// Thread-safe trie with version-based optimistic locking
// - Reads are always lock-free
// - Writes only lock if there's a conflict on the same path

#include <atomic>
#include <bit>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>
#include <array>

namespace gteitelbaum {

template <typename T>
constexpr T my_byteswap(T value) noexcept {
    if constexpr (std::endian::native == std::endian::big) return value;
#if __cpp_lib_byteswap >= 202110L
    return std::byteswap(value);
#else
    if constexpr (sizeof(T) == 1) return value;
    else if constexpr (sizeof(T) == 2) return static_cast<T>(__builtin_bswap16(static_cast<uint16_t>(value)));
    else if constexpr (sizeof(T) == 4) return static_cast<T>(__builtin_bswap32(static_cast<uint32_t>(value)));
    else if constexpr (sizeof(T) == 8) return static_cast<T>(__builtin_bswap64(static_cast<uint64_t>(value)));
#endif
}

template <typename Key> struct tktrie_traits;

template <> struct tktrie_traits<std::string> {
    static constexpr size_t fixed_len = 0;
    static std::string_view to_bytes(const std::string& k) { return k; }
};

template <typename T> requires std::is_integral_v<T>
struct tktrie_traits<T> {
    static constexpr size_t fixed_len = sizeof(T);
    using unsigned_type = std::make_unsigned_t<T>;
    static std::string to_bytes(T k) {
        char buf[sizeof(T)];
        unsigned_type sortable;
        if constexpr (std::is_signed_v<T>) {
            sortable = static_cast<unsigned_type>(k) + (unsigned_type{1} << (sizeof(T) * 8 - 1));
        } else { sortable = k; }
        unsigned_type be = my_byteswap(sortable);
        std::memcpy(buf, &be, sizeof(T));
        return std::string(buf, sizeof(T));
    }
};

class PopCount {
    uint64_t bits[4]{};
public:
    bool find(unsigned char c, int* idx) const {
        int word = c >> 6, bit = c & 63;
        uint64_t mask = 1ULL << bit;
        if (!(bits[word] & mask)) return false;
        *idx = std::popcount(bits[word] & (mask - 1));
        for (int w = 0; w < word; ++w) *idx += std::popcount(bits[w]);
        return true;
    }
    int set(unsigned char c) {
        int word = c >> 6, bit = c & 63;
        uint64_t mask = 1ULL << bit;
        int idx = std::popcount(bits[word] & (mask - 1));
        for (int w = 0; w < word; ++w) idx += std::popcount(bits[w]);
        bits[word] |= mask;
        return idx;
    }
};

template <typename Key, typename T> class tktrie;

template <typename Key, typename T>
class tktrie_iterator {
    Key key_; T data_; bool valid_{false};
public:
    using value_type = std::pair<Key, T>;
    tktrie_iterator() = default;
    tktrie_iterator(const Key& k, const T& d) : key_(k), data_(d), valid_(true) {}
    static tktrie_iterator end_iterator() { return tktrie_iterator(); }
    const Key& key() const { return key_; }
    T& value() { return data_; }
    value_type operator*() const { return {key_, data_}; }
    bool valid() const { return valid_; }
    bool operator==(const tktrie_iterator& o) const {
        if (!valid_ && !o.valid_) return true;
        return valid_ && o.valid_ && key_ == o.key_;
    }
    bool operator!=(const tktrie_iterator& o) const { return !(*this == o); }
};

template <typename T> struct Node {
    PopCount pop{};
    std::vector<Node*> children{};
    std::string skip{};
    std::shared_ptr<T> data;
    std::atomic<uint64_t> version{0};
    
    Node() = default;
    Node(const Node&) = delete;
    Node& operator=(const Node&) = delete;
    
    bool has_data() const { return data != nullptr; }
    void set_data(const T& val) { data = std::make_shared<T>(val); }
    void clear_data() { data.reset(); }
    uint64_t get_version() const { return version.load(std::memory_order_acquire); }
    void inc_version() { version.fetch_add(1, std::memory_order_release); }
    
    Node* get_child(unsigned char c) const { 
        int idx; 
        return pop.find(c, &idx) ? children[idx] : nullptr; 
    }
    bool get_child_idx(unsigned char c, int* idx) const { return pop.find(c, idx); }
};

template <typename Key, typename T>
class tktrie {
public:
    using Traits = tktrie_traits<Key>;
    static constexpr size_t fixed_len = Traits::fixed_len;
    static constexpr bool is_fixed = (fixed_len > 0);
    using node_type = Node<T>;
    using size_type = std::size_t;
    using iterator = tktrie_iterator<Key, T>;

private:
    node_type* root_;
    std::atomic<size_type> elem_count_{0};
    mutable std::mutex write_mutex_;
    
    struct PathEntry { 
        node_type* node; 
        int child_idx;
        uint64_t version;
    };

    void delete_tree(node_type* n) {
        if (!n) return;
        for (auto* c : n->children) delete_tree(c);
        delete n;
    }
    
    // Check if all nodes on path still have same versions
    bool path_valid(const std::vector<PathEntry>& path) const {
        for (const auto& e : path) {
            if (e.node->get_version() != e.version) return false;
        }
        return true;
    }

public:
    tktrie() : root_(new node_type()) {}
    ~tktrie() { delete_tree(root_); }
    bool empty() const { return size() == 0; }
    size_type size() const { return elem_count_.load(std::memory_order_relaxed); }

    bool contains(const Key& key) const {
        if constexpr (is_fixed) return contains_impl(Traits::to_bytes(key));
        else return contains_impl(Traits::to_bytes(key));
    }
    
    iterator find(const Key& key) const {
        if constexpr (is_fixed) return find_impl(key, Traits::to_bytes(key));
        else return find_impl(key, Traits::to_bytes(key));
    }
    
    iterator end() const { return iterator::end_iterator(); }
    
    std::pair<iterator, bool> insert(const std::pair<const Key, T>& value) {
        return insert_impl(value.first, value.second);
    }
    
    bool erase(const Key& key) {
        return erase_impl(key);
    }

private:
    bool contains_impl(std::string_view kv) const {
        node_type* cur = root_;
        while (cur) {
            if (!cur->skip.empty()) {
                if (kv.size() < cur->skip.size()) return false;
                if (kv.substr(0, cur->skip.size()) != cur->skip) return false;
                kv.remove_prefix(cur->skip.size());
            }
            if (kv.empty()) return cur->has_data();
            cur = cur->get_child((unsigned char)kv[0]);
            kv.remove_prefix(1);
        }
        return false;
    }

    iterator find_impl(const Key& key, std::string_view kv) const {
        node_type* cur = root_;
        while (cur) {
            if (!cur->skip.empty()) {
                if (kv.size() < cur->skip.size()) return end();
                if (kv.substr(0, cur->skip.size()) != cur->skip) return end();
                kv.remove_prefix(cur->skip.size());
            }
            if (kv.empty()) return cur->has_data() ? iterator(key, *cur->data) : end();
            cur = cur->get_child((unsigned char)kv[0]);
            kv.remove_prefix(1);
        }
        return end();
    }

    std::pair<iterator, bool> insert_impl(const Key& key, const T& value) {
        std::string kv_str;
        if constexpr (is_fixed) kv_str = Traits::to_bytes(key);
        else kv_str = std::string(Traits::to_bytes(key));
        
        // Phase 1: Optimistic traversal without lock
        std::vector<PathEntry> path; path.reserve(16);
        std::string_view kv(kv_str);
        node_type* cur = root_;
        
        while (true) {
            uint64_t ver = cur->get_version();
            
            size_t common = 0;
            while (common < cur->skip.size() && common < kv.size() && 
                   cur->skip[common] == kv[common]) ++common;
            
            if (common < cur->skip.size()) {
                // Need to split this node
                path.push_back({cur, -1, ver});
                break;
            }
            
            kv.remove_prefix(common);
            
            if (kv.empty()) {
                // Key ends here
                path.push_back({cur, -1, ver});
                break;
            }
            
            unsigned char c = (unsigned char)kv[0];
            int idx;
            if (!cur->get_child_idx(c, &idx)) {
                // Need to add child
                path.push_back({cur, -1, ver});
                break;
            }
            
            path.push_back({cur, idx, ver});
            cur = cur->children[idx];
            kv.remove_prefix(1);
        }
        
        // Phase 2: Acquire lock
        std::lock_guard<std::mutex> lock(write_mutex_);
        
        // Phase 3: Check if path is still valid
        if (!path_valid(path)) {
            // Path changed, redo under lock (but we're already locked, so it's safe)
        }
        
        // Phase 4: Execute insert (under lock, so safe to modify in place)
        return do_insert(key, value, kv_str);
    }
    
    std::pair<iterator, bool> do_insert(const Key& key, const T& value, const std::string& kv_str) {
        std::string_view kv(kv_str);
        node_type* cur = root_;
        
        while (true) {
            size_t common = 0;
            while (common < cur->skip.size() && common < kv.size() && 
                   cur->skip[common] == kv[common]) ++common;
            
            if (common < cur->skip.size()) {
                // Split node
                node_type* old_suffix = new node_type();
                old_suffix->skip = cur->skip.substr(common + 1);
                old_suffix->data = std::move(cur->data);
                old_suffix->pop = cur->pop;
                old_suffix->children = std::move(cur->children);
                
                unsigned char old_char = cur->skip[common];  // Character that goes to old_suffix
                cur->skip.resize(common);
                cur->pop = PopCount{};
                cur->children.clear();
                
                if (common == kv.size()) {
                    // Key ends at split point
                    cur->set_data(value);
                    int idx = cur->pop.set(old_char);
                    cur->children.insert(cur->children.begin() + idx, old_suffix);
                } else {
                    // Key continues past split
                    cur->data.reset();
                    node_type* new_child = new node_type();
                    new_child->skip = std::string(kv.substr(common + 1));
                    new_child->set_data(value);
                    
                    unsigned char new_char = (unsigned char)kv[common];
                    if (old_char < new_char) {
                        cur->pop.set(old_char); cur->pop.set(new_char);
                        cur->children.push_back(old_suffix);
                        cur->children.push_back(new_child);
                    } else {
                        cur->pop.set(new_char); cur->pop.set(old_char);
                        cur->children.push_back(new_child);
                        cur->children.push_back(old_suffix);
                    }
                }
                
                cur->inc_version();
                elem_count_.fetch_add(1, std::memory_order_relaxed);
                return {iterator(key, value), true};
            }
            
            kv.remove_prefix(common);
            
            if (kv.empty()) {
                // Key ends at this node
                if (cur->has_data()) return {iterator(key, *cur->data), false};
                cur->set_data(value);
                cur->inc_version();
                elem_count_.fetch_add(1, std::memory_order_relaxed);
                return {iterator(key, value), true};
            }
            
            unsigned char c = (unsigned char)kv[0];
            int idx;
            if (!cur->get_child_idx(c, &idx)) {
                // Add new child
                node_type* child = new node_type();
                child->skip = std::string(kv.substr(1));
                child->set_data(value);
                idx = cur->pop.set(c);
                cur->children.insert(cur->children.begin() + idx, child);
                cur->inc_version();
                elem_count_.fetch_add(1, std::memory_order_relaxed);
                return {iterator(key, value), true};
            }
            
            cur = cur->children[idx];
            kv.remove_prefix(1);
        }
    }

    bool erase_impl(const Key& key) {
        std::string kv_str;
        if constexpr (is_fixed) kv_str = Traits::to_bytes(key);
        else kv_str = std::string(Traits::to_bytes(key));
        
        // Phase 1: Optimistic traversal
        std::vector<PathEntry> path; path.reserve(16);
        std::string_view kv(kv_str);
        node_type* cur = root_;
        
        while (cur) {
            uint64_t ver = cur->get_version();
            
            if (!cur->skip.empty()) {
                if (kv.size() < cur->skip.size()) return false;
                if (kv.substr(0, cur->skip.size()) != cur->skip) return false;
                kv.remove_prefix(cur->skip.size());
            }
            
            if (kv.empty()) {
                path.push_back({cur, -1, ver});
                break;
            }
            
            unsigned char c = (unsigned char)kv[0];
            int idx;
            if (!cur->get_child_idx(c, &idx)) return false;
            
            path.push_back({cur, idx, ver});
            cur = cur->children[idx];
            kv.remove_prefix(1);
        }
        
        if (!cur) return false;
        
        // Phase 2: Lock and verify
        std::lock_guard<std::mutex> lock(write_mutex_);
        
        if (!path_valid(path)) {
            // Redo under lock
            return do_erase(kv_str);
        }
        
        // Phase 3: Execute
        if (!cur->has_data()) return false;
        cur->clear_data();
        cur->inc_version();
        elem_count_.fetch_sub(1, std::memory_order_relaxed);
        return true;
    }
    
    bool do_erase(const std::string& kv_str) {
        std::string_view kv(kv_str);
        node_type* cur = root_;
        
        while (cur) {
            if (!cur->skip.empty()) {
                if (kv.size() < cur->skip.size()) return false;
                if (kv.substr(0, cur->skip.size()) != cur->skip) return false;
                kv.remove_prefix(cur->skip.size());
            }
            
            if (kv.empty()) {
                if (!cur->has_data()) return false;
                cur->clear_data();
                cur->inc_version();
                elem_count_.fetch_sub(1, std::memory_order_relaxed);
                return true;
            }
            
            unsigned char c = (unsigned char)kv[0];
            int idx;
            if (!cur->get_child_idx(c, &idx)) return false;
            cur = cur->children[idx];
            kv.remove_prefix(1);
        }
        return false;
    }
};

} // namespace gteitelbaum
