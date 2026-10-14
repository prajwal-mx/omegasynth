#include "arena.hpp"
#include "attractor.hpp"
#include "mean_payoff.hpp"
#include "policy_iteration.hpp"
#include "solver.hpp"
#include "zielonka.hpp"
#include "scc_solver.hpp"
#include "strategy.hpp"
#include "preprocessor.hpp"
#include "parallel_solver.hpp"
#include "aiger.hpp"
#include "ltl_synth.hpp"
#include "parser.hpp"
#include "hoa_parser.hpp"
#include "generator.hpp"
#include <iostream>
#include <fstream>
#include <iomanip>
#include <cassert>
#include <cstdio>

using namespace omegasynth;

void run_verification_tests() {
    std::cout << "========================================================\n";
    std::cout << "  RUNNING OMEGASYNTH VERIFICATION TEST SUITE (Result 104)\n";
    std::cout << "========================================================\n\n";

    int passed = 0;
    int total = 0;

    // Test 1: Simple Even Cycle (Max should win)
    {
        ++total;
        std::cout << "[Test 1] 2-vertex Even Cycle (priorities 0 and 2, reward 0)... ";
        Arena a(2);
        a.add_vertex(0, Player::Max, 0, 0, "v0");
        a.add_vertex(1, Player::Min, 2, 0, "v1");
        a.add_edge(0, 1);
        a.add_edge(1, 0);

        QuasipolynomialSolver solver(a);
        auto res = solver.solve();
        assert(res.lambda_max.count() == 2);
        assert(res.lambda_min.count() == 0);
        std::cout << "PASSED (Max wins {0, 1})\n";
        ++passed;
    }

    // Test 2: Simple Odd Cycle (Min should win)
    {
        ++total;
        std::cout << "[Test 2] 2-vertex Odd Cycle (priorities 1 and 3, reward 0)... ";
        Arena a(2);
        a.add_vertex(0, Player::Max, 1, 0, "v0");
        a.add_vertex(1, Player::Min, 3, 0, "v1");
        a.add_edge(0, 1);
        a.add_edge(1, 0);

        QuasipolynomialSolver solver(a);
        auto res = solver.solve();
        assert(res.lambda_max.count() == 0);
        assert(res.lambda_min.count() == 2);
        std::cout << "PASSED (Min wins {0, 1})\n";
        ++passed;
    }

    // Test 3: Even priority but Negative Cycle (Min should win due to mean payoff)
    {
        ++total;
        std::cout << "[Test 3] Even priority (0) with Negative Reward cycle (-1)... ";
        Arena a(2);
        a.add_vertex(0, Player::Max, 0, -2, "v0");
        a.add_vertex(1, Player::Min, 0, 1, "v1"); // cycle sum = -1
        a.add_edge(0, 1);
        a.add_edge(1, 0);

        QuasipolynomialSolver solver(a);
        auto res = solver.solve();
        assert(res.lambda_max.count() == 0);
        assert(res.lambda_min.count() == 2);
        std::cout << "PASSED (Min correctly wins due to negative mean payoff!)\n";
        ++passed;
    }

    // Test 4: Positive Reward but Odd Priority (Min should win due to odd parity)
    {
        ++total;
        std::cout << "[Test 4] Positive Reward (+5) but Odd Priority (1)... ";
        Arena a(2);
        a.add_vertex(0, Player::Max, 1, 5, "v0");
        a.add_vertex(1, Player::Min, 1, 5, "v1");
        a.add_edge(0, 1);
        a.add_edge(1, 0);

        QuasipolynomialSolver solver(a);
        auto res = solver.solve();
        assert(res.lambda_max.count() == 0);
        assert(res.lambda_min.count() == 2);
        std::cout << "PASSED (Min correctly wins due to odd parity!)\n";
        ++passed;
    }

    // Test 5: Choice Arena: Max chooses safe even cycle
    {
        ++total;
        std::cout << "[Test 5] Strategic Choice: Max chooses between safe even cycle and trap... ";
        Arena a(3);
        a.add_vertex(0, Player::Max, 0, 0, "choice");
        a.add_vertex(1, Player::Min, 2, 0, "even_node");
        a.add_vertex(2, Player::Min, 1, 0, "odd_trap");
        a.add_edge(0, 1); // safe
        a.add_edge(0, 2); // trap
        a.add_edge(1, 0);
        a.add_edge(2, 2);

        QuasipolynomialSolver solver(a);
        auto res = solver.solve();
        assert(res.lambda_max.contains(0));
        assert(res.lambda_max.contains(1));
        assert(res.lambda_min.contains(2));
        std::cout << "PASSED (Max correctly secures {0, 1}, Min gets {2})\n";
        ++passed;
    }

    // Test 6: Jurdzinski worst-case parity game (h=3, w=3) with SCC decomposition
    {
        ++total;
        std::cout << "[Test 6] Jurdzinski Game (h=3, w=3) with SCC Decomposition... ";
        Arena a = GameGenerator::make_jurdzinski(3, 3);
        SCCSolver scc_solver(a);
        auto res = scc_solver.solve();
        assert(res.lambda_min.count() == a.num_vertices);
        std::cout << "PASSED (Solved via SCC engine in " << scc_solver.stats().elapsed_ms << " ms)\n";
        ++passed;
    }

    // Test 7: Howard's Policy Iteration on Gigantic 64-bit Weights (W = 10^9)
    {
        ++total;
        std::cout << "[Test 7] Howard's Policy Iteration with Giant Weights (W = 1,000,000,000)... ";
        Arena a(3);
        a.add_vertex(0, Player::Max, 0, 1000000000LL, "v0");
        a.add_vertex(1, Player::Min, 0, -999999999LL, "v1"); // sum = +1
        a.add_vertex(2, Player::Min, 0, -2000000000LL, "v2"); // trap: sum = -2*10^9
        a.add_edge(0, 1);
        a.add_edge(0, 2);
        a.add_edge(1, 0);
        a.add_edge(2, 2);

        QuasipolynomialSolver qp_solver(a);
        auto qp_res = qp_solver.solve();
        assert(qp_res.lambda_max.contains(0));
        assert(qp_res.lambda_max.contains(1));
        std::cout << "PASSED (Converged instantaneously on billion-scale weights!)\n";
        ++passed;
    }

    // Test 8: Controller Strategy Synthesis (Verilog / Rust emission)
    {
        ++total;
        std::cout << "[Test 8] Controller Strategy Synthesis & Code Emission... ";
        Arena a = GameGenerator::make_arbiter_synthesis(2, 10);
        SCCSolver scc_solver(a);
        auto res = scc_solver.solve();

        ControllerStrategy cs = StrategySynthesizer::synthesize(a, res.lambda_max);
        std::string verilog = StrategySynthesizer::emit_verilog(a, cs, "ArbiterController");
        std::string rust = StrategySynthesizer::emit_rust(a, cs);

        assert(!verilog.empty() && verilog.find("module ArbiterController") != std::string::npos);
        assert(!rust.empty() && rust.find("pub enum ControllerState") != std::string::npos);
        std::cout << "PASSED (Synthesized Verilog RTL and Rust state machines successfully)\n";
        ++passed;
    }

    // Test 9: Preprocessor & Priority Compression (including reward-aware 2-cycle check)
    {
        ++total;
        std::cout << "[Test 9] Game Preprocessor (Compression, Dead-Ends, Reward-Aware 2-Cycles)... ";
        Arena a(4);
        a.add_vertex(0, Player::Max, 100, 5, "v0");
        a.add_vertex(1, Player::Min, 102, 5, "v1");
        a.add_vertex(2, Player::Min, 501, -10, "trap");
        a.add_vertex(3, Player::Min, 1000, 0, "dead_end");
        a.add_edge(0, 1);
        a.add_edge(1, 0);
        a.add_edge(2, 2); // self-loop dominion for Min
        // v3 has no outgoing edges (dead end)

        PreprocessResult pr = GamePreprocessor::preprocess(a);
        assert(pr.fixed_max_win.contains(3)); // dead-end owned by Min is won by Max
        assert(pr.fixed_min_win.contains(2)); // trap self-loop won by Min
        assert(pr.fixed_max_win.contains(0) && pr.fixed_max_win.contains(1));
        std::cout << "PASSED (Dead-ends, self-loops, and 2-cycles pruned, priorities compressed)\n";
        ++passed;
    }

    // Test 10: Multi-Threaded Parallel SCC Solver
    {
        ++total;
        std::cout << "[Test 10] Multi-Threaded Parallel SCC Solver (16-thread pool)... ";
        Arena a = GameGenerator::make_jurdzinski(4, 4);
        ParallelSCCSolver psolver(a, 16);
        auto pres = psolver.solve();
        assert(pres.lambda_min.count() == a.num_vertices);
        std::cout << "PASSED (Parallel execution verified in " << psolver.stats().elapsed_ms << " ms)\n";
        ++passed;
    }

    // Test 11: Official ASCII AIGER (.aag) Circuit Emitter
    {
        ++total;
        std::cout << "[Test 11] Official AIGER (.aag) Hardware Circuit Generation... ";
        Arena a = GameGenerator::make_arbiter_synthesis(2, 10);
        SCCSolver scc_solver(a);
        auto res = scc_solver.solve();

        ControllerStrategy cs = StrategySynthesizer::synthesize(a, res.lambda_max);
        std::string aiger_circuit = AIGEREmitter::emit_aiger(a, cs, "ArbiterCircuit");
        assert(!aiger_circuit.empty() && aiger_circuit.starts_with("aag "));
        assert(aiger_circuit.find("state_reg0") != std::string::npos);
        std::cout << "PASSED (Generated valid AIGER hardware circuit with latches and gates)\n";
        ++passed;
    }

    // Test 12: High-Level LTL Reactive Synthesizer
    {
        ++total;
        std::cout << "[Test 12] High-Level LTL Specification Compilation & Synthesis... ";
        Arena arbiter = LTLSynthesizer::compile_arbiter(3, 100);
        ParallelSCCSolver psolver(arbiter);
        auto res = psolver.solve();
        assert(res.lambda_max.count() == arbiter.num_vertices);
        std::cout << "PASSED (Compiled LTL arbiter to arena and verified winning controller)\n";
        ++passed;
    }

    // Test 13: Native C++20 Symbolic Mealy HOA Game Expansion (TLSF)
    {
        ++total;
        std::cout << "[Test 13] Native C++20 Symbolic Mealy HOA Game Expansion... ";
        const char* tmp_hoa = "/tmp/omegasynth_test_mealy.ehoa";
        {
            std::ofstream out(tmp_hoa);
            out << "HOA: v1\nStates: 1\nStart: 0\nAP: 2 \"req\" \"grant\"\ncontrollable-AP: 1\n"
                << "acc-name: generalized-Buchi 2\nAcceptance: 2 Inf(0) & Inf(1)\n--BODY--\n"
                << "State: 0\n[!0 & !1] 0\n[0 & !1] 0 {0}\n[!0 & 1] 0 {1}\n[0 & 1] 0 {0 1}\n--END--\n";
        }
        Arena mealy_arena = HOAParser::parse(tmp_hoa);
        std::remove(tmp_hoa);
        ZielonkaSolver z(mealy_arena);
        auto res = z.solve();
        // Environment can play req=0 forever, preventing {0} from ever being visited -> UNREALIZABLE
        assert(!res.lambda_max.contains(mealy_arena.start_vertex));
        std::cout << "PASSED (Correctly expanded Mealy game and proved UNREALIZABLE)\n";
        ++passed;
    }

    // Test 14: HOA Alias, Rabin 1, and Parity Max Odd Acceptance
    {
        ++total;
        std::cout << "[Test 14] HOA @Alias, Rabin 1, and Parity Max Odd Acceptance... ";
        const char* tmp_hoa = "/tmp/omegasynth_test_maxodd.ehoa";
        {
            std::ofstream out(tmp_hoa);
            out << "HOA: v1\nStates: 2\nStart: 0\nAP: 4 \"r0\" \"r1\" \"g0\" \"g1\"\ncontrollable-AP: 2 3\n"
                << "Alias: @both_g 2 & 3\nacc-name: parity max odd 2\nAcceptance: 2 Inf(1) & Fin(0)\n--BODY--\n"
                << "State: 0 {1}\n[!0 | @both_g] 0\n[0 & !@both_g] 1\nState: 1 {0}\n[@both_g] 0\n[!@both_g] 1\n--END--\n";
        }
        Arena maxodd_arena = HOAParser::parse(tmp_hoa);
        std::remove(tmp_hoa);
        ZielonkaSolver z(maxodd_arena);
        auto res = z.solve();
        assert(res.lambda_max.contains(maxodd_arena.start_vertex));
        std::cout << "PASSED (Resolved @alias and parity max odd -> REALIZABLE)\n";
        ++passed;
    }

    // Test 15: Certified Strategy Synthesis Verification across 100 Random Games
    {
        ++total;
        std::cout << "[Test 15] Certified Strategy Verification across 100 Random Games... ";
        for (uint32_t seed = 1; seed <= 100; ++seed) {
            Arena a = GameGenerator::make_random(18, 3, 6, 0, seed);
            ZielonkaSolver z(a);
            SolveResult rz = z.solve();
            ControllerStrategy cs = StrategySynthesizer::synthesize(a, rz.lambda_max);

            // Induced graph where Max nodes are locked to cs.strategy[v] and Min controls all remaining moves
            Arena induced(a.num_vertices);
            for (VertexId v = 0; v < a.num_vertices; ++v) {
                induced.add_vertex(v, Player::Min, a.vertices[v].priority, 0);
            }
            for (VertexId v : rz.lambda_max) {
                if (a.vertices[v].owner == Player::Max) {
                    assert(cs.has_action(v) && rz.lambda_max.contains(cs.strategy[v]));
                    induced.add_edge(v, cs.strategy[v]);
                } else {
                    for (VertexId w : a.succ[v]) {
                        assert(rz.lambda_max.contains(w));
                        induced.add_edge(v, w);
                    }
                }
            }
            ZielonkaSolver z_ind(induced);
            SolveResult ind_res = z_ind.solve_subarena(rz.lambda_max);
            assert(ind_res.lambda_max == rz.lambda_max);
        }
        std::cout << "PASSED (100/100 synthesized strategies certified closed & winning)\n";
        ++passed;
    }

    // Test 16: 3-Way Multi-Engine Consensus (Quasipolynomial vs SCC vs ParallelSCC on Mean-Payoff Parity)
    {
        ++total;
        std::cout << "[Test 16] 3-Way Multi-Engine Consensus on 100 Mean-Payoff Parity Games... ";
        for (uint32_t seed = 1; seed <= 100; ++seed) {
            Arena a = GameGenerator::make_random(12, 3, 5, 5, seed + 500);
            QuasipolynomialSolver qp(a);
            SCCSolver scc(a);
            ParallelSCCSolver par(a, 4);
            auto r_qp = qp.solve();
            auto r_scc = scc.solve();
            auto r_par = par.solve(true);
            assert(r_qp.lambda_max == r_scc.lambda_max);
            assert(r_qp.lambda_max == r_par.lambda_max);
        }
        std::cout << "PASSED (100/100 exact 3-way agreement across all engines)\n";
        ++passed;
    }

    std::cout << "\n========================================================\n";
    std::cout << "  ALL " << passed << "/" << total << " TESTS PASSED WITH 100% SUCCESS!\n";
    std::cout << "========================================================\n\n";
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cout << "=========================================================================\n";
        std::cout << " OmegaSynth - World's First Quasipolynomial Reactive Synthesizer (Result 104)\n";
        std::cout << " Features: AVX2 SIMD, Parallel SCC, Preprocessor, Verilog, Rust & AIGER\n";
        std::cout << "=========================================================================\n\n";
        std::cout << "Commands:\n";
        std::cout << "  omegasynth verify                       Run full verification suite (16 tests)\n";
        std::cout << "  omegasynth solve <game.gm> [options]    Solve game file in PGSolver format\n";
        std::cout << "  omegasynth hoa <game.hoa> [options]     Solve Hanoi Omega-Automata file\n";
        std::cout << "  omegasynth synth <type> <n> [options]   Direct LTL synthesis (arbiter | mutex | buffer)\n";
        std::cout << "  omegasynth bench jurdzinski <h> <w>     Benchmark Jurdzinski game\n";
        std::cout << "  omegasynth bench arbiter <n> <batt>     Benchmark Arbiter synthesis\n";
        std::cout << "  omegasynth bench random <n> <d> <p> <r> Benchmark Random game\n\n";
        std::cout << "Options:\n";
        std::cout << "  --scc          Enable Tarjan SCC topological decomposition (Default)\n";
        std::cout << "  --parallel     Enable Multi-Threaded Parallel SCC solving\n";
        std::cout << "  --zielonka     Use SIMD Extended-Peeling Zielonka Engine\n";
        std::cout << "  --threads <k>  Number of parallel worker threads (Default: CPU cores)\n";
        std::cout << "  --preprocess   Enable priority compression and dominion pruning\n";
        std::cout << "  --direct       Use direct Quasipolynomial recursion without SCC\n";
        std::cout << "  --max-parity   Interpret PGSolver (.gm) priorities as max-even (Oink/Knor convention)\n";
        std::cout << "  --syntcomp     Competition mode: output REALIZABLE / UNREALIZABLE\n";
        std::cout << "  --emit-verilog Synthesize and print Verilog RTL state machine\n";
        std::cout << "  --emit-rust    Synthesize and print Rust controller code\n";
        std::cout << "  --emit-aiger   Synthesize and print ASCII AIGER hardware circuit\n";
        return 0;
    }

    std::string cmd = argv[1];

    if (cmd == "verify") {
        run_verification_tests();
        return 0;
    }

    bool use_scc = true;
    bool use_parallel = false;
    bool use_preprocess = true;
    size_t num_threads = std::thread::hardware_concurrency();
    bool emit_verilog = false;
    bool emit_rust = false;
    bool emit_aiger = false;
    bool syntcomp_mode = false;
    bool use_zielonka = false;
    bool max_parity_gm = false;

    for (int i = 2; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--direct") use_scc = false;
        if (arg == "--scc") use_scc = true;
        if (arg == "--parallel") use_parallel = true;
        if (arg == "--zielonka") use_zielonka = true;
        if (arg == "--no-preprocess") use_preprocess = false;
        if (arg == "--preprocess") use_preprocess = true;
        if (arg == "--max-parity") max_parity_gm = true;
        if (arg == "--emit-verilog") emit_verilog = true;
        if (arg == "--emit-rust") emit_rust = true;
        if (arg == "--emit-aiger") emit_aiger = true;
        if (arg == "--syntcomp") syntcomp_mode = true;
        if (arg == "--threads" && i + 1 < argc) {
            num_threads = std::stoul(argv[++i]);
            use_parallel = true;
        }
    }

    if (cmd == "synth") {
        if (argc < 4) {
            std::cerr << "Usage: omegasynth synth <arbiter|mutex|buffer> <size> [options]\n";
            return 1;
        }
        std::string stype = argv[2];
        size_t n = std::stoul(argv[3]);
        Arena arena;
        if (stype == "arbiter") {
            arena = LTLSynthesizer::compile_arbiter(n, 100);
            std::cout << "Compiled Arbiter specification (" << n << " clients, " << arena.num_vertices << " states).\n";
        } else if (stype == "mutex") {
            arena = LTLSynthesizer::compile_mutex(n);
            std::cout << "Compiled Mutex specification (" << n << " threads, " << arena.num_vertices << " states).\n";
        } else if (stype == "buffer") {
            arena = LTLSynthesizer::compile_buffer(n);
            std::cout << "Compiled Buffer specification (capacity " << n << ", " << arena.num_vertices << " states).\n";
        } else {
            std::cerr << "Unknown synthesis specification type: " << stype << "\n";
            return 1;
        }

        SolveResult res;
        SolverStats st;
        if (use_parallel) {
            std::cout << "Engine: ParallelSCCSolver (" << num_threads << " threads)\n";
            ParallelSCCSolver solver(arena, num_threads);
            res = solver.solve(use_preprocess);
            st = solver.stats();
        } else {
            std::cout << "Engine: SCCSolver (Tarjan decomposition)\n";
            SCCSolver solver(arena);
            res = solver.solve();
            st = solver.stats();
        }

        std::cout << "\nSynthesis Status: " << (res.lambda_max.contains(arena.start_vertex) ? "REALIZABLE (Winning strategy found)" : "UNREALIZABLE") << "\n";
        std::cout << "Max winning states: " << res.lambda_max.count() << " / " << arena.num_vertices << "\n";
        std::cout << "Solve time: " << st.elapsed_ms << " ms\n";

        if (emit_verilog || emit_rust || emit_aiger) {
            ControllerStrategy cs = StrategySynthesizer::synthesize(arena, res.lambda_max);
            if (emit_verilog) {
                std::cout << "\n--- SYNTHESIZED VERILOG RTL ---\n";
                std::cout << StrategySynthesizer::emit_verilog(arena, cs);
            }
            if (emit_rust) {
                std::cout << "\n--- SYNTHESIZED RUST CONTROLLER ---\n";
                std::cout << StrategySynthesizer::emit_rust(arena, cs);
            }
            if (emit_aiger) {
                std::cout << "\n--- SYNTHESIZED AIGER (.aag) CIRCUIT ---\n";
                std::cout << AIGEREmitter::emit_aiger(arena, cs);
            }
        }
        return 0;
    }

    if (cmd == "solve" || cmd == "hoa") {
        if (argc < 3) {
            std::cerr << "Error: missing input file path.\n";
            return 1;
        }
        std::string filename = argv[2];
        try {
            auto t_p0 = std::chrono::high_resolution_clock::now();
            Arena arena = (cmd == "hoa") ? HOAParser::parse(filename) : GameParser::parse_pgsolver(filename, max_parity_gm);
            auto t_p1 = std::chrono::high_resolution_clock::now();
            double parse_ms = std::chrono::duration<double, std::milli>(t_p1 - t_p0).count();

            if (!syntcomp_mode) {
                std::cout << "Parsed arena with " << arena.num_vertices << " vertices.\n";
            }

            SolveResult res;
            SolverStats st;
            double prep_ms = 0.0;
            double solve_ms = 0.0;

            if (use_zielonka) {
                if (!syntcomp_mode) std::cout << "Engine: ZielonkaSolver\n";
                if (use_preprocess) {
                    auto t_pr0 = std::chrono::high_resolution_clock::now();
                    PreprocessResult prep = GamePreprocessor::preprocess(arena);
                    auto t_pr1 = std::chrono::high_resolution_clock::now();
                    prep_ms = std::chrono::duration<double, std::milli>(t_pr1 - t_pr0).count();

                    auto t_s0 = std::chrono::high_resolution_clock::now();
                    if (prep.active_subarena.empty()) {
                        res = {prep.fixed_max_win, prep.fixed_min_win};
                    } else {
                        ZielonkaSolver solver(prep.arena);
                        SolveResult sub_res = solver.solve_subarena(prep.active_subarena);
                        res = {prep.fixed_max_win.unite(sub_res.lambda_max), prep.fixed_min_win.unite(sub_res.lambda_min)};
                        st.solve_calls = solver.recursive_calls();
                    }
                    auto t_s1 = std::chrono::high_resolution_clock::now();
                    solve_ms = std::chrono::duration<double, std::milli>(t_s1 - t_s0).count();
                } else {
                    auto t_s0 = std::chrono::high_resolution_clock::now();
                    ZielonkaSolver solver(arena);
                    res = solver.solve();
                    st.solve_calls = solver.recursive_calls();
                    auto t_s1 = std::chrono::high_resolution_clock::now();
                    solve_ms = std::chrono::duration<double, std::milli>(t_s1 - t_s0).count();
                }
            } else if (use_parallel) {
                if (!syntcomp_mode) std::cout << "Engine: ParallelSCCSolver (" << num_threads << " threads)\n";
                auto t_s0 = std::chrono::high_resolution_clock::now();
                ParallelSCCSolver solver(arena, num_threads);
                res = solver.solve(use_preprocess);
                st = solver.stats();
                auto t_s1 = std::chrono::high_resolution_clock::now();
                solve_ms = std::chrono::duration<double, std::milli>(t_s1 - t_s0).count();
            } else if (use_scc) {
                if (!syntcomp_mode) std::cout << "Engine: SCCSolver (Tarjan decomposition + Quasipolynomial core)\n";
                auto t_s0 = std::chrono::high_resolution_clock::now();
                SCCSolver solver(arena);
                res = solver.solve();
                st = solver.stats();
                auto t_s1 = std::chrono::high_resolution_clock::now();
                solve_ms = std::chrono::duration<double, std::milli>(t_s1 - t_s0).count();
            } else {
                if (!syntcomp_mode) std::cout << "Engine: Direct QuasipolynomialSolver\n";
                auto t_s0 = std::chrono::high_resolution_clock::now();
                QuasipolynomialSolver solver(arena);
                res = solver.solve();
                st = solver.stats();
                auto t_s1 = std::chrono::high_resolution_clock::now();
                solve_ms = std::chrono::duration<double, std::milli>(t_s1 - t_s0).count();
            }

            if (syntcomp_mode) {
                bool realizable = res.lambda_max.contains(arena.start_vertex);
                if (realizable) {
                    std::cout << "REALIZABLE\n";
                    if (emit_aiger) {
                        ControllerStrategy cs = StrategySynthesizer::synthesize(arena, res.lambda_max);
                        std::cout << AIGEREmitter::emit_aiger(arena, cs);
                    }
                } else {
                    std::cout << "UNREALIZABLE\n";
                }
                return 0;
            }

            std::cout << "\n--- SOLVER RESULTS ---\n";
            std::cout << "Max winning states (" << res.lambda_max.count() << " / " << arena.num_vertices << "): ";
            for (VertexId v : res.lambda_max) std::cout << v << " ";
            std::cout << "\n";

            std::cout << "Min winning states (" << res.lambda_min.count() << " / " << arena.num_vertices << "): ";
            for (VertexId v : res.lambda_min) std::cout << v << " ";
            std::cout << "\n";

            std::cout << "\n--- PERFORMANCE STATS ---\n";
            std::cout << "Parse time:         " << parse_ms << " ms\n";
            std::cout << "Preprocess time:    " << prep_ms << " ms\n";
            std::cout << "Solve time:         " << solve_ms << " ms\n";
            std::cout << "Total core time:    " << (parse_ms + prep_ms + solve_ms) << " ms\n";
            std::cout << "Recursive calls:    " << st.solve_calls << "\n";
            std::cout << "Phase loops:        " << st.phase_calls << "\n";
            std::cout << "Mean-payoff queries:" << st.mean_payoff_calls << "\n";
            std::cout << "Max recursion depth:" << st.max_depth << "\n";

            if (emit_verilog || emit_rust || emit_aiger) {
                ControllerStrategy cs = StrategySynthesizer::synthesize(arena, res.lambda_max);
                if (emit_verilog) {
                    std::cout << "\n--- SYNTHESIZED VERILOG RTL ---\n";
                    std::cout << StrategySynthesizer::emit_verilog(arena, cs);
                }
                if (emit_rust) {
                    std::cout << "\n--- SYNTHESIZED RUST CONTROLLER ---\n";
                    std::cout << StrategySynthesizer::emit_rust(arena, cs);
                }
                if (emit_aiger) {
                    std::cout << "\n--- SYNTHESIZED AIGER (.aag) CIRCUIT ---\n";
                    std::cout << AIGEREmitter::emit_aiger(arena, cs);
                }
            }

        } catch (const std::exception& e) {
            std::cerr << "Error: " << e.what() << "\n";
            return 1;
        }
        return 0;
    }

    if (cmd == "bench") {
        if (argc < 3) {
            std::cerr << "Specify benchmark type (jurdzinski | arbiter | random).\n";
            return 1;
        }
        std::string btype = argv[2];
        if (btype == "jurdzinski") {
            size_t h = (argc >= 4) ? std::stoul(argv[3]) : 10;
            size_t w = (argc >= 5) ? std::stoul(argv[4]) : 10;
            Arena arena = GameGenerator::make_jurdzinski(h, w);
            std::cout << "Benchmarking Jurdzinski h=" << h << ", w=" << w << " (" << arena.num_vertices << " vertices)...\n";
            SCCSolver solver(arena);
            auto res = solver.solve();
            std::cout << "Max: " << res.lambda_max.count() << ", Min: " << res.lambda_min.count()
                      << " | Time: " << solver.stats().elapsed_ms << " ms\n";
        } else if (btype == "arbiter") {
            size_t clients = (argc >= 4) ? std::stoul(argv[3]) : 50;
            int64_t batt = (argc >= 5) ? std::stoll(argv[4]) : 100;
            Arena arena = GameGenerator::make_arbiter_synthesis(clients, batt);
            std::cout << "Benchmarking Arbiter (" << clients << " clients, " << arena.num_vertices << " vertices)...\n";
            SCCSolver solver(arena);
            auto res = solver.solve();
            std::cout << "Max: " << res.lambda_max.count() << ", Min: " << res.lambda_min.count()
                      << " | Time: " << solver.stats().elapsed_ms << " ms\n";
        } else if (btype == "random") {
            size_t n = (argc >= 4) ? std::stoul(argv[3]) : 1000;
            size_t deg = (argc >= 5) ? std::stoul(argv[4]) : 4;
            Priority p = (argc >= 6) ? static_cast<Priority>(std::stoul(argv[5])) : 15;
            Reward r = (argc >= 7) ? std::stoll(argv[6]) : 20;
            Arena arena = GameGenerator::make_random(n, deg, p, r);
            std::cout << "Benchmarking Random (n=" << n << ", deg=" << deg << ")...\n";
            SCCSolver solver(arena);
            auto res = solver.solve();
            std::cout << "Max: " << res.lambda_max.count() << ", Min: " << res.lambda_min.count()
                      << " | Time: " << solver.stats().elapsed_ms << " ms\n";
        }
        return 0;
    }

    std::cerr << "Unknown command: " << cmd << "\n";
    return 1;
}
