// PCG32 random stream. Each subsystem owns its own stream so that changing one
// system does not shift every other system's random sequence.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace sim {

class Rng {
public:
    Rng() : Rng(0x853c49e6748fea9bULL, 0xda3e39cb94b95bdbULL) {}
    Rng(uint64_t seed, uint64_t stream) { reseed(seed, stream); }

    void reseed(uint64_t seed, uint64_t stream) {
        state_ = 0;
        inc_ = (stream << 1u) | 1u;
        next();
        state_ += seed;
        next();
    }

    uint32_t next() {
        uint64_t old = state_;
        state_ = old * 6364136223846793005ULL + inc_;
        uint32_t xorshifted = static_cast<uint32_t>(((old >> 18u) ^ old) >> 27u);
        uint32_t rot = static_cast<uint32_t>(old >> 59u);
        return (xorshifted >> rot) | (xorshifted << ((-rot) & 31));
    }

    // Uniform in [0, 1).
    double uniform() { return next() * (1.0 / 4294967296.0); }
    // Uniform integer in [lo, hi].
    int range(int lo, int hi) {
        if (hi <= lo) return lo;
        return lo + static_cast<int>(next() % static_cast<uint32_t>(hi - lo + 1));
    }
    bool chance(double p) { return uniform() < p; }

    // Index chosen with probability proportional to weight; -1 if all weights <= 0.
    int weighted(const std::vector<double>& weights) {
        double total = 0;
        for (double w : weights) total += w > 0 ? w : 0;
        if (total <= 0) return -1;
        double r = uniform() * total;
        for (size_t i = 0; i < weights.size(); ++i) {
            double w = weights[i] > 0 ? weights[i] : 0;
            if (r < w) return static_cast<int>(i);
            r -= w;
        }
        return static_cast<int>(weights.size()) - 1;
    }

    uint64_t state() const { return state_; }

private:
    uint64_t state_ = 0;
    uint64_t inc_ = 0;
};

inline uint64_t hashString(const std::string& s) {
    uint64_t h = 1469598103934665603ULL;
    for (unsigned char c : s) {
        h ^= c;
        h *= 1099511628211ULL;
    }
    return h;
}

}  // namespace sim
