#include "tktrie.h"
#include <iostream>
#include <cassert>

int main() {
    tktrie<int> trie;

    // Test basic insert and find
    std::cout << "Testing insert and find...\n";
    trie.insert("hello", 1);
    trie.insert("hell", 2);
    trie.insert("helicopter", 3);
    trie.insert("help", 4);
    trie.insert("world", 5);

    assert(trie.size() == 5);
    assert(trie.find("hello") != nullptr);
    assert(trie.find("hello")->get_data() == 1);
    assert(trie.find("hell")->get_data() == 2);
    assert(trie.find("helicopter")->get_data() == 3);
    assert(trie.find("help")->get_data() == 4);
    assert(trie.find("world")->get_data() == 5);
    assert(trie.find("hel") == nullptr);  // Not inserted
    assert(trie.find("notfound") == nullptr);
    std::cout << "  PASSED\n";

    // Test remove
    std::cout << "Testing remove...\n";
    
    // Remove a leaf node
    assert(trie.remove("helicopter") == true);
    assert(trie.find("helicopter") == nullptr);
    assert(trie.size() == 4);
    
    // Other entries still exist
    assert(trie.find("hello") != nullptr);
    assert(trie.find("hell") != nullptr);
    assert(trie.find("help") != nullptr);
    std::cout << "  PASSED\n";

    // Test remove non-existent key
    std::cout << "Testing remove non-existent...\n";
    assert(trie.remove("notfound") == false);
    assert(trie.remove("hel") == false);  // Prefix exists but no data
    assert(trie.size() == 4);
    std::cout << "  PASSED\n";

    // Test remove with compaction
    std::cout << "Testing remove with compaction...\n";
    trie.remove("hell");
    assert(trie.find("hell") == nullptr);
    assert(trie.find("hello") != nullptr);  // Should still work after compaction
    assert(trie.find("help") != nullptr);
    assert(trie.size() == 3);
    std::cout << "  PASSED\n";

    // Test clear
    std::cout << "Testing clear...\n";
    trie.clear();
    assert(trie.size() == 0);
    assert(trie.empty());
    assert(trie.find("hello") == nullptr);
    assert(trie.find("world") == nullptr);
    std::cout << "  PASSED\n";

    // Test re-insert after clear
    std::cout << "Testing re-insert after clear...\n";
    trie.insert("new", 100);
    assert(trie.size() == 1);
    assert(trie.find("new") != nullptr);
    assert(trie.find("new")->get_data() == 100);
    std::cout << "  PASSED\n";

    // Test destructor (implicit at end of scope)
    std::cout << "Testing destructor (creating and destroying trie)...\n";
    {
        tktrie<std::string> temp_trie;
        temp_trie.insert("one", "value1");
        temp_trie.insert("two", "value2");
        temp_trie.insert("three", "value3");
        temp_trie.insert("onesie", "value4");
        // Destructor called here
    }
    std::cout << "  PASSED\n";

    // Test path compression edge cases
    std::cout << "Testing path compression with remove...\n";
    {
        tktrie<int> t;
        t.insert("abcdefghij", 1);  // Long path-compressed key
        t.insert("abcdef", 2);       // Shorter prefix
        t.insert("abcdefghijklmnop", 3);  // Longer extension
        
        assert(t.find("abcdefghij")->get_data() == 1);
        assert(t.find("abcdef")->get_data() == 2);
        assert(t.find("abcdefghijklmnop")->get_data() == 3);
        
        // Remove middle key
        t.remove("abcdefghij");
        assert(t.find("abcdefghij") == nullptr);
        assert(t.find("abcdef")->get_data() == 2);
        assert(t.find("abcdefghijklmnop")->get_data() == 3);
        
        // Remove all
        t.remove("abcdef");
        t.remove("abcdefghijklmnop");
        assert(t.empty());
    }
    std::cout << "  PASSED\n";

    // Test many insertions and deletions
    std::cout << "Testing many insertions and deletions...\n";
    {
        tktrie<int> t;
        std::vector<std::string> keys = {
            "a", "ab", "abc", "abcd", "abcde",
            "b", "ba", "bac", "bad",
            "test", "testing", "tested", "tester",
            "x", "xy", "xyz", "xyzzy"
        };
        
        // Insert all
        for (size_t i = 0; i < keys.size(); ++i) {
            t.insert(keys[i], static_cast<int>(i));
        }
        assert(t.size() == keys.size());
        
        // Verify all present
        for (size_t i = 0; i < keys.size(); ++i) {
            auto* node = t.find(keys[i]);
            assert(node != nullptr);
            assert(node->get_data() == static_cast<int>(i));
        }
        
        // Remove every other key
        for (size_t i = 0; i < keys.size(); i += 2) {
            assert(t.remove(keys[i]));
        }
        
        // Verify correct keys present/absent
        for (size_t i = 0; i < keys.size(); ++i) {
            if (i % 2 == 0) {
                assert(t.find(keys[i]) == nullptr);
            } else {
                assert(t.find(keys[i]) != nullptr);
            }
        }
        
        // Clear and verify
        t.clear();
        assert(t.empty());
        for (const auto& k : keys) {
            assert(t.find(k) == nullptr);
        }
    }
    std::cout << "  PASSED\n";

    std::cout << "\nAll tests passed!\n";
    return 0;
}
