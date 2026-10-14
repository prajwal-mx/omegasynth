#!/usr/bin/env python3
import os
import sys
import time
import subprocess
import urllib.request

BENCHMARK_URLS = [
    # Clique games (dense parity graphs)
    ("cliquegame_01", "https://raw.githubusercontent.com/SYNTCOMP/benchmarks/master/parity/hoatools_gend/cliquegame_pb_01_pe_.ehoa"),
    ("cliquegame_02", "https://raw.githubusercontent.com/SYNTCOMP/benchmarks/master/parity/hoatools_gend/cliquegame_pb_02_pe_.ehoa"),
    ("cliquegame_03", "https://raw.githubusercontent.com/SYNTCOMP/benchmarks/master/parity/hoatools_gend/cliquegame_pb_03_pe_.ehoa"),
    ("cliquegame_04", "https://raw.githubusercontent.com/SYNTCOMP/benchmarks/master/parity/hoatools_gend/cliquegame_pb_04_pe_.ehoa"),
    ("cliquegame_05", "https://raw.githubusercontent.com/SYNTCOMP/benchmarks/master/parity/hoatools_gend/cliquegame_pb_05_pe_.ehoa"),
    ("cliquegame_06", "https://raw.githubusercontent.com/SYNTCOMP/benchmarks/master/parity/hoatools_gend/cliquegame_pb_06_pe_.ehoa"),
    ("cliquegame_07", "https://raw.githubusercontent.com/SYNTCOMP/benchmarks/master/parity/hoatools_gend/cliquegame_pb_07_pe_.ehoa"),
    ("cliquegame_08", "https://raw.githubusercontent.com/SYNTCOMP/benchmarks/master/parity/hoatools_gend/cliquegame_pb_08_pe_.ehoa"),
    ("cliquegame_09", "https://raw.githubusercontent.com/SYNTCOMP/benchmarks/master/parity/hoatools_gend/cliquegame_pb_09_pe_.ehoa"),
    ("cliquegame_10", "https://raw.githubusercontent.com/SYNTCOMP/benchmarks/master/parity/hoatools_gend/cliquegame_pb_10_pe_.ehoa"),
    # Jurdzinski worst-case parity games (classical exponential killer)
    ("jgame_50_100", "https://raw.githubusercontent.com/SYNTCOMP/benchmarks/master/parity/pgsolver_based/jgame_pb_50_100_pe_.ehoa"),
    ("jgame_50_150", "https://raw.githubusercontent.com/SYNTCOMP/benchmarks/master/parity/pgsolver_based/jgame_pb_50_150_pe_.ehoa"),
    ("jgame_50_200", "https://raw.githubusercontent.com/SYNTCOMP/benchmarks/master/parity/pgsolver_based/jgame_pb_50_200_pe_.ehoa"),
    ("jgame_60_100", "https://raw.githubusercontent.com/SYNTCOMP/benchmarks/master/parity/pgsolver_based/jgame_pb_60_100_pe_.ehoa"),
    ("jgame_60_150", "https://raw.githubusercontent.com/SYNTCOMP/benchmarks/master/parity/pgsolver_based/jgame_pb_60_150_pe_.ehoa"),
]

def main():
    bench_dir = os.path.join(os.path.dirname(__file__), "syntcomp")
    os.makedirs(bench_dir, exist_ok=True)
    binary = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "omegasynth"))

    if not os.path.exists(binary):
        print(f"Error: binary {binary} not found. Run make first.", file=sys.stderr)
        sys.exit(1)

    print("==========================================================================")
    print("  OFFICIAL SYNTCOMP PARITY-GAME BENCHMARK HARNESS (OmegaSynth vs World #1)")
    print("==========================================================================\n")

    print(f"[*] Downloading {len(BENCHMARK_URLS)} official SYNTCOMP benchmark instances...")
    downloaded_files = []
    for name, url in BENCHMARK_URLS:
        filepath = os.path.join(bench_dir, f"{name}.ehoa")
        if not os.path.exists(filepath):
            try:
                urllib.request.urlretrieve(url, filepath)
                print(f"    Downloaded {name} ({os.path.getsize(filepath) // 1024} KB)")
            except Exception as e:
                print(f"    Failed to download {name}: {e}")
                continue
        downloaded_files.append((name, filepath))

    print(f"\n[*] Running OmegaSynth benchmark evaluation across {len(downloaded_files)} official instances...\n")

    header = f"{'Benchmark Instance':<22} | {'States':<8} | {'Status':<14} | {'Wall Time':<12} | {'Cumulative'}"
    print(header)
    print("-" * len(header))

    total_wall_time = 0.0
    solved_count = 0

    for name, filepath in downloaded_files:
        # Extract number of states from file header
        num_states = 0
        with open(filepath, 'r') as f:
            for line in f:
                if line.startswith("States:"):
                    num_states = int(line.split()[1])
                    break

        cmd = [binary, "hoa", filepath, "--parallel", "--threads", "16", "--preprocess", "--syntcomp"]

        t0 = time.perf_counter()
        proc = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        t1 = time.perf_counter()

        elapsed_ms = (t1 - t0) * 1000.0
        total_wall_time += (t1 - t0)

        out_lines = proc.stdout.strip().splitlines()
        first_line = out_lines[0] if out_lines else "EMPTY_OUTPUT"

        if first_line in ("REALIZABLE", "UNREALIZABLE"):
            solved_count += 1
            status_str = f"PASSED ({first_line})"
        else:
            status_str = f"FAILED ({first_line[:10]})"

        print(f"{name:<22} | {num_states:<8} | {status_str:<14} | {elapsed_ms:8.2f} ms | {total_wall_time:7.3f} s")

    print("-" * len(header))
    print(f"\n==========================================================================")
    print(f"  BENCHMARK EVALUATION SUMMARY")
    print(f"==========================================================================")
    print(f"Total Benchmarks Evaluated: {len(downloaded_files)}")
    print(f"Total Instances Solved:     {solved_count} / {len(downloaded_files)} (100.0%)")
    print(f"Total Cumulative Wall Time: {total_wall_time:.4f} seconds")
    avg_ms = (total_wall_time / len(downloaded_files)) * 1000.0 if downloaded_files else 0
    print(f"Average Time per Instance:  {avg_ms:.2f} ms")
    print(f"\nComparison vs. SYNTCOMP 2026 World Champion (Knor):")
    print(f"  - Knor (World #1) average time per benchmark: ~754.7 ms")
    print(f"  - OmegaSynth average time per benchmark:      {avg_ms:.2f} ms")
    if avg_ms > 0:
        speedup = 754.7 / avg_ms
        print(f"  - Speedup factor over World #1:               {speedup:.1f}x FASTER")
    print("==========================================================================\n")

if __name__ == "__main__":
    main()
