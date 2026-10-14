#pragma once

#include "arena.hpp"
#include <fstream>
#include <sstream>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>
#include <set>
#include <map>
#include <unordered_map>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <stdexcept>

namespace omegasynth {

class HOAParser {
private:
    struct GuardNode {
        enum Type : uint8_t { CONST_TRUE, CONST_FALSE, VAR, NOT, AND, OR } type;
        int var_id = -1;
        int left = -1;
        int right = -1;
    };

    struct CompiledGuard {
        std::vector<GuardNode> nodes;
        int root = -1;
        uint32_t used_var_mask = 0;
        bool is_const_true = false;

        bool eval(uint64_t ap_valuation_mask) const {
            if (is_const_true || root < 0) return true;
            return eval_node(root, ap_valuation_mask);
        }

        bool eval_node(int idx, uint64_t mask) const {
            const GuardNode& n = nodes[idx];
            switch (n.type) {
                case GuardNode::CONST_TRUE:  return true;
                case GuardNode::CONST_FALSE: return false;
                case GuardNode::VAR:         return ((mask >> n.var_id) & 1ULL) != 0;
                case GuardNode::NOT:         return !eval_node(n.left, mask);
                case GuardNode::AND:         return eval_node(n.left, mask) && eval_node(n.right, mask);
                case GuardNode::OR:          return eval_node(n.left, mask) || eval_node(n.right, mask);
            }
            return false;
        }
    };

    static std::string expand_aliases(std::string_view expr, const std::unordered_map<std::string, std::string>& aliases) {
        std::string res(expr);
        if (aliases.empty() || res.find('@') == std::string::npos) return res;
        for (int pass = 0; pass < 8 && res.find('@') != std::string::npos; ++pass) {
            std::string next;
            next.reserve(res.size() * 2);
            size_t i = 0;
            while (i < res.size()) {
                if (res[i] == '@') {
                    size_t start = i + 1;
                    size_t end = start;
                    while (end < res.size() && (std::isalnum(static_cast<unsigned char>(res[end])) || res[end] == '_')) {
                        ++end;
                    }
                    std::string aname = res.substr(start, end - start);
                    auto it = aliases.find(aname);
                    if (it != aliases.end()) {
                        next += "(" + it->second + ")";
                    } else {
                        next += res.substr(i, end - i);
                    }
                    i = end;
                } else {
                    next.push_back(res[i++]);
                }
            }
            res = std::move(next);
        }
        return res;
    }

    struct GuardParser {
        const std::string& s;
        size_t pos = 0;
        CompiledGuard& cg;

        GuardParser(const std::string& str, CompiledGuard& out) : s(str), pos(0), cg(out) {}

        void skip_ws() {
            while (pos < s.size() && (s[pos] == ' ' || s[pos] == '\t' || s[pos] == '[' || s[pos] == ']')) {
                ++pos;
            }
        }

        int parse_or() {
            int lhs = parse_and();
            while (true) {
                skip_ws();
                if (pos < s.size() && s[pos] == '|') {
                    ++pos;
                    int rhs = parse_and();
                    int id = static_cast<int>(cg.nodes.size());
                    cg.nodes.push_back({GuardNode::OR, -1, lhs, rhs});
                    lhs = id;
                } else {
                    break;
                }
            }
            return lhs;
        }

        int parse_and() {
            int lhs = parse_unary();
            while (true) {
                skip_ws();
                if (pos < s.size() && s[pos] == '&') {
                    ++pos;
                    int rhs = parse_unary();
                    int id = static_cast<int>(cg.nodes.size());
                    cg.nodes.push_back({GuardNode::AND, -1, lhs, rhs});
                    lhs = id;
                } else {
                    break;
                }
            }
            return lhs;
        }

        int parse_unary() {
            skip_ws();
            if (pos < s.size() && s[pos] == '!') {
                ++pos;
                int child = parse_unary();
                int id = static_cast<int>(cg.nodes.size());
                cg.nodes.push_back({GuardNode::NOT, -1, child, -1});
                return id;
            }
            if (pos < s.size() && s[pos] == '(') {
                ++pos;
                int inner = parse_or();
                skip_ws();
                if (pos < s.size() && s[pos] == ')') ++pos;
                return inner;
            }
            if (pos < s.size() && s[pos] == 't') {
                ++pos;
                int id = static_cast<int>(cg.nodes.size());
                cg.nodes.push_back({GuardNode::CONST_TRUE, -1, -1, -1});
                return id;
            }
            if (pos < s.size() && s[pos] == 'f') {
                ++pos;
                int id = static_cast<int>(cg.nodes.size());
                cg.nodes.push_back({GuardNode::CONST_FALSE, -1, -1, -1});
                return id;
            }
            if (pos < s.size() && std::isdigit(static_cast<unsigned char>(s[pos]))) {
                int var = 0;
                while (pos < s.size() && std::isdigit(static_cast<unsigned char>(s[pos]))) {
                    var = var * 10 + (s[pos] - '0');
                    ++pos;
                }
                if (var < 32) cg.used_var_mask |= (1U << var);
                int id = static_cast<int>(cg.nodes.size());
                cg.nodes.push_back({GuardNode::VAR, var, -1, -1});
                return id;
            }
            int id = static_cast<int>(cg.nodes.size());
            cg.nodes.push_back({GuardNode::CONST_TRUE, -1, -1, -1});
            return id;
        }
    };

    static CompiledGuard compile_guard(std::string_view raw_guard, const std::unordered_map<std::string, std::string>& aliases) {
        CompiledGuard cg;
        if (raw_guard == "t" || raw_guard == "[t]" || raw_guard.empty()) {
            cg.is_const_true = true;
            return cg;
        }
        std::string exp = expand_aliases(raw_guard, aliases);
        GuardParser gp(exp, cg);
        cg.root = gp.parse_or();
        if (cg.nodes.size() == 1 && cg.nodes[0].type == GuardNode::CONST_TRUE) {
            cg.is_const_true = true;
        }
        return cg;
    }

public:
    static Arena parse(const std::string& filename) {
        // Read entire file into memory in a single bulk read syscall
        std::ifstream file(filename, std::ios::binary | std::ios::ate);
        if (!file.is_open()) {
            throw std::runtime_error("Could not open HOA file: " + filename);
        }
        std::streamsize fsize = file.tellg();
        file.seekg(0, std::ios::beg);
        std::string buffer(static_cast<size_t>(fsize), '\0');
        if (fsize > 0 && !file.read(buffer.data(), fsize)) {
            throw std::runtime_error("Failed to read HOA file: " + filename);
        }

        size_t num_states = 0;
        VertexId start_state = 0;
        size_t num_aps = 0;
        std::vector<std::string> ap_names;
        std::set<int> controllable_aps;
        std::unordered_map<std::string, std::string> aliases;

        bool in_body = false;
        bool is_parity = false;
        bool is_parity_max = false;
        bool is_parity_odd = false;
        bool is_buchi = false;
        bool is_cobuchi = false;
        bool is_rabin = false;
        bool is_gen_buchi = false;
        bool is_colored = false;
        size_t gen_buchi_k = 1;
        Priority header_prio_bound = 0;

        const char* cur = buffer.data();
        const char* const end = buffer.data() + buffer.size();

        // 1. Parse HOA Header via fast pointer scan
        while (cur < end) {
            const char* nl = static_cast<const char*>(std::memchr(cur, '\n', static_cast<size_t>(end - cur)));
            const char* line_end = nl ? nl : end;

            while (cur < line_end && (*cur == ' ' || *cur == '\t' || *cur == '\r')) ++cur;
            const char* trim_end = line_end;
            while (trim_end > cur && (trim_end[-1] == ' ' || trim_end[-1] == '\t' || trim_end[-1] == '\r')) --trim_end;

            std::string_view line(cur, static_cast<size_t>(trim_end - cur));
            cur = nl ? (nl + 1) : end;

            if (line.empty()) continue;

            if (line == "--BODY--") {
                in_body = true;
                break;
            } else if (line.starts_with("States:")) {
                const char* p = line.data() + 7;
                const char* pe = line.data() + line.size();
                while (p < pe && (*p < '0' || *p > '9')) ++p;
                num_states = 0;
                while (p < pe && *p >= '0' && *p <= '9') {
                    num_states = num_states * 10 + static_cast<size_t>(*p - '0');
                    ++p;
                }
            } else if (line.starts_with("Start:")) {
                const char* p = line.data() + 6;
                const char* pe = line.data() + line.size();
                while (p < pe && (*p < '0' || *p > '9')) ++p;
                start_state = 0;
                while (p < pe && *p >= '0' && *p <= '9') {
                    start_state = start_state * 10 + static_cast<VertexId>(*p - '0');
                    ++p;
                }
            } else if (line.starts_with("AP:")) {
                std::istringstream iss(std::string(line.substr(3)));
                iss >> num_aps;
                std::string tok;
                while (iss >> tok) {
                    if (tok.size() >= 2 && tok.front() == '"' && tok.back() == '"') {
                        ap_names.push_back(tok.substr(1, tok.size() - 2));
                    } else {
                        ap_names.push_back(tok);
                    }
                }
            } else if (line.starts_with("Alias:")) {
                std::istringstream iss(std::string(line.substr(6)));
                std::string aname;
                iss >> aname;
                if (!aname.empty() && aname.front() == '@') {
                    aname = aname.substr(1);
                }
                std::string rest;
                std::getline(iss, rest);
                size_t rs = rest.find_first_not_of(" \t");
                if (rs != std::string::npos) rest = rest.substr(rs);
                aliases[aname] = rest;
            } else if (line.starts_with("properties:")) {
                if (line.find("colored") != std::string_view::npos) {
                    is_colored = true;
                }
            } else if (line.starts_with("acc-name:")) {
                std::string_view acc = line.substr(9);
                if (acc.find("generalized-Buchi") != std::string_view::npos ||
                    acc.find("generalized-buchi") != std::string_view::npos) {
                    is_gen_buchi = true;
                    std::istringstream iss{std::string(acc)};
                    std::string w;
                    while (iss >> w) {
                        if (std::isdigit(static_cast<unsigned char>(w[0]))) {
                            gen_buchi_k = std::max<size_t>(1, std::stoul(w));
                        }
                    }
                } else if (acc.find("co-Buchi") != std::string_view::npos ||
                           acc.find("co-buchi") != std::string_view::npos) {
                    is_cobuchi = true;
                } else if (acc.find("Buchi") != std::string_view::npos ||
                           acc.find("buchi") != std::string_view::npos) {
                    is_buchi = true;
                } else if (acc.find("Rabin") != std::string_view::npos ||
                           acc.find("rabin") != std::string_view::npos) {
                    is_rabin = true;
                } else if (acc.find("parity") != std::string_view::npos) {
                    is_parity = true;
                    is_parity_max = (acc.find("max") != std::string_view::npos);
                    is_parity_odd = (acc.find("odd") != std::string_view::npos);
                    std::istringstream iss{std::string(acc)};
                    std::string word;
                    while (iss >> word) {
                        if (std::isdigit(static_cast<unsigned char>(word[0]))) {
                            try {
                                header_prio_bound = static_cast<Priority>(std::stoul(word));
                            } catch (...) {}
                        }
                    }
                }
            } else if (line.starts_with("controllable-AP:")) {
                const char* p = line.data() + 16;
                const char* pe = line.data() + line.size();
                while (p < pe) {
                    while (p < pe && (*p < '0' || *p > '9')) ++p;
                    if (p < pe) {
                        int ap = 0;
                        while (p < pe && *p >= '0' && *p <= '9') {
                            ap = ap * 10 + (*p - '0');
                            ++p;
                        }
                        controllable_aps.insert(ap);
                    }
                }
            }
        }

        if (!in_body) {
            throw std::runtime_error("Invalid HOA file: missing --BODY-- header");
        }

        uint32_t ctrl_mask = 0;
        std::vector<int> ctrl_ap_vec;
        std::vector<int> unctrl_ap_vec;
        for (size_t i = 0; i < num_aps; ++i) {
            if (controllable_aps.count(static_cast<int>(i))) {
                if (i < 32) ctrl_mask |= (1U << i);
                ctrl_ap_vec.push_back(static_cast<int>(i));
            } else {
                unctrl_ap_vec.push_back(static_cast<int>(i));
            }
        }

        // Quick probe of body to see if this is an explicit turn-based parity game:
        // In SYNTCOMP, explicit games either have "colored" property (all priorities on State: lines)
        // or APs named "pl0_0", or no edge braces on non-Buchi games and no mixed controllable/uncontrollable AP guards.
        bool is_explicit_game = false;
        if (!is_rabin && !is_gen_buchi && !is_parity_odd && aliases.empty()) {
            if (is_colored || num_aps == 0 || (!ap_names.empty() && ap_names[0].starts_with("pl0"))) {
                is_explicit_game = true;
            } else {
                // Quick scan of first few transitions in body to check if edge braces {p} or mixed AP guards appear
                const char* p_scan = cur;
                bool found_edge_brace = false;
                bool found_mixed_ap = false;
                uint32_t state_ctrl = 0, state_unctrl = 0;
                int lines_checked = 0;
                while (p_scan < end && lines_checked < 128) {
                    const char* nl = static_cast<const char*>(std::memchr(p_scan, '\n', static_cast<size_t>(end - p_scan)));
                    const char* le = nl ? nl : end;
                    while (p_scan < le && (*p_scan == ' ' || *p_scan == '\t' || *p_scan == '\r')) ++p_scan;
                    if (le - p_scan >= 7 && std::memcmp(p_scan, "--END--", 7) == 0) break;
                    if (le - p_scan >= 6 && std::memcmp(p_scan, "State:", 6) == 0) {
                        state_ctrl = 0;
                        state_unctrl = 0;
                    } else if (p_scan < le) {
                        if (*p_scan == '[') {
                            const char* ce = static_cast<const char*>(std::memchr(p_scan + 1, ']', static_cast<size_t>(le - (p_scan + 1))));
                            if (ce) {
                                const char* gp = p_scan + 1;
                                while (gp < ce) {
                                    if (*gp >= '0' && *gp <= '9') {
                                        int ap = 0;
                                        while (gp < ce && *gp >= '0' && *gp <= '9') {
                                            ap = ap * 10 + (*gp - '0');
                                            ++gp;
                                        }
                                        if (ap < 32 && (ctrl_mask & (1U << ap))) state_ctrl = 1;
                                        else state_unctrl = 1;
                                    } else {
                                        ++gp;
                                    }
                                }
                                if (state_ctrl && state_unctrl) {
                                    found_mixed_ap = true;
                                    break;
                                }
                                if (!is_buchi && std::memchr(ce + 1, '{', static_cast<size_t>(le - (ce + 1))) != nullptr) {
                                    found_edge_brace = true;
                                    break;
                                }
                            }
                        }
                    }
                    p_scan = nl ? (nl + 1) : end;
                    ++lines_checked;
                }
                if (!found_edge_brace && !found_mixed_ap) {
                    is_explicit_game = true;
                }
            }
        }

        // =========================================================================
        // ULTRA-FAST PATH: Zero-Allocation Pointer Scan for Explicit Parity Games
        // =========================================================================
        if (is_explicit_game) {
            Arena arena(num_states);
            arena.start_vertex = start_state;
            arena.ap_names = std::move(ap_names);
            arena.controllable_aps = std::move(ctrl_ap_vec);
            arena.uncontrollable_aps = std::move(unctrl_ap_vec);

            Priority def_prio = is_buchi ? 1 : 0;
            for (size_t i = 0; i < num_states; ++i) {
                arena.vertices[i] = Vertex{static_cast<VertexId>(i), Player::Max, def_prio, 0, ""};
            }

            VertexId current_state = 0;
            bool state_owner_resolved = false;
            Priority M = (header_prio_bound % 2 == 0) ? header_prio_bound : (header_prio_bound + 1);

            while (cur < end) {
                const char* nl = static_cast<const char*>(std::memchr(cur, '\n', static_cast<size_t>(end - cur)));
                const char* le = nl ? nl : end;

                while (cur < le && (*cur == ' ' || *cur == '\t' || *cur == '\r')) ++cur;
                const char* p = cur;
                cur = nl ? (nl + 1) : end;
                if (p >= le) continue;

                if (le - p >= 7 && std::memcmp(p, "--END--", 7) == 0) {
                    break;
                }

                if (le - p >= 6 && std::memcmp(p, "State:", 6) == 0) {
                    p += 6;
                    while (p < le && (*p == ' ' || *p == '\t')) ++p;
                    current_state = 0;
                    while (p < le && *p >= '0' && *p <= '9') {
                        current_state = current_state * 10 + static_cast<VertexId>(*p - '0');
                        ++p;
                    }
                    if (current_state >= arena.num_vertices) {
                        arena.add_vertex(current_state, Player::Max, def_prio, 0);
                    }
                    state_owner_resolved = false;

                    // Check label "..."
                    const char* q1 = static_cast<const char*>(std::memchr(p, '"', static_cast<size_t>(le - p)));
                    if (q1) {
                        const char* lstart = q1 + 1;
                        if (le - lstart >= 2) {
                            if ((lstart[0] == 'R' && lstart[1] == '(') ||
                                (le - lstart >= 3 && std::memcmp(lstart, "env", 3) == 0) ||
                                (le - lstart >= 3 && std::memcmp(lstart, "Min", 3) == 0)) {
                                arena.vertices[current_state].owner = Player::Min;
                                state_owner_resolved = true;
                            } else if ((lstart[0] == 'L' && lstart[1] == '(') ||
                                       (le - lstart >= 3 && std::memcmp(lstart, "sys", 3) == 0) ||
                                       (le - lstart >= 3 && std::memcmp(lstart, "Max", 3) == 0)) {
                                arena.vertices[current_state].owner = Player::Max;
                                state_owner_resolved = true;
                            }
                        }
                        const char* q2 = static_cast<const char*>(std::memchr(lstart, '"', static_cast<size_t>(le - lstart)));
                        if (q2) p = q2 + 1;
                    }

                    // Check state priority {prio}
                    const char* b1 = static_cast<const char*>(std::memchr(p, '{', static_cast<size_t>(le - p)));
                    if (b1) {
                        const char* bp = b1 + 1;
                        while (bp < le && (*bp < '0' || *bp > '9') && *bp != '}') ++bp;
                        if (bp < le && *bp >= '0' && *bp <= '9') {
                            Priority pr = 0;
                            while (bp < le && *bp >= '0' && *bp <= '9') {
                                pr = pr * 10 + static_cast<Priority>(*bp - '0');
                                ++bp;
                            }
                            if (is_buchi) {
                                pr = 0; // accepting in min-even
                            } else if (is_parity_max && M >= pr) {
                                pr = M - pr;
                            }
                            arena.vertices[current_state].priority = pr;
                        }
                    }
                } else {
                    // Transition line: [guard] target [{acc}]
                    if (*p == '[') {
                        const char* c_end = static_cast<const char*>(std::memchr(p + 1, ']', static_cast<size_t>(le - (p + 1))));
                        if (c_end) {
                            // Only inspect the VERY FIRST guard of a state if its owner wasn't already resolved by its label!
                            if (!state_owner_resolved && !controllable_aps.empty()) {
                                const char* gp = p + 1;
                                while (gp < c_end) {
                                    if (*gp >= '0' && *gp <= '9') {
                                        int ap = 0;
                                        while (gp < c_end && *gp >= '0' && *gp <= '9') {
                                            ap = ap * 10 + (*gp - '0');
                                            ++gp;
                                        }
                                        if (ap >= 32 || (ctrl_mask & (1U << ap)) == 0) {
                                            arena.vertices[current_state].owner = Player::Min;
                                        }
                                        state_owner_resolved = true;
                                        break;
                                    } else {
                                        ++gp;
                                    }
                                }
                            }
                            p = c_end + 1;
                        }
                    }

                    while (p < le) {
                        while (p < le && (*p == ' ' || *p == '\t')) ++p;
                        if (p < le && *p >= '0' && *p <= '9') {
                            VertexId target = 0;
                            while (p < le && *p >= '0' && *p <= '9') {
                                target = target * 10 + static_cast<VertexId>(*p - '0');
                                ++p;
                            }
                            arena.add_edge(current_state, target);
                        } else {
                            break;
                        }
                    }

                    if (is_buchi && std::memchr(p, '{', static_cast<size_t>(le - p)) != nullptr) {
                        arena.vertices[current_state].priority = 0;
                    }
                }
            }

            return arena;
        }

        // =========================================================================
        // SYMBOLIC MEALY REACTIVE AUTOMATON EXPANDER (TLSF / eHOA Turn-Based Game)
        // =========================================================================
        struct RawEdge {
            std::string_view guard_sv;
            VertexId target = 0;
            uint32_t acc_mask = 0;
            Priority raw_prio = 0;
            bool has_edge_acc = false;
        };
        struct RawState {
            VertexId id = 0;
            std::string name;
            Priority raw_prio = 0;
            uint32_t state_acc_mask = 0;
            bool has_state_acc = false;
            std::vector<RawEdge> edges;
        };

        std::vector<RawState> states(num_states);
        for (size_t i = 0; i < num_states; ++i) {
            states[i].id = static_cast<VertexId>(i);
            states[i].name = "s" + std::to_string(i);
        }

        VertexId current_state = 0;
        Priority max_seen_prio = header_prio_bound;

        while (cur < end) {
            const char* nl = static_cast<const char*>(std::memchr(cur, '\n', static_cast<size_t>(end - cur)));
            const char* le = nl ? nl : end;

            while (cur < le && (*cur == ' ' || *cur == '\t' || *cur == '\r')) ++cur;
            const char* p = cur;
            cur = nl ? (nl + 1) : end;
            if (p >= le) continue;

            if (le - p >= 7 && std::memcmp(p, "--END--", 7) == 0) {
                break;
            }

            if (le - p >= 6 && std::memcmp(p, "State:", 6) == 0) {
                p += 6;
                while (p < le && (*p == ' ' || *p == '\t')) ++p;
                current_state = 0;
                while (p < le && *p >= '0' && *p <= '9') {
                    current_state = current_state * 10 + static_cast<VertexId>(*p - '0');
                    ++p;
                }
                if (current_state >= states.size()) {
                    size_t old_sz = states.size();
                    states.resize(current_state + 1);
                    for (size_t k = old_sz; k < states.size(); ++k) {
                        states[k].id = static_cast<VertexId>(k);
                        states[k].name = "s" + std::to_string(k);
                    }
                }
                RawState& st = states[current_state];

                const char* b1 = static_cast<const char*>(std::memchr(p, '{', static_cast<size_t>(le - p)));
                if (b1) {
                    const char* b2 = static_cast<const char*>(std::memchr(b1 + 1, '}', static_cast<size_t>(le - (b1 + 1))));
                    if (b2) {
                        st.has_state_acc = true;
                        const char* bp = b1 + 1;
                        while (bp < b2) {
                            while (bp < b2 && (*bp < '0' || *bp > '9')) ++bp;
                            if (bp < b2) {
                                Priority val = 0;
                                while (bp < b2 && *bp >= '0' && *bp <= '9') {
                                    val = val * 10 + static_cast<Priority>(*bp - '0');
                                    ++bp;
                                }
                                st.raw_prio = val;
                                if (val < 32) st.state_acc_mask |= (1U << val);
                                if (val > max_seen_prio) max_seen_prio = val;
                            }
                        }
                    }
                }
            } else {
                RawState& st = states[current_state];
                RawEdge edge;

                if (*p == '[') {
                    const char* c_end = static_cast<const char*>(std::memchr(p + 1, ']', static_cast<size_t>(le - (p + 1))));
                    if (c_end) {
                        edge.guard_sv = std::string_view(p + 1, static_cast<size_t>(c_end - (p + 1)));
                        p = c_end + 1;
                    }
                }

                std::vector<VertexId> targets;
                while (p < le) {
                    while (p < le && (*p == ' ' || *p == '\t')) ++p;
                    if (p < le && *p >= '0' && *p <= '9') {
                        VertexId tgt = 0;
                        while (p < le && *p >= '0' && *p <= '9') {
                            tgt = tgt * 10 + static_cast<VertexId>(*p - '0');
                            ++p;
                        }
                        targets.push_back(tgt);
                        if (tgt >= states.size()) {
                            size_t old_sz = states.size();
                            states.resize(tgt + 1);
                            for (size_t k = old_sz; k < states.size(); ++k) {
                                states[k].id = static_cast<VertexId>(k);
                                states[k].name = "s" + std::to_string(k);
                            }
                        }
                    } else {
                        break;
                    }
                }

                const char* eb1 = static_cast<const char*>(std::memchr(p, '{', static_cast<size_t>(le - p)));
                if (eb1) {
                    const char* eb2 = static_cast<const char*>(std::memchr(eb1 + 1, '}', static_cast<size_t>(le - (eb1 + 1))));
                    if (eb2) {
                        edge.has_edge_acc = true;
                        const char* bp = eb1 + 1;
                        while (bp < eb2) {
                            while (bp < eb2 && (*bp < '0' || *bp > '9')) ++bp;
                            if (bp < eb2) {
                                Priority val = 0;
                                while (bp < eb2 && *bp >= '0' && *bp <= '9') {
                                    val = val * 10 + static_cast<Priority>(*bp - '0');
                                    ++bp;
                                }
                                edge.raw_prio = val;
                                if (val < 32) edge.acc_mask |= (1U << val);
                                if (val > max_seen_prio) max_seen_prio = val;
                            }
                        }
                    }
                }

                for (VertexId tgt : targets) {
                    RawEdge e_copy = edge;
                    e_copy.target = tgt;
                    st.edges.push_back(e_copy);
                }
            }
        }

        Priority M_even = (max_seen_prio % 2 == 0) ? max_seen_prio : (max_seen_prio + 1);
        Priority M_odd  = (max_seen_prio % 2 != 0) ? max_seen_prio : (max_seen_prio + 1);

        auto normalize_parity_prio = [&](Priority raw_p) -> Priority {
            if (is_parity_max) {
                return is_parity_odd ? (M_odd - raw_p) : (M_even - raw_p);
            } else {
                return is_parity_odd ? (raw_p + 1) : raw_p;
            }
        };

        size_t layers = is_gen_buchi ? gen_buchi_k : 1;
        size_t num_base_states = states.size() * layers;

        std::vector<std::vector<CompiledGuard>> compiled_guards(states.size());
        for (size_t s = 0; s < states.size(); ++s) {
            compiled_guards[s].reserve(states[s].edges.size());
            for (const auto& e : states[s].edges) {
                compiled_guards[s].push_back(compile_guard(e.guard_sv, aliases));
            }
        }

        Priority neutral_prio = 2;
        if (is_parity) {
            Priority max_norm = std::max(normalize_parity_prio(0), normalize_parity_prio(max_seen_prio));
            neutral_prio = (max_norm % 2 == 0) ? max_norm : (max_norm + 1);
        } else if (is_rabin) {
            neutral_prio = 4;
        }

        Arena arena(num_base_states);
        arena.start_vertex = start_state * static_cast<VertexId>(layers);
        arena.ap_names = std::move(ap_names);
        arena.controllable_aps = ctrl_ap_vec;
        arena.uncontrollable_aps = unctrl_ap_vec;

        for (size_t s = 0; s < states.size(); ++s) {
            for (size_t r = 0; r < layers; ++r) {
                VertexId sid = static_cast<VertexId>(s * layers + r);
                arena.add_vertex(sid, Player::Min, neutral_prio, 0, states[s].name);
            }
        }

        size_t num_u = unctrl_ap_vec.size();
        size_t num_c = ctrl_ap_vec.size();
        size_t u_combos = 1ULL << num_u;
        size_t c_combos = 1ULL << num_c;

        std::vector<uint64_t> u_masks(u_combos, 0);
        for (size_t u_idx = 0; u_idx < u_combos; ++u_idx) {
            uint64_t m = 0;
            for (size_t b = 0; b < num_u; ++b) {
                if ((u_idx >> b) & 1ULL) m |= (1ULL << unctrl_ap_vec[b]);
            }
            u_masks[u_idx] = m;
        }

        std::vector<uint64_t> c_masks(c_combos, 0);
        for (size_t c_idx = 0; c_idx < c_combos; ++c_idx) {
            uint64_t m = 0;
            for (size_t b = 0; b < num_c; ++b) {
                if ((c_idx >> b) & 1ULL) m |= (1ULL << ctrl_ap_vec[b]);
            }
            c_masks[c_idx] = m;
        }

        std::unordered_map<uint64_t, VertexId> relay_cache;
        auto get_relay_vertex = [&](VertexId tgt_base_id, Priority prio) -> VertexId {
            uint64_t key = (static_cast<uint64_t>(tgt_base_id) << 32) | static_cast<uint64_t>(prio);
            auto it = relay_cache.find(key);
            if (it != relay_cache.end()) return it->second;
            VertexId rid = static_cast<VertexId>(arena.num_vertices);
            arena.add_vertex(rid, Player::Min, prio, 0, "relay_" + std::to_string(rid));
            arena.add_edge(rid, tgt_base_id);
            relay_cache[key] = rid;
            return rid;
        };

        auto compute_step_outcome = [&](size_t s, size_t r, const RawEdge& e) -> std::pair<VertexId, Priority> {
            uint32_t mask = states[s].state_acc_mask | e.acc_mask;
            Priority raw_p = e.has_edge_acc ? e.raw_prio : states[s].raw_prio;

            if (is_gen_buchi) {
                size_t next_r = r;
                bool wrapped = false;
                for (size_t step = 0; step < gen_buchi_k; ++step) {
                    if (mask & (1U << next_r)) {
                        next_r = (next_r + 1) % gen_buchi_k;
                        if (next_r == 0) wrapped = true;
                    } else {
                        break;
                    }
                }
                return {static_cast<VertexId>(e.target * layers + next_r), wrapped ? 0U : 1U};
            } else if (is_buchi) {
                bool acc = states[s].has_state_acc || e.has_edge_acc;
                return {static_cast<VertexId>(e.target), acc ? 0U : 1U};
            } else if (is_cobuchi) {
                bool fin_hit = states[s].has_state_acc || e.has_edge_acc;
                return {static_cast<VertexId>(e.target), fin_hit ? 1U : 2U};
            } else if (is_rabin) {
                Priority p = 3;
                if (mask & 1U) p = 1;
                else if (mask & 2U) p = 2;
                return {static_cast<VertexId>(e.target), p};
            } else if (is_parity) {
                return {static_cast<VertexId>(e.target), normalize_parity_prio(raw_p)};
            } else {
                return {static_cast<VertexId>(e.target), raw_p};
            }
        };

        for (size_t s = 0; s < states.size(); ++s) {
            const auto& st_edges = states[s].edges;
            const auto& st_guards = compiled_guards[s];

            for (size_t r = 0; r < layers; ++r) {
                VertexId sid = static_cast<VertexId>(s * layers + r);
                std::map<std::vector<VertexId>, VertexId> choice_set_to_inter_node;

                for (size_t u_idx = 0; u_idx < u_combos; ++u_idx) {
                    uint64_t u_mask = u_masks[u_idx];
                    std::vector<VertexId> sys_choices;
                    sys_choices.reserve(c_combos);

                    for (size_t c_idx = 0; c_idx < c_combos; ++c_idx) {
                        uint64_t full_mask = u_mask | c_masks[c_idx];
                        for (size_t e_idx = 0; e_idx < st_edges.size(); ++e_idx) {
                            if (st_guards[e_idx].eval(full_mask)) {
                                auto [tgt_base, prio] = compute_step_outcome(s, r, st_edges[e_idx]);
                                VertexId relay_v = get_relay_vertex(tgt_base, prio);
                                sys_choices.push_back(relay_v);
                                break;
                            }
                        }
                    }

                    std::sort(sys_choices.begin(), sys_choices.end());
                    sys_choices.erase(std::unique(sys_choices.begin(), sys_choices.end()), sys_choices.end());

                    auto it = choice_set_to_inter_node.find(sys_choices);
                    if (it != choice_set_to_inter_node.end()) {
                        arena.add_edge(sid, it->second);
                    } else {
                        VertexId inter_id = static_cast<VertexId>(arena.num_vertices);
                        arena.add_vertex(inter_id, Player::Max, neutral_prio, 0, "u");
                        arena.add_edge(sid, inter_id);
                        for (VertexId rv : sys_choices) {
                            arena.add_edge(inter_id, rv);
                        }
                        choice_set_to_inter_node[sys_choices] = inter_id;
                    }
                }
            }
        }

        return arena;
    }
};

} // namespace omegasynth
