#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <iterator>
#include <mutex>
#include <string>
#include <shared_mutex>
#include <vector>
#include <utility>
#include <functional>
#include <initializer_list>
#include <stdexcept>

// 256-bit bitmap for sparse child indexing
class alignas(32) pop_tp {
    using bits_t = uint64_t;
    std::array<bits_t, 4> bits{};

public:
    bool find_pop(char c, int* cnt) const {
        uint8_t v = static_cast<uint8_t>(c);
        const int word = v >> 6;
        const int bit = v & 63;
        const bits_t mask = 1ULL << bit;
        if (!(bits[word] & mask)) return false;
        int idx = std::popcount(bits[word] & (mask - 1));
        for (int i = 0; i < word; ++i) idx += std::popcount(bits[i]);
        *cnt = idx;
        return true;
    }

    int set_bit(char c) {
        uint8_t v = static_cast<uint8_t>(c);
        const int word = v >> 6;
        const int bit = v & 63;
        const bits_t mask = 1ULL << bit;
        int idx = std::popcount(bits[word] & (mask - 1));
        for (int i = 0; i < word; ++i) idx += std::popcount(bits[i]);
        bits[word] |= mask;
        return idx;
    }

    int clear_bit(char c) {
        uint8_t v = static_cast<uint8_t>(c);
        const int word = v >> 6;
        const int bit = v & 63;
        const bits_t mask = 1ULL << bit;
        int idx = std::popcount(bits[word] & (mask - 1));
        for (int i = 0; i < word; ++i) idx += std::popcount(bits[i]);
        bits[word] &= ~mask;
        return idx;
    }

    bool has(char c) const {
        uint8_t v = static_cast<uint8_t>(c);
        return bits[v >> 6] & (1ULL << (v & 63));
    }

    int count() const {
        int total = 0;
        for (const auto& w : bits) total += std::popcount(w);
        return total;
    }

    bool empty() const {
        for (const auto& w : bits) if (w != 0) return false;
        return true;
    }

    char char_at_index(int target_idx) const {
        int current_idx = 0;
        for (int word = 0; word < 4; ++word) {
            bits_t w = bits[word];
            while (w != 0) {
                int bit = std::countr_zero(w);
                if (current_idx == target_idx) return static_cast<char>((word << 6) | bit);
                ++current_idx;
                w &= w - 1;
            }
        }
        return '\0';
    }

    char first_char() const {
        for (int word = 0; word < 4; ++word) {
            if (bits[word] != 0) {
                int bit = std::countr_zero(bits[word]);
                return static_cast<char>((word << 6) | bit);
            }
        }
        return '\0';
    }

    char next_char(char c) const {
        uint8_t v = static_cast<uint8_t>(c);
        int word = v >> 6;
        int bit = v & 63;
        bits_t mask = ~((1ULL << (bit + 1)) - 1);
        bits_t remaining = bits[word] & mask;
        if (remaining != 0) return static_cast<char>((word << 6) | std::countr_zero(remaining));
        for (int w = word + 1; w < 4; ++w) {
            if (bits[w] != 0) return static_cast<char>((w << 6) | std::countr_zero(bits[w]));
        }
        return '\0';
    }
};

template <class Key, class T> class tktrie;

template <class Key, class T>
class tktrie_node {
    friend class tktrie<Key, T>;

    mutable std::shared_mutex mtx;
    std::atomic<uint64_t> version{0};
    pop_tp pop{};
    tktrie_node* parent{nullptr};
    std::string skip{};
    std::vector<tktrie_node*> children{};
    bool has_data{false};
    T data{};
    char parent_edge{'\0'};

public:
    tktrie_node() = default;
    ~tktrie_node() { for (auto* c : children) delete c; }
    tktrie_node(const tktrie_node&) = delete;
    tktrie_node& operator=(const tktrie_node&) = delete;

    bool has_value() const { return has_data; }
    T& get_data() { return data; }
    const T& get_data() const { return data; }
    const std::string& get_skip() const { return skip; }
    int child_count() const { return pop.count(); }
    bool is_leaf() const { return pop.empty(); }
    tktrie_node* get_parent() const { return parent; }
    char get_parent_edge() const { return parent_edge; }
    uint64_t get_version() const { return version.load(std::memory_order_acquire); }

    tktrie_node* get_child(char c) const {
        int idx;
        if (pop.find_pop(c, &idx)) return children[idx];
        return nullptr;
    }

    char first_child_char() const { return pop.first_char(); }
    char next_child_char(char c) const { return pop.next_char(c); }

    void read_lock() const { mtx.lock_shared(); }
    void read_unlock() const { mtx.unlock_shared(); }
    void write_lock() { mtx.lock(); }
    void write_unlock() { mtx.unlock(); }
    void bump_version() { version.fetch_add(1, std::memory_order_release); }
};

// Forward declarations
template <class Key, class T> class tktrie_iterator;
template <class Key, class T> class tktrie_const_iterator;

// Iterator
template <class Key, class T>
class tktrie_iterator {
public:
    using iterator_category = std::bidirectional_iterator_tag;
    using value_type = std::pair<const Key, T>;
    using difference_type = std::ptrdiff_t;
    using pointer = value_type*;
    using reference = std::pair<const Key&, T&>;
    using node_type = tktrie_node<Key, T>;

private:
    node_type* current{nullptr};
    Key current_key;

    void find_next_data(node_type* n, Key prefix) {
        while (n) {
            current_key = prefix + n->get_skip();
            if (n->has_value()) { current = n; return; }
            char fc = n->first_child_char();
            if (fc != '\0') {
                prefix = current_key + fc;
                n = n->get_child(fc);
                continue;
            }
            while (n) {
                node_type* p = n->get_parent();
                if (!p) { current = nullptr; return; }
                char edge = n->get_parent_edge();
                size_t pkl = current_key.length() - n->get_skip().length() - 1;
                Key pk = current_key.substr(0, pkl);
                char next = p->next_child_char(edge);
                if (next != '\0') {
                    prefix = pk + next;
                    n = p->get_child(next);
                    break;
                }
                current_key = pk;
                n = p;
            }
        }
        current = nullptr;
    }

    void find_prev_data(node_type* n, Key prefix) {
        // Find rightmost node with data at or before current position
        while (n) {
            current_key = prefix + n->get_skip();
            
            // Try to go to last child first (rightmost path)
            char lc = '\0';
            for (int i = n->child_count() - 1; i >= 0; --i) {
                lc = n->pop.char_at_index(i);
                break;
            }
            
            if (lc != '\0') {
                prefix = current_key + lc;
                n = n->get_child(lc);
                continue;
            }
            
            // Leaf node - if has data, we're done
            if (n->has_value()) { current = n; return; }
            
            // Go up to find previous sibling or ancestor with data
            while (n) {
                node_type* p = n->get_parent();
                if (!p) { current = nullptr; return; }
                char edge = n->get_parent_edge();
                size_t pkl = current_key.length() - n->get_skip().length() - 1;
                Key pk = current_key.substr(0, pkl);
                
                // Find previous sibling
                char prev = '\0';
                for (int i = p->child_count() - 1; i >= 0; --i) {
                    char c = p->pop.char_at_index(i);
                    if (c < edge) { prev = c; break; }
                }
                
                if (prev != '\0') {
                    prefix = pk + prev;
                    n = p->get_child(prev);
                    break;
                }
                
                // No previous sibling - check if parent has data
                if (p->has_value()) {
                    current_key = pk;
                    current = p;
                    return;
                }
                
                current_key = pk;
                n = p;
            }
        }
        current = nullptr;
    }

    void advance() {
        if (!current) return;
        node_type* n = current;
        char fc = n->first_child_char();
        if (fc != '\0') {
            find_next_data(n->get_child(fc), current_key + fc);
            return;
        }
        while (n) {
            node_type* p = n->get_parent();
            if (!p) { current = nullptr; return; }
            char edge = n->get_parent_edge();
            size_t pkl = current_key.length() - n->get_skip().length() - 1;
            Key pk = current_key.substr(0, pkl);
            char next = p->next_child_char(edge);
            if (next != '\0') {
                find_next_data(p->get_child(next), pk + next);
                return;
            }
            current_key = pk;
            n = p;
        }
        current = nullptr;
    }

    void retreat() {
        if (!current) return;
        node_type* n = current;
        node_type* p = n->get_parent();
        if (!p) { current = nullptr; return; }
        
        char edge = n->get_parent_edge();
        size_t pkl = current_key.length() - n->get_skip().length() - 1;
        Key pk = current_key.substr(0, pkl);
        
        // Find previous sibling
        char prev = '\0';
        for (int i = p->child_count() - 1; i >= 0; --i) {
            char c = p->pop.char_at_index(i);
            if (c < edge) { prev = c; break; }
        }
        
        if (prev != '\0') {
            find_prev_data(p->get_child(prev), pk + prev);
            return;
        }
        
        // No previous sibling - parent is previous if it has data
        if (p->has_value()) {
            current_key = pk;
            current = p;
            return;
        }
        
        // Go up further
        current_key = pk;
        current = p;
        retreat();
    }

public:
    tktrie_iterator() = default;
    tktrie_iterator(node_type* root, bool is_end = false) {
        if (is_end || !root) { current = nullptr; return; }
        find_next_data(root, "");
    }
    tktrie_iterator(node_type* n, const Key& key) : current(n), current_key(key) {}
    
    reference operator*() const { return {current_key, current->get_data()}; }
    
    tktrie_iterator& operator++() { advance(); return *this; }
    tktrie_iterator operator++(int) { auto tmp = *this; advance(); return tmp; }
    tktrie_iterator& operator--() { retreat(); return *this; }
    tktrie_iterator operator--(int) { auto tmp = *this; retreat(); return tmp; }
    
    bool operator==(const tktrie_iterator& o) const { return current == o.current; }
    bool operator!=(const tktrie_iterator& o) const { return current != o.current; }
    
    node_type* get_node() const { return current; }
    const Key& key() const { return current_key; }
};

// Const iterator
template <class Key, class T>
class tktrie_const_iterator {
public:
    using iterator_category = std::bidirectional_iterator_tag;
    using value_type = std::pair<const Key, T>;
    using difference_type = std::ptrdiff_t;
    using pointer = const value_type*;
    using reference = std::pair<const Key&, const T&>;
    using node_type = tktrie_node<Key, T>;

private:
    const node_type* current{nullptr};
    Key current_key;

    void find_next_data(const node_type* n, Key prefix) {
        while (n) {
            current_key = prefix + n->get_skip();
            if (n->has_value()) { current = n; return; }
            char fc = n->first_child_char();
            if (fc != '\0') {
                prefix = current_key + fc;
                n = n->get_child(fc);
                continue;
            }
            while (n) {
                const node_type* p = n->get_parent();
                if (!p) { current = nullptr; return; }
                char edge = n->get_parent_edge();
                size_t pkl = current_key.length() - n->get_skip().length() - 1;
                Key pk = current_key.substr(0, pkl);
                char next = p->next_child_char(edge);
                if (next != '\0') {
                    prefix = pk + next;
                    n = p->get_child(next);
                    break;
                }
                current_key = pk;
                n = p;
            }
        }
        current = nullptr;
    }

    void find_prev_data(const node_type* n, Key prefix) {
        while (n) {
            current_key = prefix + n->get_skip();
            char lc = '\0';
            for (int i = n->child_count() - 1; i >= 0; --i) {
                lc = n->pop.char_at_index(i);
                break;
            }
            if (lc != '\0') {
                prefix = current_key + lc;
                n = n->get_child(lc);
                continue;
            }
            if (n->has_value()) { current = n; return; }
            while (n) {
                const node_type* p = n->get_parent();
                if (!p) { current = nullptr; return; }
                char edge = n->get_parent_edge();
                size_t pkl = current_key.length() - n->get_skip().length() - 1;
                Key pk = current_key.substr(0, pkl);
                char prev = '\0';
                for (int i = p->child_count() - 1; i >= 0; --i) {
                    char c = p->pop.char_at_index(i);
                    if (c < edge) { prev = c; break; }
                }
                if (prev != '\0') {
                    prefix = pk + prev;
                    n = p->get_child(prev);
                    break;
                }
                if (p->has_value()) {
                    current_key = pk;
                    current = p;
                    return;
                }
                current_key = pk;
                n = p;
            }
        }
        current = nullptr;
    }

    void advance() {
        if (!current) return;
        const node_type* n = current;
        char fc = n->first_child_char();
        if (fc != '\0') {
            find_next_data(n->get_child(fc), current_key + fc);
            return;
        }
        while (n) {
            const node_type* p = n->get_parent();
            if (!p) { current = nullptr; return; }
            char edge = n->get_parent_edge();
            size_t pkl = current_key.length() - n->get_skip().length() - 1;
            Key pk = current_key.substr(0, pkl);
            char next = p->next_child_char(edge);
            if (next != '\0') {
                find_next_data(p->get_child(next), pk + next);
                return;
            }
            current_key = pk;
            n = p;
        }
        current = nullptr;
    }

    void retreat() {
        if (!current) return;
        const node_type* n = current;
        const node_type* p = n->get_parent();
        if (!p) { current = nullptr; return; }
        char edge = n->get_parent_edge();
        size_t pkl = current_key.length() - n->get_skip().length() - 1;
        Key pk = current_key.substr(0, pkl);
        char prev = '\0';
        for (int i = p->child_count() - 1; i >= 0; --i) {
            char c = p->pop.char_at_index(i);
            if (c < edge) { prev = c; break; }
        }
        if (prev != '\0') {
            find_prev_data(p->get_child(prev), pk + prev);
            return;
        }
        if (p->has_value()) {
            current_key = pk;
            current = p;
            return;
        }
        current_key = pk;
        current = const_cast<node_type*>(p);
        retreat();
    }

public:
    tktrie_const_iterator() = default;
    tktrie_const_iterator(const node_type* root, bool is_end = false) {
        if (is_end || !root) { current = nullptr; return; }
        find_next_data(root, "");
    }
    tktrie_const_iterator(const node_type* n, const Key& key) : current(n), current_key(key) {}
    tktrie_const_iterator(const tktrie_iterator<Key, T>& other) 
        : current(other.get_node()), current_key(other.key()) {}
    
    reference operator*() const { return {current_key, current->get_data()}; }
    
    tktrie_const_iterator& operator++() { advance(); return *this; }
    tktrie_const_iterator operator++(int) { auto tmp = *this; advance(); return tmp; }
    tktrie_const_iterator& operator--() { retreat(); return *this; }
    tktrie_const_iterator operator--(int) { auto tmp = *this; retreat(); return tmp; }
    
    bool operator==(const tktrie_const_iterator& o) const { return current == o.current; }
    bool operator!=(const tktrie_const_iterator& o) const { return current != o.current; }
    
    const node_type* get_node() const { return current; }
    const Key& key() const { return current_key; }
};

/**
 * Thread-safe prefix trie (radix tree) with std::map-like interface.
 * 
 * THREAD SAFETY:
 * - All operations are internally synchronized using fine-grained locking.
 * - Multiple readers can operate concurrently.
 * - Writers are serialized at the node level.
 * 
 * IMPORTANT LIMITATIONS:
 * - Iterators and references are NOT protected after the function returns.
 *   Using an iterator while another thread modifies the trie is undefined behavior.
 * - For safe concurrent iteration, hold an external lock or use keys_with_prefix()
 *   which returns a snapshot.
 * - at() and operator[] return references that may become invalid if another
 *   thread erases the key.
 * 
 * SAFE PATTERNS:
 * - Single-threaded use
 * - Multiple readers with no concurrent writers
 * - Concurrent insert/find/erase on different keys
 * - Use keys_with_prefix() for thread-safe prefix queries
 * 
 * UNSAFE PATTERNS:
 * - Iterating while another thread modifies the trie
 * - Holding references from at()/operator[] across other operations
 * - Using iterators returned by find() while another thread may erase
 */
template <class Key, class T>
class tktrie {
public:
    // Standard container type aliases (std::map compatible)
    using key_type = Key;
    using mapped_type = T;
    using value_type = std::pair<const Key, T>;
    using size_type = std::size_t;
    using difference_type = std::ptrdiff_t;
    using reference = value_type&;
    using const_reference = const value_type&;
    using pointer = value_type*;
    using const_pointer = const value_type*;
    using iterator = tktrie_iterator<Key, T>;
    using const_iterator = tktrie_const_iterator<Key, T>;
    using reverse_iterator = std::reverse_iterator<iterator>;
    using const_reverse_iterator = std::reverse_iterator<const_iterator>;
    using node_type = tktrie_node<Key, T>;

private:
    node_type head;
    std::atomic<size_type> elem_count{0};
    mutable std::shared_mutex global_mtx;
    static constexpr int MAX_RETRIES = 100;

public:
    // Constructors
    tktrie() = default;
    
    tktrie(std::initializer_list<value_type> init) {
        for (const auto& p : init) insert(p);
    }
    
    template <class InputIt>
    tktrie(InputIt first, InputIt last) {
        for (; first != last; ++first) insert(first->first, first->second);
    }
    
    ~tktrie() = default;
    
    tktrie(const tktrie&) = delete;
    tktrie& operator=(const tktrie&) = delete;
    tktrie(tktrie&&) = delete;
    tktrie& operator=(tktrie&&) = delete;

    // Iterators
    iterator begin() noexcept { std::shared_lock g(global_mtx); return iterator(&head); }
    iterator end() noexcept { return iterator(nullptr, true); }
    const_iterator begin() const noexcept { std::shared_lock g(global_mtx); return const_iterator(&head); }
    const_iterator end() const noexcept { return const_iterator(nullptr, true); }
    const_iterator cbegin() const noexcept { return begin(); }
    const_iterator cend() const noexcept { return end(); }
    reverse_iterator rbegin() noexcept { return reverse_iterator(end()); }
    reverse_iterator rend() noexcept { return reverse_iterator(begin()); }
    const_reverse_iterator rbegin() const noexcept { return const_reverse_iterator(end()); }
    const_reverse_iterator rend() const noexcept { return const_reverse_iterator(begin()); }
    const_reverse_iterator crbegin() const noexcept { return rbegin(); }
    const_reverse_iterator crend() const noexcept { return rend(); }

    // Capacity
    [[nodiscard]] bool empty() const noexcept { return size() == 0; }
    size_type size() const noexcept { return elem_count.load(std::memory_order_relaxed); }
    size_type max_size() const noexcept { return std::numeric_limits<size_type>::max(); }

    // Element access
    T& at(const Key& key) {
        node_type* n = find_node(key);
        if (!n) throw std::out_of_range("tktrie::at: key not found");
        return n->get_data();
    }
    
    const T& at(const Key& key) const {
        const node_type* n = find_node(key);
        if (!n) throw std::out_of_range("tktrie::at: key not found");
        return n->get_data();
    }
    
    T& operator[](const Key& key) {
        node_type* n = find_node(key);
        if (n) return n->get_data();
        insert(std::make_pair(key, T{}));
        return find_node(key)->get_data();
    }
    
    T& operator[](Key&& key) {
        node_type* n = find_node(key);
        if (n) return n->get_data();
        Key key_copy = key;  // Copy before potential move
        insert(std::make_pair(std::move(key), T{}));
        return find_node(key_copy)->get_data();
    }

    // Modifiers
    void clear() noexcept {
        std::unique_lock glock(global_mtx);
        head.write_lock();
        for (auto* c : head.children) delete c;
        head.children.clear();
        head.pop = pop_tp{};
        head.skip.clear();
        head.has_data = false;
        head.data = T{};
        head.bump_version();
        head.write_unlock();
        elem_count.store(0);
    }

    std::pair<iterator, bool> insert(const value_type& value) {
        return insert_impl(value.first, value.second);
    }
    
    std::pair<iterator, bool> insert(value_type&& value) {
        return insert_impl(std::move(value.first), std::move(value.second));
    }
    
    template <class P, typename = std::enable_if_t<std::is_constructible_v<value_type, P&&>>>
    std::pair<iterator, bool> insert(P&& value) {
        return insert(value_type(std::forward<P>(value)));
    }
    
    iterator insert(const_iterator hint, const value_type& value) {
        (void)hint;  // Hint ignored for trie
        return insert(value).first;
    }
    
    template <class InputIt>
    void insert(InputIt first, InputIt last) {
        for (; first != last; ++first) insert(*first);
    }
    
    void insert(std::initializer_list<value_type> ilist) {
        for (const auto& v : ilist) insert(v);
    }

    template <class M>
    std::pair<iterator, bool> insert_or_assign(const Key& key, M&& obj) {
        // Try to find and update existing
        {
            std::shared_lock glock(global_mtx);
            node_type* n = find_node_internal(key);
            if (n) {
                n->write_lock();
                n->data = std::forward<M>(obj);
                n->bump_version();
                n->write_unlock();
                return {iterator(n, key), false};
            }
        }
        // Insert new
        return insert_impl(key, std::forward<M>(obj));
    }

    template <class... Args>
    std::pair<iterator, bool> emplace(Args&&... args) {
        value_type val(std::forward<Args>(args)...);
        return insert(std::move(val));
    }

    template <class... Args>
    std::pair<iterator, bool> try_emplace(const Key& key, Args&&... args) {
        // Check if exists first
        {
            std::shared_lock glock(global_mtx);
            node_type* n = find_node_internal(key);
            if (n) return {iterator(n, key), false};
        }
        // Doesn't exist - insert (may race, insert_impl handles that)
        return insert_impl(key, T(std::forward<Args>(args)...));
    }

    iterator erase(const_iterator pos) {
        if (pos == cend()) return end();
        Key k = pos.key();
        auto next = pos;
        ++next;
        Key next_key = (next != cend()) ? next.key() : Key{};
        bool has_next = (next != cend());
        
        erase(k);
        
        if (has_next) return find(next_key);
        return end();
    }
    
    iterator erase(const_iterator first, const_iterator last) {
        std::vector<Key> to_erase;
        for (auto it = first; it != last; ++it) {
            to_erase.push_back(it.key());
        }
        for (const auto& k : to_erase) erase(k);
        return (last != cend()) ? find(last.key()) : end();
    }
    
    size_type erase(const Key& key) {
        for (int retry = 0; retry < MAX_RETRIES; ++retry) {
            auto [success, was_removed] = try_remove(key);
            if (success) {
                if (was_removed) {
                    elem_count.fetch_sub(1, std::memory_order_relaxed);
                    return 1;
                }
                return 0;
            }
        }
        return remove_fallback(key) ? 1 : 0;
    }

    void swap(tktrie& other) noexcept {
        // Not truly safe for concurrent access, but matches std::map signature
        std::scoped_lock lock(global_mtx, other.global_mtx);
        std::swap(head, other.head);
        size_type tmp = elem_count.load();
        elem_count.store(other.elem_count.load());
        other.elem_count.store(tmp);
    }

    // Lookup
    size_type count(const Key& key) const { return find_node(key) ? 1 : 0; }
    
    iterator find(const Key& key) {
        std::shared_lock glock(global_mtx);
        node_type* n = find_node_internal(key);
        if (!n) return end();
        return iterator(n, key);
    }
    
    const_iterator find(const Key& key) const {
        std::shared_lock glock(global_mtx);
        const node_type* n = find_node_internal(key);
        if (!n) return end();
        return const_iterator(n, key);
    }
    
    bool contains(const Key& key) const { return find_node(key) != nullptr; }

    std::pair<iterator, iterator> equal_range(const Key& key) {
        std::shared_lock glock(global_mtx);
        node_type* n = find_node_internal(key);
        if (!n) return {end(), end()};
        iterator it(n, key);
        auto next = it;
        ++next;
        return {it, next};
    }
    
    std::pair<const_iterator, const_iterator> equal_range(const Key& key) const {
        std::shared_lock glock(global_mtx);
        const node_type* n = find_node_internal(key);
        if (!n) return {end(), end()};
        const_iterator it(n, key);
        auto next = it;
        ++next;
        return {it, next};
    }

    iterator lower_bound(const Key& key) {
        std::shared_lock glock(global_mtx);
        return lower_bound_impl(key);
    }
    
    const_iterator lower_bound(const Key& key) const {
        std::shared_lock glock(global_mtx);
        return lower_bound_impl(key);
    }

    iterator upper_bound(const Key& key) {
        std::shared_lock glock(global_mtx);
        auto it = lower_bound_impl(key);
        if (it != end() && it.key() == key) ++it;
        return it;
    }
    
    const_iterator upper_bound(const Key& key) const {
        std::shared_lock glock(global_mtx);
        auto it = lower_bound_impl(key);
        if (it != end() && it.key() == key) ++it;
        return it;
    }

    // === TRIE-SPECIFIC EXTENSIONS ===
    
    // Compact the trie by removing dead nodes and merging single-child paths
    void compact() {
        std::unique_lock glock(global_mtx);
        compact_node(&head);
    }

    // Find range of all keys with given prefix
    // Returns {first_match, past_last_match}
    // If no matches: {end(), end()}
    std::pair<iterator, iterator> prefixed_range(const Key& prefix) {
        std::shared_lock glock(global_mtx);
        
        // Find the subtree root and the key path to it
        node_type* cur = const_cast<node_type*>(&head);
        size_t kpos = 0;
        Key path_key;

        while (kpos < prefix.size()) {
            const std::string& skip = cur->get_skip();
            
            size_t common = 0;
            while (common < skip.size() && kpos + common < prefix.size() &&
                   skip[common] == prefix[kpos + common]) ++common;
            
            path_key += skip.substr(0, common);
            kpos += common;
            
            if (common < skip.size()) {
                if (kpos == prefix.size()) {
                    // Prefix ended in middle of skip - all descendants match
                    break;
                }
                // Prefix diverges from skip - no matches
                return {end(), end()};
            }
            
            if (kpos < prefix.size()) {
                char c = prefix[kpos];
                node_type* child = cur->get_child(c);
                if (!child) return {end(), end()};
                path_key += c;
                cur = child;
                kpos++;
            }
        }
        
        // cur is the subtree root, path_key is the key to get there
        // Find first key in subtree
        iterator first_it = find_leftmost_in_subtree(cur, path_key);
        if (first_it == end()) return {end(), end()};
        
        // Find first key NOT in subtree (past the end of prefix range)
        iterator end_it = find_first_past_subtree(cur, path_key);
        
        return {first_it, end_it};
    }
    
    std::pair<const_iterator, const_iterator> prefixed_range(const Key& prefix) const {
        auto [first, last] = const_cast<tktrie*>(this)->prefixed_range(prefix);
        return {const_iterator(first.get_node(), first.key()), 
                const_iterator(last.get_node(), last.key())};
    }

private:
    // Find leftmost key in a subtree
    iterator find_leftmost_in_subtree(node_type* subtree_root, const Key& path_to_root) {
        Key key = path_to_root;
        node_type* cur = subtree_root;
        
        while (cur) {
            key += cur->get_skip();
            
            if (cur->has_value()) {
                return iterator(cur, key);
            }
            
            char fc = cur->first_child_char();
            if (fc == '\0') {
                // Dead node with no children and no data - skip it
                // This shouldn't happen in well-formed trie, but handle it
                return end();
            }
            
            key += fc;
            cur = cur->get_child(fc);
        }
        
        return end();
    }
    
    // Find first key that is NOT under the given subtree
    iterator find_first_past_subtree(node_type* subtree_root, const Key& path_to_root) {
        node_type* cur = subtree_root;
        Key cur_path = path_to_root;
        
        // Go up the tree looking for a next sibling
        while (cur) {
            node_type* parent = cur->get_parent();
            if (!parent) {
                // Reached root - no keys after this subtree
                return end();
            }
            
            char edge = cur->get_parent_edge();
            // Compute parent's path
            Key parent_path = cur_path.substr(0, cur_path.size() - cur->get_skip().size() - 1);
            
            // Look for next sibling
            char next_edge = parent->next_child_char(edge);
            if (next_edge != '\0') {
                // Found a sibling - find leftmost key in that subtree
                return find_leftmost_in_subtree(parent->get_child(next_edge), parent_path + next_edge);
            }
            
            // No sibling - go up further
            cur = parent;
            cur_path = parent_path;
        }
        
        return end();
    }

public:
    std::vector<Key> keys_with_prefix(const Key& prefix) const {
        std::shared_lock glock(global_mtx);
        std::vector<Key> result;
        node_type* cur = const_cast<node_type*>(&head);
        size_t kpos = 0;

        while (kpos < prefix.size()) {
            const std::string& skip = cur->get_skip();
            size_t common = 0;
            while (common < skip.size() && kpos + common < prefix.size() &&
                   skip[common] == prefix[kpos + common]) ++common;
            kpos += common;
            if (common < skip.size()) {
                if (kpos == prefix.size()) break;
                return result;
            }
            if (kpos == prefix.size()) break;
            char c = prefix[kpos++];
            node_type* child = cur->get_child(c);
            if (!child) return result;
            cur = child;
        }

        std::function<void(const node_type*, const Key&)> collect =
            [&](const node_type* n, const Key& ksf) {
                Key fk = ksf + n->get_skip();
                if (n->has_value()) result.push_back(fk);
                for (int i = 0; i < n->child_count(); ++i) {
                    char c = n->pop.char_at_index(i);
                    collect(n->children[i], fk + c);
                }
            };
        collect(cur, prefix.substr(0, prefix.size() - (prefix.size() - kpos)));
        return result;
    }

private:
    node_type* find_node(const Key& key) const {
        std::shared_lock glock(global_mtx);
        return find_node_internal(key);
    }

    node_type* find_node_internal(const Key& key) const {
        node_type* cur = const_cast<node_type*>(&head);
        cur->read_lock();
        size_t kpos = 0;

        while (true) {
            const std::string& skip = cur->get_skip();
            if (!skip.empty()) {
                if (key.size() - kpos < skip.size()) { cur->read_unlock(); return nullptr; }
                for (size_t i = 0; i < skip.size(); ++i) {
                    if (key[kpos + i] != skip[i]) { cur->read_unlock(); return nullptr; }
                }
                kpos += skip.size();
            }
            if (kpos == key.size()) {
                bool has = cur->has_value();
                cur->read_unlock();
                return has ? cur : nullptr;
            }
            char c = key[kpos++];
            node_type* child = cur->get_child(c);
            if (!child) { cur->read_unlock(); return nullptr; }
            child->read_lock();
            cur->read_unlock();
            cur = child;
        }
    }

    std::pair<iterator, bool> insert_impl(const Key& key, const T& value) {
        for (int retry = 0; retry < MAX_RETRIES; ++retry) {
            auto [success, was_new, node_ptr] = try_insert(key, value);
            if (success) {
                if (was_new) elem_count.fetch_add(1, std::memory_order_relaxed);
                return {iterator(node_ptr, key), was_new};
            }
        }
        return insert_fallback_with_iter(key, value);
    }

    std::tuple<bool, bool, node_type*> try_insert(const Key& key, const T& value) {
        std::shared_lock glock(global_mtx);
        node_type* cur = &head;
        size_t kpos = 0;
        cur->read_lock();
        uint64_t cur_ver = cur->get_version();

        while (true) {
            const std::string& skip = cur->skip;
            size_t common = 0;
            while (common < skip.size() && kpos + common < key.size() &&
                   skip[common] == key[kpos + common]) ++common;

            if (kpos + common == key.size() && common == skip.size()) {
                cur->read_unlock();
                cur->write_lock();
                if (cur->get_version() != cur_ver) { cur->write_unlock(); return {false, false, nullptr}; }
                bool was_new = !cur->has_data;
                cur->has_data = true;
                if (was_new) cur->data = value;
                cur->bump_version();
                cur->write_unlock();
                return {true, was_new, cur};
            }

            if (kpos + common == key.size()) {
                cur->read_unlock();
                cur->write_lock();
                if (cur->get_version() != cur_ver) { cur->write_unlock(); return {false, false, nullptr}; }
                auto* child = new node_type();
                child->skip = skip.substr(common + 1);
                child->has_data = cur->has_data;
                child->data = std::move(cur->data);
                child->children = std::move(cur->children);
                child->pop = cur->pop;
                child->parent = cur;
                child->parent_edge = skip[common];
                for (auto* gc : child->children) if (gc) gc->parent = child;
                cur->skip = skip.substr(0, common);
                cur->has_data = true;
                cur->data = value;
                cur->children.clear();
                cur->pop = pop_tp{};
                int idx = cur->pop.set_bit(child->parent_edge);
                cur->children.insert(cur->children.begin() + idx, child);
                cur->bump_version();
                cur->write_unlock();
                return {true, true, cur};
            }

            if (common == skip.size()) {
                kpos += common;
                char c = key[kpos];
                node_type* child = cur->get_child(c);
                if (child) {
                    child->read_lock();
                    cur->read_unlock();
                    cur = child;
                    cur_ver = cur->get_version();
                    kpos++;
                    continue;
                }
                cur->read_unlock();
                cur->write_lock();
                if (cur->get_version() != cur_ver) { cur->write_unlock(); return {false, false, nullptr}; }
                if (cur->get_child(c)) { cur->write_unlock(); return {false, false, nullptr}; }
                auto* newc = new node_type();
                newc->skip = key.substr(kpos + 1);
                newc->has_data = true;
                newc->data = value;
                newc->parent = cur;
                newc->parent_edge = c;
                int idx = cur->pop.set_bit(c);
                cur->children.insert(cur->children.begin() + idx, newc);
                cur->bump_version();
                cur->write_unlock();
                return {true, true, newc};
            }

            cur->read_unlock();
            cur->write_lock();
            if (cur->get_version() != cur_ver) { cur->write_unlock(); return {false, false, nullptr}; }
            auto* old_child = new node_type();
            old_child->skip = skip.substr(common + 1);
            old_child->has_data = cur->has_data;
            old_child->data = std::move(cur->data);
            old_child->children = std::move(cur->children);
            old_child->pop = cur->pop;
            old_child->parent = cur;
            old_child->parent_edge = skip[common];
            for (auto* gc : old_child->children) if (gc) gc->parent = old_child;
            auto* new_child = new node_type();
            new_child->skip = key.substr(kpos + common + 1);
            new_child->has_data = true;
            new_child->data = value;
            new_child->parent = cur;
            new_child->parent_edge = key[kpos + common];
            cur->skip = skip.substr(0, common);
            cur->has_data = false;
            cur->data = T{};
            cur->children.clear();
            cur->pop = pop_tp{};
            int i1 = cur->pop.set_bit(old_child->parent_edge);
            cur->children.insert(cur->children.begin() + i1, old_child);
            int i2 = cur->pop.set_bit(new_child->parent_edge);
            cur->children.insert(cur->children.begin() + i2, new_child);
            cur->bump_version();
            cur->write_unlock();
            return {true, true, new_child};
        }
    }

    std::pair<bool, bool> try_remove(const Key& key) {
        std::shared_lock glock(global_mtx);
        node_type* cur = &head;
        size_t kpos = 0;
        cur->read_lock();
        uint64_t cur_ver = cur->get_version();

        while (true) {
            const std::string& skip = cur->skip;
            if (!skip.empty()) {
                if (key.size() - kpos < skip.size()) { cur->read_unlock(); return {true, false}; }
                for (size_t i = 0; i < skip.size(); ++i) {
                    if (key[kpos + i] != skip[i]) { cur->read_unlock(); return {true, false}; }
                }
                kpos += skip.size();
            }
            if (kpos == key.size()) {
                if (!cur->has_data) { cur->read_unlock(); return {true, false}; }
                cur->read_unlock();
                cur->write_lock();
                if (cur->get_version() != cur_ver) { cur->write_unlock(); return {false, false}; }
                if (!cur->has_data) { cur->write_unlock(); return {true, false}; }
                cur->has_data = false;
                cur->data = T{};
                cur->bump_version();
                cur->write_unlock();
                return {true, true};
            }
            char c = key[kpos];
            node_type* child = cur->get_child(c);
            if (!child) { cur->read_unlock(); return {true, false}; }
            child->read_lock();
            cur->read_unlock();
            cur = child;
            cur_ver = cur->get_version();
            kpos++;
        }
    }

    std::pair<iterator, bool> insert_fallback_with_iter(const Key& key, const T& value) {
        std::unique_lock glock(global_mtx);
        node_type* cur = &head;
        size_t kpos = 0;

        while (true) {
            cur->write_lock();
            const std::string& skip = cur->skip;
            size_t common = 0;
            while (common < skip.size() && kpos + common < key.size() &&
                   skip[common] == key[kpos + common]) ++common;

            if (kpos + common == key.size() && common == skip.size()) {
                bool was_new = !cur->has_data;
                cur->has_data = true;
                if (was_new) { cur->data = value; elem_count.fetch_add(1); }
                cur->bump_version();
                cur->write_unlock();
                return {iterator(cur, key), was_new};
            }

            if (kpos + common == key.size()) {
                auto* child = new node_type();
                child->skip = skip.substr(common + 1);
                child->has_data = cur->has_data;
                child->data = std::move(cur->data);
                child->children = std::move(cur->children);
                child->pop = cur->pop;
                child->parent = cur;
                child->parent_edge = skip[common];
                for (auto* gc : child->children) if (gc) gc->parent = child;
                cur->skip = skip.substr(0, common);
                cur->has_data = true;
                cur->data = value;
                cur->children.clear();
                cur->pop = pop_tp{};
                int idx = cur->pop.set_bit(child->parent_edge);
                cur->children.insert(cur->children.begin() + idx, child);
                cur->bump_version();
                cur->write_unlock();
                elem_count.fetch_add(1);
                return {iterator(cur, key), true};
            }

            if (common == skip.size()) {
                kpos += common;
                char c = key[kpos];
                node_type* child = cur->get_child(c);
                if (child) { cur->write_unlock(); cur = child; kpos++; continue; }
                auto* newc = new node_type();
                newc->skip = key.substr(kpos + 1);
                newc->has_data = true;
                newc->data = value;
                newc->parent = cur;
                newc->parent_edge = c;
                int idx = cur->pop.set_bit(c);
                cur->children.insert(cur->children.begin() + idx, newc);
                cur->bump_version();
                cur->write_unlock();
                elem_count.fetch_add(1);
                return {iterator(newc, key), true};
            }

            auto* old_child = new node_type();
            old_child->skip = skip.substr(common + 1);
            old_child->has_data = cur->has_data;
            old_child->data = std::move(cur->data);
            old_child->children = std::move(cur->children);
            old_child->pop = cur->pop;
            old_child->parent = cur;
            old_child->parent_edge = skip[common];
            for (auto* gc : old_child->children) if (gc) gc->parent = old_child;
            auto* new_child = new node_type();
            new_child->skip = key.substr(kpos + common + 1);
            new_child->has_data = true;
            new_child->data = value;
            new_child->parent = cur;
            new_child->parent_edge = key[kpos + common];
            cur->skip = skip.substr(0, common);
            cur->has_data = false;
            cur->data = T{};
            cur->children.clear();
            cur->pop = pop_tp{};
            int i1 = cur->pop.set_bit(old_child->parent_edge);
            cur->children.insert(cur->children.begin() + i1, old_child);
            int i2 = cur->pop.set_bit(new_child->parent_edge);
            cur->children.insert(cur->children.begin() + i2, new_child);
            cur->bump_version();
            cur->write_unlock();
            elem_count.fetch_add(1);
            return {iterator(new_child, key), true};
        }
    }

    bool remove_fallback(const Key& key) {
        std::unique_lock glock(global_mtx);
        node_type* cur = &head;
        size_t kpos = 0;

        while (true) {
            cur->write_lock();
            const std::string& skip = cur->skip;
            if (!skip.empty()) {
                if (key.size() - kpos < skip.size()) { cur->write_unlock(); return false; }
                for (size_t i = 0; i < skip.size(); ++i) {
                    if (key[kpos + i] != skip[i]) { cur->write_unlock(); return false; }
                }
                kpos += skip.size();
            }
            if (kpos == key.size()) {
                if (!cur->has_data) { cur->write_unlock(); return false; }
                cur->has_data = false;
                cur->data = T{};
                cur->bump_version();
                cur->write_unlock();
                elem_count.fetch_sub(1);
                return true;
            }
            char c = key[kpos];
            node_type* child = cur->get_child(c);
            cur->write_unlock();
            if (!child) return false;
            cur = child;
            kpos++;
        }
    }

    bool compact_node(node_type* n) {
        n->write_lock();
        std::vector<char> children_to_remove;
        for (int i = 0; i < n->child_count(); ++i) {
            char c = n->pop.char_at_index(i);
            node_type* child = n->get_child(c);
            if (child) {
                n->write_unlock();
                bool should_delete = compact_node(child);
                n->write_lock();
                if (should_delete) children_to_remove.push_back(c);
            }
        }
        for (char c : children_to_remove) {
            node_type* child = n->get_child(c);
            if (child) {
                int idx;
                if (n->pop.find_pop(c, &idx)) {
                    n->pop.clear_bit(c);
                    n->children.erase(n->children.begin() + idx);
                    delete child;
                }
            }
        }
        if (!n->has_data && n->pop.empty() && n->parent != nullptr) {
            n->bump_version();
            n->write_unlock();
            return true;
        }
        if (!n->has_data && n->child_count() == 1 && n->parent != nullptr) {
            char c = n->pop.first_char();
            node_type* child = n->get_child(c);
            if (child) {
                child->write_lock();
                std::string merged_skip = n->skip + c + child->skip;
                n->skip = merged_skip;
                n->has_data = child->has_data;
                n->data = std::move(child->data);
                n->pop = child->pop;
                std::vector<node_type*> grandchildren = std::move(child->children);
                child->children.clear();
                child->pop = pop_tp{};
                n->children = std::move(grandchildren);
                for (auto* gc : n->children) {
                    if (gc) { gc->write_lock(); gc->parent = n; gc->write_unlock(); }
                }
                child->write_unlock();
                delete child;
                n->bump_version();
            }
        }
        n->bump_version();
        n->write_unlock();
        return false;
    }

    template <class Iter>
    std::pair<Iter, Iter> prefixed_range_impl(const Key& prefix) {
        node_type* cur = const_cast<node_type*>(&head);
        size_t kpos = 0;
        Key built_prefix;

        // Navigate to the node that matches the prefix
        while (kpos < prefix.size()) {
            const std::string& skip = cur->get_skip();
            
            // Check how much of skip matches remaining prefix
            size_t common = 0;
            while (common < skip.size() && kpos + common < prefix.size() &&
                   skip[common] == prefix[kpos + common]) ++common;
            
            built_prefix += skip.substr(0, common);
            kpos += common;
            
            if (common < skip.size()) {
                // Prefix ended in middle of skip or diverged
                if (kpos == prefix.size()) {
                    // Prefix ended in middle of skip - all keys under this node match
                    // Find first and last+1 key under cur
                    Iter first_it(cur, built_prefix);
                    if (!cur->has_value()) {
                        // Advance to first actual data node
                        ++first_it;
                        if (first_it == Iter(nullptr, true)) {
                            return {Iter(nullptr, true), Iter(nullptr, true)};
                        }
                    }
                    
                    // Find the key just after all keys with this prefix
                    Iter end_it = find_past_prefix_end<Iter>(cur, built_prefix);
                    return {first_it, end_it};
                }
                // Diverged - prefix doesn't match anything
                return {Iter(nullptr, true), Iter(nullptr, true)};
            }
            
            // Skip fully matched, continue to child if more prefix remains
            if (kpos < prefix.size()) {
                char c = prefix[kpos];
                node_type* child = cur->get_child(c);
                if (!child) {
                    // No child with this char - no matches
                    return {Iter(nullptr, true), Iter(nullptr, true)};
                }
                built_prefix += c;
                cur = child;
                kpos++;
            }
        }
        
        // Found node matching prefix exactly (prefix fully consumed)
        // Return range of all keys under this subtree
        Iter first_it(cur, built_prefix);
        if (!cur->has_value()) {
            // Need to find first actual data node in subtree
            // The iterator constructor will do this
            first_it = Iter(cur, false);
            // Reconstruct the proper key
            if (first_it != Iter(nullptr, true)) {
                // Iterator found something - we need to rebuild with correct prefix
            }
        }
        
        // Actually let's use a simpler approach: find first matching and first non-matching
        auto first = find_first_with_prefix<Iter>(cur, built_prefix);
        auto last = find_past_prefix_end<Iter>(cur, built_prefix);
        
        return {first, last};
    }

    template <class Iter>
    Iter find_first_with_prefix(node_type* subtree_root, const Key& prefix_so_far) {
        // Find leftmost key in subtree
        node_type* cur = subtree_root;
        Key built_key = prefix_so_far;
        
        while (cur) {
            built_key = prefix_so_far.substr(0, prefix_so_far.size() - (cur == subtree_root ? 0 : 0));
            
            // Traverse to leftmost data node
            std::function<Iter(node_type*, const Key&)> find_leftmost = 
                [&](node_type* n, const Key& key_so_far) -> Iter {
                    Key full_key = key_so_far + n->get_skip();
                    
                    if (n->has_value()) {
                        return Iter(n, full_key);
                    }
                    
                    char fc = n->first_child_char();
                    if (fc == '\0') {
                        // Dead node - shouldn't happen after compact but handle it
                        return Iter(nullptr, true);
                    }
                    
                    return find_leftmost(n->get_child(fc), full_key + fc);
                };
            
            return find_leftmost(subtree_root, prefix_so_far.substr(0, prefix_so_far.size() - subtree_root->get_skip().size()));
        }
        
        return Iter(nullptr, true);
    }

    template <class Iter>
    Iter find_past_prefix_end(node_type* subtree_root, const Key& prefix_at_root) {
        // Find the first key that does NOT have the prefix
        // This is the key just after the rightmost key in the subtree
        
        // Go up to parent and find next sibling
        node_type* cur = subtree_root;
        Key cur_prefix = prefix_at_root;
        
        while (cur) {
            node_type* p = cur->get_parent();
            if (!p) {
                // We're at root - no more keys after this subtree
                return Iter(nullptr, true);
            }
            
            char edge = cur->get_parent_edge();
            // Find key up to parent
            size_t parent_key_len = cur_prefix.size() - cur->get_skip().size() - 1;
            Key parent_key = cur_prefix.substr(0, parent_key_len);
            
            // Find next sibling after 'edge'
            char next_edge = p->next_child_char(edge);
            if (next_edge != '\0') {
                // Found a sibling - find leftmost key in that subtree
                node_type* sibling = p->get_child(next_edge);
                Key sibling_prefix = parent_key + next_edge;
                
                std::function<Iter(node_type*, const Key&)> find_leftmost = 
                    [&](node_type* n, const Key& key_so_far) -> Iter {
                        Key full_key = key_so_far + n->get_skip();
                        if (n->has_value()) return Iter(n, full_key);
                        char fc = n->first_child_char();
                        if (fc == '\0') return Iter(nullptr, true);
                        return find_leftmost(n->get_child(fc), full_key + fc);
                    };
                
                return find_leftmost(sibling, sibling_prefix);
            }
            
            // No next sibling - go up further
            cur = p;
            cur_prefix = parent_key;
        }
        
        return Iter(nullptr, true);
    }

    const_iterator lower_bound_impl(const Key& key) const {
        node_type* cur = const_cast<node_type*>(&head);
        size_t kpos = 0;
        Key built_key;

        while (true) {
            const std::string& skip = cur->get_skip();
            
            // Compare skip with remaining key
            size_t i = 0;
            while (i < skip.size() && kpos + i < key.size()) {
                if (skip[i] < key[kpos + i]) {
                    // Skip is less - need to go to next subtree
                    built_key += skip;
                    return find_next_after_subtree(cur, built_key);
                } else if (skip[i] > key[kpos + i]) {
                    // Skip is greater - first key in this subtree is the answer
                    built_key += skip;
                    return find_first_in_subtree(cur, built_key);
                }
                ++i;
            }
            
            built_key += skip.substr(0, i);
            kpos += i;
            
            if (i < skip.size()) {
                // Key exhausted in middle of skip
                // All keys in this subtree are >= key
                return find_first_in_subtree(cur, built_key);
            }
            
            if (kpos == key.size()) {
                // Key exactly matches path so far
                if (cur->has_value()) {
                    return const_iterator(cur, built_key);
                }
                // No value here - find first in subtree
                return find_first_in_subtree(cur, built_key);
            }
            
            // Continue to child
            char c = key[kpos];
            
            // Check if there's a child less than c that we should skip
            char fc = cur->first_child_char();
            if (fc == '\0') {
                // No children - need to backtrack
                return find_next_after_subtree(cur, built_key);
            }
            
            if (fc > c) {
                // First child is greater than what we're looking for
                // First key in first child's subtree is the answer
                built_key += fc;
                return find_first_in_subtree(cur->get_child(fc), built_key);
            }
            
            node_type* child = cur->get_child(c);
            if (child) {
                built_key += c;
                cur = child;
                kpos++;
                continue;
            }
            
            // No exact child match - find next greater child
            char next = cur->next_child_char(c);
            if (next != '\0') {
                built_key += next;
                return find_first_in_subtree(cur->get_child(next), built_key);
            }
            
            // No greater child - backtrack
            return find_next_after_subtree(cur, built_key);
        }
    }

    const_iterator find_first_in_subtree(node_type* n, const Key& prefix) const {
        while (n) {
            Key full_key = prefix + n->get_skip().substr(prefix.size() >= n->get_skip().size() ? 0 : 0);
            // Recalculate full key properly
            if (n->has_value()) {
                // Build the key from scratch
                Key k;
                std::vector<std::pair<node_type*, char>> path;
                node_type* tmp = n;
                while (tmp->get_parent()) {
                    path.push_back({tmp, tmp->get_parent_edge()});
                    tmp = tmp->get_parent();
                }
                // Build from root
                tmp = const_cast<node_type*>(&head);
                k = tmp->get_skip();
                for (auto it = path.rbegin(); it != path.rend(); ++it) {
                    k += it->second;
                    k += it->first->get_skip();
                }
                return const_iterator(n, k);
            }
            char fc = n->first_child_char();
            if (fc == '\0') {
                // Dead end - backtrack
                return find_next_after_subtree(n, prefix);
            }
            n = n->get_child(fc);
        }
        return const_iterator(nullptr, true);
    }

    const_iterator find_next_after_subtree(node_type* n, const Key& prefix) const {
        while (n) {
            node_type* p = n->get_parent();
            if (!p) return const_iterator(nullptr, true);
            
            char edge = n->get_parent_edge();
            char next = p->next_child_char(edge);
            
            if (next != '\0') {
                // Build parent's key
                Key parent_key;
                std::vector<std::pair<node_type*, char>> path;
                node_type* tmp = p;
                while (tmp->get_parent()) {
                    path.push_back({tmp, tmp->get_parent_edge()});
                    tmp = tmp->get_parent();
                }
                tmp = const_cast<node_type*>(&head);
                parent_key = tmp->get_skip();
                for (auto it = path.rbegin(); it != path.rend(); ++it) {
                    parent_key += it->second;
                    parent_key += it->first->get_skip();
                }
                
                return find_first_in_subtree(p->get_child(next), parent_key + next);
            }
            
            n = p;
        }
        return const_iterator(nullptr, true);
    }
};

// Deduction guide
template <class Key, class T>
tktrie(std::initializer_list<std::pair<Key, T>>) -> tktrie<Key, T>;

// Comparison operators
template <class K, class T>
bool operator==(const tktrie<K, T>& a, const tktrie<K, T>& b) {
    if (a.size() != b.size()) return false;
    auto ai = a.begin(), bi = b.begin();
    while (ai != a.end()) {
        if (ai.key() != bi.key() || (*ai).second != (*bi).second) return false;
        ++ai; ++bi;
    }
    return true;
}

template <class K, class T>
bool operator!=(const tktrie<K, T>& a, const tktrie<K, T>& b) { return !(a == b); }

template <class K, class T>
void swap(tktrie<K, T>& a, tktrie<K, T>& b) noexcept { a.swap(b); }

// Type alias for common use case
template <class T>
using string_trie = tktrie<std::string, T>;
