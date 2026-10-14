#pragma once

#include "arena.hpp"
#include "attractor.hpp"
#include <vector>
#include <limits>
#include <cmath>
#include <algorithm>

namespace omegasynth {

// Exact Alternating Howard Policy Iteration (Hoffman-Karp) for Two-Player Mean-Payoff Games.
// Evaluates exact cycle gains g(v) and biases b(v) on 1-regular functional subgraphs.
// Handles arbitrary 64-bit signed integer rewards without pseudopolynomial slowdown.
class PolicyIterationMP {
public:
    static VertexSet solve(const Arena& arena, const VertexSet& subarena) {
        if (subarena.empty()) {
            return VertexSet(arena.num_vertices, false);
        }

        // First peel any dead-ends inside subarena
        VertexSet active = subarena;
        bool peeled = true;
        while (peeled && !active.empty()) {
            peeled = false;
            VertexSet max_dead(arena.num_vertices, false);
            VertexSet min_dead(arena.num_vertices, false);
            for (VertexId v : active) {
                bool has_succ = false;
                for (VertexId w : arena.succ[v]) {
                    if (active.contains(w)) {
                        has_succ = true;
                        break;
                    }
                }
                if (!has_succ) {
                    if (arena.vertices[v].owner == Player::Max) max_dead.insert(v);
                    else min_dead.insert(v);
                }
            }
            if (!max_dead.empty()) {
                // Max has no move -> Min wins max_dead and its Min-attractor
                VertexSet attr = compute_attractor(arena, active, Player::Min, max_dead);
                active.inplace_difference(attr);
                peeled = true;
            }
            if (!min_dead.empty() && !active.empty()) {
                // Min has no move -> Max wins min_dead and its Max-attractor;
                // Wait: if Min is forced into min_dead, Max wins immediately, so we can self-loop min_dead with +1 reward
                break;
            }
        }

        if (active.empty()) {
            return VertexSet(arena.num_vertices, false);
        }

        // Quick check: if all rewards in active are >= 0, Max always secures non-negative gain.
        bool all_nonneg = true;
        for (VertexId v : active) {
            if (arena.vertices[v].reward < 0) {
                all_nonneg = false;
                break;
            }
        }
        if (all_nonneg) return active;

        // Next-hop policy for every vertex in active
        std::vector<VertexId> next_hop(arena.num_vertices, 0);
        for (VertexId v : active) {
            next_hop[v] = v; // fallback self-loop if Min dead-end
            for (VertexId w : arena.succ[v]) {
                if (active.contains(w)) {
                    next_hop[v] = w;
                    break;
                }
            }
        }

        std::vector<double> g(arena.num_vertices, 0.0);
        std::vector<double> b(arena.num_vertices, 0.0);

        const int max_outer = 50 + static_cast<int>(2 * active.count());
        const int max_inner = 50 + static_cast<int>(2 * active.count());

        for (int outer = 0; outer < max_outer; ++outer) {
            // 1. Inner loop: optimize Player::Min counter-strategy to convergence against current Max strategy
            for (int inner = 0; inner < max_inner; ++inner) {
                evaluate_functional_graph(arena, active, next_hop, g, b);
                bool min_changed = false;
                for (VertexId v : active) {
                    if (arena.vertices[v].owner != Player::Min) continue;
                    VertexId current_succ = next_hop[v];
                    double best_gain = g[current_succ];
                    double best_bias = b[current_succ] + static_cast<double>(arena.vertices[v].reward);
                    VertexId best_succ = current_succ;

                    for (VertexId succ : arena.succ[v]) {
                        if (!active.contains(succ)) continue;
                        double cand_gain = g[succ];
                        double cand_bias = b[succ] + static_cast<double>(arena.vertices[v].reward);

                        if (cand_gain < best_gain - 1e-9 ||
                           (std::abs(cand_gain - best_gain) <= 1e-9 && cand_bias < best_bias - 1e-9)) {
                            best_gain = cand_gain;
                            best_bias = cand_bias;
                            best_succ = succ;
                        }
                    }
                    if (best_succ != current_succ) {
                        next_hop[v] = best_succ;
                        min_changed = true;
                    }
                }
                if (!min_changed) break;
            }

            // 2. Outer step: improve Player::Max strategy
            bool max_changed = false;
            for (VertexId v : active) {
                if (arena.vertices[v].owner != Player::Max) continue;
                VertexId current_succ = next_hop[v];
                double best_gain = g[current_succ];
                double best_bias = b[current_succ] + static_cast<double>(arena.vertices[v].reward);
                VertexId best_succ = current_succ;

                for (VertexId succ : arena.succ[v]) {
                    if (!active.contains(succ)) continue;
                    double cand_gain = g[succ];
                    double cand_bias = b[succ] + static_cast<double>(arena.vertices[v].reward);

                    if (cand_gain > best_gain + 1e-9 ||
                       (std::abs(cand_gain - best_gain) <= 1e-9 && cand_bias > best_bias + 1e-9)) {
                        best_gain = cand_gain;
                        best_bias = cand_bias;
                        best_succ = succ;
                    }
                }
                if (best_succ != current_succ) {
                    next_hop[v] = best_succ;
                    max_changed = true;
                }
            }

            if (!max_changed) {
                break;
            }
        }

        // Winning condition for Max in Mean-Payoff games: gain >= 0
        VertexSet max_win(arena.num_vertices, false);
        for (VertexId v : active) {
            if (g[v] >= -1e-9) {
                max_win.insert(v);
            }
        }

        // Ensure safety: Min attractor to strictly negative region
        VertexSet min_strictly_neg = active.difference(max_win);
        if (!min_strictly_neg.empty()) {
            VertexSet min_attr = compute_attractor(arena, active, Player::Min, min_strictly_neg);
            return active.difference(min_attr);
        }

        return max_win;
    }

private:
    static void evaluate_functional_graph(const Arena& arena,
                                          const VertexSet& subarena,
                                          const std::vector<VertexId>& next_hop,
                                          std::vector<double>& g,
                                          std::vector<double>& b) {
        std::vector<uint8_t> state(arena.num_vertices, 0);

        for (VertexId start : subarena) {
            if (state[start] != 0) continue;

            std::vector<VertexId> path;
            VertexId curr = start;

            while (subarena.contains(curr) && state[curr] == 0) {
                state[curr] = 1;
                path.push_back(curr);
                curr = next_hop[curr];
            }

            if (subarena.contains(curr) && state[curr] == 1) {
                size_t cycle_start = 0;
                while (cycle_start < path.size() && path[cycle_start] != curr) {
                    ++cycle_start;
                }

                double sum_rew = 0.0;
                size_t cycle_len = path.size() - cycle_start;
                for (size_t i = cycle_start; i < path.size(); ++i) {
                    sum_rew += static_cast<double>(arena.vertices[path[i]].reward);
                }
                double cycle_gain = sum_rew / static_cast<double>(cycle_len);

                // Reference node is c_0 = path[cycle_start] with bias 0.0
                VertexId ref_node = path[cycle_start];
                b[ref_node] = 0.0;
                for (size_t i = cycle_start; i < path.size(); ++i) {
                    VertexId v = path[i];
                    g[v] = cycle_gain;
                    state[v] = 2;
                }

                // Backpropagate biases along cycle:
                // c_{k-1} = path.back() transitions directly to c_0 = path[cycle_start]
                if (cycle_len > 1) {
                    VertexId last_v = path.back();
                    b[last_v] = static_cast<double>(arena.vertices[last_v].reward) - cycle_gain + b[ref_node];
                    for (size_t i = path.size() - 1; i > cycle_start + 1; --i) {
                        VertexId u = path[i - 1];
                        VertexId v = path[i];
                        b[u] = static_cast<double>(arena.vertices[u].reward) - cycle_gain + b[v];
                    }
                }

                // Propagate upstream along the tree path leading into the cycle
                if (cycle_start > 0) {
                    for (size_t i = cycle_start; i-- > 0;) {
                        VertexId u = path[i];
                        VertexId v = path[i + 1];
                        g[u] = cycle_gain;
                        b[u] = static_cast<double>(arena.vertices[u].reward) - cycle_gain + b[v];
                        state[u] = 2;
                    }
                }
            } else if (subarena.contains(curr) && state[curr] == 2) {
                double target_gain = g[curr];
                for (size_t i = path.size(); i-- > 0;) {
                    VertexId u = path[i];
                    VertexId next = next_hop[u];
                    g[u] = target_gain;
                    b[u] = static_cast<double>(arena.vertices[u].reward) - target_gain + b[next];
                    state[u] = 2;
                }
            } else {
                for (VertexId u : path) {
                    g[u] = -1e9;
                    b[u] = 0.0;
                    state[u] = 2;
                }
            }
        }
    }
};

} // namespace omegasynth
