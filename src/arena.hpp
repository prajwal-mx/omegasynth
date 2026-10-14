#pragma once

#include <cstdint>
#include <vector>
#include <string>
#include <iostream>
#include <algorithm>
#include <cassert>
#if defined(__AVX2__)
#include <immintrin.h>
#endif

namespace omegasynth {

enum class Player : uint8_t {
    Max = 0,
    Min = 1
};

inline Player opponent(Player p) {
    return p == Player::Max ? Player::Min : Player::Max;
}

inline std::string player_name(Player p) {
    return p == Player::Max ? "Max" : "Min";
}

using VertexId = uint32_t;
using Priority = uint32_t;
using Reward = int64_t;

// Compact dynamic bitset for representing vertex subsets in subarenas
class VertexSet {
private:
    std::vector<uint64_t> words_;
    size_t size_;
    size_t count_;

public:
    VertexSet() : size_(0), count_(0) {}
    explicit VertexSet(size_t n, bool fill = false) : size_(n), count_(fill ? n : 0) {
        size_t num_words = (n + 63) / 64;
        words_.assign(num_words, fill ? ~0ULL : 0ULL);
        if (fill && (n % 64 != 0)) {
            words_.back() = (1ULL << (n % 64)) - 1;
        }
    }

    size_t capacity() const { return size_; }
    size_t count() const { return count_; }
    bool empty() const { return count_ == 0; }
    const std::vector<uint64_t>& words() const { return words_; }

    void clear() {
        std::fill(words_.begin(), words_.end(), 0ULL);
        count_ = 0;
    }

    bool contains(VertexId v) const {
        if (v >= size_) return false;
        return (words_[v / 64] & (1ULL << (v % 64))) != 0;
    }

    void insert(VertexId v) {
        if (v < size_) {
            uint64_t mask = 1ULL << (v % 64);
            uint64_t& w = words_[v / 64];
            if ((w & mask) == 0) {
                w |= mask;
                ++count_;
            }
        }
    }

    void remove(VertexId v) {
        if (v < size_) {
            uint64_t mask = 1ULL << (v % 64);
            uint64_t& w = words_[v / 64];
            if ((w & mask) != 0) {
                w &= ~mask;
                --count_;
            }
        }
    }

    bool operator==(const VertexSet& other) const {
        if (size_ != other.size_ || count_ != other.count_) return false;
        return words_ == other.words_;
    }
    bool operator!=(const VertexSet& other) const {
        return !(*this == other);
    }

    // Fast non-allocating check if (this & other) != empty
    bool intersects(const VertexSet& other) const {
        assert(size_ == other.size_);
        if (empty() || other.empty()) return false;
#if defined(__AVX2__)
        size_t num_avx = words_.size() / 4;
        for (size_t i = 0; i < num_avx; ++i) {
            __m256i a = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(&words_[i * 4]));
            __m256i b = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(&other.words_[i * 4]));
            if (!_mm256_testz_si256(a, b)) return true;
        }
        for (size_t i = num_avx * 4; i < words_.size(); ++i) {
            if ((words_[i] & other.words_[i]) != 0) return true;
        }
#else
        for (size_t i = 0; i < words_.size(); ++i) {
            if ((words_[i] & other.words_[i]) != 0) return true;
        }
#endif
        return false;
    }

    // In-place difference: this \= other
    void inplace_difference(const VertexSet& other) {
        assert(size_ == other.size_);
        if (empty() || other.empty()) return;
#if defined(__AVX2__)
        size_t num_avx = words_.size() / 4;
        for (size_t i = 0; i < num_avx; ++i) {
            __m256i a = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(&words_[i * 4]));
            __m256i b = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(&other.words_[i * 4]));
            __m256i c = _mm256_andnot_si256(b, a);
            _mm256_storeu_si256(reinterpret_cast<__m256i*>(&words_[i * 4]), c);
        }
        for (size_t i = num_avx * 4; i < words_.size(); ++i) {
            words_[i] &= ~other.words_[i];
        }
#else
        for (size_t i = 0; i < words_.size(); ++i) {
            words_[i] &= ~other.words_[i];
        }
#endif
        recount();
    }

    // In-place union: this |= other
    void inplace_unite(const VertexSet& other) {
        assert(size_ == other.size_);
        if (other.empty()) return;
#if defined(__AVX2__)
        size_t num_avx = words_.size() / 4;
        for (size_t i = 0; i < num_avx; ++i) {
            __m256i a = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(&words_[i * 4]));
            __m256i b = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(&other.words_[i * 4]));
            __m256i c = _mm256_or_si256(a, b);
            _mm256_storeu_si256(reinterpret_cast<__m256i*>(&words_[i * 4]), c);
        }
        for (size_t i = num_avx * 4; i < words_.size(); ++i) {
            words_[i] |= other.words_[i];
        }
#else
        for (size_t i = 0; i < words_.size(); ++i) {
            words_[i] |= other.words_[i];
        }
#endif
        recount();
    }

    // Set difference: this \ other
    VertexSet difference(const VertexSet& other) const {
        assert(size_ == other.size_);
        VertexSet res(size_, false);
#if defined(__AVX2__)
        size_t num_avx = words_.size() / 4;
        for (size_t i = 0; i < num_avx; ++i) {
            __m256i a = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(&words_[i * 4]));
            __m256i b = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(&other.words_[i * 4]));
            __m256i c = _mm256_andnot_si256(b, a); // a & ~b
            _mm256_storeu_si256(reinterpret_cast<__m256i*>(&res.words_[i * 4]), c);
        }
        for (size_t i = num_avx * 4; i < words_.size(); ++i) {
            res.words_[i] = words_[i] & ~other.words_[i];
        }
#else
        for (size_t i = 0; i < words_.size(); ++i) {
            res.words_[i] = words_[i] & ~other.words_[i];
        }
#endif
        res.recount();
        return res;
    }

    // Set intersection: this & other
    VertexSet intersect(const VertexSet& other) const {
        assert(size_ == other.size_);
        VertexSet res(size_, false);
#if defined(__AVX2__)
        size_t num_avx = words_.size() / 4;
        for (size_t i = 0; i < num_avx; ++i) {
            __m256i a = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(&words_[i * 4]));
            __m256i b = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(&other.words_[i * 4]));
            __m256i c = _mm256_and_si256(a, b);
            _mm256_storeu_si256(reinterpret_cast<__m256i*>(&res.words_[i * 4]), c);
        }
        for (size_t i = num_avx * 4; i < words_.size(); ++i) {
            res.words_[i] = words_[i] & other.words_[i];
        }
#else
        for (size_t i = 0; i < words_.size(); ++i) {
            res.words_[i] = words_[i] & other.words_[i];
        }
#endif
        res.recount();
        return res;
    }

    // Set union: this | other
    VertexSet unite(const VertexSet& other) const {
        assert(size_ == other.size_);
        VertexSet res(size_, false);
#if defined(__AVX2__)
        size_t num_avx = words_.size() / 4;
        for (size_t i = 0; i < num_avx; ++i) {
            __m256i a = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(&words_[i * 4]));
            __m256i b = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(&other.words_[i * 4]));
            __m256i c = _mm256_or_si256(a, b);
            _mm256_storeu_si256(reinterpret_cast<__m256i*>(&res.words_[i * 4]), c);
        }
        for (size_t i = num_avx * 4; i < words_.size(); ++i) {
            res.words_[i] = words_[i] | other.words_[i];
        }
#else
        for (size_t i = 0; i < words_.size(); ++i) {
            res.words_[i] = words_[i] | other.words_[i];
        }
#endif
        res.recount();
        return res;
    }

    void recount() {
        count_ = 0;
        for (uint64_t w : words_) {
            count_ += __builtin_popcountll(w);
        }
    }

    std::vector<VertexId> to_vector() const {
        std::vector<VertexId> res;
        res.reserve(count_);
        for (size_t i = 0; i < words_.size(); ++i) {
            uint64_t w = words_[i];
            while (w) {
                int bit = __builtin_ctzll(w);
                res.push_back(static_cast<VertexId>(i * 64 + bit));
                w &= w - 1;
            }
        }
        return res;
    }

    // Iterator support
    class Iterator {
        const VertexSet* set_;
        size_t word_idx_;
        uint64_t current_word_;

        void advance_to_valid() {
            while (word_idx_ < set_->words_.size() && current_word_ == 0) {
                ++word_idx_;
                if (word_idx_ < set_->words_.size()) {
                    current_word_ = set_->words_[word_idx_];
                }
            }
        }

    public:
        Iterator(const VertexSet* s, size_t w_idx, uint64_t cw)
            : set_(s), word_idx_(w_idx), current_word_(cw) {
            advance_to_valid();
        }

        VertexId operator*() const {
            return static_cast<VertexId>(word_idx_ * 64 + __builtin_ctzll(current_word_));
        }

        Iterator& operator++() {
            current_word_ &= current_word_ - 1; // clear lowest set bit
            advance_to_valid();
            return *this;
        }

        bool operator!=(const Iterator& other) const {
            return word_idx_ != other.word_idx_ || current_word_ != other.current_word_;
        }
    };

    Iterator begin() const {
        if (words_.empty()) return end();
        return Iterator(this, 0, words_[0]);
    }

    Iterator end() const {
        return Iterator(this, words_.size(), 0);
    }
};

struct Vertex {
    VertexId id = 0;
    Player owner = Player::Max;
    Priority priority = 0;
    Reward reward = 0;
    std::string name;
};

class Arena {
public:
    size_t num_vertices = 0;
    VertexId start_vertex = 0;
    bool has_negative_rewards = false;
    bool has_nonzero_rewards = false;
    std::vector<Vertex> vertices;
    std::vector<std::vector<VertexId>> succ; // Outgoing edges
    std::vector<std::vector<VertexId>> pred; // Incoming edges

    // Optional reactive synthesis metadata (from HOA / TLSF)
    std::vector<std::string> ap_names;
    std::vector<int> controllable_aps;
    std::vector<int> uncontrollable_aps;

    Arena(size_t n = 0)
        : num_vertices(n), start_vertex(0), has_negative_rewards(false),
          has_nonzero_rewards(false), vertices(n), succ(n), pred(n) {}

    void add_vertex(VertexId id, Player owner, Priority prio, Reward reward, std::string name = "") {
        if (id >= num_vertices) {
            num_vertices = id + 1;
            vertices.resize(num_vertices);
            succ.resize(num_vertices);
            pred.resize(num_vertices);
        }
        if (reward < 0) has_negative_rewards = true;
        if (reward != 0) has_nonzero_rewards = true;
        vertices[id] = Vertex{id, owner, prio, reward, name.empty() ? std::string("v").append(std::to_string(id)) : std::move(name)};
    }

    void add_edge(VertexId from, VertexId to) {
        if (from >= num_vertices) {
            add_vertex(from, Player::Max, 0, 0);
        }
        if (to >= num_vertices) {
            add_vertex(to, Player::Max, 0, 0);
        }
        succ[from].push_back(to);
        pred[to].push_back(from);
    }

    void recompute_reward_flags() {
        has_negative_rewards = false;
        has_nonzero_rewards = false;
        for (const auto& v : vertices) {
            if (v.reward < 0) has_negative_rewards = true;
            if (v.reward != 0) has_nonzero_rewards = true;
        }
    }

    VertexSet all_vertices() const {
        return VertexSet(num_vertices, true);
    }
};

} // namespace omegasynth
