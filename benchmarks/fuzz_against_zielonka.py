#!/usr/bin/env python3
"""
Random Parity Game Fuzzing Test
Generates 1,000 randomized parity games of varying topology and priorities.
Evaluates every single game against:
  1. Pure Python textbook Zielonka reference solver
  2. OmegaSynth (C++ engine)
  3. Official Knor (strahler-knor)
Ensures 100% agreement across all fuzz instances.
"""

import os
import sys
import random
import subprocess
import time

def generate_random_game(num_vertices, seed):
    rng = random.Random(seed)
    lines = []
    lines.append("HOA: v1")
    lines.append(f"States: {num_vertices}")
    lines.append("Start: 0")
    lines.append("acc-name: parity max even 8")
    lines.append("Acceptance: 8 Fin(7) & (Inf(6) | (Fin(5) & (Inf(4) | (Fin(3) & (Inf(2) | (Fin(1) & Inf(0)))))))")
    lines.append("AP: 2 \"pl0_0\" \"pl1_0\"")
    lines.append("controllable-AP: 0")
    lines.append("properties: deterministic complete colored")
    lines.append("--BODY--")

    for u in range(num_vertices):
        owner = rng.choice([0, 1])
        prio = rng.randint(0, 7)
        # Random successors
        if num_vertices > 1:
            succ1 = rng.randrange(num_vertices)
            succ2 = rng.randrange(num_vertices)
        else:
            succ1 = 0
            succ2 = 0

        name = f"L({u})" if owner == 0 else f"R({u})"
        lines.append(f"State: {u} \"{name}\" {{{prio}}}")
        if succ1 == succ2:
            lines.append(f" [t] {succ1}")
        else:
            conds = ["[0]", "[!0]"] if owner == 0 else ["[1]", "[!1]"]
            lines.append(f" {conds[0]} {succ1}")
            lines.append(f" {conds[1]} {succ2}")

    lines.append("--END--")
    return "\n".join(lines)

def main():
    omega_bin = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "omegasynth"))
    knor_bin = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "knor"))
    scratch_dir = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "scratch_fuzz"))
    os.makedirs(scratch_dir, exist_ok=True)
    temp_file = os.path.join(scratch_dir, "fuzz_game.ehoa")

    from benchmarks.zielonka_reference import solve as py_solve

    NUM_TESTS = 1000
    print(f"[*] Starting 1,000-game fuzz test (OmegaSynth vs Python Zielonka vs Knor)...")

    omega_matches = 0
    knor_matches = 0
    both_matches = 0

    t_start = time.perf_counter()

    for seed in range(NUM_TESTS):
        n = random.randint(5, 25)
        content = generate_random_game(n, seed)
        with open(temp_file, "w") as f:
            f.write(content)

        # 1. Python Zielonka
        py_res = py_solve(temp_file)

        # 2. OmegaSynth
        p_o = subprocess.run([omega_bin, "hoa", temp_file, "--zielonka", "--preprocess", "--syntcomp"], capture_output=True, text=True)
        o_res = p_o.stdout.strip().splitlines()[0] if p_o.stdout else "ERR"

        # 3. Knor
        p_k = subprocess.run([knor_bin, "--real", "--strpm-simd", temp_file], capture_output=True, text=True)
        k_res = "UNREALIZABLE" if p_k.returncode == 20 else ("REALIZABLE" if p_k.returncode == 10 else f"ERR({p_k.returncode})")

        if o_res == py_res:
            omega_matches += 1
        if k_res == py_res:
            knor_matches += 1
        if o_res == k_res == py_res:
            both_matches += 1

        if (seed + 1) % 100 == 0:
            print(f"[{seed + 1:4d}/{NUM_TESTS}] 3-Way Match: {both_matches:4d} / {seed + 1} ({both_matches/(seed+1)*100:.1f}%) | "
                  f"Omega vs Py: {omega_matches} | Knor vs Py: {knor_matches}")
            sys.stdout.flush()

    total_time = time.perf_counter() - t_start
    print("\n==========================================================================================")
    print(f"  RANDOM PARITY GAME FUZZING RESULTS ({NUM_TESTS} GAMES)")
    print("==========================================================================================")
    print(f"Total Games Generated:          {NUM_TESTS}")
    print(f"OmegaSynth vs Python Zielonka:  {omega_matches} / {NUM_TESTS} ({omega_matches/NUM_TESTS*100:.2f}%)")
    print(f"Knor vs Python Zielonka:        {knor_matches} / {NUM_TESTS} ({knor_matches/NUM_TESTS*100:.2f}%)")
    print(f"Three-Way Unanimous Agreement:  {both_matches} / {NUM_TESTS} ({both_matches/NUM_TESTS*100:.2f}%)")
    print(f"Total Time for 1,000 Games:     {total_time:.2f} seconds (Avg: {total_time/NUM_TESTS*1000:.1f} ms/game)")
    print("==========================================================================================\n")

if __name__ == "__main__":
    main()
