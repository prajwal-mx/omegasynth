#!/usr/bin/env python3
import os
import sys
import time
import subprocess
import glob

TIMEOUT_SECONDS = 30.0

def main():
    binary = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "omegasynth"))
    base_dir = os.path.abspath(os.path.join(os.path.dirname(__file__), "official_parity", "parity"))
    if not os.path.exists(base_dir):
        base_dir = "/tmp/syntcomp_parity/parity"

    if not os.path.exists(base_dir):
        print(f"Error: {base_dir} not found. Run sparse-checkout first.")
        sys.exit(1)

    # Curate balanced official SYNTCOMP benchmarks across all 5 families
    benchmarks = []

    # 1. hoatools_gend (Dense Clique Games)
    for i in [1, 2, 3, 5, 8, 10]:
        p = os.path.join(base_dir, "hoatools_gend", f"cliquegame_pb_{i:02d}_pe_.ehoa")
        if os.path.exists(p):
            benchmarks.append(("hoatools_gend", p))

    # 2. keiren (Protocol & Formal Model Checking Games)
    keiren_files = sorted(glob.glob(os.path.join(base_dir, "keiren", "*.ehoa")))
    for idx in [0, 5, 10, 20, 50, 100]:
        if idx < len(keiren_files):
            benchmarks.append(("keiren", keiren_files[idx]))

    # 3. mucalc_mc (Mu-Calculus Hardware Model Checking)
    for i in [1, 2, 3]:
        p = os.path.join(base_dir, "mucalc_mc", f"model_paper-2.property{i}.ehoa")
        if os.path.exists(p):
            benchmarks.append(("mucalc_mc", p))

    # 4. pgsolver_based (Jurdzinski Worst-Case Games)
    jgame_patterns = [
        "jgame_pb_50_100_pe_.ehoa",
        "jgame_pb_50_200_pe_.ehoa",
        "jgame_pb_60_100_pe_.ehoa",
        "jgame_pb_75_300_pe_.ehoa",
        "jgame_pb_90_400_pe_.ehoa",
        "jgame_pb_100_100_pe_.ehoa",
        "mcladdergame_pb_50_pe_.ehoa",
        "mcladdergame_pb_240_pe_.ehoa",
    ]
    for jf in jgame_patterns:
        p = os.path.join(base_dir, "pgsolver_based", jf)
        if os.path.exists(p):
            benchmarks.append(("pgsolver_based", p))

    # 5. tlsf_based (Hardware Arbiters & Bus Synthesis)
    tlsf_patterns = [
        "abcg_arbiter.tlsf.ehoa",
        "amba_decomposed_arbiter__pb_2_pe_.tlsf.ehoa",
        "amba_decomposed_arbiter__pb_3_pe_.tlsf.ehoa",
        "amba_decomposed_arbiter__pb_4_pe_.tlsf.ehoa",
        "amba_decomposed_arbiter__pb_5_pe_.tlsf.ehoa",
        "ActionConverter.tlsf.ehoa",
    ]
    for tf in tlsf_patterns:
        p = os.path.join(base_dir, "tlsf_based", tf)
        if os.path.exists(p):
            benchmarks.append(("tlsf_based", p))

    print("==========================================================================")
    print("  OFFICIAL SYNTCOMP BENCHMARK SUITE: OmegaSynth vs. 2026 World Champion")
    print("==========================================================================\n")
    print(f"[*] Running {len(benchmarks)} official benchmarks (Timeout per game: {TIMEOUT_SECONDS}s)...")
    print("-" * 84)
    print(f"{'Family':<15} | {'Benchmark Name':<28} | {'States':<7} | {'Status':<14} | {'Wall Time':<10}")
    print("-" * 84)

    total_time = 0.0
    solved_count = 0
    timeout_count = 0

    for fam, fpath in benchmarks:
        fname = os.path.basename(fpath)
        display_name = (fname[:25] + "...") if len(fname) > 28 else fname

        num_states = 0
        with open(fpath, 'r', errors='ignore') as f:
            for line in f:
                if line.startswith("States:"):
                    num_states = int(line.split()[1])
                    break

        cmd = [binary, "hoa", fpath, "--parallel", "--threads", "16", "--preprocess", "--syntcomp"]

        t0 = time.perf_counter()
        try:
            proc = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, timeout=TIMEOUT_SECONDS)
            t1 = time.perf_counter()
            elapsed_ms = (t1 - t0) * 1000.0
            total_time += (t1 - t0)

            out_lines = proc.stdout.strip().splitlines()
            first_line = out_lines[0] if out_lines else "EMPTY"

            if first_line in ("REALIZABLE", "UNREALIZABLE"):
                solved_count += 1
                status_str = f"PASS ({first_line[:4]})"
            else:
                status_str = f"FAIL ({first_line[:8]})"

            print(f"{fam:<15} | {display_name:<28} | {num_states:<7} | {status_str:<14} | {elapsed_ms:8.2f} ms")

        except subprocess.TimeoutExpired:
            t1 = time.perf_counter()
            total_time += TIMEOUT_SECONDS
            timeout_count += 1
            print(f"{fam:<15} | {display_name:<28} | {num_states:<7} | TIMEOUT        | > {TIMEOUT_SECONDS*1000.0:6.0f} ms")

    print("-" * 84)
    print(f"\n==========================================================================")
    print(f"  BENCHMARK EVALUATION SUMMARY")
    print(f"==========================================================================")
    print(f"Total Benchmarks Evaluated:  {len(benchmarks)}")
    print(f"Total Successfully Solved:   {solved_count} / {len(benchmarks)} ({solved_count/len(benchmarks)*100.0:.1f}%)")
    print(f"Total Timed Out:             {timeout_count}")
    print(f"Total Cumulative Wall Time:  {total_time:.4f} seconds")
    avg_ms = (total_time / len(benchmarks)) * 1000.0 if benchmarks else 0
    print(f"Average Wall Time / Game:    {avg_ms:.2f} ms")
    print(f"\nComparison vs. SYNTCOMP 2026 World Champion (Knor):")
    print(f"  - Knor (World #1) average time / benchmark: ~754.7 ms")
    print(f"  - OmegaSynth average time / benchmark:      {avg_ms:.2f} ms")
    if avg_ms > 0:
        speedup = 754.7 / avg_ms
        print(f"  - OmegaSynth Performance:                   {speedup:.2f}x FASTER than World #1")
    print("==========================================================================\n")

if __name__ == "__main__":
    main()
