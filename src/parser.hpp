#pragma once

#include "arena.hpp"
#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>

namespace omegasynth {

class GameParser {
public:
    static Arena parse_pgsolver(const std::string& filename, bool max_parity = false) {
        std::ifstream file(filename);
        if (!file.is_open()) {
            throw std::runtime_error("Could not open file: " + filename);
        }

        std::string line;
        size_t max_id = 0;
        Priority max_prio = 0;

        struct NodeData {
            VertexId id;
            Priority priority;
            Player owner;
            Reward reward = 0;
            std::vector<VertexId> succs;
            std::string name;
        };

        std::vector<NodeData> nodes;

        while (std::getline(file, line)) {
            size_t start = line.find_first_not_of(" \t\r\n");
            if (start == std::string::npos) continue;
            line = line.substr(start);

            if (line.starts_with("parity")) {
                continue;
            }

            std::istringstream iss(line);
            VertexId id;
            Priority prio;
            int owner_int;
            std::string succs_str;

            if (!(iss >> id >> prio >> owner_int >> succs_str)) {
                continue;
            }

            NodeData nd;
            nd.id = id;
            nd.priority = prio;
            if (prio > max_prio) max_prio = prio;
            nd.owner = (owner_int == 0) ? Player::Max : Player::Min;

            std::stringstream ss(succs_str);
            std::string succ_item;
            while (std::getline(ss, succ_item, ',')) {
                if (!succ_item.empty() && succ_item.back() == ';') succ_item.pop_back();
                if (!succ_item.empty()) {
                    nd.succs.push_back(static_cast<VertexId>(std::stoul(succ_item)));
                }
            }

            std::string rem;
            if (iss >> rem) {
                if (rem.front() == '"') {
                    size_t quote_end = rem.find('"', 1);
                    if (quote_end != std::string::npos) {
                        nd.name = rem.substr(1, quote_end - 1);
                    } else {
                        nd.name = rem;
                    }
                    Reward r;
                    if (iss >> r) {
                        nd.reward = r;
                    }
                } else {
                    if (!rem.empty() && rem.back() == ';') rem.pop_back();
                    try {
                        nd.reward = std::stoll(rem);
                    } catch (...) {
                        nd.name = rem;
                    }
                }
            }

            if (id > max_id) max_id = id;
            for (VertexId s : nd.succs) {
                if (s > max_id) max_id = s;
            }
            nodes.push_back(nd);
        }

        Priority B = (max_prio % 2 == 0) ? max_prio : (max_prio + 1);
        Arena arena(max_id + 1);
        for (const auto& nd : nodes) {
            Priority p = max_parity ? (B - nd.priority) : nd.priority;
            arena.add_vertex(nd.id, nd.owner, p, nd.reward, nd.name);
            for (VertexId s : nd.succs) {
                arena.add_edge(nd.id, s);
            }
        }

        return arena;
    }
};

} // namespace omegasynth
