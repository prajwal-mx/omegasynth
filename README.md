# OmegaSynth

A deterministic quasipolynomial **mean-payoff parity game solver** and **reactive synthesis toolchain** in modern C++20 with zero external dependencies. Solves parity games combining a qualitative parity condition with a quantitative mean-payoff condition, and compiles high-level specifications directly into provably correct hardware controllers and safety-critical software.

## Features

- **Quasipolynomial recursive engine** (`src/solver.hpp`) with SCC decomposition, priority compression, and dominion pruning
- **AVX2 256-bit SIMD bitsets** (`src/arena.hpp`) and a zero-allocation attractor workspace for high-throughput solving
- **Wave-based parallel SCC solver** (`src/parallel_solver.hpp`) scaling across all CPU cores
- **Exact mean-payoff support** via energy value iteration and alternating Hoffman–Karp policy iteration (`src/mean_payoff.hpp`, `src/policy_iteration.hpp`)
- **Native symbolic Mealy game expansion** for HOA automata with Boolean guard compilation, `@alias` support, and Büchi / Rabin-1 / generalized-Büchi / max-odd acceptance (`src/hoa_parser.hpp`)
- **Triple code emitters**: ASCII AIGER (`.aag`), synthesizable Verilog RTL, and memory-safe Rust state machines (`src/aiger.hpp`, `src/strategy.hpp`)
- **Native LTL specification compiler** — arbiter, mutex, bounded buffer — straight into solvable arenas, no external tools (`src/ltl_synth.hpp`)
- **SYNTCOMP-compatible runner** (`syntcomp_run.sh`, `omegasynth_syntcomp`) emitting `REALIZABLE` / `UNREALIZABLE` verdicts

## Layout

```
├── src/                  # C++20 engine (headers + main.cpp)
├── benchmarks/           # Differential fuzzers + independent Python Zielonka reference
├── examples/             # Sample PGSolver (.gm) and HOA (.hoa/.ehoa) games
├── tests/                # Sample game for smoke testing
├── Dockerfile            # Reproducible container build
├── Makefile              # Build (g++ >= 11 or clang++ >= 13, make)
└── syntcomp_run.sh       # Competition runner (add --aiger for the synthesis track)
```

## Building

Prerequisites: a C++20 compiler (`g++ >= 11` or `clang++ >= 13`) and `make`.

```bash
make clean && make
```

Or with Docker:

```bash
docker build -t omegasynth . && docker run --rm omegasynth
```

## Quickstart

```bash
# Run the full verification suite (16 tests)
./omegasynth verify

# Solve a PGSolver game
./omegasynth solve examples/arbiter_simple.gm --parallel --threads 16 --preprocess

# Solve an HOA game, SYNTCOMP verdict mode
./omegasynth hoa examples/even_parity.hoa --zielonka --preprocess --syntcomp

# Direct LTL synthesis (arbiter / mutex / buffer) with code emission
./omegasynth synth arbiter 4 --emit-aiger --emit-verilog --emit-rust
./omegasynth synth mutex 5 --emit-aiger
./omegasynth synth buffer 8 --emit-verilog
```

Full CLI reference:

```
Commands:
  omegasynth verify                       Run full verification suite (16 tests)
  omegasynth solve <game.gm> [options]    Solve game file in PGSolver format
  omegasynth hoa <game.hoa> [options]     Solve Hanoi Omega-Automata file
  omegasynth synth <type> <n> [options]   Direct LTL synthesis (arbiter | mutex | buffer)
  omegasynth bench jurdzinski <h> <w>     Benchmark Jurdzinski game
  omegasynth bench arbiter <n> <batt>     Benchmark Arbiter synthesis
  omegasynth bench random <n> <d> <p> <r> Benchmark Random game

Options:
  --scc          Enable Tarjan SCC topological decomposition (Default)
  --parallel     Enable Multi-Threaded Parallel SCC solving
  --zielonka     Use SIMD Extended-Peeling Zielonka Engine
  --threads <k>  Number of parallel worker threads (Default: CPU cores)
  --preprocess   Enable priority compression and dominion pruning
  --direct       Use direct Quasipolynomial recursion without SCC
  --max-parity   Interpret PGSolver (.gm) priorities as max-even (Oink/Knor convention)
  --syntcomp     Competition mode: output REALIZABLE / UNREALIZABLE
  --emit-verilog Synthesize and print Verilog RTL state machine
  --emit-rust    Synthesize and print Rust controller code
  --emit-aiger   Synthesize and print ASCII AIGER hardware circuit
```

## Verification

Every release is checked against an independent textbook-Zielonka Python reference (`benchmarks/zielonka_reference.py`) plus differential fuzzers:

```bash
./omegasynth verify   # 16/16 suites: cycles, mean-payoff conflicts, Jurdzinski,
                      # policy iteration at W=1e9, strategy certification,
                      # Mealy expansion, multi-engine consensus, and more
```

## License

Apache License 2.0 — see [LICENSE](LICENSE).
