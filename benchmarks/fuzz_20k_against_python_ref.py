#!/usr/bin/env python3
"""
Large-Scale Multi-Engine Fuzzing: OmegaSynth vs Independent Python Zielonka Reference
Generates 20,000 randomized parity games.
Compares OmegaSynth (C++ binary with preprocessor + Zielonka) against
pure Python textbook Zielonka reference (completely independent implementation).
"""

import os
import sys
sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..")))
import random
import subprocess
import time
from multiprocessing import Pool, cpu_count
from benchmarks.zielonka_reference import solve as py_solve
from benchmarks.fuzz_against_zielonka import generate_random_game

OMEGA_BIN = os.path.abspath("./omegasynth")

def test_single_game(args):
    seed, worker_id = args
    rng = random.Random(seed)
    n = rng.randint(6, 25)
    content = generate_random_game(n, seed)
    
    tmp_path = f"./scratch_fuzz/fuzz_{seed}.ehoa"
    with open(tmp_path, "w") as f:
        f.write(content)
        
    # 1. Independent Python Reference
    py_res = py_solve(tmp_path)
    
    # 2. OmegaSynth (C++ engine with preprocessor)
    p = subprocess.run([OMEGA_BIN, "hoa", tmp_path, "--zielonka", "--preprocess", "--syntcomp"],
                       capture_output=True, text=True)
    out = p.stdout.strip().splitlines()
    o_res = out[0] if out else "ERR"
    
    try:
        os.remove(tmp_path)
    except OSError:
        pass
        
    return (py_res == o_res, seed, n, py_res, o_res)

def main():
    os.makedirs("./scratch_fuzz", exist_ok=True)
    num_games = 20000
    num_workers = min(cpu_count(), 8)
    
    print("==========================================================================")
    print(f"  FUZZING OMEGASYNTH AGAINST INDEPENDENT PYTHON REFERENCE ({num_games} GAMES)")
    print("==========================================================================")
    print(f"[*] Reference: benchmarks/zielonka_reference.py (Textbook Zielonka 1998)")
    print(f"[*] Candidate: ./omegasynth (C++20, preprocessed + peeled Zielonka)")
    print(f"[*] Workers: {num_workers} parallel worker processes")
    print(f"[*] Game sizes: 6 to 25 vertices, 2 players, priorities 0 to 7\n")
    
    tasks = [(i + 200000, i % num_workers) for i in range(num_games)]
    
    t0 = time.perf_counter()
    matches = 0
    mismatches = 0
    
    with Pool(num_workers) as pool:
        for idx, (is_match, seed, n, py_res, o_res) in enumerate(pool.imap_unordered(test_single_game, tasks, chunksize=100), 1):
            if is_match:
                matches += 1
            else:
                mismatches += 1
                print(f"[MISMATCH at seed {seed}, n={n}]: Python={py_res} vs OmegaSynth={o_res}", file=sys.stderr)
                if mismatches >= 5:
                    print("Aborting after 5 mismatches.", file=sys.stderr)
                    break
                    
            if idx % 5000 == 0 or idx == num_games:
                el = time.perf_counter() - t0
                print(f"[{idx:5d}/{num_games}] Agreement: {matches}/{idx} ({matches/idx*100:.2f}%) | Elapsed: {el:.2f}s ({idx/el:.1f} games/s)")
                sys.stdout.flush()
                
    total_time = time.perf_counter() - t0
    print("\n==========================================================================")
    print("  INDEPENDENT PYTHON REFERENCE FUZZING SUMMARY")
    print("==========================================================================")
    print(f"Total Games Evaluated:           {num_games}")
    print(f"Exact Verdict Agreement:         {matches} / {num_games} ({matches/num_games*100:.2f}%)")
    print(f"Mismatches:                      {mismatches}")
    print(f"Total Wall Time:                 {total_time:.2f} seconds")
    print(f"Throughput:                      {num_games/total_time:.1f} games/second")
    print("==========================================================================")
    
    return 0 if mismatches == 0 else 1

if __name__ == "__main__":
    sys.exit(main())
