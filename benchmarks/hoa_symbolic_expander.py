#!/usr/bin/env python3
"""
HOA Symbolic Expander & Verifier
Parses HOA automata with AP and controllable-AP, expands them into explicit
2-player turn-based parity games (Mealy semantics: Environment moves first with inputs,
System moves second with outputs), and solves them with textbook Zielonka.
Used to validate ground truth against Knor.
"""

import sys
import os
sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..")))
import re

def parse_label_expr(label_str, num_aps):
    """
    Parses a boolean label expression like [!0&1&2 | !0&!2&!3] into a predicate
    that takes an AP valuation (tuple of booleans of length num_aps).
    """
    # Replace AP indices with val[i]
    # Tokenize numbers
    expr = label_str.strip()
    if expr.startswith("[") and expr.endswith("]"):
        expr = expr[1:-1]
    if expr == "t":
        return lambda val: True
    if expr == "f":
        return lambda val: False
        
    # Replace !i with (not val[i]), i with val[i], & with and, | with or
    # Be careful with multi-digit numbers: sort by length descending
    # Replace numbers with val[...]
    tokens = re.split(r'([0-9]+)', expr)
    py_tokens = []
    for t in tokens:
        if t.isdigit():
            py_tokens.append(f"val[{t}]")
        else:
            t_sub = t.replace("!", " not ").replace("&", " and ").replace("|", " or ")
            py_tokens.append(t_sub)
    py_expr = "".join(py_tokens).strip()
    code = compile(py_expr, "<expr>", "eval")
    return lambda val: eval(code, {"val": val})

class HOAAutomaton:
    def __init__(self, filepath):
        self.filepath = filepath
        self.num_states = 0
        self.start_state = 0
        self.num_aps = 0
        self.ap_names = []
        self.controllable_aps = set() # output APs (sys)
        self.input_aps = set()        # input APs (env)
        self.acc_name = ""
        self.max_prio = 0
        self.is_max_parity = False
        self.transitions = {} # s -> list of (pred_fn, target_s, prio)
        
        self._parse()
        
    def _parse(self):
        with open(self.filepath, "r") as f:
            lines = f.readlines()
            
        in_body = False
        curr_state = None
        
        for line in lines:
            line = line.strip()
            if not line:
                continue
            if not in_body:
                if line.startswith("States:"):
                    self.num_states = int(line[7:].strip())
                elif line.startswith("Start:"):
                    self.start_state = int(line[6:].strip())
                elif line.startswith("AP:"):
                    parts = line[3:].strip().split()
                    self.num_aps = int(parts[0])
                    self.ap_names = parts[1:]
                elif line.startswith("controllable-AP:"):
                    for tok in line[16:].split():
                        if tok.isdigit():
                            self.controllable_aps.add(int(tok))
                elif line.startswith("acc-name:"):
                    self.acc_name = line[9:].strip()
                    if "parity" in self.acc_name:
                        toks = self.acc_name.split()
                        self.is_max_parity = ("max" in toks)
                        for t in toks:
                            if t.isdigit():
                                self.max_prio = int(t)
                elif line == "--BODY--":
                    in_body = True
                    self.input_aps = set(range(self.num_aps)) - self.controllable_aps
            else:
                if line == "--END--":
                    break
                if line.startswith("State:"):
                    parts = line[6:].strip().split()
                    curr_state = int(parts[0])
                    if curr_state not in self.transitions:
                        self.transitions[curr_state] = []
                elif curr_state is not None:
                    # Transition line: [label] target {prio}
                    # e.g. [!0&!1&2] 0 {2}
                    m = re.match(r'\[(.*?)\]\s+([0-9]+)(?:\s+\{([0-9]+)\})?', line)
                    if m:
                        lbl_str = m.group(1)
                        target = int(m.group(2))
                        prio = int(m.group(3)) if m.group(3) else 0
                        pred = parse_label_expr(lbl_str, self.num_aps)
                        self.transitions[curr_state].append((pred, target, prio))

def expand_and_solve(filepath):
    """
    Expands an HOA automaton into a 2-player turn-based game and solves with Zielonka.
    Turn order (Mealy reactive synthesis):
    1. At automaton state s, Environment (Player 1) chooses input valuation in_val.
       Moves to intermediate game node (s, in_val) owned by System (Player 0).
    2. At (s, in_val), System (Player 0) chooses output valuation out_val.
       Moves to next automaton state s', taking priority of the matched transition.
    """
    aut = HOAAutomaton(filepath)
    
    in_ap_list = sorted(list(aut.input_aps))
    out_ap_list = sorted(list(aut.controllable_aps))
    
    num_in = len(in_ap_list)
    num_out = len(out_ap_list)
    
    # Vertices of the turn-based game:
    # State nodes: s (0 <= s < num_states), owned by Player 1 (Env)
    # Intermediate nodes: (s, in_idx), owned by Player 0 (Sys)
    # Target node priority: on edge or on state
    
    V = set()
    edges = {}
    owner = {}
    priority = {}
    
    state_node_id = lambda s: s
    inter_node_id = lambda s, in_idx: aut.num_states + s * (1 << num_in) + in_idx
    
    for s in range(aut.num_states):
        sid = state_node_id(s)
        V.add(sid)
        owner[sid] = 1 # Environment moves first
        priority[sid] = 0
        edges[sid] = []
        
        for in_idx in range(1 << num_in):
            iid = inter_node_id(s, in_idx)
            V.add(iid)
            owner[iid] = 0 # System moves second
            priority[iid] = 0
            edges[iid] = []
            edges[sid].append(iid)
            
            # Reconstruct valuation
            # For each sys out_idx in range(1 << num_out):
            for out_idx in range(1 << num_out):
                val = [False] * aut.num_aps
                for bit, ap_idx in enumerate(in_ap_list):
                    if (in_idx & (1 << bit)):
                        val[ap_idx] = True
                for bit, ap_idx in enumerate(out_ap_list):
                    if (out_idx & (1 << bit)):
                        val[ap_idx] = True
                        
                # Match transition
                matched = False
                for pred, target, prio in aut.transitions.get(s, []):
                    if pred(val):
                        # Convert priority if max parity: bound must be even to preserve parity modulo 2
                        bound = aut.max_prio if (aut.max_prio % 2 == 0) else (aut.max_prio + 1)
                        canonical_p = (bound - prio) if aut.is_max_parity else prio
                        # Target is target state node
                        tid = state_node_id(target)
                        edges[iid].append(tid)
                        # Intermediate node inherits priority of move
                        priority[iid] = canonical_p
                        matched = True
                        break
                        
    # Textbook Zielonka import
    from benchmarks.zielonka_reference import compute_attractor, zielonka
    
    W0, W1 = zielonka(V, edges, owner, priority)
    return "REALIZABLE" if state_node_id(aut.start_state) in W0 else "UNREALIZABLE"

if __name__ == "__main__":
    test_files = [
        "benchmarks/official_parity/parity/tlsf_based/round_robin_arbiter_unreal_pb_2_pe_.tlsf.ehoa",
        "benchmarks/official_parity/parity/tlsf_based/lilydemo11.tlsf.ehoa",
        "benchmarks/official_parity/parity/tlsf_based/OneCounter.tlsf.ehoa",
        "benchmarks/official_parity/parity/tlsf_based/SensorPart.tlsf.ehoa",
    ]
    
    print("==========================================================================")
    print("  HOA EXPANDER VALIDATION ON REAL TLSF BENCHMARKS (COMPARISON WITH KNOR)")
    print("==========================================================================")
    for f in test_files:
        if os.path.exists(f):
            bname = os.path.basename(f)
            exp_res = expand_and_solve(f)
            # Run knor
            import subprocess
            p = subprocess.run(["./knor", "--real", f], capture_output=True, text=True)
            k_res = "REALIZABLE" if p.returncode == 10 else ("UNREALIZABLE" if p.returncode == 20 else f"ERR({p.returncode})")
            match = "MATCH" if exp_res == k_res else "MISMATCH"
            print(f"  {bname:45s} -> Expander: {exp_res:12s} | Knor: {k_res:12s} [{match}]")
    print("==========================================================================")
