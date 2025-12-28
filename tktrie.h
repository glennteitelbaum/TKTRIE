#include <array>
#include <cstdint>
#include <string>
#include <shared_mutex>
#include <vector>

class alignas(32) pop_tp {
    using bits_t=uint64_t;
    std::array<bits_t, 4> bits;
public:
    bool find_pop(char c, int* cnt) const {
        uint8_t v = static_cast<uint8_t>(c);
        const int word = v >> 6;      // v / 64 -- Which 64-bit word (0-3)
        const int bit = v & 63;       // v % 64 -- Which bit within word
        const bits_t mask = 1ULL << bit;

        // Base offset: 4 (skip bitmap) + bits in this word
        const int w0 = 4 + std::popcount(bits[word] & (mask - 1));

        // Cumulative offsets for each word
        const int w1 = w0 + std::popcount(bits[0]); // add bits in word 0
        const int w2 = w1 + std::popcount(bits[1]); // add bits in word 1
        const int w3 = w2 + std::popcount(bits[2]); // add bits in word 2

        // Select correct offset based on word index
        const std::array<int, 4> before{w0, w1, w2, w3};
        *cnt = before[word];
        return true;
    }
};

class key_tp {
    std::string& key;
    size_t offset;
public:
    key_tp(std::string& orig) : key(orig), offset(0) {}
    bool match(std::string& skip) const {
        return  (skip==key.substr(offset, skip.size()));
    }
    bool is_empty() const {return key.size()>=offset;}
    size_t size() const { return key.size()-offset; }
    void eat(size_t sz) {offset+=sz;}
    char cur() { auto ret=key[offset]; eat(1); return ret;}
};

template <class T>
class node {
    std::shared_mutex shared{};
    pop_tp pop{};
    node * eos{nullptr};
    node * parent{nullptr};
    std::string skip{};
    std::vector<node *> nxt{};  
    T data{};
public:
     node * find_internal(key_tp& key) {
        //std::shared_lock<std::shared_mutex> lock(shared);
        if (eos) { // data in node
            if (key.match(skip)) [[ likely ]] {
                auto skip_sz=skip.size();
                auto key_sz=key.size();
                if (key_sz==skip_sz) {
                    return this;
                } else { // more to read
                    key.eat(skip_sz);
                }
            } else { // too short or mismatch
                return nullptr;
            }
        }
        int offset=0;
        auto c=key.cur();
        if (!pop.find_pop(c, &offset)) [[ unlikely ]]{
            return nullptr;
        }
        return nxt[offset];
    }
};

template <class T>
class tktrie {
    node<T> head;
    std::shared_mutex shared; // protect count
    size_t count;
public:
    node<T> * find(std::string& key) {
        key_tp cp=key;
        node<T>* run=&head;

        for (;;) {
            auto nxt=run->find_internal(cp);
            if (!nxt) [[ unlikely]] {break;} // not found
            if (nxt==run) {break;} // found
            run=nxt; // loop
        }
        return run;
     }
};

using test_type=std::array<char, 24>;
#if 0
//using test_type=char;
int pop_sz() { return sizeof(pop_tp);}
int shared_sz() { return sizeof(std::shared_mutex);}
int ptr_sz() { return sizeof (node<test_type> *);}
int string_sz() { return sizeof(key_tp);}
int vector_sz() { return sizeof(std::vector<node<test_type> *>);}
int data_sz() { return sizeof(test_type);}

int sum_sz() { return shared_sz()+pop_sz()+2*ptr_sz()+string_sz()+vector_sz()+data_sz(); }
int node_sz() { return sizeof(node<test_type>);}
int tktrie_sz() { return sizeof(tktrie<test_type>);}
#endif

auto tst_find(tktrie<test_type>& N, std::string& key) {
    return N.find(key);
}
