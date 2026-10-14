#pragma once

#include "arena.hpp"
#include "strategy.hpp"
#include <sstream>
#include <string>
#include <vector>
#include <unordered_map>
#include <cmath>
#include <cstdint>

namespace omegasynth {

class AIGEREmitter {
public:
    static std::string emit_aiger(const Arena& arena, const ControllerStrategy& cs, const std::string& circuit_name = "OmegaController") {
        std::vector<VertexId> winning_states;
        winning_states.reserve(cs.winning_states.count());
        // Ensure start_vertex is at index 0 if present so latches reset (all-0) to start_vertex!
        if (cs.winning_states.contains(arena.start_vertex)) {
            winning_states.push_back(arena.start_vertex);
        }
        for (VertexId v : cs.winning_states) {
            if (v != arena.start_vertex) {
                winning_states.push_back(v);
            }
        }

        if (winning_states.empty()) {
            return "aag 0 0 0 0 0\nc Empty controller\n";
        }

        size_t num_states = winning_states.size();
        std::unordered_map<VertexId, size_t> state_to_idx;
        state_to_idx.reserve(num_states);
        for (size_t i = 0; i < num_states; ++i) {
            state_to_idx[winning_states[i]] = i;
        }

        // Determine maximum out-degree of Min states in winning_states to size environment inputs
        size_t max_min_deg = 1;
        for (VertexId v : winning_states) {
            if (arena.vertices[v].owner == Player::Min) {
                size_t d = 0;
                for (VertexId w : arena.succ[v]) {
                    if (cs.winning_states.contains(w)) ++d;
                }
                if (d > max_min_deg) max_min_deg = d;
            }
        }

        size_t num_inputs = std::max<size_t>(1, arena.uncontrollable_aps.size());
        while ((1ULL << num_inputs) < max_min_deg && num_inputs < 16) {
            ++num_inputs;
        }

        size_t num_latches = 1;
        while ((1ULL << num_latches) < num_states) {
            ++num_latches;
        }

        uint32_t next_var = 1;
        std::vector<uint32_t> input_vars;
        for (size_t i = 0; i < num_inputs; ++i) {
            input_vars.push_back(next_var++);
        }

        std::vector<uint32_t> latch_vars;
        for (size_t i = 0; i < num_latches; ++i) {
            latch_vars.push_back(next_var++);
        }

        struct AndGate {
            uint32_t lhs;
            uint32_t rhs1;
            uint32_t rhs2;
        };
        std::vector<AndGate> and_gates;

        auto make_and = [&](uint32_t lit1, uint32_t lit2) -> uint32_t {
            if (lit1 == 0 || lit2 == 0) return 0;
            if (lit1 == 1) return lit2;
            if (lit2 == 1) return lit1;
            if (lit1 == lit2) return lit1;
            if ((lit1 ^ 1) == lit2) return 0;
            uint32_t out_var = next_var++;
            uint32_t out_lit = 2 * out_var;
            and_gates.push_back({out_lit, lit1, lit2});
            return out_lit;
        };

        auto make_or = [&](uint32_t lit1, uint32_t lit2) -> uint32_t {
            if (lit1 == 1 || lit2 == 1) return 1;
            if (lit1 == 0) return lit2;
            if (lit2 == 0) return lit1;
            uint32_t a = make_and(lit1 ^ 1, lit2 ^ 1);
            return a ^ 1;
        };

        // Decoder: for each state index i in 0 .. num_states-1, compute condition: current state == i
        std::vector<uint32_t> is_state(num_states, 1);
        for (size_t i = 0; i < num_states; ++i) {
            uint32_t cond = 1;
            for (size_t bit = 0; bit < num_latches; ++bit) {
                uint32_t latch_lit = 2 * latch_vars[bit];
                bool is_set = ((i >> bit) & 1) != 0;
                uint32_t bit_lit = is_set ? latch_lit : (latch_lit ^ 1);
                cond = make_and(cond, bit_lit);
            }
            is_state[i] = cond;
        }

        // Next-state functions for each latch bit
        std::vector<uint32_t> next_latch_lits(num_latches, 0);

        for (size_t i = 0; i < num_states; ++i) {
            VertexId v = winning_states[i];
            if (arena.vertices[v].owner == Player::Max) {
                VertexId next_v = cs.has_action(v) ? cs.strategy[v] : v;
                auto it = state_to_idx.find(next_v);
                size_t next_idx = (it != state_to_idx.end()) ? it->second : i;
                uint32_t active_cond = is_state[i];
                for (size_t bit = 0; bit < num_latches; ++bit) {
                    if (((next_idx >> bit) & 1) != 0) {
                        next_latch_lits[bit] = make_or(next_latch_lits[bit], active_cond);
                    }
                }
            } else {
                // Player::Min state: use input bits to select among valid winning successors
                std::vector<size_t> succ_indices;
                for (VertexId w : arena.succ[v]) {
                    auto it = state_to_idx.find(w);
                    if (it != state_to_idx.end()) {
                        succ_indices.push_back(it->second);
                    }
                }
                if (succ_indices.empty()) {
                    succ_indices.push_back(i);
                }
                if (succ_indices.size() == 1) {
                    size_t next_idx = succ_indices[0];
                    for (size_t bit = 0; bit < num_latches; ++bit) {
                        if (((next_idx >> bit) & 1) != 0) {
                            next_latch_lits[bit] = make_or(next_latch_lits[bit], is_state[i]);
                        }
                    }
                } else {
                    size_t sel_bits = 1;
                    while ((1ULL << sel_bits) < succ_indices.size() && sel_bits < num_inputs) {
                        ++sel_bits;
                    }
                    size_t num_branches = 1ULL << sel_bits;
                    for (size_t b_idx = 0; b_idx < num_branches; ++b_idx) {
                        size_t next_idx = succ_indices[b_idx % succ_indices.size()];
                        if (next_idx == 0) continue; // all-0 bits require no OR gates
                        uint32_t branch_cond = is_state[i];
                        for (size_t bit = 0; bit < sel_bits; ++bit) {
                            uint32_t in_lit = 2 * input_vars[bit];
                            bool is_set = ((b_idx >> bit) & 1) != 0;
                            branch_cond = make_and(branch_cond, is_set ? in_lit : (in_lit ^ 1));
                        }
                        for (size_t bit = 0; bit < num_latches; ++bit) {
                            if (((next_idx >> bit) & 1) != 0) {
                                next_latch_lits[bit] = make_or(next_latch_lits[bit], branch_cond);
                            }
                        }
                    }
                }
            }
        }

        // Outputs: controllable APs (if present) or current state latch bits
        size_t num_outputs = arena.controllable_aps.empty() ? num_latches : arena.controllable_aps.size();
        std::vector<uint32_t> output_lits;
        for (size_t i = 0; i < num_outputs; ++i) {
            output_lits.push_back(2 * latch_vars[i % num_latches]);
        }

        std::ostringstream ss;
        uint32_t max_var = next_var - 1;
        ss << "aag " << max_var << " " << num_inputs << " " << num_latches << " "
           << output_lits.size() << " " << and_gates.size() << "\n";

        for (uint32_t invar : input_vars) {
            ss << (2 * invar) << "\n";
        }
        for (size_t i = 0; i < num_latches; ++i) {
            ss << (2 * latch_vars[i]) << " " << next_latch_lits[i] << "\n";
        }
        for (uint32_t outlit : output_lits) {
            ss << outlit << "\n";
        }
        for (const auto& ag : and_gates) {
            ss << ag.lhs << " " << ag.rhs1 << " " << ag.rhs2 << "\n";
        }

        // Symbol table
        for (size_t i = 0; i < num_inputs; ++i) {
            if (i < arena.uncontrollable_aps.size() &&
                static_cast<size_t>(arena.uncontrollable_aps[i]) < arena.ap_names.size()) {
                ss << "i" << i << " " << arena.ap_names[arena.uncontrollable_aps[i]] << "\n";
            } else {
                ss << "i" << i << " env_in" << i << "\n";
            }
        }
        for (size_t i = 0; i < num_latches; ++i) {
            ss << "l" << i << " state_reg" << i << "\n";
        }
        for (size_t i = 0; i < output_lits.size(); ++i) {
            if (i < arena.controllable_aps.size() &&
                static_cast<size_t>(arena.controllable_aps[i]) < arena.ap_names.size()) {
                ss << "o" << i << " " << arena.ap_names[arena.controllable_aps[i]] << "\n";
            } else {
                ss << "o" << i << " out_state" << i << "\n";
            }
        }

        ss << "c\n";
        ss << "Synthesized by OmegaSynth (Result 104 Quasipolynomial Reactive Synthesizer)\n";
        ss << "Circuit: " << circuit_name << "\n";
        ss << "States: " << num_states << ", Latches: " << num_latches << "\n";

        return ss.str();
    }
};

} // namespace omegasynth
