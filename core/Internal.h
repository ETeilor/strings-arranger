// Shared internals of the engine (not part of the public API).
#pragma once
#include <limits>
#include "ChoirEngine.h"

namespace choir {

constexpr double INF = std::numeric_limits<double>::infinity();
constexpr int Q = 480, E = 240, X = 120;

inline int mod12(int x) { return ((x % 12) + 12) % 12; }
inline int floordiv(int a, int b) { return (a >= 0) ? a / b : -((-a + b - 1) / b); }

// voices are indexed bottom-up, as in the prototype: B=0, T=1, A=2, S=3
enum V { VB = 0, VT = 1, VA = 2, VS = 3 };
extern const char VOICE_CH[4];          // 'B','T','A','S'
extern const int RANGE_LO[4], RANGE_HI[4];

struct Levels {
    int v[4] = {4, 4, 4, 4};             // by voice index (B,T,A,S)
    int max() const;
};

Levels resolveLevels(const Settings& s, bool melodyIsReal);

// deterministic noise in [0, 1) for alternative variants; 0 for variant 0 (the default arrangement)
inline double vnoise(int variant, int a, int b) {
    if (variant <= 0) return 0.0;
    unsigned h = 2166136261u;
    for (int x : {variant, a, b}) {
        h ^= (unsigned)x;
        h *= 16777619u;
        h ^= h >> 13;
    }
    return (double)(h % 10007u) / 10007.0;
}

// melody harmonization (Harmonize.cpp)
bool isMonophonic(const std::vector<Note>& notes);
std::vector<Chord> harmonize(const std::vector<Note>& melody, const Meta& meta, Style style, int level, int variant,
                             int& tonic, bool& minor);
std::string issuePos(int t, int num);

}  // namespace choir
