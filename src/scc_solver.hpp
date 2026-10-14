#pragma once

#include "arena.hpp"
#include "attractor.hpp"
#include "scc.hpp"
#include "solver.hpp"
#include <chrono>

namespace omegasynth {

class SCCSolver {
private:
    const Arena& arena_;
    SolverStats combined_stats_;

public:
    explicit SCCSolver(const Arena& arena) : arena_(arena) {}

    SolveResult solve() {
        auto start = std::chrono::high_resolution_clock::now();
        combined_stats_ = SolverStats{};

        VertexSet remaining = arena_.all_vertices();
        VertexSet global_lambda_max(arena_.num_vertices, false);
        VertexSet global_lambda_min(arena_.num_vertices, false);

        while (!remaining.empty()) {
            TarjanSCC tarjan(arena_, remaining);
            SCCResult scc_res = tarjan.run();

            if (scc_res.components.empty()) {
                break;
            }

            // First component in Tarjan's output is always a bottom/terminal SCC in remaining
            const auto& sink_scc_vec = scc_res.components.front();

            VertexSet sink_scc(arena_.num_vertices, false);
            for (VertexId v : sink_scc_vec) {
                sink_scc.insert(v);
            }

            // Special fast-path optimization: Single-vertex terminal SCC
            if (sink_scc.count() == 1) {
                VertexId v = sink_scc_vec[0];
                bool has_self_loop = false;
                for (VertexId w : arena_.succ[v]) {
                    if (w == v) {
                        has_self_loop = true;
                        break;
                    }
                }

                Player winner;
                if (has_self_loop) {
                    Priority prio = arena_.vertices[v].priority;
                    Reward rew = arena_.vertices[v].reward;
                    bool max_wins = (prio % 2 == 0) && (rew >= 0);
                    winner = max_wins ? Player::Max : Player::Min;
                } else {
                    // Dead-end in remaining: owner has no valid moves in remaining and loses
                    winner = opponent(arena_.vertices[v].owner);
                }

                VertexSet target(arena_.num_vertices, false);
                target.insert(v);
                VertexSet attr = compute_attractor(arena_, remaining, winner, target);

                if (winner == Player::Max) {
                    global_lambda_max.inplace_unite(attr);
                } else {
                    global_lambda_min.inplace_unite(attr);
                }
                remaining.inplace_difference(attr);
                continue;
            }

            // Solve the terminal SCC using the Quasipolynomial Solver
            QuasipolynomialSolver sub_solver(arena_);
            SizeBounds bounds{sink_scc.count(), sink_scc.count()};
            SolveResult sub_res = sub_solver.solve_subarena(sink_scc, bounds);

            const auto& s = sub_solver.stats();
            combined_stats_.solve_calls += s.solve_calls;
            combined_stats_.phase_calls += s.phase_calls;
            combined_stats_.mean_payoff_calls += s.mean_payoff_calls;
            combined_stats_.max_depth = std::max(combined_stats_.max_depth, s.max_depth);

            if (!sub_res.lambda_max.empty()) {
                VertexSet attr_max = compute_attractor(arena_, remaining, Player::Max, sub_res.lambda_max);
                global_lambda_max.inplace_unite(attr_max);
                remaining.inplace_difference(attr_max);
            }

            if (!sub_res.lambda_min.empty() && !remaining.empty()) {
                VertexSet attr_min = compute_attractor(arena_, remaining, Player::Min, sub_res.lambda_min);
                global_lambda_min.inplace_unite(attr_min);
                remaining.inplace_difference(attr_min);
            }
        }

        auto finish = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double, std::milli> diff = finish - start;
        combined_stats_.elapsed_ms = diff.count();

        return {global_lambda_max, global_lambda_min};
    }

    const SolverStats& stats() const {
        return combined_stats_;
    }
};

} // namespace omegasynth
