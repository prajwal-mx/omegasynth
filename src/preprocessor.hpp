#pragma once

#include "arena.hpp"
#include "attractor.hpp"
#include <map>
#include <set>
#include <vector>
#include <algorithm>
#include <limits>

namespace omegasynth {

struct PreprocessResult {
    Arena arena;                     // Arena with compressed priorities
    VertexSet active_subarena;       // Vertices remaining to be solved by the core engine
    VertexSet fixed_max_win;         // Vertices proven to be won by Max during preprocessing
    VertexSet fixed_min_win;         // Vertices proven to be won by Min during preprocessing
    std::vector<VertexId> strategy;  // Winning moves recorded for Max vertices in fixed_max_win
    size_t original_num_vertices = 0;
    size_t reduced_num_vertices = 0;
    size_t distinct_priorities = 0;
};

// High-Performance Parity & Mean-Payoff Game Preprocessor
// Implements immediate O(|Attr|) dead-end and self-loop dominion peeling,
// sound reward-aware 2-cycle dominion elimination, and priority compression.
class GamePreprocessor {
public:
    static PreprocessResult preprocess(const Arena& original_arena) {
        PreprocessResult res;
        res.original_num_vertices = original_arena.num_vertices;
        res.arena = original_arena;
        res.active_subarena = original_arena.all_vertices();
        res.fixed_max_win = VertexSet(original_arena.num_vertices, false);
        res.fixed_min_win = VertexSet(original_arena.num_vertices, false);
        res.strategy.assign(original_arena.num_vertices, std::numeric_limits<VertexId>::max());

        bool progress = true;
        while (progress && !res.active_subarena.empty()) {
            progress = false;

            // 1. Immediate Self-Loop Dominion Elimination (catches Jurdzinski/escape traps at vertex 0/1 in O(|Attr|)):
            for (VertexId v : res.active_subarena) {
                bool has_self_loop = false;
                for (VertexId w : res.arena.succ[v]) {
                    if (w == v) {
                        has_self_loop = true;
                        break;
                    }
                }
                if (!has_self_loop) continue;

                Priority p = res.arena.vertices[v].priority;
                Reward r = res.arena.vertices[v].reward;

                if (res.arena.vertices[v].owner == Player::Max && (p % 2 == 0) && r >= 0) {
                    res.strategy[v] = v;
                    VertexSet seed(res.arena.num_vertices, false);
                    seed.insert(v);
                    VertexSet attr = compute_attractor(res.arena, res.active_subarena, Player::Max, seed, &res.strategy);
                    res.fixed_max_win.inplace_unite(attr);
                    res.active_subarena.inplace_difference(attr);
                    progress = true;
                    break;
                } else if (res.arena.vertices[v].owner == Player::Min && ((p % 2 != 0) || r < 0)) {
                    VertexSet seed(res.arena.num_vertices, false);
                    seed.insert(v);
                    VertexSet attr = compute_attractor(res.arena, res.active_subarena, Player::Min, seed);
                    res.fixed_min_win.inplace_unite(attr);
                    res.active_subarena.inplace_difference(attr);
                    progress = true;
                    break;
                }
            }

            if (progress || res.active_subarena.empty()) continue;

            // 2. Dead-End Elimination:
            for (VertexId v : res.active_subarena) {
                bool has_active_succ = false;
                for (VertexId w : res.arena.succ[v]) {
                    if (res.active_subarena.contains(w)) {
                        has_active_succ = true;
                        break;
                    }
                }

                if (!has_active_succ) {
                    Player winner = opponent(res.arena.vertices[v].owner);
                    VertexSet seed(res.arena.num_vertices, false);
                    seed.insert(v);
                    VertexSet attr = compute_attractor(
                        res.arena, res.active_subarena, winner, seed,
                        (winner == Player::Max) ? &res.strategy : nullptr
                    );
                    if (winner == Player::Max) {
                        res.fixed_max_win.inplace_unite(attr);
                    } else {
                        res.fixed_min_win.inplace_unite(attr);
                    }
                    res.active_subarena.inplace_difference(attr);
                    progress = true;
                    break;
                }
            }

            if (progress || res.active_subarena.empty()) continue;

            // 3. Sound Reward-Aware 2-Cycle Dominion Elimination:
            for (VertexId u : res.active_subarena) {
                for (VertexId v : res.arena.succ[u]) {
                    if (v == u || !res.active_subarena.contains(v)) continue;

                    bool v_to_u = false;
                    for (VertexId w : res.arena.succ[v]) {
                        if (w == u) {
                            v_to_u = true;
                            break;
                        }
                    }
                    if (!v_to_u) continue;

                    Priority min_p = std::min(res.arena.vertices[u].priority, res.arena.vertices[v].priority);
                    Reward cycle_rew = res.arena.vertices[u].reward + res.arena.vertices[v].reward;

                    Player winner;
                    if (min_p % 2 == 0) {
                        if (cycle_rew < 0) continue;
                        winner = Player::Max;
                    } else {
                        winner = Player::Min;
                    }

                    auto check_node = [&](VertexId node, VertexId other) -> bool {
                        if (res.arena.vertices[node].owner == winner) {
                            return true;
                        }
                        for (VertexId w : res.arena.succ[node]) {
                            if (!res.active_subarena.contains(w)) continue;
                            if (w != node && w != other) return false;
                            if (w == node) {
                                Priority sp = res.arena.vertices[node].priority;
                                Reward sr = res.arena.vertices[node].reward;
                                if (winner == Player::Max) {
                                    if ((sp % 2 != 0) || sr < 0) return false;
                                } else {
                                    if ((sp % 2 == 0) && sr >= 0) return false;
                                }
                            }
                        }
                        return true;
                    };

                    if (!check_node(u, v) || !check_node(v, u)) continue;

                    if (winner == Player::Max) {
                        if (res.arena.vertices[u].owner == Player::Max) res.strategy[u] = v;
                        if (res.arena.vertices[v].owner == Player::Max) res.strategy[v] = u;
                    }

                    VertexSet seed(res.arena.num_vertices, false);
                    seed.insert(u);
                    seed.insert(v);
                    VertexSet attr = compute_attractor(
                        res.arena, res.active_subarena, winner, seed,
                        (winner == Player::Max) ? &res.strategy : nullptr
                    );

                    if (winner == Player::Max) {
                        res.fixed_max_win.inplace_unite(attr);
                    } else {
                        res.fixed_min_win.inplace_unite(attr);
                    }

                    res.active_subarena.inplace_difference(attr);
                    progress = true;
                    break;
                }
                if (progress) break;
            }
        }

        res.reduced_num_vertices = res.active_subarena.count();

        // 4. Priority Compression on the remaining active subarena:
        if (!res.active_subarena.empty()) {
            std::set<Priority> active_prios;
            for (VertexId v : res.active_subarena) {
                active_prios.insert(res.arena.vertices[v].priority);
            }
            res.distinct_priorities = active_prios.size();

            std::vector<Priority> sorted_p(active_prios.begin(), active_prios.end());
            if (!sorted_p.empty()) {
                std::map<Priority, Priority> prio_map;
                Priority current_comp = (sorted_p[0] % 2 == 0) ? 0 : 1;
                prio_map[sorted_p[0]] = current_comp;

                for (size_t i = 1; i < sorted_p.size(); ++i) {
                    Priority prev_orig = sorted_p[i - 1];
                    Priority curr_orig = sorted_p[i];
                    Priority prev_comp = prio_map[prev_orig];

                    if ((curr_orig % 2) == (prev_comp % 2)) {
                        prio_map[curr_orig] = prev_comp;
                    } else {
                        prio_map[curr_orig] = prev_comp + 1;
                    }
                }

                for (VertexId v : res.active_subarena) {
                    res.arena.vertices[v].priority = prio_map[res.arena.vertices[v].priority];
                }
            }
        }

        return res;
    }
};

} // namespace omegasynth
