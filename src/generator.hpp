#pragma once

#include "arena.hpp"
#include <random>

namespace omegasynth {

class GameGenerator {
public:
    // Generates a Jurdzinski worst-case parity game of dimensions (h, w).
    // These games are famous for forcing classic recursive algorithms into exponential time!
    static Arena make_jurdzinski(size_t h, size_t w) {
        size_t n = 2 * h * w;
        Arena arena(n);

        auto u = [w](size_t i, size_t j) -> VertexId {
            return static_cast<VertexId>(2 * (i * w + j));
        };
        auto v = [w](size_t i, size_t j) -> VertexId {
            return static_cast<VertexId>(2 * (i * w + j) + 1);
        };

        for (size_t i = 0; i < h; ++i) {
            for (size_t j = 0; j < w; ++j) {
                VertexId uid = u(i, j);
                VertexId vid = v(i, j);

                Priority prio = static_cast<Priority>(2 * (i + 1));
                arena.add_vertex(uid, Player::Min, prio, 0, "u_" + std::to_string(i) + "_" + std::to_string(j));
                arena.add_vertex(vid, Player::Max, 1, 0, "v_" + std::to_string(i) + "_" + std::to_string(j));

                // u connects to v
                arena.add_edge(uid, vid);

                // v connects backward or across
                if (j + 1 < w) {
                    arena.add_edge(vid, u(i, j + 1));
                }
                if (i > 0) {
                    arena.add_edge(vid, u(i - 1, j));
                }
                if (i + 1 < h) {
                    arena.add_edge(vid, u(i + 1, j));
                }
                // self/cycle fallback if dead-end
                if (j + 1 >= w && i == 0 && i + 1 >= h) {
                    arena.add_edge(vid, uid);
                } else if (arena.succ[vid].empty()) {
                    arena.add_edge(vid, uid);
                }
            }
        }
        return arena;
    }

    // Generates a Reactive Synthesis Arbiter Game (Request-Grant with latency/energy penalty)
    static Arena make_arbiter_synthesis(size_t num_clients, [[maybe_unused]] int64_t battery_budget = 0) {
        // Models clients requesting resources, arbiter granting.
        // Even priorities: Grant visited.
        // Odd priorities: Starvation state.
        // Rewards: Energy consumption per grant/idle.
        size_t n = 2 * num_clients + 2;
        Arena arena(n);

        VertexId idle = 0;
        arena.add_vertex(idle, Player::Max, 2, 1, "IDLE"); // reward +1 recharge

        VertexId arbiter = 1;
        arena.add_vertex(arbiter, Player::Max, 4, -1, "ARBITER"); // cost -1 dispatch

        arena.add_edge(idle, arbiter);
        arena.add_edge(arbiter, idle);

        for (size_t i = 0; i < num_clients; ++i) {
            VertexId req = static_cast<VertexId>(2 + 2 * i);
            VertexId grant = static_cast<VertexId>(3 + 2 * i);

            // Request state owned by Environment (Min)
            arena.add_vertex(req, Player::Min, static_cast<Priority>(3 + 2 * i), -2, "REQ_" + std::to_string(i));
            // Grant state owned by System (Max)
            arena.add_vertex(grant, Player::Max, static_cast<Priority>(2), -5, "GRANT_" + std::to_string(i));

            arena.add_edge(arbiter, req);
            arena.add_edge(req, grant);
            arena.add_edge(grant, idle);
            arena.add_edge(req, idle); // cancel
        }

        return arena;
    }

    // Generates a random game for stress testing
    static Arena make_random(size_t n, size_t deg, Priority max_prio, Reward max_abs_reward, uint64_t seed = 42) {
        Arena arena(n);
        std::mt19937_64 rng(seed);
        std::uniform_int_distribution<int> owner_dist(0, 1);
        std::uniform_int_distribution<uint32_t> prio_dist(0, max_prio);
        std::uniform_int_distribution<int64_t> rew_dist(-max_abs_reward, max_abs_reward);
        std::uniform_int_distribution<size_t> vert_dist(0, n - 1);

        for (size_t i = 0; i < n; ++i) {
            Player p = owner_dist(rng) == 0 ? Player::Max : Player::Min;
            Priority pr = prio_dist(rng);
            Reward rw = rew_dist(rng);
            arena.add_vertex(static_cast<VertexId>(i), p, pr, rw);
        }

        // Add edges ensuring at least 1 out-edge per vertex
        for (size_t i = 0; i < n; ++i) {
            size_t actual_deg = std::max<size_t>(1, deg);
            for (size_t d = 0; d < actual_deg; ++d) {
                VertexId target = static_cast<VertexId>(vert_dist(rng));
                arena.add_edge(static_cast<VertexId>(i), target);
            }
        }

        return arena;
    }
};

} // namespace omegasynth
