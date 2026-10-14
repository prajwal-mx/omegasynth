#!/usr/bin/env python3
"""
Independent Symbolic TLSF Expander & Parity Solver
Zero dependencies on Knor. Expands Mealy symbolic HOA automata into explicit
turn-based parity games and solves via min-parity Zielonka.
"""

import sys
import os
import re
import itertools
from benchmarks.zielonka_reference import zielonka

def eval_guard(guard_str, val):
    g = guard_str.strip('[]')
    if g == 't': return True
    if g == 'f': return False
    tokens = re.split(r'([0-9]+)', g)
    res = []
    for t in tokens:
        if t.isdigit():
            res.append(str(val.get(int(t), False)))
        else:
            res.append(t.replace('&', ' and ').replace('|', ' or ').replace('!', ' not '))
    return eval(''.join(res))

def solve_tlsf(filepath):
    with open(filepath) as f:
        lines = [l.strip() for l in f if l.strip()]

    num_aps = 0
    controllable_aps = set()
    in_body = False
    transitions = {}
    current_state = None

    for line in lines:
        if line.startswith('AP:'):
            parts = line.split()
            num_aps = int(parts[1])
        elif line.startswith('controllable-AP:'):
            controllable_aps = set(int(x) for x in line.split()[1:])
        elif line == '--BODY--':
            in_body = True
        elif line == '--END--':
            break
        elif in_body:
            if line.startswith('State:'):
                current_state = int(line.split()[1])
                transitions[current_state] = []
            elif line.startswith('['):
                g_end = line.find(']')
                guard = line[:g_end+1]
                rest = line[g_end+1:].strip()
                tokens = rest.split()
                target = int(tokens[0])
                prio = 0
                if len(tokens) > 1 and tokens[1].startswith('{'):
                    prio = int(tokens[1].strip('{}'))
                transitions[current_state].append((guard, target, prio))

    uncontrollable_aps = [i for i in range(num_aps) if i not in controllable_aps]
    ctrl_aps = sorted(list(controllable_aps))

    V = set()
    edges = {}
    owner = {}
    priority = {}
    node_id = 0
    state_to_id = {}
    for s in transitions:
        state_to_id[s] = node_id
        V.add(node_id)
        owner[node_id] = 1
        priority[node_id] = 0
        node_id += 1

    for s, s_id in list(state_to_id.items()):
        edges[s_id] = []
        u_combos = list(itertools.product([False, True], repeat=len(uncontrollable_aps)))
        for u_vals in u_combos:
            u_dict = {uncontrollable_aps[i]: u_vals[i] for i in range(len(uncontrollable_aps))}
            u_node = node_id
            node_id += 1
            V.add(u_node)
            owner[u_node] = 0
            priority[u_node] = 0
            edges[s_id].append(u_node)
            edges[u_node] = []

            c_combos = list(itertools.product([False, True], repeat=len(ctrl_aps)))
            for c_vals in c_combos:
                c_dict = {ctrl_aps[i]: c_vals[i] for i in range(len(ctrl_aps))}
                full_val = {**u_dict, **c_dict}

                for guard, target, prio in transitions[s]:
                    if eval_guard(guard, full_val):
                        tgt_id = state_to_id[target]
                        p_node = node_id
                        node_id += 1
                        V.add(p_node)
                        owner[p_node] = 1
                        priority[p_node] = prio
                        edges[p_node] = [tgt_id]
                        edges[u_node].append(p_node)
                        break

    max_p = max(priority.values()) if priority else 0
    B = max_p if (max_p % 2 == 0) else (max_p + 1)
    for v in V:
        priority[v] = B - priority[v]

    W0, W1 = zielonka(V, edges, owner, priority)
    return 'REALIZABLE' if 0 in W0 else 'UNREALIZABLE'

if __name__ == '__main__':
    if len(sys.argv) < 2:
        print("Usage: independent_tlsf_solver.py <file.tlsf.ehoa>")
        sys.exit(1)
    print(solve_tlsf(sys.argv[1]))
