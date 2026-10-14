#pragma once

#include "arena.hpp"
#include "attractor.hpp"
#include "policy_iteration.hpp"
#include <vector>
#include <queue>
#include <cmath>
#include <limits>

namespace omegasynth {

constexpr int64_t INF_ENERGY = std::numeric_limits<int64_t>::max() / 2;

// Computes U_mp(U): the set of vertices in subarena U where Player Max
// can force liminf mean payoff >= 0 against any history-dependent strategy of Min.
inline VertexSet compute_mean_payoff_winning_set(const Arena& arena, const VertexSet& subarena) {
    if (subarena.empty()) {
        return VertexSet(arena.num_vertices, false);
    }

    // O(1) fast-path for pure parity / non-negative reward games
    if (!arena.has_negative_rewards) {
        return subarena;
    }

    size_t n = subarena.count();
    int64_t max_weight = 0;
    int64_t min_weight = 0;
    bool first = true;

    for (VertexId v : subarena) {
        Reward r = arena.vertices[v].reward;
        int64_t abs_r = (r < 0) ? -r : r;
        if (abs_r > max_weight) max_weight = abs_r;
        if (first) {
            min_weight = r;
            first = false;
        } else {
            if (r < min_weight) min_weight = r;
        }
    }

    if (min_weight >= 0) {
        return subarena;
    }

    // Capacity bound: (n - 1) * max_weight.
    // If the pseudopolynomial bound exceeds 50,000, dispatch to exact Hoffman-Karp Policy Iteration
    // to prevent pseudopolynomial hangs on large binary weights (e.g., W = 10^9).
    int64_t span = static_cast<int64_t>(n > 1 ? (n - 1) : 1);
    if (max_weight > 50000 / span) {
        return PolicyIterationMP::solve(arena, subarena);
    }

    int64_t bound = span * max_weight;
    if (bound == 0) {
        return subarena;
    }

    // Energy Game Value Iteration (Brim et al.)
    std::vector<int64_t> energy(arena.num_vertices, 0);
    std::vector<bool> in_queue(arena.num_vertices, false);
    std::queue<VertexId> queue;

    for (VertexId v : subarena) {
        queue.push(v);
        in_queue[v] = true;
    }

    while (!queue.empty()) {
        VertexId v = queue.front();
        queue.pop();
        in_queue[v] = false;

        if (!subarena.contains(v) || energy[v] >= INF_ENERGY) {
            continue;
        }

        int64_t old_val = energy[v];
        int64_t new_val = 0;
        Reward r = arena.vertices[v].reward;

        if (arena.vertices[v].owner == Player::Max) {
            int64_t min_cand = INF_ENERGY;
            for (VertexId succ : arena.succ[v]) {
                if (!subarena.contains(succ)) continue;
                int64_t cand = energy[succ] - r;
                if (cand < 0) cand = 0;
                if (cand < min_cand) {
                    min_cand = cand;
                }
            }
            new_val = min_cand;
        } else {
            int64_t max_cand = 0;
            for (VertexId succ : arena.succ[v]) {
                if (!subarena.contains(succ)) continue;
                if (energy[succ] >= INF_ENERGY) {
                    max_cand = INF_ENERGY;
                    break;
                }
                int64_t cand = energy[succ] - r;
                if (cand < 0) cand = 0;
                if (cand > max_cand) {
                    max_cand = cand;
                }
            }
            new_val = max_cand;
        }

        if (new_val > bound) {
            new_val = INF_ENERGY;
        }

        if (new_val > old_val) {
            energy[v] = new_val;
            for (VertexId pred : arena.pred[v]) {
                if (subarena.contains(pred) && energy[pred] < INF_ENERGY && !in_queue[pred]) {
                    queue.push(pred);
                    in_queue[pred] = true;
                }
            }
        }
    }

    VertexSet losing(arena.num_vertices, false);
    for (VertexId v : subarena) {
        if (energy[v] >= INF_ENERGY) {
            losing.insert(v);
        }
    }

    if (!losing.empty()) {
        VertexSet losing_attr = compute_attractor(arena, subarena, Player::Min, losing);
        return subarena.difference(losing_attr);
    }

    return subarena;
}

} // namespace omegasynth
