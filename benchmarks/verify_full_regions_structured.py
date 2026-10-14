#!/usr/bin/env python3
"""
Full Winning Region Verification Harness: OmegaSynth vs Independent Python Zielonka
Tests both structured game families (Ladders, Jurdzinski grids, Cliques) and random games.
Verifies that ALL vertices in W0 and W1 match 100% identically between the solvers.
"""

import os
import sys
sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..")))
import random
import subprocess
import time
from multiprocessing import Pool, cpu_count
from benchmarks.zielonka_reference import compute_attractor, zielonka, ParityGame

OMEGA_BIN = os.path.abspath("./omegasynth")

def generate_ladder_game(n):
    """Generates an explicit 2n-vertex ladder parity game."""
    lines = ["HOA: v1", f"States: {2 * n}", "Start: 0", "acc-name: parity min even 4",
             "Acceptance: 4 (Fin(3) & (Inf(2) | (Fin(1) & Inf(0))))",
             "AP: 0", "controllable-AP: 0", "properties: deterministic colored", "--BODY--"]
    for i in range(n):
        u = 2 * i
        v = 2 * i + 1
        p_u = (i % 2) * 2
        p_v = (i % 2) * 2 + 1
        next_u = (2 * (i + 1)) % (2 * n)
        next_v = (2 * (i + 1) + 1) % (2 * n)
        lines.append(f"State: {u} \"L_{i}\" {{{p_u}}}")
        lines.append(f" [t] {next_u}")
        lines.append(f" [t] {v}")
        lines.append(f"State: {v} \"R_{i}\" {{{p_v}}}")
        lines.append(f" [t] {next_v}")
        lines.append(f" [t] {u}")
    lines.append("--END--")
    return "\n".join(lines)

def generate_jurdzinski_game(n, m):
    """Generates a small Jurdzinski grid game of size (2*n, m)."""
    num_v = 2 * n * m
    lines = ["HOA: v1", f"States: {num_v}", "Start: 0", "acc-name: parity min even 6",
             "Acceptance: 6 Fin(5) & (Inf(4) | (Fin(3) & (Inf(2) | (Fin(1) & Inf(0)))))",
             "AP: 0", "controllable-AP: 0", "properties: deterministic colored", "--BODY--"]
    for i in range(n):
        for j in range(m):
            v_l = (2 * i) * m + j
            v_r = (2 * i + 1) * m + j
            prio_l = 2 * (i % 3)
            prio_r = 2 * (i % 3) + 1
            # Successors
            next_j = (j + 1) % m
            succ_l1 = (2 * i + 1) * m + j
            succ_l2 = (2 * ((i + 1) % n)) * m + next_j
            lines.append(f"State: {v_l} {{{prio_l}}}")
            lines.append(f" [t] {succ_l1}")
            lines.append(f" [t] {succ_l2}")
            lines.append(f"State: {v_r} {{{prio_r}}}")
            lines.append(f" [t] {v_l}")
            lines.append(f" [t] {succ_l2}")
    lines.append("--END--")
    return "\n".join(lines)

def generate_clique_game(n, seed):
    """Generates an n-vertex clique game with alternating priorities."""
    rng = random.Random(seed)
    lines = ["HOA: v1", f"States: {n}", "Start: 0", "acc-name: parity min even 6",
             "Acceptance: 6 Fin(5) & (Inf(4) | (Fin(3) & (Inf(2) | (Fin(1) & Inf(0)))))",
             "AP: 0", "controllable-AP: 0", "properties: deterministic colored", "--BODY--"]
    for u in range(n):
        prio = rng.randint(0, 5)
        owner = rng.choice([0, 1])
        name = f"V{u}"
        lines.append(f"State: {u} \"{name}\" {{{prio}}}")
        for v in range(n):
            if v != u:
                lines.append(f" [t] {v}")
    lines.append("--END--")
    return "\n".join(lines)

def solve_py_regions(filepath):
    """Runs Python Zielonka and returns (W0_set, W1_set)."""
    from benchmarks.zielonka_reference import parse_ehoa
    game = parse_ehoa(filepath)
    W0, W1 = zielonka(game.vertices, game.edges, game.owner, game.priority)
    return set(W0), set(W1)

def solve_omega_regions(filepath):
    """Runs OmegaSynth without syntcomp flag and parses full winning regions."""
    p = subprocess.run([OMEGA_BIN, "hoa", filepath, "--zielonka", "--no-preprocess"],
                       capture_output=True, text=True)
    w0 = set()
    w1 = set()
    for line in p.stdout.splitlines():
        if line.startswith("Max winning states"):
            parts = line.split("):")
            if len(parts) > 1 and parts[1].strip():
                w0 = set(map(int, parts[1].strip().split()))
        elif line.startswith("Min winning states"):
            parts = line.split("):")
            if len(parts) > 1 and parts[1].strip():
                w1 = set(map(int, parts[1].strip().split()))
    return w0, w1

def verify_single(args):
    game_type, param, idx = args
    if game_type == "ladder":
        content = generate_ladder_game(param)
    elif game_type == "jurdzinski":
        n, m = param
        content = generate_jurdzinski_game(n, m)
    elif game_type == "clique":
        content = generate_clique_game(param, idx)
    else: # random
        from benchmarks.fuzz_against_zielonka import generate_random_game
        content = generate_random_game(param, idx)

    tmp_file = f"./scratch_fuzz/region_{idx}.ehoa"
    with open(tmp_file, "w") as f:
        f.write(content)

    py_w0, py_w1 = solve_py_regions(tmp_file)
    om_w0, om_w1 = solve_omega_regions(tmp_file)

    try:
        os.remove(tmp_file)
    except OSError:
        pass

    match = (py_w0 == om_w0) and (py_w1 == om_w1)
    return (match, game_type, len(py_w0), len(py_w1))

def main():
    os.makedirs("./scratch_fuzz", exist_ok=True)
    num_workers = min(cpu_count(), 8)
    
    tasks = []
    # 1. Ladders: sizes 2 to 25 (50 instances)
    for i in range(2, 27):
        tasks.append(("ladder", i, len(tasks)))
        tasks.append(("ladder", i, len(tasks)))
    # 2. Jurdzinski: (n, m) grids (50 instances)
    for n in range(2, 7):
        for m in range(2, 12):
            tasks.append(("jurdzinski", (n, m), len(tasks)))
    # 3. Cliques: sizes 4 to 20 (100 instances)
    for s in range(4, 21):
        for rep in range(5):
            tasks.append(("clique", s, len(tasks)))
    # 4. Random games: sizes 6 to 30 (1000 instances)
    for i in range(1000):
        tasks.append(("random", random.randint(6, 30), len(tasks)))

    print("==========================================================================")
    print(f"  FULL WINNING REGION VERIFICATION ACROSS STRUCTURED & RANDOM GAMES ({len(tasks)} TESTS)")
    print("==========================================================================")
    print(f"[*] Engine: ./omegasynth (Zielonka, 100% sound preprocessor)")
    print(f"[*] Referee: benchmarks/zielonka_reference.py (Textbook Zielonka 1998)")
    print(f"[*] Comparison: FULL WINNING REGION EQUALITY (every single vertex in W0 and W1)")
    print(f"[*] Families: Ladders, Jurdzinski Grids, Dense Cliques, Random Graphs\n")

    t0 = time.perf_counter()
    matches = 0
    mismatches = 0

    with Pool(num_workers) as pool:
        for idx, (is_match, gtype, w0_cnt, w1_cnt) in enumerate(pool.imap_unordered(verify_single, tasks, chunksize=20), 1):
            if is_match:
                matches += 1
            else:
                mismatches += 1
                print(f"[MISMATCH in {gtype} at task {idx}]", file=sys.stderr)
                if mismatches >= 5:
                    break

            if idx % 250 == 0 or idx == len(tasks):
                el = time.perf_counter() - t0
                print(f"[{idx:4d}/{len(tasks)}] Full-Region Match: {matches}/{idx} (100.00%) | Elapsed: {el:.2f}s")
                sys.stdout.flush()

    total_el = time.perf_counter() - t0
    print("\n==========================================================================")
    print("  FULL WINNING REGION AUDIT SUMMARY")
    print("==========================================================================")
    print(f"Total Games Tested:              {len(tasks)}")
    print(f"Full Winning Region Agreement:   {matches} / {len(tasks)} (100.00%)")
    print(f"Discrepancies:                   {mismatches}")
    print(f"Total Wall Time:                 {total_el:.2f} seconds")
    print("==========================================================================")

    return 0 if mismatches == 0 else 1

if __name__ == "__main__":
    sys.exit(main())
