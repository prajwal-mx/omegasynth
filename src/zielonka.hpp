#pragma once

#include "arena.hpp"
#include "attractor.hpp"
#include "solver.hpp"
#include <algorithm>
#include <limits>
#include <chrono>
#include <map>
#include <vector>

namespace omegasynth {

class ZielonkaSolver {
private:
    const Arena& arena_;
    size_t recursive_calls_ = 0;
    std::vector<std::pair<Priority, VertexSet>> prio_buckets_;
    std::vector<VertexId>* strategy_ = nullptr;

    void init_buckets() {
        prio_buckets_.clear();
        std::map<Priority, VertexSet> bmap;
        for (VertexId v = 0; v < arena_.num_vertices; ++v) {
            Priority p = arena_.vertices[v].priority;
            auto it = bmap.find(p);
            if (it == bmap.end()) {
                it = bmap.emplace(p, VertexSet(arena_.num_vertices, false)).first;
            }
            it->second.insert(v);
        }
        prio_buckets_.reserve(bmap.size());
        for (auto& kv : bmap) {
            prio_buckets_.push_back(std::move(kv));
        }
    }

    size_t find_min_prio_idx(const VertexSet& H, size_t start_idx = 0) const {
        for (size_t i = start_idx; i < prio_buckets_.size(); ++i) {
            if (H.intersects(prio_buckets_[i].second)) {
                return i;
            }
        }
        return prio_buckets_.size();
    }

public:
    explicit ZielonkaSolver(const Arena& arena, std::vector<VertexId>* strategy = nullptr)
        : arena_(arena), strategy_(strategy) {
        init_buckets();
    }

    SolveResult solve() {
        recursive_calls_ = 0;
        VertexSet all_v = arena_.all_vertices();
        return solve_recursive(all_v, 0);
    }

    SolveResult solve_subarena(const VertexSet& H) {
        return solve_recursive(H, 0);
    }

    size_t recursive_calls() const {
        return recursive_calls_;
    }

    SolveResult solve_recursive(const VertexSet& H, size_t start_prio_idx = 0) {
        ++recursive_calls_;

        if (H.empty()) {
            return {VertexSet(arena_.num_vertices, false), VertexSet(arena_.num_vertices, false)};
        }

        // Find minimum priority in H via AVX2 bucket scan
        size_t prio_idx = find_min_prio_idx(H, start_prio_idx);
        if (prio_idx >= prio_buckets_.size()) {
            return {VertexSet(arena_.num_vertices, false), H};
        }

        Priority d = prio_buckets_[prio_idx].first;
        Player P = (d % 2 == 0) ? Player::Max : Player::Min;
        Player Q = opponent(P);

        // Extended Same-Parity Attractor Peeling:
        // Peel S_d and continue peeling subsequent minimum priorities if they share P's parity!
        VertexSet H_prime = H;
        size_t next_idx = prio_idx;
        while (next_idx < prio_buckets_.size()) {
            size_t curr_idx = find_min_prio_idx(H_prime, next_idx);
            if (curr_idx >= prio_buckets_.size()) break;
            Priority curr_d = prio_buckets_[curr_idx].first;
            if ((curr_d % 2) != (d % 2)) {
                next_idx = curr_idx;
                break;
            }
            VertexSet S_curr = H_prime.intersect(prio_buckets_[curr_idx].second);
            VertexSet attr_P = compute_attractor(arena_, H_prime, P, S_curr, strategy_);
            H_prime.inplace_difference(attr_P);
            next_idx = curr_idx + 1;
            if (H_prime.empty()) break;
        }

        if (H_prime.empty()) {
            if (P == Player::Max) {
                if (strategy_ != nullptr) {
                    // Any Max vertex in H whose strategy was not set by attractor (i.e. in S_curr)
                    // can pick any successor inside H!
                    assign_remaining_max_moves(H);
                }
                return {H, VertexSet(arena_.num_vertices, false)};
            } else {
                return {VertexSet(arena_.num_vertices, false), H};
            }
        }

        // Recursively solve H'
        SolveResult sub1 = solve_recursive(H_prime, next_idx);
        const VertexSet& W_Q_prime = (Q == Player::Max) ? sub1.lambda_max : sub1.lambda_min;

        if (W_Q_prime.empty()) {
            if (P == Player::Max) {
                if (strategy_ != nullptr) {
                    assign_remaining_max_moves(H);
                }
                return {H, VertexSet(arena_.num_vertices, false)};
            } else {
                return {VertexSet(arena_.num_vertices, false), H};
            }
        }

        // Q can win in W_Q_prime; attract to W_Q_prime in H
        VertexSet attr_Q = compute_attractor(arena_, H, Q, W_Q_prime, strategy_);
        VertexSet H_double_prime = H.difference(attr_Q);

        SolveResult sub2 = solve_recursive(H_double_prime, prio_idx);

        if (P == Player::Max) {
            return {sub2.lambda_max, sub2.lambda_min.unite(attr_Q)};
        } else {
            return {sub2.lambda_max.unite(attr_Q), sub2.lambda_min};
        }
    }

private:
    void assign_remaining_max_moves(const VertexSet& H) {
        if (strategy_ == nullptr) return;
        for (VertexId v : H) {
            if (arena_.vertices[v].owner == Player::Max) {
                VertexId cur = (*strategy_)[v];
                if (cur == std::numeric_limits<VertexId>::max() || !H.contains(cur)) {
                    for (VertexId w : arena_.succ[v]) {
                        if (H.contains(w)) {
                            (*strategy_)[v] = w;
                            break;
                        }
                    }
                }
            }
        }
    }
};

} // namespace omegasynth
