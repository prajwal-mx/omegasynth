#pragma once

#include "arena.hpp"
#include "attractor.hpp"
#include "solver.hpp"
#include "zielonka.hpp"
#include "scc.hpp"
#include "preprocessor.hpp"
#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <future>
#include <functional>
#include <atomic>
#include <chrono>

namespace omegasynth {

class ThreadPool {
public:
    explicit ThreadPool(size_t num_threads = std::thread::hardware_concurrency()) : stop_(false) {
        if (num_threads == 0) num_threads = 1;
        for (size_t i = 0; i < num_threads; ++i) {
            workers_.emplace_back([this]() {
                while (true) {
                    std::function<void()> task;
                    {
                        std::unique_lock<std::mutex> lock(queue_mutex_);
                        cv_.wait(lock, [this]() { return stop_ || !tasks_.empty(); });
                        if (stop_ && tasks_.empty()) return;
                        task = std::move(tasks_.front());
                        tasks_.pop();
                    }
                    task();
                }
            });
        }
    }

    template<class F, class... Args>
    auto submit(F&& f, Args&&... args) -> std::future<std::invoke_result_t<F, Args...>> {
        using return_type = std::invoke_result_t<F, Args...>;
        auto task = std::make_shared<std::packaged_task<return_type()>>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...)
        );
        std::future<return_type> res = task->get_future();
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            tasks_.emplace([task]() { (*task)(); });
        }
        cv_.notify_one();
        return res;
    }

    ~ThreadPool() {
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            stop_ = true;
        }
        cv_.notify_all();
        for (std::thread& worker : workers_) {
            if (worker.joinable()) worker.join();
        }
    }

    size_t size() const { return workers_.size(); }

private:
    std::vector<std::thread> workers_;
    std::queue<std::function<void()>> tasks_;
    std::mutex queue_mutex_;
    std::condition_variable cv_;
    bool stop_;
};

// Sound, Multi-Threaded Wave-Based Parallel SCC Solver:
// In each wave on the active subarena, finds ALL true terminal (sink) SCCs in the condensation DAG,
// solves independent terminal SCCs concurrently across the worker thread pool, and propagates attractors.
class ParallelSCCSolver {
private:
    const Arena& arena_;
    size_t num_threads_;
    SolverStats combined_stats_;

public:
    explicit ParallelSCCSolver(const Arena& arena, size_t num_threads = std::thread::hardware_concurrency())
        : arena_(arena), num_threads_(num_threads == 0 ? 1 : num_threads) {}

    const SolverStats& stats() const {
        return combined_stats_;
    }

    SolveResult solve(bool run_preprocessing = true) {
        auto start_time = std::chrono::high_resolution_clock::now();
        combined_stats_ = SolverStats{};

        VertexSet global_max(arena_.num_vertices, false);
        VertexSet global_min(arena_.num_vertices, false);
        VertexSet active = arena_.all_vertices();
        Arena working_arena = arena_;

        // 1. Run Preprocessing if enabled
        if (run_preprocessing) {
            PreprocessResult prep = GamePreprocessor::preprocess(arena_);
            global_max.inplace_unite(prep.fixed_max_win);
            global_min.inplace_unite(prep.fixed_min_win);
            active = prep.active_subarena;
            working_arena = prep.arena;
        }

        if (active.empty()) {
            auto end_time = std::chrono::high_resolution_clock::now();
            combined_stats_.elapsed_ms = std::chrono::duration<double, std::milli>(end_time - start_time).count();
            return {global_max, global_min};
        }

        // Fast-path: if unweighted pure parity game and a single SCC or small graph,
        // check SCC structure first
        std::unique_ptr<ThreadPool> pool;
        std::vector<int32_t> comp_id(working_arena.num_vertices, -1);

        while (!active.empty()) {
            TarjanSCC tarjan(working_arena, active);
            SCCResult scc_res = tarjan.run();

            if (scc_res.components.empty()) {
                break;
            }

            // Label each active vertex with its component index
            for (size_t c_idx = 0; c_idx < scc_res.components.size(); ++c_idx) {
                for (VertexId v : scc_res.components[c_idx]) {
                    comp_id[v] = static_cast<int32_t>(c_idx);
                }
            }

            // Identify ALL true terminal (sink) SCCs in 'active'
            // A component C_idx is a sink SCC in 'active' iff no vertex in C_idx has an edge to another component in 'active'.
            std::vector<size_t> sink_indices;
            for (size_t c_idx = 0; c_idx < scc_res.components.size(); ++c_idx) {
                bool is_sink = true;
                for (VertexId u : scc_res.components[c_idx]) {
                    for (VertexId w : working_arena.succ[u]) {
                        if (active.contains(w) && comp_id[w] != static_cast<int32_t>(c_idx)) {
                            is_sink = false;
                            break;
                        }
                    }
                    if (!is_sink) break;
                }
                if (is_sink) {
                    sink_indices.push_back(c_idx);
                }
            }

            VertexSet wave_max_seeds(working_arena.num_vertices, false);
            VertexSet wave_min_seeds(working_arena.num_vertices, false);
            std::vector<size_t> nontrivial_sinks;

            // Process trivial (size-1) sink SCCs immediately in O(1) each
            for (size_t c_idx : sink_indices) {
                const auto& comp_vec = scc_res.components[c_idx];
                if (comp_vec.size() == 1) {
                    VertexId v = comp_vec[0];
                    bool has_self_loop = false;
                    for (VertexId w : working_arena.succ[v]) {
                        if (w == v) {
                            has_self_loop = true;
                            break;
                        }
                    }
                    Player winner;
                    if (has_self_loop) {
                        Priority prio = working_arena.vertices[v].priority;
                        Reward rew = working_arena.vertices[v].reward;
                        winner = ((prio % 2 == 0) && (rew >= 0)) ? Player::Max : Player::Min;
                    } else {
                        // Dead-end in active subarena: owner loses
                        winner = opponent(working_arena.vertices[v].owner);
                    }
                    if (winner == Player::Max) {
                        wave_max_seeds.insert(v);
                    } else {
                        wave_min_seeds.insert(v);
                    }
                } else {
                    nontrivial_sinks.push_back(c_idx);
                }
            }

            // Solve nontrivial sink SCCs (in parallel if multiple and large enough)
            if (nontrivial_sinks.size() == 1 || num_threads_ <= 1) {
                for (size_t c_idx : nontrivial_sinks) {
                    const auto& comp_vec = scc_res.components[c_idx];
                    VertexSet comp_scc(working_arena.num_vertices, false);
                    for (VertexId v : comp_vec) {
                        comp_scc.insert(v);
                    }
                    if (!working_arena.has_negative_rewards) {
                        ZielonkaSolver z_solver(working_arena);
                        SolveResult sub_res = z_solver.solve_subarena(comp_scc);
                        combined_stats_.solve_calls += z_solver.recursive_calls();
                        wave_max_seeds.inplace_unite(sub_res.lambda_max);
                        wave_min_seeds.inplace_unite(sub_res.lambda_min);
                    } else {
                        size_t n = comp_scc.count();
                        SizeBounds bounds{n, n};
                        QuasipolynomialSolver sub_solver(working_arena);
                        SolveResult sub_res = sub_solver.solve_subarena(comp_scc, bounds);
                        const auto& st = sub_solver.stats();
                        combined_stats_.solve_calls += st.solve_calls;
                        combined_stats_.phase_calls += st.phase_calls;
                        combined_stats_.mean_payoff_calls += st.mean_payoff_calls;
                        combined_stats_.max_depth = std::max(combined_stats_.max_depth, st.max_depth);
                        wave_max_seeds.inplace_unite(sub_res.lambda_max);
                        wave_min_seeds.inplace_unite(sub_res.lambda_min);
                    }
                }
            } else if (nontrivial_sinks.size() > 1) {
                if (!pool) {
                    pool = std::make_unique<ThreadPool>(num_threads_);
                }
                struct TaskOut {
                    SolveResult res;
                    SolverStats st;
                };
                std::vector<std::future<TaskOut>> futures;
                futures.reserve(nontrivial_sinks.size());

                for (size_t c_idx : nontrivial_sinks) {
                    futures.push_back(pool->submit([&, c_idx]() -> TaskOut {
                        const auto& comp_vec = scc_res.components[c_idx];
                        VertexSet comp_scc(working_arena.num_vertices, false);
                        for (VertexId v : comp_vec) {
                            comp_scc.insert(v);
                        }
                        TaskOut out;
                        if (!working_arena.has_negative_rewards) {
                            ZielonkaSolver z_solver(working_arena);
                            out.res = z_solver.solve_subarena(comp_scc);
                            out.st.solve_calls = z_solver.recursive_calls();
                        } else {
                            size_t n = comp_scc.count();
                            SizeBounds bounds{n, n};
                            QuasipolynomialSolver sub_solver(working_arena);
                            out.res = sub_solver.solve_subarena(comp_scc, bounds);
                            out.st = sub_solver.stats();
                        }
                        return out;
                    }));
                }

                for (auto& fut : futures) {
                    TaskOut out = fut.get();
                    combined_stats_.solve_calls += out.st.solve_calls;
                    combined_stats_.phase_calls += out.st.phase_calls;
                    combined_stats_.mean_payoff_calls += out.st.mean_payoff_calls;
                    combined_stats_.max_depth = std::max(combined_stats_.max_depth, out.st.max_depth);
                    wave_max_seeds.inplace_unite(out.res.lambda_max);
                    wave_min_seeds.inplace_unite(out.res.lambda_min);
                }
            }

            // Propagate wave attractors across 'active'
            if (!wave_max_seeds.empty()) {
                VertexSet max_attr = compute_attractor(working_arena, active, Player::Max, wave_max_seeds);
                global_max.inplace_unite(max_attr);
                active.inplace_difference(max_attr);
            }

            if (!wave_min_seeds.empty() && !active.empty()) {
                VertexSet valid_min_seeds = wave_min_seeds.intersect(active);
                if (!valid_min_seeds.empty()) {
                    VertexSet min_attr = compute_attractor(working_arena, active, Player::Min, valid_min_seeds);
                    global_min.inplace_unite(min_attr);
                    active.inplace_difference(min_attr);
                }
            }
        }

        auto end_time = std::chrono::high_resolution_clock::now();
        combined_stats_.elapsed_ms = std::chrono::duration<double, std::milli>(end_time - start_time).count();

        return {global_max, global_min};
    }
};

} // namespace omegasynth
