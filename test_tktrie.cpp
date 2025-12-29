#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <map>
#include <mutex>
#include <random>
#include <shared_mutex>
#include <thread>
#include <unordered_map>
#include <vector>

#include "tktrie.h"

// ============ uint64_t key support ============

inline uint64_t byteswap64(uint64_t x) {
    return __builtin_bswap64(x);
}

inline const std::string& key_to_string(uint64_t key, std::string& buf) {
    uint64_t be = byteswap64(key);
    buf.assign(reinterpret_cast<const char*>(&be), 8);
    return buf;
}

template<typename V>
class tktrie_uint64 {
    gteitelbaum::tktrie<std::string, V> trie_;
    thread_local static std::string buf_;
public:
    auto find(uint64_t key) { return trie_.find(key_to_string(key, buf_)); }
    auto end() { return trie_.end(); }
    void insert(uint64_t key, const V& value) { 
        trie_.insert({key_to_string(key, buf_), value}); 
    }
    void insert(const std::pair<uint64_t, V>& kv) { 
        insert(kv.first, kv.second);
    }
    size_t erase(uint64_t key) { return trie_.erase(key_to_string(key, buf_)); }
    size_t size() { return trie_.size(); }
};

template<typename V>
thread_local std::string tktrie_uint64<V>::buf_;

// ============ Locked container wrappers ============

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

template<typename K, typename V>
class locked_unordered_map {
    std::unordered_map<K, V> data;
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

// ============ String keys data ============

const std::vector<std::string> WORDS = {
    "the", "be", "to", "of", "and", "a", "in", "that", "have", "I",
    "it", "for", "not", "on", "with", "he", "as", "you", "do", "at",
    "this", "but", "his", "by", "from", "they", "we", "say", "her", "she",
    "or", "an", "will", "my", "one", "all", "would", "there", "their", "what",
    "so", "up", "out", "if", "about", "who", "get", "which", "go", "me",
    "when", "make", "can", "like", "time", "no", "just", "him", "know", "take",
    "people", "into", "year", "your", "good", "some", "could", "them", "see", "other",
    "than", "then", "now", "look", "only", "come", "its", "over", "think", "also",
    "back", "after", "use", "two", "how", "our", "work", "first", "well", "way",
    "even", "new", "want", "because", "any", "these", "give", "day", "most", "us",
    "is", "was", "are", "been", "has", "had", "were", "said", "each", "made",
    "does", "did", "got", "may", "part", "find", "long", "down", "made", "many",
    "before", "must", "through", "much", "where", "should", "very", "after", "most", "might",
    "being", "such", "more", "those", "never", "still", "world", "last", "own", "public",
    "while", "next", "less", "both", "life", "under", "same", "right", "here", "state",
    "place", "high", "every", "going", "another", "school", "number", "always", "however", "every",
    "without", "great", "small", "between", "something", "important", "family", "government", "since", "system",
    "group", "always", "children", "often", "money", "called", "water", "business", "almost", "program",
    "point", "hand", "having", "once", "away", "different", "night", "large", "order", "things",
    "already", "nothing", "possible", "second", "rather", "problem", "against", "though", "again", "person",
    "looking", "morning", "house", "during", "side", "power", "further", "young", "turned", "until",
    "something", "start", "given", "working", "anything", "perhaps", "question", "reason", "early", "himself",
    "making", "enough", "better", "open", "show", "case", "seemed", "kind", "name", "read",
    "began", "believe", "several", "across", "office", "later", "usually", "city", "least", "story",
    "coming", "country", "social", "company", "close", "turned", "brought", "national", "service", "idea",
    "although", "behind", "true", "really", "home", "became", "become", "days", "taking", "within",
    "change", "available", "women", "level", "local", "mother", "doing", "development", "certain", "form",
    "whether", "door", "course", "member", "others", "center", "themselves", "best", "short", "white",
    "following", "around", "already", "given", "political", "face", "either", "using", "hours", "together",
    "interest", "whole", "community", "seen", "therefore", "along", "sure", "itself", "experience", "education",
    "keep", "light", "area", "study", "body", "started", "human", "nature", "president", "major",
    "sense", "result", "quite", "toward", "policy", "general", "control", "figure", "action", "process",
    "american", "provide", "based", "free", "support", "include", "believe", "church", "period", "future",
    "room", "common", "effect", "history", "probably", "need", "table", "later", "special", "particular",
    "continue", "personal", "sometimes", "current", "complete", "everything", "actually", "individual", "seems", "care",
    "difficult", "simple", "economic", "research", "perhaps", "clear", "evidence", "recent", "strong", "private",
    "themselves", "remember", "subject", "field", "position", "sense", "cannot", "class", "various", "outside",
    "report", "security", "building", "seems", "meeting", "value", "necessary", "likely", "return", "moment",
    "analysis", "central", "above", "force", "example", "similar", "thus", "stand", "type", "important",
    "society", "entire", "decision", "north", "help", "mind", "everyone", "today", "federal", "terms",
    "view", "international", "according", "finally", "total", "love", "party", "single", "lost", "south",
    "information", "brought", "military", "section", "living", "provides", "main", "student", "role", "available",
    "lines", "director", "knowledge", "court", "expected", "moved", "past", "standard", "expected", "attention",
    "especially", "basic", "half", "appeared", "allow", "treatment", "addition", "chance", "growth", "design",
    "difficult", "itself", "previous", "management", "established", "provides", "wrong", "language", "board", "considered",
    "events", "approach", "actually", "range", "simply", "significant", "situation", "performance", "behavior", "difference",
    "words", "access", "hospital", "issues", "involved", "opportunity", "material", "training", "street", "modern",
    "higher", "blood", "response", "changes", "theory", "population", "inside", "pressure", "financial", "data",
    "effort", "developed", "meaning", "production", "provides", "method", "foreign", "physical", "amount", "traditional",
    "generally", "medical", "patient", "activity", "technology", "voice", "character", "environmental", "natural", "directly",
    "choice", "results", "office", "project", "relationship", "needed", "function", "understanding", "factor", "operation",
    "concerned", "create", "consider", "black", "structure", "positive", "potential", "purpose", "paper", "successful",
    "western", "resource", "prepared", "established", "learning", "serious", "middle", "space", "commission", "successful",
    "running", "model", "condition", "wall", "series", "culture", "official", "congress", "source", "described",
    "increase", "analysis", "created", "science", "organization", "clearly", "network", "surface", "agreement", "agency",
    "works", "practice", "extent", "earlier", "recently", "performance", "additional", "likely", "challenge", "primary",
    "effective", "product", "significant", "presented", "suggested", "technical", "growing", "responsible", "written", "determined",
    "reality", "similar", "developed", "response", "focus", "economy", "final", "professional", "approach", "rules",
    "strategy", "balance", "quality", "legal", "decade", "image", "responsibility", "applied", "critical", "religious",
    "workers", "attention", "movement", "generally", "capital", "associated", "direct", "defined", "values", "appropriate",
    "increase", "effective", "independent", "planning", "regular", "identify", "complex", "commercial", "limited", "demand",
    "energy", "alternative", "original", "conference", "article", "concerned", "application", "principles", "insurance", "procedure",
    "capacity", "statement", "institution", "specific", "benefit", "official", "democratic", "generally", "considerable", "normal",
    "industrial", "standards", "literature", "credit", "pattern", "content", "negative", "aspect", "coverage", "regional",
    "volume", "solutions", "primary", "element", "variable", "communication", "generation", "contract", "customer", "legislation",
    "assessment", "influence", "distinction", "distribution", "executive", "reduction", "selection", "definition", "perspective", "consequence",
    "version", "framework", "revolution", "protection", "resolution", "characteristic", "interpretation", "dimension", "representation", "contribution",
    "recognition", "acquisition", "investigation", "recommendation", "implementation", "consideration", "administration", "participation", "determination", "demonstration",
    "discrimination", "accommodation", "authentication", "authorization", "certification", "classification", "communication", "configuration", "consolidation", "construction",
    "consultation", "consumption", "contamination", "continuation", "contribution", "conversation", "cooperation", "coordination", "corporation", "correlation",
    "customization", "deactivation", "decomposition", "decompression", "decentralization", "declaration", "decommission", "decoration", "dedication", "deformation",
    "degradation", "deliberation", "delineation", "demonstration", "denomination", "departmental", "depreciation", "deprivation", "derivation", "description",
    "desegregation", "desensitization", "designation", "desperation", "destination", "destruction", "deterioration", "determination", "detoxification", "devaluation",
    "devastation", "differentiation", "digitalization", "dimensionality", "disadvantageous", "disappointment", "disassociation", "discontinuation", "discouragement", "discrimination",
    "disenchantment", "disengagement", "disfigurement", "disillusionment", "disintegration", "dismemberment", "disorientation", "displacement", "disproportionate", "disqualification",
    "dissatisfaction", "dissemination", "dissertation", "distillation", "distribution", "diversification", "documentation", "domestication", "dramatization", "duplication",
    "ecclesiastical", "economization", "editorialization", "effectuation", "effortlessness", "egalitarianism", "electrification", "electromagnetic", "electronically", "elementarily",
    "emancipation", "embellishment", "embodiment", "emotionalism", "empowerment", "encapsulation", "encouragement", "endangerment", "enlightenment", "entertainment",
    "enthusiastically", "entrepreneurial", "environmental", "epistemological", "equalization", "equilibrium", "establishment", "evangelization", "evaporation", "exacerbation",
    "exaggeration", "examination", "exasperation", "excommunication", "exemplification", "exhaustiveness", "exhilaration", "existentialism", "experiential", "experimentation",
    "exploitation", "exploration", "exportation", "expropriation", "externalization", "extermination", "extraordinarily", "extraterrestrial", "extravagance", "fabrication",
    "facilitation", "falsification", "familiarization", "fantastically", "fascination", "featherweight", "federalization", "fertilization", "fictionalization", "fingerprinting",
    "firefighting", "flashforward", "flawlessness", "flexibilities", "flourishing", "fluctuation", "fluorescence", "forgetfulness", "formalization", "fortification",
    "fossilization", "fragmentation", "franchising", "fraternization", "freestanding", "friendliness", "frontrunners", "fruitfulness", "frustration", "fulfillment",
    "functionality", "fundamentalism", "fundraising", "galvanization", "generalization", "gentrification", "globalization", "glorification", "governmental", "gracelessness",
    "gradualness", "grandchildren", "grandparents", "gratification", "gravitational", "greenhouse", "groundbreaking", "groundskeeper", "guardianship", "habituation",
    "hallucination", "handcrafted", "handicapping", "handwriting", "happenstance", "hardworking", "headquarters", "heartbreaking", "heartwarming", "helplessness",
    "hemispheric", "hermeneutics", "heterogeneous", "hierarchical", "highlighting", "historically", "homecoming", "homelessness", "homogenization", "hopelessness",
    "horticulture", "hospitality", "housekeeping", "humanitarian", "humiliation", "hybridization", "hydroelectric", "hyperactive", "hypersensitive", "hypothetical",
    "iconoclastic", "idealization", "identification", "ideologically", "idiosyncratic", "illegitimate", "illumination", "illustration", "imaginative", "immeasurable",
    "immobilization", "immortalization", "immunization", "impartiality", "impersonation", "implementation", "implication", "impossibility", "impoverishment", "impressionable",
    "improvisation", "inaccessible", "inadmissible", "inappropriate", "incarceration", "incidentally", "incineration", "incommunicado", "incompatibility", "incomprehensible",
    "inconsistency", "incorporation", "incrimination", "indebtedness", "indefinitely", "independently", "indescribable", "indestructible", "indeterminate", "indifferent",
    "indispensable", "individualism", "individualistic", "individualization", "indoctrination", "industrialism", "industrialization", "ineffectively", "inefficiency", "inevitability",
    "inexperienced", "infinitesimal", "inflammability", "inflexibility", "infrastructure", "ingeniousness", "initialization", "insignificance", "institutionalism", "institutionalization",
    "instrumentation", "insubordinate", "insufficiency", "intellectualism", "intensification", "intentionally", "interactively", "intercontinental", "interdependence", "interdisciplinary",
    "interestingly", "interferometer", "intergovernmental", "intermediary", "intermittently", "internalization", "internationally", "interoperability", "interpretation", "interrelationship"
};

// ============ uint64_t keys data ============

constexpr int NUM_UINT64_KEYS = 100000;

std::vector<uint64_t> generate_uint64_keys() {
    std::vector<uint64_t> keys;
    keys.reserve(NUM_UINT64_KEYS);
    std::mt19937_64 rng(42);
    std::uniform_int_distribution<uint64_t> dist(0, UINT64_MAX);
    for (int i = 0; i < NUM_UINT64_KEYS; ++i) {
        keys.push_back(dist(rng));
    }
    return keys;
}

// ============ Benchmark infrastructure ============

std::atomic<long long> total_ops{0};

// String key worker
template<typename Container>
void string_worker(Container& c, int thread_id, unsigned seed) {
    std::vector<std::string> words = WORDS;
    std::mt19937 rng(seed);
    std::shuffle(words.begin(), words.end(), rng);
    
    long long ops = 0;
    for (const auto& word : words) {
        int value = thread_id * 10000 + ops;
        auto it = c.find(word); (void)it; ops++;
        c.insert({word, value}); ops++;
        it = c.find(word); (void)it; ops++;
        c.erase(word); ops++;
        it = c.find(word); (void)it; ops++;
        c.insert({word, value + 1}); ops++;
        it = c.find(word); (void)it; ops++;
    }
    total_ops += ops;
}

// uint64_t key worker
template<typename Container>
void uint64_worker(Container& c, int thread_id, unsigned seed, const std::vector<uint64_t>& keys) {
    std::vector<uint64_t> local_keys = keys;
    std::mt19937_64 rng(seed);
    std::shuffle(local_keys.begin(), local_keys.end(), rng);
    
    long long ops = 0;
    for (const auto& key : local_keys) {
        int value = thread_id * 1000000 + ops;
        auto it = c.find(key); (void)it; ops++;
        c.insert({key, value}); ops++;
        it = c.find(key); (void)it; ops++;
        c.erase(key); ops++;
        it = c.find(key); (void)it; ops++;
        c.insert({key, value + 1}); ops++;
        it = c.find(key); (void)it; ops++;
    }
    total_ops += ops;
}

// Overload for string worker (no extra args)
template<typename Container>
double benchmark_string(int num_threads) {
    Container c;
    total_ops = 0;
    
    std::vector<std::thread> threads;
    auto start = std::chrono::high_resolution_clock::now();
    
    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back(string_worker<Container>, std::ref(c), i, i * 12345);
    }
    for (auto& t : threads) t.join();
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    
    return total_ops.load() * 1000000.0 / duration.count();
}

// Overload for uint64 worker
template<typename Container>
double benchmark_uint64(int num_threads, const std::vector<uint64_t>& keys) {
    Container c;
    total_ops = 0;
    
    std::vector<std::thread> threads;
    auto start = std::chrono::high_resolution_clock::now();
    
    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back(uint64_worker<Container>, std::ref(c), i, i * 12345, std::cref(keys));
    }
    for (auto& t : threads) t.join();
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    
    return total_ops.load() * 1000000.0 / duration.count();
}

int main(int argc, char* argv[]) {
    auto uint64_keys = generate_uint64_keys();
    
    // Warm up
    benchmark_string<gteitelbaum::tktrie<std::string, int>>(2);
    benchmark_string<locked_map<std::string, int>>(2);
    benchmark_string<locked_unordered_map<std::string, int>>(2);
    benchmark_uint64<tktrie_uint64<int>>(2, uint64_keys);
    benchmark_uint64<locked_map<uint64_t, int>>(2, uint64_keys);
    benchmark_uint64<locked_unordered_map<uint64_t, int>>(2, uint64_keys);
    
    // String tests
    std::cout << "STRING " << WORDS.size() << std::endl;
    for (int threads : {1, 2, 4, 8, 16}) {
        double trie = benchmark_string<gteitelbaum::tktrie<std::string, int>>(threads);
        double map = benchmark_string<locked_map<std::string, int>>(threads);
        double umap = benchmark_string<locked_unordered_map<std::string, int>>(threads);
        std::cout << threads << " " << (long)trie << " " << (long)map << " " << (long)umap << std::endl;
    }
    
    // uint64 tests
    std::cout << "UINT64 " << NUM_UINT64_KEYS << std::endl;
    for (int threads : {1, 2, 4, 8, 16}) {
        double trie = benchmark_uint64<tktrie_uint64<int>>(threads, uint64_keys);
        double map = benchmark_uint64<locked_map<uint64_t, int>>(threads, uint64_keys);
        double umap = benchmark_uint64<locked_unordered_map<uint64_t, int>>(threads, uint64_keys);
        std::cout << threads << " " << (long)trie << " " << (long)map << " " << (long)umap << std::endl;
    }
    
    return 0;
}
