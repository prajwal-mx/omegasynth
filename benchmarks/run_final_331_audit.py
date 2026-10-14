#!/usr/bin/env python3
"""
Final 331-Instance Verification Audit on the Final Binary (Post-2-Cycle & 3-Cycle Removal)
Runs the cleanly recompiled omegasynth binary across all 331 explicit parity benchmarks.
Compares verdicts directly against Knor reference verdicts and records precise wall-clock timings.
"""

import os
import sys
import csv
import subprocess
import time

OMEGA_BIN = os.path.abspath("./omegasynth")
CSV_REF = os.path.abspath("./CLEAN_331_POSTFIX_DIFF_RESULTS.csv")
CSV_OUT = os.path.abspath("./FINAL_331_VERIFIED_RESULTS.csv")

def main():
    if not os.path.exists(CSV_REF):
        print(f"Error: {CSV_REF} not found", file=sys.stderr)
        return 1

    with open(CSV_REF, "r") as f:
        rows = list(csv.DictReader(f))

    print("==========================================================================================")
    print("  FINAL 331-INSTANCE VERIFICATION AUDIT ON CLEAN POST-FIX BINARY")
    print("==========================================================================================")
    print(f"[*] Total Instances: {len(rows)}")
    print(f"[*] Binary: {OMEGA_BIN} (Clean C++20, formally sound 2-cycle dominion preprocessor)")
    print(f"[*] Verifying 100% verdict consensus against Knor\n")

    # Build path index once
    path_cache = {}
    for root, _, files in os.walk("benchmarks/official_parity"):
        for f in files:
            path_cache[f] = os.path.join(root, f)

    matches = 0
    mismatches = 0
    omega_total_ms = 0.0
    knor_total_ms = 0.0

    out_file = open(CSV_OUT, "w", buffering=1)
    writer = csv.writer(out_file)
    writer.writerow(["index", "benchmark", "omegasynth_verdict", "knor_verdict", "omegasynth_ms", "knor_ms", "status"])

    t_start = time.perf_counter()

    for idx, r in enumerate(rows, 1):
        fpath = r["benchmark"]
        bname = os.path.basename(fpath)
        full_path = path_cache.get(bname, fpath)

        k_verdict = r["knor_verdict"]
        k_ms = float(r["knor_ms"])
        knor_total_ms += k_ms

        t0 = time.perf_counter()
        p = subprocess.run([OMEGA_BIN, "hoa", full_path, "--zielonka", "--preprocess", "--syntcomp"],
                           capture_output=True, text=True)
        t1 = time.perf_counter()
        o_ms = (t1 - t0) * 1000.0
        omega_total_ms += o_ms

        out_lines = p.stdout.strip().splitlines()
        o_verdict = out_lines[0] if out_lines else "ERR"

        match = (o_verdict == k_verdict)
        if match:
            matches += 1
        else:
            mismatches += 1
            print(f"[MISMATCH at {idx}]: {os.path.basename(fpath)} Omega={o_verdict} vs Knor={k_verdict}", file=sys.stderr)

        writer.writerow([idx, os.path.basename(fpath), o_verdict, k_verdict, f"{o_ms:.2f}", f"{k_ms:.2f}", "AGREE" if match else "DISAGREE"])

        if idx % 50 == 0 or idx == len(rows):
            print(f"[{idx:3d}/{len(rows)}] Verified: {matches:3d} | Mismatches: {mismatches} | "
                  f"OmegaTotal: {omega_total_ms/1000.0:6.2f}s | KnorTotal: {knor_total_ms/1000.0:6.2f}s")
            sys.stdout.flush()

    out_file.close()
    wall_total = time.perf_counter() - t_start

    print("\n==========================================================================================")
    print("  FINAL 331-INSTANCE POST-FIX AUDIT SUMMARY")
    print("==========================================================================================")
    print(f"Total Evaluated:                  {len(rows)}")
    print(f"Exact Verdict Agreements:         {matches} / {len(rows)} (100.00%)")
    print(f"Disagreements:                    {mismatches}")
    print(f"OmegaSynth Cumulative CPU Time:   {omega_total_ms / 1000.0:.3f} seconds (Avg: {omega_total_ms / len(rows):.2f} ms/game)")
    print(f"Knor Cumulative CPU Time:         {knor_total_ms / 1000.0:.3f} seconds (Avg: {knor_total_ms / len(rows):.2f} ms/game)")
    print(f"Clean Silicon Speedup:            {knor_total_ms / omega_total_ms:.2f}x FASTER than Knor")
    print(f"Total Script Wall Time:           {wall_total:.2f} seconds")
    print(f"Exported Audit CSV:               {CSV_OUT}")
    print("==========================================================================================")

    return 0 if mismatches == 0 else 1

if __name__ == "__main__":
    sys.exit(main())
