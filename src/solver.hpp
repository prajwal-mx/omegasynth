#pragma once

#include "arena.hpp"
#include "attractor.hpp"
#include "mean_payoff.hpp"
#include <iostream>
#include <chrono>
#include <map>
#include <vector>
#include <algorithm>

namespace omegasynth {

struct SizeBounds {
    size_t b_max;
    size_t b_min;

    size_t get(Player p) const {
        return p == Player::Max ? b_max : b_min;
    }

    void set(Player p, size_t val) {
        if (p == Player::Max) b_max = val;
        else b_min = val;
    }
};

struct SolveResult {
    VertexSet lambda_max;
    VertexSet lambda_min;
};

struct SolverStats {
    uint64_t solve_calls = 0;
    uint64_t phase_calls = 0;
    uint64_t mean_payoff_calls = 0;
    uint32_t max_depth = 0;
    double elapsed_ms = 0.0;
};

class QuasipolynomialSolver {
private:
    const Arena& arena_;
    SolverStats stats_;
    uint32_t current_depth_ = 0;
    std::vector<std::pair<Priority, VertexSet>> prio_buckets_;
    VertexSet even_prio_vertices_;
    VertexSet odd_prio_vertices_;

    void init_buckets() {
        prio_buckets_.clear();
        even_prio_vertices_ = VertexSet(arena_.num_vertices, false);
        odd_prio_vertices_ = VertexSet(arena_.num_vertices, false);

        std::map<Priority, VertexSet> bmap;
        for (VertexId v = 0; v < arena_.num_vertices; ++v) {
            Priority p = arena_.vertices[v].priority;
            auto it = bmap.find(p);
            if (it == bmap.end()) {
                it = bmap.emplace(p, VertexSet(arena_.num_vertices, false)).first;
            }
            it->second.insert(v);
            if (p % 2 == 0) {
                even_prio_vertices_.insert(v);
            } else {
                odd_prio_vertices_.insert(v);
            }
        }
        prio_buckets_.reserve(bmap.size());
        for (auto& kv : bmap) {
            prio_buckets_.push_back(std::move(kv));
        }
    }

    // Fast AVX2 lookup of minimum priority index in H starting from min_idx
    size_t find_min_prio_idx(const VertexSet& H, size_t start_idx = 0) const {
        for (size_t i = start_idx; i < prio_buckets_.size(); ++i) {
            if (H.intersects(prio_buckets_[i].second)) {
                return i;
            }
        }
        return prio_buckets_.size();
    }

    // Procedure Phase(U) from Section 4 of OpenAI Result 104
    VertexSet phase(VertexSet U, size_t prio_idx, Priority d, Player P, Player Q, const SizeBounds& b_minus) {
        ++stats_.phase_calls;
        const VertexSet& bucket_d = prio_buckets_[prio_idx].second;

        while (!U.empty()) {
            // Step 1: If d is even and arena has negative rewards, compute X = U \ U_mp(U)
            if ((d % 2 == 0) && arena_.has_negative_rewards) {
                ++stats_.mean_payoff_calls;
                VertexSet U_mp = compute_mean_payoff_winning_set(arena_, U);
                VertexSet X = U.difference(U_mp);
                if (!X.empty()) {
                    VertexSet attr_Q = compute_attractor(arena_, U, Q, X);
                    U.inplace_difference(attr_Q);
                    continue;
                }
            }

            // Step 2: Form R_U = U \ Attr_P^U(S_U) using vectorized bucket intersection
            VertexSet S_U = U.intersect(bucket_d);
            VertexSet attr_P = compute_attractor(arena_, U, P, S_U);
            VertexSet R_U = U.difference(attr_P);

            if (R_U.empty()) {
                return U; // All vertices attracted to S_U; P wins everywhere in U
            }

            // Tighten b_minus to actual available vertices in R_U
            SizeBounds tight_b = b_minus;
            tight_b.b_max = std::min(tight_b.b_max, R_U.count());
            tight_b.b_min = std::min(tight_b.b_min, R_U.count());

            // Call Solve(R_U, b^-) starting at prio_idx + 1
            SolveResult child_res = solve_recursive(R_U, tight_b, prio_idx + 1);
            const VertexSet& C = (Q == Player::Max) ? child_res.lambda_max : child_res.lambda_min;

            if (C.empty()) {
                return U; // Phase terminates and returns current U
            }

            // Step 3: Replace U by U \ Attr_Q^U(C) and restart while loop
            VertexSet attr_Q_C = compute_attractor(arena_, U, Q, C);
            U.inplace_difference(attr_Q_C);
        }

        return VertexSet(arena_.num_vertices, false);
    }

    // Procedure Solve(H, b) from Section 4 of OpenAI Result 104
    SolveResult solve_recursive(const VertexSet& H, SizeBounds b, size_t start_prio_idx = 0) {
        ++stats_.solve_calls;
        ++current_depth_;
        if (current_depth_ > stats_.max_depth) {
            stats_.max_depth = current_depth_;
        }

        // Base Case 1: H is empty
        if (H.empty()) {
            --current_depth_;
            return {VertexSet(arena_.num_vertices, false), VertexSet(arena_.num_vertices, false)};
        }

        // Clamp bounds to |H|
        b.b_max = std::min(b.b_max, H.count());
        b.b_min = std::min(b.b_min, H.count());

        // Base Case 2: b_Max == 0 -> label every vertex Min
        if (b.b_max == 0) {
            --current_depth_;
            return {VertexSet(arena_.num_vertices, false), H};
        }

        // Base Case 3: b_Max > 0 and b_Min == 0 -> label every vertex Max
        if (b.b_min == 0) {
            --current_depth_;
            return {H, VertexSet(arena_.num_vertices, false)};
        }

        // Find minimum priority d in H via AVX2 bucket scan
        size_t prio_idx = find_min_prio_idx(H, start_prio_idx);
        if (prio_idx >= prio_buckets_.size()) {
            --current_depth_;
            return {VertexSet(arena_.num_vertices, false), H};
        }
        Priority d = prio_buckets_[prio_idx].first;
        const VertexSet& bucket_d = prio_buckets_[prio_idx].second;

        Player P = (d % 2 == 0) ? Player::Max : Player::Min;
        Player Q = opponent(P);

        // Compute halved bound b^-
        size_t b_Q = b.get(Q);
        size_t b_prime = b_Q / 2;
        SizeBounds b_minus = b;
        b_minus.set(Q, b_prime);

        // Step (i): Set U = H, and replace U by Phase(U)
        VertexSet U = phase(H, prio_idx, d, P, Q, b_minus);

        // Step (ii): If U != empty, form R_U and make call with unchanged bounds
        if (!U.empty()) {
            VertexSet S_U = U.intersect(bucket_d);
            VertexSet attr_P = compute_attractor(arena_, U, P, S_U);
            VertexSet R_U = U.difference(attr_P);
            if (!R_U.empty()) {
                SizeBounds mid_b = b;
                mid_b.b_max = std::min(mid_b.b_max, R_U.count());
                mid_b.b_min = std::min(mid_b.b_min, R_U.count());
                SolveResult mid_res = solve_recursive(R_U, mid_b, prio_idx + 1);
                const VertexSet& C = (Q == Player::Max) ? mid_res.lambda_max : mid_res.lambda_min;

                if (!C.empty()) {
                    VertexSet attr_Q_C = compute_attractor(arena_, U, Q, C);
                    U.inplace_difference(attr_Q_C);
                }
            }
        }

        // Step (iii): Replace U by Phase(U) once more (only needed if U shrank in Step (ii) and b_prime > 0)
        if (!U.empty() && b_prime > 0) {
            U = phase(U, prio_idx, d, P, Q, b_minus);
        }

        // Step (iv): Return Lambda_P = U and Lambda_Q = H \ U
        VertexSet Lambda_P = U;
        VertexSet Lambda_Q = H.difference(U);

        --current_depth_;
        if (P == Player::Max) {
            return {Lambda_P, Lambda_Q};
        } else {
            return {Lambda_Q, Lambda_P};
        }
    }

public:
    explicit QuasipolynomialSolver(const Arena& arena) : arena_(arena) {
        init_buckets();
    }

    SolveResult solve() {
        auto start = std::chrono::high_resolution_clock::now();
        stats_ = SolverStats{};
        current_depth_ = 0;

        size_t n = arena_.num_vertices;
        VertexSet all_v = arena_.all_vertices();
        SizeBounds initial_bounds{n, n};

        SolveResult result = solve_recursive(all_v, initial_bounds, 0);

        auto finish = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double, std::milli> diff = finish - start;
        stats_.elapsed_ms = diff.count();

        return result;
    }

    SolveResult solve_subarena(const VertexSet& H, const SizeBounds& bounds) {
        return solve_recursive(H, bounds, 0);
    }

    const SolverStats& stats() const {
        return stats_;
    }
};

} // namespace omegasynth
