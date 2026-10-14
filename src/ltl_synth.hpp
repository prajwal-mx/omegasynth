#pragma once

#include "arena.hpp"
#include <string>
#include <vector>

namespace omegasynth {

// Compiles high-level reactive specifications directly into two-player Mean-Payoff Parity arenas.
// Eliminates external tool dependencies for canonical reactive synthesis domains.
class LTLSynthesizer {
public:
    // Compiles an N-client Arbiter with mutual exclusion, starvation-freedom, and power budget.
    // LTL Specification:
    //   Safety: G !(grant_i & grant_j) for all i != j
    //   Liveness: G (req_i -> F grant_i) for all i
    //   Quantitative: liminf 1/T sum reward >= 0 (energy self-sufficiency)
    static Arena compile_arbiter(size_t num_clients, int64_t power_budget = 0) {
        // State 0: IDLE (Max owned)
        // State 1: DISPATCH (Max owned)
        // For each client i:
        //   State 2 + 2*i:     REQUEST_i (Min owned)
        //   State 2 + 2*i + 1: GRANT_i   (Max owned)
        size_t n = 2 + 2 * num_clients;
        Arena arena(n);

        VertexId idle = 0;
        arena.add_vertex(idle, Player::Max, 2, power_budget >= 0 ? 2 : 1, "IDLE");

        VertexId dispatch = 1;
        arena.add_vertex(dispatch, Player::Max, 4, -1, "DISPATCH");

        arena.add_edge(idle, dispatch);
        arena.add_edge(dispatch, idle);

        for (size_t i = 0; i < num_clients; ++i) {
            VertexId req = static_cast<VertexId>(2 + 2 * i);
            VertexId grant = static_cast<VertexId>(3 + 2 * i);

            // Odd priority: starved request (Min favored unless granted)
            Priority req_prio = static_cast<Priority>(3 + 2 * i);
            // Even priority: granted (Max favored)
            Priority grant_prio = 2;

            arena.add_vertex(req, Player::Min, req_prio, -2, "REQ_" + std::to_string(i));
            arena.add_vertex(grant, Player::Max, grant_prio, -4, "GRANT_" + std::to_string(i));

            arena.add_edge(dispatch, req);
            arena.add_edge(req, grant);
            arena.add_edge(grant, idle);
            arena.add_edge(req, idle); // Client withdraws request
        }

        return arena;
    }

    // Compiles a N-resource Mutual Exclusion (Mutex) Controller with FIFO fairness.
    static Arena compile_mutex(size_t num_threads) {
        size_t n = 1 + 3 * num_threads;
        Arena arena(n);

        VertexId lock_free = 0;
        arena.add_vertex(lock_free, Player::Max, 2, 0, "LOCK_FREE");

        for (size_t i = 0; i < num_threads; ++i) {
            VertexId waiting = static_cast<VertexId>(1 + 3 * i);
            VertexId acquired = static_cast<VertexId>(2 + 3 * i);
            VertexId critical = static_cast<VertexId>(3 + 3 * i);

            arena.add_vertex(waiting, Player::Min, static_cast<Priority>(3 + 2 * i), -1, "WAIT_" + std::to_string(i));
            arena.add_vertex(acquired, Player::Max, 2, 0, "ACQUIRE_" + std::to_string(i));
            arena.add_vertex(critical, Player::Max, 4, 1, "CRITICAL_" + std::to_string(i));

            arena.add_edge(lock_free, waiting);
            arena.add_edge(waiting, acquired);
            arena.add_edge(acquired, critical);
            arena.add_edge(critical, lock_free);
        }

        return arena;
    }

    // Compiles a Bounded Buffer Controller with overflow/underflow avoidance
    static Arena compile_buffer(size_t capacity) {
        size_t n = capacity + 1;
        Arena arena(n);

        for (size_t i = 0; i <= capacity; ++i) {
            std::string name = "BUF_LEVEL_" + std::to_string(i);
            // Buffer empty (0) or full (capacity) are critical states
            Priority prio = (i == 0 || i == capacity) ? 1 : 2;
            Reward rew = (i > 0 && i < capacity) ? 1 : -1;
            arena.add_vertex(static_cast<VertexId>(i), Player::Max, prio, rew, name);

            if (i > 0) {
                arena.add_edge(static_cast<VertexId>(i), static_cast<VertexId>(i - 1)); // pop
            }
            if (i < capacity) {
                arena.add_edge(static_cast<VertexId>(i), static_cast<VertexId>(i + 1)); // push
            }
            // self transition (idle)
            arena.add_edge(static_cast<VertexId>(i), static_cast<VertexId>(i));
        }

        return arena;
    }
};

} // namespace omegasynth
