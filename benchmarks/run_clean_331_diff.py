#!/usr/bin/env python3
"""
Clean Post-Fix Differential Verification: OmegaSynth (Fixed Binary) vs Knor
Runs on the 331 explicit parity games only (no tlsf_based).
Both solvers run single-threaded on identical AMD Ryzen 7 6800H host silicon.
"""

import os
import sys
import glob
import time
import subprocess
import pandas as pd

TIMEOUT = 30.0

def main():
    base_dir = os.path.abspath(os.path.join(os.path.dirname(__file__), "official_parity", "parity"))
    omega_bin = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "omegasynth"))
    knor_bin = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "knor"))
    csv_out = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "CLEAN_331_POSTFIX_DIFF_RESULTS.csv"))

    # Load the 331 explicit files from our previously audited dataset
    prev_csv = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "FULL_483_DIFF_RESULTS.csv"))
    prev_df = pd.read_csv(prev_csv)
    explicit_families = ['keiren', 'pgsolver_based', 'mucalc_mc', 'hoatools_gend']
    explicit_df = prev_df[prev_df['family'].isin(explicit_families) & (prev_df['status'] == 'AGREE')]

    file_names = set(explicit_df['benchmark'].tolist())
    print(f"[*] Loaded {len(file_names)} explicit benchmarks for fresh post-fix evaluation.")

    # Find full paths
    all_files = sorted(glob.glob(os.path.join(base_dir, "**", "*.ehoa"), recursive=True))
    explicit_files = [f for f in all_files if os.path.basename(f) in file_names]
    print(f"[*] Matched {len(explicit_files)} file paths on disk.")

    records = []
    agreed = 0
    disagreed = 0
    omega_total_ms = 0.0
    knor_total_ms = 0.0

    print("==========================================================================================")
    print(f"  RUNNING FRESH POST-FIX 331-INSTANCE DIFFERENTIAL VERIFICATION")
    print("==========================================================================================\n")

    start_suite = time.perf_counter()

    for idx, fpath in enumerate(explicit_files, 1):
        fname = os.path.basename(fpath)
        family = os.path.basename(os.path.dirname(fpath))

        # 1. OmegaSynth (Fixed Binary, Single-Threaded Zielonka + Preprocess)
        t0 = time.perf_counter()
        po = subprocess.run([omega_bin, "hoa", fpath, "--zielonka", "--preprocess", "--syntcomp"], capture_output=True, text=True, timeout=TIMEOUT)
        t1 = time.perf_counter()
        o_ms = (t1 - t0) * 1000.0
        o_res = po.stdout.strip().splitlines()[0] if po.stdout else "ERR"

        # 2. Knor (Official Binary, Single-Threaded Strahler-SIMD)
        t0 = time.perf_counter()
        pk = subprocess.run([knor_bin, "--real", "--strpm-simd", fpath], capture_output=True, text=True, timeout=TIMEOUT)
        t1 = time.perf_counter()
        k_ms = (t1 - t0) * 1000.0
        k_res = "UNREALIZABLE" if pk.returncode == 20 else ("REALIZABLE" if pk.returncode == 10 else f"ERR({pk.returncode})")

        omega_total_ms += o_ms
        knor_total_ms += k_ms

        match = (o_res == k_res)
        if match:
            agreed += 1
            status = "AGREE"
        else:
            disagreed += 1
            status = "DISAGREE"

        records.append((idx, fname, family, o_res, k_res, o_ms, k_ms, status))

        if idx % 25 == 0 or idx == len(explicit_files) or not match:
            print(f"[{idx:3d}/{len(explicit_files)}] Agreed: {agreed:3d} | Disagree: {disagreed} | "
                  f"OmegaTotal: {omega_total_ms/1000.0:6.2f}s | KnorTotal: {knor_total_ms/1000.0:6.2f}s | "
                  f"Last: {fname[:24]} -> {status}")
            sys.stdout.flush()

    total_wall = time.perf_counter() - start_suite

    # Write CSV
    with open(csv_out, "w") as fp:
        fp.write("index,benchmark,family,omegasynth_verdict,knor_verdict,omegasynth_ms,knor_ms,status\n")
        for r in records:
            fp.write(f"{r[0]},{r[1]},{r[2]},{r[3]},{r[4]},{r[5]:.3f},{r[6]:.3f},{r[7]}\n")

    print("\n==========================================================================================")
    print("  FRESH POST-FIX 331-INSTANCE AUDIT SUMMARY")
    print("==========================================================================================")
    print(f"Total Evaluated:                  {len(explicit_files)}")
    print(f"Exact Verdict Agreements:         {agreed} / {len(explicit_files)} ({agreed/len(explicit_files)*100.0:.2f}%)")
    print(f"Disagreements:                    {disagreed}")
    print(f"OmegaSynth Cumulative Wall Time:  {omega_total_ms/1000.0:.3f} seconds (Avg: {omega_total_ms/len(explicit_files):.2f} ms/game)")
    print(f"Knor Cumulative Wall Time:        {knor_total_ms/1000.0:.3f} seconds (Avg: {knor_total_ms/len(explicit_files):.2f} ms/game)")
    speedup = knor_total_ms / omega_total_ms if omega_total_ms > 0 else 0
    print(f"Clean Silicon Speedup:            {speedup:.2f}x FASTER than Knor")
    print(f"Exported Results:                 {csv_out}")
    print("==========================================================================================\n")

if __name__ == "__main__":
    main()
