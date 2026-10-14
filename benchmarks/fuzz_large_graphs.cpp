#include "src/arena.hpp"
#include "src/solver.hpp"
#include "src/scc_solver.hpp"
#include "src/preprocessor.hpp"
#include "src/zielonka.hpp"
#include <iostream>
#include <random>
#include <chrono>

using namespace omegasynth;

Arena generate_random_arena(size_t n, uint32_t seed, Priority max_prio = 20) {
    std::mt19937 rng(seed);
    Arena arena(n);

    for (size_t u = 0; u < n; ++u) {
        Player owner = (rng() % 2 == 0) ? Player::Max : Player::Min;
        Priority prio = static_cast<Priority>(rng() % (max_prio + 1));
        arena.add_vertex(static_cast<VertexId>(u), owner, prio, 0);

        // 2 to 6 random successors
        size_t deg = 2 + (rng() % std::min<size_t>(5, n));
        for (size_t d = 0; d < deg; ++d) {
            VertexId v = static_cast<VertexId>(rng() % n);
            arena.add_edge(u, v);
        }
    }
    return arena;
}

int main(int argc, char* argv[]) {
    size_t num_games = (argc > 1) ? std::stoul(argv[1]) : 20000;
    std::cout << "========================================================================\n";
    std::cout << "  OMEGASYNTH LARGE GRAPH MULTI-ENGINE FUZZ TEST (" << num_games << " GAMES)\n";
    std::cout << "========================================================================\n\n";
    std::cout << "[*] Evaluating 3 Core Engine Configurations per game:\n";
    std::cout << "    - Config 1: Pure Recursive Zielonka (canonical baseline)\n";
    std::cout << "    - Config 2: GamePreprocessor + Zielonka (peeled + compressed)\n";
    std::cout << "    - Config 3: Tarjan SCCSolver + Quasipolynomial Core\n";
    std::cout << "[*] Graph sizes: 50 to 200 vertices, Priorities: 0 to 25\n";
    std::cout << "[*] Full VertexSet equality verified across ALL vertices\n\n";

    size_t agreed = 0;
    size_t disagreed = 0;

    auto t0 = std::chrono::high_resolution_clock::now();

    for (size_t i = 1; i <= num_games; ++i) {
        uint32_t seed = static_cast<uint32_t>(i * 7919 + 101);
        std::mt19937 size_rng(seed);
        size_t n = 50 + (size_rng() % 151); // 50 to 200 vertices
        Priority max_p = 10 + (size_rng() % 16); // 10 to 25

        Arena arena = generate_random_arena(n, seed, max_p);

        // 1. Pure Zielonka
        ZielonkaSolver z_solver(arena);
        SolveResult res_z = z_solver.solve();

        // 2. Preprocessed Zielonka
        PreprocessResult prep = GamePreprocessor::preprocess(arena);
        SolveResult res_pz;
        if (prep.active_subarena.empty()) {
            res_pz = {prep.fixed_max_win, prep.fixed_min_win};
        } else {
            ZielonkaSolver pz_solver(prep.arena);
            SolveResult sub = pz_solver.solve_subarena(prep.active_subarena);
            res_pz = {prep.fixed_max_win.unite(sub.lambda_max), prep.fixed_min_win.unite(sub.lambda_min)};
        }

        // 3. SCCSolver (Quasipolynomial core)
        SCCSolver scc_solver(arena);
        SolveResult res_scc = scc_solver.solve();

        bool match_z_pz = (res_z.lambda_max == res_pz.lambda_max);
        bool match_z_scc = (res_z.lambda_max == res_scc.lambda_max);

        if (match_z_pz && match_z_scc) {
            ++agreed;
        } else {
            ++disagreed;
            std::cerr << "[DISAGREE at game " << i << " (seed " << seed << ", n=" << n << ")] "
                      << "PureZielonka count=" << res_z.lambda_max.count()
                      << " PrepZielonka count=" << res_pz.lambda_max.count()
                      << " SCCSolver count=" << res_scc.lambda_max.count() << "\n";
            if (disagreed >= 5) {
                std::cerr << "Aborting after 5 mismatches.\n";
                break;
            }
        }

        if (i % 2000 == 0 || i == num_games) {
            auto t_now = std::chrono::high_resolution_clock::now();
            double el = std::chrono::duration<double>(t_now - t0).count();
            std::cout << "[" << i << "/" << num_games << "] 3-Way Agreement: " << agreed << " / " << i
                      << " (" << (static_cast<double>(agreed) / i * 100.0) << "%) | Elapsed: " << el << "s\n";
        }
    }

    auto t1 = std::chrono::high_resolution_clock::now();
    double total_sec = std::chrono::duration<double>(t1 - t0).count();

    std::cout << "\n========================================================================\n";
    std::cout << "  LARGE GRAPH MULTI-ENGINE FUZZING SUMMARY\n";
    std::cout << "========================================================================\n";
    std::cout << "Total Games Fuzzed:            " << num_games << "\n";
    std::cout << "Unanimous Consensus (3-Way):   " << agreed << " / " << num_games 
              << " (" << (static_cast<double>(agreed) / num_games * 100.0) << "%)\n";
    std::cout << "Disagreements:                 " << disagreed << "\n";
    std::cout << "Total Fuzz Execution Time:     " << total_sec << " seconds\n";
    std::cout << "Average Throughput:            " << (num_games / total_sec) << " games / second\n";
    std::cout << "========================================================================\n";

    return (disagreed == 0) ? 0 : 1;
}
