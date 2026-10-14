#!/usr/bin/env python3
"""
Textbook Zielonka Parity Game Reference Solver
Implements the canonical recursive Zielonka algorithm (Zielonka 1998)
Used for independent differential verification of OmegaSynth.
"""

import sys
import os

class ParityGame:
    def __init__(self):
        self.vertices = set()
        self.edges = {}       # u -> list of v
        self.owner = {}       # u -> 0 (Player 0 / System / Max) or 1 (Player 1 / Env / Min)
        self.priority = {}    # u -> non-negative integer (even = Player 0, odd = Player 1)
        self.initial_state = 0

def parse_ehoa(filename):
    game = ParityGame()
    controllable_aps = set()
    is_parity_max = False
    max_prio_bound = 0
    is_buchi = False

    with open(filename, "r") as f:
        lines = f.readlines()

    in_body = False
    current_state = 0

    for line in lines:
        line = line.strip()
        if not line:
            continue
        if not in_body:
            if line.startswith("acc-name:"):
                if "Buchi" in line or "buchi" in line:
                    is_buchi = True
                if "parity max" in line:
                    is_parity_max = True
                    for tok in line.split():
                        if tok.isdigit():
                            max_prio_bound = int(tok)
            elif line.startswith("controllable-AP:"):
                for tok in line[16:].split():
                    if tok.isdigit():
                        controllable_aps.add(int(tok))
            elif line == "--BODY--":
                in_body = True
            elif line.startswith("Start:"):
                game.initial_state = int(line[6:].strip())
        else:
            if line == "--END--":
                break
            if line.startswith("State:"):
                parts = line[6:].strip().split()
                current_state = int(parts[0])
                game.vertices.add(current_state)
                if current_state not in game.edges:
                    game.edges[current_state] = []
                # Default owner Player 0
                game.owner[current_state] = 0
                # Default priority
                game.priority[current_state] = 1 if is_buchi else 0

                # Check label
                if '"' in line:
                    q1 = line.find('"')
                    q2 = line.find('"', q1 + 1)
                    if q2 != -1:
                        lbl = line[q1+1:q2]
                        if lbl.startswith("R(") or lbl.startswith("env") or lbl.startswith("Min"):
                            game.owner[current_state] = 1
                        elif lbl.startswith("L(") or lbl.startswith("sys") or lbl.startswith("Max"):
                            game.owner[current_state] = 0

                # Check priority {p}
                if "{" in line and "}" in line:
                    b1 = line.find("{")
                    b2 = line.find("}")
                    prio_str = line[b1+1:b2].strip()
                    if prio_str.isdigit():
                        p = int(prio_str)
                        if is_parity_max:
                            M = max_prio_bound if max_prio_bound % 2 == 0 else max_prio_bound + 1
                            if M >= p:
                                p = M - p
                        game.priority[current_state] = p
            else:
                # Transition line: [cond] target [{acc}]
                if current_state not in game.vertices:
                    game.vertices.add(current_state)
                    game.edges[current_state] = []
                    game.owner[current_state] = 0
                    game.priority[current_state] = 1 if is_buchi else 0

                # Check uncontrollable APs in condition
                if controllable_aps and line.startswith("["):
                    c_end = line.find("]")
                    if c_end != -1:
                        cond = line[1:c_end]
                        # Extract AP numbers
                        import re
                        ap_nums = [int(n) for n in re.findall(r"\b\d+\b", cond)]
                        for ap in ap_nums:
                            if ap not in controllable_aps:
                                game.owner[current_state] = 1
                                break

                # Extract target
                after_cond = line[line.find("]")+1:] if "]" in line else line
                tokens = after_cond.split()
                for tok in tokens:
                    if tok.isdigit():
                        target = int(tok)
                        game.vertices.add(target)
                        if target not in game.edges:
                            game.edges[target] = []
                            game.owner[target] = 0
                            game.priority[target] = 1 if is_buchi else 0
                        game.edges[current_state].append(target)
                    elif tok.startswith("{"):
                        if is_buchi:
                            game.priority[current_state] = 2 # even / accepting

    return game

def compute_attractor(V, edges, owner, target_set, player):
    """Computes Attr_player(target_set) restricted to V."""
    attr = set(target_set & V)
    changed = True
    while changed:
        changed = False
        for u in V - attr:
            succs = [v for v in edges.get(u, []) if v in V]
            if not succs:
                continue
            if owner[u] == player:
                # Player can force edge into attr
                if any(v in attr for v in succs):
                    attr.add(u)
                    changed = True
            else:
                # Opponent owns u: all valid moves must stay in attr
                if all(v in attr for v in succs):
                    attr.add(u)
                    changed = True
    return attr

def zielonka(V, edges, owner, priority):
    """Recursive textbook Zielonka parity game solver."""
    if not V:
        return set(), set()

    # Find minimum priority present in V (parity min even)
    min_p = min(priority[v] for v in V)
    p_player = min_p % 2 # 0 = Player 0 (Even), 1 = Player 1 (Odd)

    U = {v for v in V if priority[v] == min_p}
    A = compute_attractor(V, edges, owner, U, p_player)

    W0_prime, W1_prime = zielonka(V - A, edges, owner, priority)
    opp_win = W1_prime if p_player == 0 else W0_prime

    if not opp_win:
        # Player p_player wins all of V
        if p_player == 0:
            return set(V), set()
        else:
            return set(), set(V)

    # Opponent has a winning region in V \ A
    B = compute_attractor(V, edges, owner, opp_win, 1 - p_player)
    W0_double, W1_double = zielonka(V - B, edges, owner, priority)

    if p_player == 0:
        return W0_double, W1_double | B
    else:
        return W0_double | B, W1_double

def solve(filename):
    game = parse_ehoa(filename)
    W0, W1 = zielonka(game.vertices, game.edges, game.owner, game.priority)
    realizable = game.initial_state in W0
    return "REALIZABLE" if realizable else "UNREALIZABLE"

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: zielonka_reference.py <file.ehoa>")
        sys.exit(1)
    res = solve(sys.argv[1])
    print(res)
