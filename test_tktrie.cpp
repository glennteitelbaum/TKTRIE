#include "tktrie.h"
#include <iostream>
#include <thread>
#include <vector>
#include <string>
#include <random>
#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>

// 1000 common English words
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

std::atomic<int> total_ops{0};
std::atomic<int> errors{0};

void thread_worker(tktrie<int>& trie, int thread_id, unsigned seed) {
    // Create a shuffled copy of the word list
    std::vector<std::string> words = WORDS;
    std::mt19937 rng(seed);
    std::shuffle(words.begin(), words.end(), rng);
    
    int local_ops = 0;
    int local_errors = 0;
    
    for (const auto& word : words) {
        // Unique value for this thread/word combination
        int value = thread_id * 10000 + local_ops;
        
        // Step 1: find(x) - might or might not exist
        auto* node1 = trie.find(word);
        (void)node1;  // May be null or valid
        local_ops++;
        
        // Step 2: insert(x)
        trie.insert(word, value);
        local_ops++;
        
        // Step 3: find(x) - should exist, but another thread might remove it
        // This is a race condition, not an error in the data structure
        auto* node2 = trie.find(word);
        (void)node2;  // May fail due to concurrent remove - that's OK
        local_ops++;
        
        // Step 4: remove(x)
        bool removed = trie.remove(word);
        (void)removed;  // Might fail if another thread removed it
        local_ops++;
        
        // Step 5: find(x) - might or might not exist (race with other threads)
        auto* node3 = trie.find(word);
        (void)node3;
        local_ops++;
        
        // Step 6: insert(x) again
        trie.insert(word, value + 1);
        local_ops++;
        
        // Step 7: find(x) - should exist, but race condition possible
        auto* node4 = trie.find(word);
        (void)node4;  // May fail due to concurrent remove - that's OK
        local_ops++;
    }
    
    total_ops += local_ops;
    errors += local_errors;
    
    std::cout << "Thread " << thread_id << " completed: " 
              << local_ops << " ops\n";
}

int main() {
    std::cout << "=== Concurrent Trie Test ===\n";
    std::cout << "Words: " << WORDS.size() << "\n";
    std::cout << "Threads: 16\n";
    std::cout << "Operations per thread: " << WORDS.size() * 7 << "\n";
    std::cout << "Total operations: " << WORDS.size() * 7 * 16 << "\n\n";
    
    tktrie<int> trie;
    
    auto start = std::chrono::high_resolution_clock::now();
    
    // Create 16 threads with different random seeds
    std::vector<std::thread> threads;
    std::random_device rd;
    
    for (int i = 0; i < 16; ++i) {
        unsigned seed = rd();
        threads.emplace_back(thread_worker, std::ref(trie), i, seed);
    }
    
    // Wait for all threads to complete
    for (auto& t : threads) {
        t.join();
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    std::cout << "\n=== Results ===\n";
    std::cout << "Total operations: " << total_ops.load() << "\n";
    std::cout << "Time: " << duration.count() << " ms\n";
    std::cout << "Ops/sec: " << (total_ops.load() * 1000.0 / duration.count()) << "\n";
    
    // Verify final state - all words should exist (last operation was insert)
    std::cout << "\nVerifying final state...\n";
    int missing = 0;
    for (const auto& word : WORDS) {
        if (!trie.find(word)) {
            missing++;
            if (missing <= 10) {
                std::cout << "  Missing: " << word << "\n";
            }
        }
    }
    
    if (missing > 0) {
        std::cout << "WARNING: " << missing << " words missing from final trie\n";
    } else {
        std::cout << "All " << WORDS.size() << " words present in final trie\n";
    }
    
    std::cout << "Final trie size: " << trie.size() << "\n";
    
    // Size might not exactly match WORDS.size() due to duplicates in word list
    // and race conditions, but should be close
    
    if (missing == 0) {
        std::cout << "\n=== TEST PASSED ===\n";
        return 0;
    } else {
        std::cout << "\n=== TEST FAILED ===\n";
        return 1;
    }
}
