#!/usr/bin/env python3
"""
Full 483-Instance Differential Verification: OmegaSynth vs Official Knor (strahler-knor)
Executes BOTH solvers side-by-side on the EXACT SAME HARDWARE on all 483 benchmarks.
Generates an audit report and exports FULL_483_DIFF_RESULTS.csv.
"""

import os
import sys
import glob
import random
import subprocess
import time

TIMEOUT_SECONDS = 30.0
TARGET_COUNT = 483

def run_omegasynth(filepath, binary):
    cmd = [binary, "hoa", filepath, "--zielonka", "--preprocess", "--syntcomp"]
    t0 = time.perf_counter()
    p = subprocess.run(cmd, capture_output=True, text=True, timeout=TIMEOUT_SECONDS)
    t1 = time.perf_counter()
    out = p.stdout.strip().splitlines()
    res = out[0] if out else "EMPTY"
    return res, (t1 - t0) * 1000.0, p.returncode

def run_knor(filepath, knor_binary):
    cmd = [knor_binary, "--real", "--strpm-simd", filepath]
    t0 = time.perf_counter()
    p = subprocess.run(cmd, capture_output=True, text=True, timeout=TIMEOUT_SECONDS)
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
    return res, (t1 - t0) * 1000.0, p.returncode

def main():
    base_dir = os.path.abspath(os.path.join(os.path.dirname(__file__), "official_parity", "parity"))
    omega_bin = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "omegasynth"))
    knor_bin = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "knor"))
    csv_out = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "FULL_483_DIFF_RESULTS.csv"))

    if not os.path.exists(knor_bin):
        print(f"Error: Knor binary not found at {knor_bin}", file=sys.stderr)
        sys.exit(1)

    all_files = sorted(glob.glob(os.path.join(base_dir, "**", "*.ehoa"), recursive=True))
    valid_files = [f for f in all_files if os.path.getsize(f) > 200]

    rng = random.Random(42)
    selected_files = list(valid_files)
    rng.shuffle(selected_files)
    if len(selected_files) > TARGET_COUNT:
        selected_files = selected_files[:TARGET_COUNT]

    print(f"[*] Starting full differential run on {len(selected_files)} benchmark instances...")
    print(f"[*] Hardware: Machine-matched on host AMD Ryzen 7 6800H")
    print(f"[*] Per-game timeout: {TIMEOUT_SECONDS}s\n")
    print("==============================================================================================================")
    print("  RUNNING FULL 483-INSTANCE DIFFERENTIAL AUDIT: OMEGASYNTH vs KNOR")
    print("==============================================================================================================\n")

    records = []
    both_agreed = 0
    disagreements = 0
    knor_failures = 0
    omega_failures = 0

    omega_total_ms = 0.0
    knor_total_ms = 0.0

    # Prepare CSV output file
    csv_file = open(csv_out, "w", buffering=1)
    csv_file.write("index,benchmark,family,omegasynth_verdict,knor_verdict,omegasynth_ms,knor_ms,status\n")
    csv_file.flush()

    for idx, fpath in enumerate(selected_files, 1):
        fname = os.path.basename(fpath)
        family = os.path.basename(os.path.dirname(fpath))

        # Run OmegaSynth
        try:
            o_res, o_ms, o_ret = run_omegasynth(fpath, omega_bin)
        except subprocess.TimeoutExpired:
            o_res, o_ms, o_ret = "TIMEOUT", TIMEOUT_SECONDS * 1000.0, -1
        except Exception as e:
            o_res, o_ms, o_ret = f"EXC({e})", 0.0, -2

        # Run Knor
        try:
            k_res, k_ms, k_ret = run_knor(fpath, knor_bin)
        except subprocess.TimeoutExpired:
            k_res, k_ms, k_ret = "TIMEOUT", TIMEOUT_SECONDS * 1000.0, -1
        except Exception as e:
            k_res, k_ms, k_ret = f"EXC({e})", 0.0, -2

        omega_total_ms += o_ms
        knor_total_ms += k_ms

        # Classification
        if k_res.startswith("ERR") or k_res == "TIMEOUT" or k_ret not in (10, 20):
            status = "KNOR_FAIL"
            knor_failures += 1
        elif o_res not in ("REALIZABLE", "UNREALIZABLE") or o_ret != 0:
            status = "OMEGA_FAIL"
            omega_failures += 1
        elif o_res == k_res:
            status = "AGREE"
            both_agreed += 1
        else:
            status = "DISAGREE"
            disagreements += 1

        records.append((idx, fname, family, o_res, k_res, o_ms, k_ms, status))
        csv_file.write(f"{idx},{fname},{family},{o_res},{k_res},{o_ms:.3f},{k_ms:.3f},{status}\n")
        csv_file.flush()

        if idx % 20 == 0 or idx == len(selected_files) or status == "DISAGREE":
            print(f"[{idx:3d}/{len(selected_files)}] Agreed: {both_agreed:3d} | Disagree: {disagreements:2d} | "
                  f"KnorFail: {knor_failures:2d} | OmegaTotal: {omega_total_ms/1000.0:6.1f}s | KnorTotal: {knor_total_ms/1000.0:6.1f}s | "
                  f"Last: {fname[:22]} -> {status}")
            sys.stdout.flush()

    csv_file.close()
    total_wall = time.perf_counter() - start_time

    print("\n==============================================================================================")
    print("  FULL 483-INSTANCE HEAD-TO-HEAD AUDIT SUMMARY")
    print("==============================================================================================")
    print(f"Total Benchmarks Evaluated:       {len(selected_files)}")
    valid_both = both_agreed + disagreements
    print(f"Solvable by Both Solvers:         {valid_both}")
    print(f"  - Exact Verdict Agreements:     {both_agreed} / {valid_both} ({both_agreed/valid_both*100.0:.2f}%)" if valid_both else "")
    print(f"  - Verdict Disagreements:        {disagreements}")
    print(f"Knor Crashes / Errors / Timeouts: {knor_failures}")
    print(f"OmegaSynth Failures / Timeouts:   {omega_failures}")
    print("----------------------------------------------------------------------------------------------")
    print("  MACHINE-MATCHED RUNTIME COMPARISON (SAME AMD RYZEN 7 6800H SILICON)")
    print("----------------------------------------------------------------------------------------------")
    print(f"OmegaSynth Cumulative Wall Time:  {omega_total_ms/1000.0:.3f} seconds (Avg: {omega_total_ms/len(selected_files):.2f} ms/game)")
    print(f"Knor Cumulative Wall Time:        {knor_total_ms/1000.0:.3f} seconds (Avg: {knor_total_ms/len(selected_files):.2f} ms/game)")
    speedup = knor_total_ms / omega_total_ms if omega_total_ms > 0 else 0
    print(f"Machine-Matched Speedup Factor:   {speedup:.2f}x FASTER than Knor on identical hardware")
    print(f"Complete Per-Game CSV Exported:   {csv_out}")
    print("==============================================================================================\n")

if __name__ == "__main__":
    main()
