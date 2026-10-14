#!/usr/bin/env python3
"""
Differential Verification: OmegaSynth vs Official World Champion Knor (strahler-knor)
Executes both engines on official SYNTCOMP benchmark instances and diffs the verdicts.
"""

import os
import sys
import glob
import random
import subprocess
import time

def run_omegasynth(filepath):
    cmd = ["./omegasynth", "hoa", filepath, "--zielonka", "--preprocess", "--syntcomp"]
    t0 = time.perf_counter()
    p = subprocess.run(cmd, capture_output=True, text=True, timeout=15)
    t1 = time.perf_counter()
    out = p.stdout.strip().splitlines()
    res = out[0] if out else "EMPTY"
    return res, (t1 - t0) * 1000.0

def run_knor(filepath):
    cmd = ["./knor", "--real", "--strpm-simd", filepath]
    t0 = time.perf_counter()
    p = subprocess.run(cmd, capture_output=True, text=True, timeout=15)
    t1 = time.perf_counter()
    out = p.stdout.strip().splitlines()
    res = "UNKNOWN"
    for line in out:
        if "UNREALIZABLE" in line:
            res = "UNREALIZABLE"
            break
        elif "REALIZABLE" in line:
            res = "REALIZABLE"
            break
    if res == "UNKNOWN":
        if p.returncode == 10:
            res = "REALIZABLE"
        elif p.returncode == 20:
            res = "UNREALIZABLE"
        else:
            res = f"ERR({p.returncode})"
    return res, (t1 - t0) * 1000.0

def main():
    base_dir = "benchmarks/official_parity/parity"
    all_files = sorted(glob.glob(os.path.join(base_dir, "**", "*.ehoa"), recursive=True))
    valid_files = [f for f in all_files if os.path.getsize(f) > 200]
    
    rng = random.Random(42)
    selected_files = list(valid_files)
    rng.shuffle(selected_files)
    sample = selected_files[:25]

    print("====================================================================================================================")
    print("  OFFICIAL DIFFERENTIAL TESTING: OmegaSynth vs Knor (strahler-knor)")
    print("====================================================================================================================")
    print(f"%-4s | %-12s | %-38s | %-12s | %-12s | %-7s | %-7s | %s" % 
          ("#", "Family", "Benchmark Name", "OmegaSynth", "Knor", "O-Time", "K-Time", "Match?"))
    print("-" * 116)

    agreements = 0
    total_evaluated = 0

    for idx, f in enumerate(sample, 1):
        fname = os.path.basename(f)
        family = os.path.basename(os.path.dirname(f))

        try:
            o_res, o_time = run_omegasynth(f)
        except Exception as e:
            o_res, o_time = f"EXC({e})", 0.0

        try:
            k_res, k_time = run_knor(f)
        except Exception as e:
            k_res, k_time = f"EXC({e})", 0.0

        if k_res.startswith("ERR"):
            # Knor failed or rejected format (e.g. generalized Buchi)
            match_str = f"SKIP (Knor {k_res})"
        else:
            total_evaluated += 1
            if o_res == k_res:
                agreements += 1
                match_str = "MATCH (100%)"
            else:
                match_str = "DISAGREE!"

        print(f"%-4d | %-12s | %-38s | %-12s | %-12s | %6.1fms | %6.1fms | %s" % 
              (idx, family[:12], fname[:38], o_res, k_res, o_time, k_time, match_str))

    print("-" * 116)
    rate = (agreements / total_evaluated * 100.0) if total_evaluated else 0.0
    print(f"Total Valid Comparison Instances: {total_evaluated}")
    print(f"Exact Verdict Agreements:         {agreements} / {total_evaluated} ({rate:.1f}%)")
    print("====================================================================================================================\n")

if __name__ == "__main__":
    main()
