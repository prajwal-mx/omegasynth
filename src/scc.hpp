#pragma once

#include "arena.hpp"
#include "attractor.hpp"
#include <vector>
#include <algorithm>

namespace omegasynth {

struct SCCResult {
    // List of SCCs, where each SCC is a list of VertexIds.
    // Returned in reverse topological order (leaves/bottom SCCs first!).
    std::vector<std::vector<VertexId>> components;
};

// Non-recursive, iterative Tarjan's SCC algorithm.
// Safely handles graphs with millions of states and deep chains (e.g. 100,000-state ladders)
// without risk of C++ call stack overflow.
class TarjanSCC {
private:
    const Arena& arena_;
    const VertexSet& subarena_;
    std::vector<int> index_;
    std::vector<int> lowlink_;
    std::vector<bool> on_stack_;
    std::vector<VertexId> tarjan_stack_;
    int current_index_ = 0;
    SCCResult result_;

    struct DfsFrame {
        VertexId u;
        size_t next_edge_idx;
    };

    void iterative_strongconnect(VertexId root) {
        std::vector<DfsFrame> dfs_stack;
        dfs_stack.reserve(256);

        index_[root] = current_index_;
        lowlink_[root] = current_index_;
        ++current_index_;
        tarjan_stack_.push_back(root);
        on_stack_[root] = true;
        dfs_stack.push_back({root, 0});

        while (!dfs_stack.empty()) {
            DfsFrame& frame = dfs_stack.back();
            VertexId u = frame.u;
            const auto& succs = arena_.succ[u];

            bool pushed_child = false;
            while (frame.next_edge_idx < succs.size()) {
                VertexId w = succs[frame.next_edge_idx++];
                if (!subarena_.contains(w)) continue;

                if (index_[w] == -1) {
                    // Unvisited child: push new frame
                    index_[w] = current_index_;
                    lowlink_[w] = current_index_;
                    ++current_index_;
                    tarjan_stack_.push_back(w);
                    on_stack_[w] = true;

                    dfs_stack.push_back({w, 0});
                    pushed_child = true;
                    break;
                } else if (on_stack_[w]) {
                    lowlink_[u] = std::min(lowlink_[u], index_[w]);
                }
            }

            if (pushed_child) {
                continue;
            }

            // All successors of u have been processed
            // If u was a child of some parent, update parent's lowlink
            if (dfs_stack.size() > 1) {
                VertexId parent = dfs_stack[dfs_stack.size() - 2].u;
                lowlink_[parent] = std::min(lowlink_[parent], lowlink_[u]);
            }

            // Check if u is the root of an SCC
            if (lowlink_[u] == index_[u]) {
                std::vector<VertexId> scc;
                while (true) {
                    VertexId top_v = tarjan_stack_.back();
                    tarjan_stack_.pop_back();
                    on_stack_[top_v] = false;
                    scc.push_back(top_v);
                    if (top_v == u) break;
                }
                result_.components.push_back(std::move(scc));
            }

            dfs_stack.pop_back();
        }
    }

public:
    TarjanSCC(const Arena& arena, const VertexSet& subarena)
        : arena_(arena), subarena_(subarena),
          index_(arena.num_vertices, -1),
          lowlink_(arena.num_vertices, -1),
          on_stack_(arena.num_vertices, false) {}

    SCCResult run() {
        result_.components.clear();
        current_index_ = 0;
        tarjan_stack_.clear();

        for (VertexId v : subarena_) {
            if (index_[v] == -1) {
                iterative_strongconnect(v);
            }
        }
        return result_;
    }
};

} // namespace omegasynth
