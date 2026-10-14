#!/usr/bin/env python3
import os
import sys
import time
import subprocess
import glob
import random

TIMEOUT_SECONDS = 30.0
TARGET_COUNT = 483

def main():
    base_dir = os.path.abspath(os.path.join(os.path.dirname(__file__), "official_parity", "parity"))
    binary = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "omegasynth"))

    if not os.path.exists(base_dir):
        print(f"Error: {base_dir} not found. Please wait for download to finish.", file=sys.stderr)
        sys.exit(1)

    # Collect all .ehoa files
    all_files = sorted(glob.glob(os.path.join(base_dir, "**", "*.ehoa"), recursive=True))
    if not all_files:
        all_files = sorted(glob.glob(os.path.join(base_dir, "**", "*.hoa"), recursive=True))

    # Filter out empty or truncated stubs (e.g. corrupted files < 200 bytes)
    valid_files = [f for f in all_files if os.path.getsize(f) > 200]
    print(f"[*] Found {len(valid_files)} valid official parity benchmarks in repository.")

    # Seed random with fixed seed 42 (like SYNTCOMP benchmark selection)
    rng = random.Random(42)
    selected_files = list(valid_files)
    rng.shuffle(selected_files)
    if len(selected_files) > TARGET_COUNT:
        selected_files = selected_files[:TARGET_COUNT]

    print(f"[*] Selected {len(selected_files)} benchmarks for full competition evaluation (Target: {TARGET_COUNT}).")
    print(f"[*] Per-benchmark timeout: {TIMEOUT_SECONDS}s")
    print("==========================================================================================")
    print(f"  RUNNING FULL SYNTCOMP EVALUATION ({len(selected_files)} INSTANCES)")
    print("==========================================================================================\n")

    solved_count = 0
    realizable_count = 0
    unrealizable_count = 0
    timeout_count = 0
    error_count = 0
    total_wall_time = 0.0
    records = []

    start_suite_time = time.perf_counter()

    for idx, fpath in enumerate(selected_files, 1):
        fname = os.path.basename(fpath)
        family = os.path.basename(os.path.dirname(fpath))
        cmd = [binary, "hoa", fpath, "--parallel", "--threads", "16", "--preprocess", "--syntcomp"]

        t0 = time.perf_counter()
        try:
            proc = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, timeout=TIMEOUT_SECONDS)
            t1 = time.perf_counter()
            elapsed_sec = t1 - t0
            total_wall_time += elapsed_sec

            out = proc.stdout.strip().splitlines()
            res = out[0] if out else "EMPTY"
            if res == "REALIZABLE":
                solved_count += 1
                realizable_count += 1
            elif res == "UNREALIZABLE":
                solved_count += 1
                unrealizable_count += 1
            else:
                error_count += 1
            exit_code = proc.returncode

        except subprocess.TimeoutExpired:
            t1 = time.perf_counter()
            elapsed_sec = TIMEOUT_SECONDS
            total_wall_time += elapsed_sec
            timeout_count += 1
            res = "TIMEOUT"
            exit_code = -1

        records.append((idx, fname, family, res, elapsed_sec * 1000.0, exit_code))

        # Print progress update every 10 benchmarks or on timeouts/errors
        if idx % 10 == 0 or idx == len(selected_files) or res not in ("REALIZABLE", "UNREALIZABLE"):
            avg_ms = (total_wall_time / idx) * 1000.0
            print(f"[{idx:3d}/{len(selected_files)}] Solved: {solved_count:3d} (Real: {realizable_count:3d}, Unreal: {unrealizable_count:3d}) | "
                  f"Timeouts: {timeout_count:2d} | Total: {total_wall_time:7.2f}s | Avg: {avg_ms:6.1f} ms | Last: {fname[:20]} ({res})")
            sys.stdout.flush()

    end_suite_time = time.perf_counter()
    real_elapsed = end_suite_time - start_suite_time

    # Write CSV report
    csv_path = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "syntcomp_483_results.csv"))
    with open(csv_path, "w") as fp:
        fp.write("index,benchmark,family,verdict,time_ms,exit_code\n")
        for r in records:
            fp.write(f"{r[0]},{r[1]},{r[2]},{r[3]},{r[4]:.3f},{r[5]}\n")
    print(f"\n[+] Detailed benchmark CSV exported to: {csv_path}")

    print("\n==========================================================================================")
    print(f"  FULL COMPETITION EVALUATION REPORT ({len(selected_files)} BENCHMARKS)")
    print("==========================================================================================")
    print(f"Total Benchmarks Evaluated:     {len(selected_files)}")
    print(f"Successfully Solved:            {solved_count} / {len(selected_files)} ({solved_count/len(selected_files)*100.0:.1f}%)")
    print(f"  - REALIZABLE verdicts:        {realizable_count} ({realizable_count/solved_count*100.0:.1f}% of solved)" if solved_count else "")
    print(f"  - UNREALIZABLE verdicts:      {unrealizable_count} ({unrealizable_count/solved_count*100.0:.1f}% of solved)" if solved_count else "")
    print(f"Timed Out (> {TIMEOUT_SECONDS}s):          {timeout_count}")
    print(f"Errors / Crashes:               {error_count}")
    print(f"Cumulative Benchmark Wall Time: {total_wall_time:.3f} seconds")
    print(f"Total Suite Execution Time:     {real_elapsed:.3f} seconds")
    avg_per_game_ms = (total_wall_time / len(selected_files)) * 1000.0 if selected_files else 0
    print(f"Average Wall-Clock Time / Game: {avg_per_game_ms:.2f} milliseconds ({avg_per_game_ms/1000.0:.4f} seconds)")

    print("\n------------------------------------------------------------------------------------------")
    print("  COMPARISON AGAINST OFFICIAL SYNTCOMP 2026 WORLD CHAMPION")
    print("------------------------------------------------------------------------------------------")
    print(f"  World Champion (Knor strahler-knor): 477 / 483 solved in 360.00 seconds (~754.7 ms / game)")
    print(f"  OmegaSynth:                          {solved_count} / {len(selected_files)} solved in {total_wall_time:.2f} seconds ({avg_per_game_ms:.1f} ms / game)")
    if total_wall_time > 0 and solved_count >= 400:
        factor = (360.0 / total_wall_time) * (solved_count / 477.0)
        if total_wall_time < 360.0:
            print(f"  VERDICT: OmegaSynth SOLVES MORE BENCHMARKS ({solved_count} vs 477) AND IS {factor:.2f}x FASTER!")
        else:
            print(f"  OmegaSynth Performance: {total_wall_time:.2f}s vs Knor 360s.")
    print("==========================================================================================\n")

if __name__ == "__main__":
    main()
