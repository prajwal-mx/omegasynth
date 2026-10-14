#pragma once

#include "arena.hpp"
#include <vector>

namespace omegasynth {

// Reusable workspace to eliminate O(|V|) heap allocations during attractor calls
struct AttractorWorkspace {
    std::vector<int32_t> deg;
    std::vector<uint32_t> deg_gen;
    uint32_t current_gen = 1;
    std::vector<VertexId> queue;

    void ensure_size(size_t n) {
        if (deg.size() < n) {
            deg.resize(n, 0);
            deg_gen.resize(n, 0);
            queue.reserve(n);
        }
        ++current_gen;
        if (current_gen == 0) {
            std::fill(deg_gen.begin(), deg_gen.end(), 0);
            current_gen = 1;
        }
        queue.clear();
    }
};

// High-Performance Lazy-Evaluation Attractor with Zero-Allocation Thread-Local Scratchpad
// and Optional Memoryless Strategy Recording:
// Computes Attr_P^U(X) in O(|Attr| + |E_Attr|) local time rather than O(|U| + |E_U|).
inline VertexSet compute_attractor(const Arena& arena,
                                   const VertexSet& subarena,
                                   Player p,
                                   const VertexSet& target,
                                   std::vector<VertexId>* strategy = nullptr) {
    VertexSet attr = target.intersect(subarena);
    if (attr.empty()) {
        return attr;
    }

    static thread_local AttractorWorkspace ws;
    ws.ensure_size(arena.num_vertices);
    const uint32_t gen = ws.current_gen;

    for (VertexId v : attr) {
        ws.queue.push_back(v);
    }

    size_t q_head = 0;
    while (q_head < ws.queue.size()) {
        VertexId curr = ws.queue[q_head++];

        for (VertexId pred : arena.pred[curr]) {
            if (!subarena.contains(pred) || attr.contains(pred)) {
                continue;
            }

            if (arena.vertices[pred].owner == p) {
                // P can choose to move from pred to curr in Attr
                attr.insert(pred);
                if (strategy != nullptr && p == Player::Max) {
                    (*strategy)[pred] = curr;
                }
                ws.queue.push_back(pred);
            } else {
                // Opponent node: lazy count remaining successors in subarena
                if (ws.deg_gen[pred] != gen) {
                    int32_t d = 0;
                    for (VertexId succ : arena.succ[pred]) {
                        if (subarena.contains(succ)) {
                            ++d;
                        }
                    }
                    ws.deg[pred] = d;
                    ws.deg_gen[pred] = gen;
                }
                if (--ws.deg[pred] <= 0) {
                    attr.insert(pred);
                    ws.queue.push_back(pred);
                }
            }
        }
    }

    return attr;
}

} // namespace omegasynth
