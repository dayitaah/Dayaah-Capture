#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

struct ModeCandidate {
    uint32_t width = 0;
    uint32_t height = 0;
    double fps = 0;
    int format = 0;
};

enum class ModeAxis { Resolution, Fps, Format };

inline int SelectClosestMode(const std::vector<ModeCandidate>& modes,
                             const ModeCandidate& current,
                             const ModeCandidate& requested,
                             ModeAxis axis) {
    int best = -1;
    double bestScore = -std::numeric_limits<double>::infinity();
    for (size_t i = 0; i < modes.size(); ++i) {
        const ModeCandidate& mode = modes[i];
        bool eligible = false;
        double score = 0;
        if (axis == ModeAxis::Resolution) {
            eligible = mode.width == requested.width && mode.height == requested.height;
            score += mode.format == current.format ? 1000000.0 : 0.0;
            score -= std::fabs(mode.fps - current.fps) * 1000.0;
        } else if (axis == ModeAxis::Fps) {
            eligible = std::fabs(mode.fps - requested.fps) < 0.01;
            score += mode.width == current.width && mode.height == current.height ? 1000000.0 : 0.0;
            score += mode.format == current.format ? 500000.0 : 0.0;
            score -= std::fabs(static_cast<double>(mode.width) * mode.height -
                               static_cast<double>(current.width) * current.height) / 1000.0;
        } else {
            eligible = mode.format == requested.format;
            score += mode.width == current.width && mode.height == current.height ? 1000000.0 : 0.0;
            score -= std::fabs(mode.fps - current.fps) * 1000.0;
        }
        if (eligible && score > bestScore) {
            best = static_cast<int>(i);
            bestScore = score;
        }
    }
    return best;
}
