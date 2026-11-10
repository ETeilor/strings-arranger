// Family A (Bach chorale, Orthodox obikhod): harmony translation, Viterbi voicing,
// complexity knob, ornament pass, rule check.
#include <algorithm>
#include <cmath>
#include <functional>
#include <sstream>
#include "Internal.h"

namespace choir {

const char VOICE_CH[4] = {'B', 'T', 'A', 'S'};
const int RANGE_LO[4] = {41, 48, 55, 60};
const int RANGE_HI[4] = {62, 67, 74, 79};

int Levels::max() const { return std::max(std::max(v[0], v[1]), std::max(v[2], v[3])); }

static int clamp10(int x) { return std::max(0, std::min(10, x)); }

Levels resolveLevels(const Settings& s, bool melodyIsReal) {
    Levels L;
    int g = clamp10(s.global);
    L.v[VB] = L.v[VT] = L.v[VA] = g;
    L.v[VS] = melodyIsReal ? 0 : g;
    if (s.S >= 0) L.v[VS] = clamp10(s.S);
    if (s.A >= 0) L.v[VA] = clamp10(s.A);
    if (s.T >= 0) L.v[VT] = clamp10(s.T);
    if (s.B >= 0) L.v[VB] = clamp10(s.B);
    return L;
}

Settings Settings::fromSpec(Style st, const std::string& spec) {
    Settings s;
    s.style = st;
    if (spec.empty()) return s;
    std::stringstream ss(spec);
    std::string part;
    while (std::getline(ss, part, ',')) {
        part.erase(0, part.find_first_not_of(' '));
        if (part.empty()) continue;
        char c = (char)std::toupper((unsigned char)part[0]);
        if (c == 'S' || c == 'A' || c == 'T' || c == 'B') {
            int x = std::stoi(part.substr(1));
            if (c == 'S') s.S = x;
            if (c == 'A') s.A = x;
            if (c == 'T') s.T = x;
            if (c == 'B') s.B = x;
        } else {
            s.global = std::stoi(part);
        }
    }
    return s;
}

std::string issuePos(int t, int num) {
    int bar = t / (num * TPB) + 1;
    double beat = (double)(t % (num * TPB)) / TPB + 1;
    std::ostringstream o;
    o << bar << '.' << beat;
    return o.str();
}

namespace {

// ------------------------------------------------------------------ style profile
struct StyleP {
    std::string id = "bach", label;
    int rangeLo[4] = {41, 48, 55, 60}, rangeHi[4] = {62, 67, 74, 79};   // B T A S
    int melodyVoice = VS;                     // the voice that carries the user's melody
    int melLo = 60, melHi = 79, melCLo = 65, melCHi = 77;   // octave placement of the melody
    bool keepColors = false;                  // 9ths, 6ths, added tones stay as colour tones
    double wColor = 0.0;                      // reward (negative) for a colour tone in the voicing
    int maxST = 99;                           // soprano-tenor span (gospel: close voicing)
    const char* partNames[4] = {"Bass", "Tenor", "Alto", "Soprano"};
    bool checkParallels = true;
    std::map<std::string, std::pair<std::string, std::vector<int>>> colorMap;   // quality -> (base, colours)
    std::set<std::string> keep;
    std::map<std::string, std::string> reduce;     // "" + special "?" = diatonic third
    bool sus4AsSuspension = true, rootPosition = false, staticInside = false;
    bool homorhythm = false, recitation = false;
    int pulse = 480, bassPulse = 960;
    std::vector<std::string> bassAlt{"root", "third"};
    double wTess = 1.0;
    int comfortLo[4] = {45, 52, 57, 65}, comfortHi[4] = {57, 64, 72, 77};   // B T A S
    int maxSA = 12, maxAT = 12, maxTB = 19;
    double wUnison = 4.0, wNoFifth = 1.5, wNoSeventh = 3.0;
    double dRoot = 0.0, dFifth = 1.0, dThird = 3.0, dThirdInv = 1.0;
    double wMoveA = 1.0, wMoveT = 1.0, wMoveB = 0.5;
    double wLeapInner = 2.0, wLeapBass = 0.5, wTritone = 6.0, wBigLeap = 15.0;
    double parallels = 100.0, wHidden = 6.0, wOverlap = 4.0;
    double wLeadOuter = 6.0, wLeadInner = 3.0, wSeventh = 6.0;
    double wBassStatic = 3.0, wOffbeat = 20.0, wSusUnprep = 4.0, wInversion = 12.0;
    // complexity-derived
    double wHold[4] = {0, 0, 0, 0};
    double leapScale[4] = {1, 1, 1, 1};
    bool hasHold = false;
    Levels levels;
    std::map<std::string, int> figureMin;   // empty -> default table
    std::vector<int> figureVoices{VB, VT, VA, VS};
    // variants, locks, three-part choirs
    int variant = 0;
    int dropVoice = -1;                       // SAB drops the tenor, SSA the bass: searched, not sung
    bool relaxed = false;                     // last resort: an incomplete chord costs instead of failing
    std::array<std::vector<Note>, 4> lockNotes;
    std::array<bool, 4> locked{};
};

StyleP bachStyle() {
    StyleP s;
    s.label = "Хорал Баха";
    s.keep = {"", "m", "dim", "aug", "7", "maj7", "m7", "m7b5", "dim7"};
    s.reduce = {{"6", ""},      {"m6", "m"},      {"add9", ""},  {"madd9", "m"}, {"9", "7"},
                {"maj9", "maj7"}, {"m9", "m7"},   {"mmaj7", "m"}, {"sus2", "?"},  {"sus4", "?"},
                {"5", "?"}};
    return s;
}

StyleP obikhodStyle() {
    StyleP s = bachStyle();
    s.label = "Православный обиход";
    s.keep = {"", "m", "dim", "7"};
    s.reduce = {{"6", ""},  {"m6", "m"},    {"add9", ""},    {"madd9", "m"}, {"9", "7"},
                {"maj9", ""}, {"m9", "m"},  {"mmaj7", "m"},  {"maj7", ""},   {"m7", "m"},
                {"m7b5", "dim"}, {"dim7", "dim"}, {"aug", ""}, {"sus2", "?"}, {"sus4", "?"},
                {"5", "?"}};
    s.sus4AsSuspension = false;
    s.rootPosition = true;
    s.wInversion = 6.0;
    s.staticInside = true;
    s.homorhythm = true;
    s.recitation = true;
    int lo[4] = {43, 52, 57, 64}, hi[4] = {55, 64, 70, 76};
    for (int i = 0; i < 4; ++i) s.comfortLo[i] = lo[i], s.comfortHi[i] = hi[i];
    s.maxTB = 24;
    s.dRoot = 0.0, s.dFifth = 1.5, s.dThird = 5.0, s.dThirdInv = 3.0;
    s.wNoFifth = 3.0;
    s.wMoveA = 1.5, s.wMoveT = 1.5, s.wMoveB = 0.6;
    s.figureMin = {{"pass8", 5}, {"pass8x2", 8}, {"neighbor", 9}};
    s.figureVoices = {VA, VT};
    return s;
}

void tuneStyle(StyleP& s, const Levels& lv) {
    int LB = lv.v[VB];
    if (LB <= 1) {
        s.bassAlt.clear();
        s.wBassStatic = 0.0;
    } else {
        s.bassAlt = LB >= 7 ? std::vector<std::string>{"root", "third", "fifth"}
                            : std::vector<std::string>{"root", "third"};
        s.wBassStatic = 1.0 + 0.5 * LB;
    }
    s.bassPulse = LB >= 9 ? 480 : 960;
    for (int v = 0; v < 4; ++v) {
        int L = lv.v[v];
        if (L == 0) s.wHold[v] = 8.0;
        else if (L <= 3) s.wHold[v] = 2.0;
        else if (L <= 5) s.wHold[v] = 0.0;
        else s.wHold[v] = -0.3 * (L - 5);
        s.leapScale[v] = 1.0 - 0.05 * L;
    }
    s.hasHold = true;
    if (s.staticInside) {
        s.bassAlt.clear();
        s.wBassStatic = 0.0;
        for (int v = 0; v < 4; ++v) s.wHold[v] = 8.0;
    }
    s.levels = lv;
}

// ------------------------------------------------------------------ style chords
const int SCALE_MAJ[7] = {0, 2, 4, 5, 7, 9, 11};
const int SCALE_MIN[7] = {0, 2, 3, 5, 7, 8, 10};

bool inScale(int pc, int tonic, bool minor) {
    const int* sc = minor ? SCALE_MIN : SCALE_MAJ;
    for (int i = 0; i < 7; ++i)
        if (mod12(pc - tonic) == sc[i]) return true;
    return false;
}

std::vector<int> qualityIv(const std::string& q) {
    if (q == "") return {4, 7};
    if (q == "m") return {3, 7};
    if (q == "dim") return {3, 6};
    if (q == "aug") return {4, 8};
    if (q == "7") return {4, 7, 10};
    if (q == "maj7") return {4, 7, 11};
    if (q == "m7") return {3, 7, 10};
    if (q == "m7b5") return {3, 6, 10};
    if (q == "dim7") return {3, 6, 9};
    return {4, 7};
}

enum Role { R_NONE = 0, R_ROOT, R_THIRD, R_FIFTH, R_SEVENTH, R_SUS, R_COLOR };

struct SChord {
    int start = 0, end = 0, root = 0, bassPc = 0;
    std::string quality, srcName;
    std::array<int, 12> role{};          // Role per pitch class
    int third = -1, fifth = -1, seventh = -1, sus = -1, susRes = -1;
    std::vector<int> colors;             // 9ths, 6ths, added tones (styles with colours only)
    std::string shown;                   // displayed quality when colours were added
    bool dominant = false;
    bool has(int pc) const { return role[mod12(pc)] != R_NONE; }
    std::string name() const {
        std::string q = sus >= 0 ? std::string("sus4") : !shown.empty() ? shown : quality;
        std::string n = std::string(NAMES[root]) + q;
        return bassPc == root ? n : n + "/" + NAMES[bassPc];
    }
};

std::string diatonicThird(int root, int tonic, bool minor) {
    return inScale((root + 4) % 12, tonic, minor) ? "" : "m";
}

SChord build(int start, int end, int root, const std::string& q, int userBass, int tonic,
             const std::string& name, bool sus) {
    SChord c;
    c.start = start, c.end = end, c.root = root, c.quality = q, c.srcName = name;
    auto iv = qualityIv(q);
    c.role.fill(R_NONE);
    c.role[root] = R_ROOT;
    int third = (root + iv[0]) % 12, fifth = (root + iv[1]) % 12;
    c.role[fifth] = R_FIFTH;
    c.fifth = fifth;
    if (sus) {
        c.sus = (root + 5) % 12;
        c.susRes = third;
        c.role[c.sus] = R_SUS;
        c.third = -1;
    } else {
        c.role[third] = R_THIRD;
        c.third = third;
    }
    if (iv.size() > 2) {
        c.seventh = (root + iv[2]) % 12;
        c.role[c.seventh] = R_SEVENTH;
    }
    c.bassPc = c.role[userBass] != R_NONE ? userBass : root;
    c.dominant = (mod12(root - tonic) == 7 && iv[0] == 4 && !sus) ||
                 (mod12(root - tonic) == 11 && (q == "dim" || q == "m7b5" || q == "dim7"));
    return c;
}

std::vector<SChord> translateAll(const std::vector<Chord>& chords, int tonic, bool minor,
                                 const StyleP& st) {
    std::vector<SChord> out;
    for (size_t i = 0; i < chords.size(); ++i) {
        const Chord& c = chords[i];
        std::string q = c.quality;
        if (q == "sus4" && st.sus4AsSuspension) {
            std::string tq = diatonicThird(c.root, tonic, minor);
            bool resolves = false;
            if (i + 1 < chords.size()) {
                auto& nx = chords[i + 1];
                resolves = nx.root == c.root &&
                           (nx.quality == "" || nx.quality == "m" || nx.quality == "7" || nx.quality == "m7");
            }
            if (resolves) {
                out.push_back(build(c.start, c.end, c.root, tq, c.bassPc, tonic, c.name(), true));
                continue;
            }
            int dur = c.end - c.start;
            if (dur >= 2 * st.pulse) {
                int mid = c.start + (dur / 2) / st.pulse * st.pulse;
                out.push_back(build(c.start, mid, c.root, tq, c.bassPc, tonic, c.name(), true));
                out.push_back(build(mid, c.end, c.root, tq, c.bassPc, tonic, c.name(), false));
                continue;
            }
        }
        if (!st.keep.count(q)) {
            auto it = st.reduce.find(q);
            std::string q2 = it == st.reduce.end() ? std::string("") : it->second;
            q = q2 == "?" ? diatonicThird(c.root, tonic, minor) : q2;
        }
        std::vector<int> cols;
        std::string shown;
        if (st.keepColors) {
            auto it = st.colorMap.find(c.quality);
            if (it != st.colorMap.end()) {
                q = it->second.first == "?" ? diatonicThird(c.root, tonic, minor) : it->second.first;
                cols = it->second.second;
                shown = c.quality;
            }
        }
        int bass = st.rootPosition ? c.root : c.bassPc;
        SChord sc = build(c.start, c.end, c.root, q, bass, tonic, c.name(), false);
        for (int iv : cols) {
            int pc = (c.root + iv) % 12;
            if (sc.role[pc] == R_NONE) {
                sc.role[pc] = R_COLOR;
                sc.colors.push_back(pc);
            }
        }
        sc.shown = shown;
        if (sc.role[c.bassPc] != R_NONE && sc.role[c.bassPc] != R_COLOR && !st.rootPosition) sc.bassPc = c.bassPc;
        out.push_back(sc);
    }
    return out;
}

// ------------------------------------------------------------------ melody & slices
int melodyShift(const std::vector<Note>& melody, const StyleP& st) {
    const int lo = st.melLo, hi = st.melHi, clo = st.melCLo, chi = st.melCHi;
    bool have = false;
    long long bo = 0, bc = 0;
    int bk = 0, bestK = 0;
    for (int k = -4; k <= 4; ++k) {
        long long out = 0, comf = 0;
        for (auto& n : melody) {
            int p = n.pitch + 12 * k;
            out += (long long)(std::max(0, lo - p) + std::max(0, p - hi)) * n.dur();
            comf += (long long)(std::max(0, clo - p) + std::max(0, p - chi)) * n.dur();
        }
        int ak = std::abs(k);
        bool better = !have || out < bo || (out == bo && (comf < bc || (comf == bc && ak < bk)));
        if (better) {
            have = true, bo = out, bc = comf, bk = ak, bestK = k;
        }
    }
    return have ? 12 * bestK : 0;
}

struct Slice {
    int start, end, ci;
    int mel;          // -1 none
    int melId;
    bool flex, onBeat, first, final_;
};

std::vector<Slice> makeSlices(const std::vector<SChord>& ch, const std::vector<Note>& melody,
                              int shift, int pulse, bool flex) {
    int t0 = ch.front().start, t1 = ch.back().end;
    std::set<int> cuts{t0, t1};
    for (auto& c : ch) cuts.insert(c.start), cuts.insert(c.end);
    for (auto& n : melody) cuts.insert(n.start), cuts.insert(n.end);
    for (int t = floordiv(t0, pulse) * pulse; t < t1; t += pulse) cuts.insert(t);
    std::vector<int> cs;
    for (int x : cuts)
        if (t0 <= x && x <= t1) cs.push_back(x);
    std::vector<Slice> out;
    int ci = 0;
    for (size_t k = 0; k + 1 < cs.size(); ++k) {
        int a = cs[k], b = cs[k + 1];
        while (ci + 1 < (int)ch.size() && ch[ci + 1].start <= a) ++ci;
        int mel = -1, melId = -1;
        for (size_t j = 0; j < melody.size(); ++j)
            if (melody[j].start <= a && a < melody[j].end) {
                mel = melody[j].pitch + shift;
                melId = (int)j;
                break;
            }
        out.push_back({a, b, ci, mel, melId, flex, a % pulse == 0, a == ch[ci].start,
                       ci == (int)ch.size() - 1});
    }
    return out;
}

// ------------------------------------------------------------------ voicing search
using Voicing = std::array<int, 4>;   // B T A S

std::vector<int> pitchesFor(const std::function<bool(int)>& ok, int v, const StyleP& st) {
    std::vector<int> r;
    for (int p = st.rangeLo[v]; p <= st.rangeHi[v]; ++p)
        if (ok(mod12(p))) r.push_back(p);
    return r;
}

double tess(int p, int v, const StyleP& st) {
    return (double)(std::max(0, st.comfortLo[v] - p) + std::max(0, p - st.comfortHi[v]));
}

double verticalCost(const Voicing& v, const SChord& ch, const StyleP& st, int tonic) {
    int B = v[0], T = v[1], A = v[2], S = v[3];
    double c = 0.0;
    for (int i = 0; i < 4; ++i) c += st.wTess * tess(v[i], i, st);
    if (T == A) c += st.wUnison;
    if (A == S) c += st.wUnison;
    bool present[12] = {false};
    for (int i = 0; i < 4; ++i)
        if (i != st.dropVoice) present[mod12(v[i])] = true;
    if (st.dropVoice >= 0) {
        // three sung voices: a missing root or third costs, the silent voice does not count
        if (!present[ch.root]) c += 4.0;
        if (ch.third >= 0 && !present[ch.third]) c += 8.0;
        if (st.dropVoice == VT && T == A) c -= st.wUnison;   // the silent tenor may share the alto's note
    } else if (st.relaxed) {
        if (!present[ch.root]) c += 12.0;
        if (ch.third >= 0 && !present[ch.third]) c += 16.0;
    } else if (!present[ch.root] || (ch.third >= 0 && !present[ch.third])) return INF;
    if (ch.sus >= 0 && !present[ch.sus]) return INF;
    if (!present[ch.fifth]) c += st.wNoFifth;
    if (ch.seventh >= 0 && !present[ch.seventh]) c += st.wNoSeventh;
    for (int col : ch.colors)
        if (present[col]) c += st.wColor;
    // counts in insertion order of the voices (B, T, A, S)
    std::vector<std::pair<int, int>> counts;
    for (int p : v) {
        int pc = mod12(p);
        if (!ch.has(pc)) continue;
        bool found = false;
        for (auto& kv : counts)
            if (kv.first == pc) kv.second++, found = true;
        if (!found) counts.push_back({pc, 1});
    }
    int leading = (tonic + 11) % 12;
    for (auto& kv : counts) {
        int pc = kv.first, n = kv.second;
        if (n < 2) continue;
        int role = ch.role[pc];
        if (st.relaxed && (role == R_SEVENTH || role == R_SUS || role == R_COLOR || (pc == leading && ch.dominant))) {
            c += 20.0;
            continue;
        }
        if (role == R_SEVENTH) return INF;
        if (role == R_SUS || role == R_COLOR) return INF;
        if (pc == leading && ch.dominant) return INF;
        if (role == R_THIRD)
            c += (mod12(B) == pc ? st.dThirdInv : st.dThird) * (n - 1);
        else if (role == R_ROOT)
            c += st.dRoot * (n - 1);
        else if (role == R_FIFTH)
            c += st.dFifth * (n - 1);
    }
    return c;
}

struct Cand {
    Voicing v;
    double vc;
};

std::vector<Cand> candidates(const Slice& sl, const SChord& ch, const StyleP& st, int tonic) {
    auto inChord = [&](int pc) { return ch.has(pc); };
    std::vector<int> Sopts;
    if (sl.mel >= 0 && sl.flex) {
        int mpc = mod12(sl.mel);
        Sopts = pitchesFor([&](int pc) { return pc == mpc; }, st.melodyVoice, st);
    } else if (sl.mel >= 0) {
        Sopts = {sl.mel};
    } else {
        Sopts = pitchesFor(inChord, st.melodyVoice, st);
    }
    std::set<int> bassPcs{ch.bassPc};
    if (sl.first || sl.final_) {
        bassPcs.insert(ch.root);
        if (ch.third >= 0) bassPcs.insert(ch.third);
    } else {
        for (auto& r : st.bassAlt) {
            int pc;
            if (r == "root") pc = ch.root;
            else if (r == "fifth") pc = ch.fifth;
            else pc = ch.third >= 0 ? ch.third : ch.sus;
            if (pc >= 0) bassPcs.insert(pc);
        }
    }
    auto Bs = pitchesFor([&](int pc) { return bassPcs.count(pc) > 0; }, VB, st);
    auto Ts = pitchesFor(inChord, VT, st);
    // the melody voice takes the melody options, the other upper voice the chord tones
    std::vector<int> As = st.melodyVoice == VA ? Sopts : pitchesFor(inChord, VA, st);
    if (st.melodyVoice == VA) Sopts = pitchesFor(inChord, VS, st);
    std::vector<Cand> out;
    for (int S : Sopts)
        for (int A : As) {
            if (A > S || S - A > st.maxSA) continue;
            for (int T : Ts) {
                if (T > A || A - T > st.maxAT || S - T > st.maxST) continue;
                for (int B : Bs) {
                    if (B >= T || T - B > st.maxTB) continue;
                    Voicing v{B, T, A, S};
                    double vc = verticalCost(v, ch, st, tonic);
                    if ((sl.first || sl.final_) && mod12(B) != ch.bassPc) vc += st.wInversion;
                    if (st.variant > 0)
                        vc += 6.0 * vnoise(st.variant, sl.start, ((B * 131 + T) * 131 + A) * 131 + S);
                    if (vc < INF) out.push_back({v, vc});
                }
            }
        }
    // locked voices keep their pitch wherever the lock has a note at this slice
    for (int lv = 0; lv < 4; ++lv) {
        if (!st.locked[lv]) continue;
        int want = -1;
        for (auto& n : st.lockNotes[lv])
            if (n.start <= sl.start && sl.start < n.end) want = n.pitch;
        if (want < 0) continue;
        std::vector<Cand> keep;
        for (auto& c : out)
            if (c.v[lv] == want) keep.push_back(c);
        if (!keep.empty()) out.swap(keep);
    }
    return out;
}

double bassInsideChord(const Voicing& pv, const Voicing& cv, const Slice& csl, const StyleP& st) {
    double c = 0.0;
    if (csl.start % st.bassPulse == 0) {
        if (cv[0] == pv[0]) c += st.wBassStatic;
    } else if (cv[0] != pv[0]) {
        c += st.wOffbeat / 2;
    }
    return c;
}

double transitionCost(const Voicing& pv, const Voicing& cv, const Slice& psl, const Slice& csl,
                      const SChord& pch, const SChord& cch, const StyleP& st, int tonic) {
    double c = 0.0;
    bool same = psl.ci == csl.ci;
    const int mv = st.melodyVoice;
    bool othersMoved = false;
    for (int i = 0; i < 4; ++i)
        if (i != mv && pv[i] != cv[i]) othersMoved = true;
    if (same && !csl.onBeat && othersMoved) c += st.wOffbeat;
    if (csl.melId >= 0 && csl.melId == psl.melId && cv[mv] != pv[mv]) c += 100.0;
    const int order[4] = {3, 2, 1, 0};
    for (int idx : order) {
        double w = idx == 3 || idx == 2 ? st.wMoveA : idx == 1 ? st.wMoveT : st.wMoveB;
        int d = std::abs(cv[idx] - pv[idx]);
        c += w * d;
        double ls = st.leapScale[idx];
        bool bass = idx == 0;
        if (d == 6) c += st.wTritone * ls;
        if (d > 9 && !(bass && d == 12)) c += st.wBigLeap * ls;
        if (!bass && d > 4) c += st.wLeapInner * (d - 4) * ls;
        if (bass && 5 < d && d < 12) c += st.wLeapBass * (d - 5) * ls;
    }
    if (st.parallels != 0) {
        for (int i = 0; i < 4; ++i)
            for (int j = i + 1; j < 4; ++j) {
                if (pv[i] == cv[i] || pv[j] == cv[j]) continue;
                int a = mod12(pv[j] - pv[i]), b = mod12(cv[j] - cv[i]);
                if (a == b && (b == 0 || b == 7)) c += st.parallels;
            }
    }
    int dB = cv[0] - pv[0], dS = cv[3] - pv[3];
    if (dB && dS && ((dB > 0) == (dS > 0)) &&
        (mod12(cv[3] - cv[0]) == 0 || mod12(cv[3] - cv[0]) == 7) && std::abs(dS) > 2)
        c += st.wHidden;
    for (int lo = 0; lo < 3; ++lo) {
        int hi = lo + 1;
        if (cv[lo] > pv[hi] || cv[hi] < pv[lo]) c += st.wOverlap;
    }
    if (!same) {
        int leading = (tonic + 11) % 12;
        if (pch.dominant && cch.has(tonic)) {
            for (int idx = 0; idx < 3; ++idx)
                if (mod12(pv[idx]) == leading && cv[idx] != pv[idx] + 1)
                    c += idx == 0 ? st.wLeadOuter : st.wLeadInner;
        }
        if (pch.sus >= 0)
            for (int idx = 0; idx < 4; ++idx) {
                int d = cv[idx] - pv[idx];
                if (mod12(pv[idx]) == pch.sus && d != -1 && d != -2) c += st.wSeventh;
            }
        if (cch.sus >= 0)
            for (int idx = 0; idx < 4; ++idx)
                if (mod12(cv[idx]) == cch.sus && pv[idx] != cv[idx]) c += st.wSusUnprep;
        if (pch.seventh >= 0)
            for (int idx = 0; idx < 3; ++idx)
                if (mod12(pv[idx]) == pch.seventh) {
                    int d = cv[idx] - pv[idx];
                    if (d == -1 || d == -2) continue;
                    c += d == 0 ? 2.0 : st.wSeventh;
                }
    } else if (csl.final_) {
        for (int idx = 0; idx < 3; ++idx)
            if (cv[idx] != pv[idx]) c += 8.0;
    } else {
        if (csl.onBeat && st.hasHold) {
            const int ho[3] = {0, 1, 2};
            for (int idx : ho)
                if (cv[idx] != pv[idx]) c += st.wHold[idx];
        }
        c += bassInsideChord(pv, cv, csl, st);
    }
    return c;
}

bool viterbi(const std::vector<Slice>& slices, const std::vector<SChord>& ch, const StyleP& st,
             int tonic, std::vector<Voicing>& path, std::string& err) {
    std::vector<std::vector<Cand>> cands;
    for (size_t i = 0; i < slices.size(); ++i) {
        cands.push_back(candidates(slices[i], ch[slices[i].ci], st, tonic));
        if (cands.back().empty()) {
            // a melody note the style's spacing cannot accommodate: search again with wide spacing
            StyleP wide = st;
            wide.maxSA = wide.maxAT = 24, wide.maxTB = 28, wide.maxST = 99;
            wide.rangeLo[VB] -= 5, wide.rangeLo[VT] -= 3;      // and a little more room below
            cands.back() = candidates(slices[i], ch[slices[i].ci], wide, tonic);
            if (cands.back().empty()) {
                // equal-voice and amateur choirs: more room below, and an incomplete chord if need be
                for (int v = 0; v < 4; ++v)
                    if (v != wide.melodyVoice) wide.rangeLo[v] -= 4;
                wide.relaxed = true;
                cands.back() = candidates(slices[i], ch[slices[i].ci], wide, tonic);
            }
        }
        if (cands.back().empty()) {
            err = "нет допустимого расположения голосов в аккорде " + ch[slices[i].ci].srcName + " (доля " +
                  std::to_string(slices[i].start / TPB + 1) + ", мелодия " + std::to_string(slices[i].mel) + ")";
            return false;
        }
    }
    std::vector<double> cost;
    for (auto& c : cands[0]) cost.push_back(c.vc);
    std::vector<std::vector<int>> back;
    for (size_t i = 1; i < slices.size(); ++i) {
        const Slice &psl = slices[i - 1], &csl = slices[i];
        const SChord &pch = ch[psl.ci], &cch = ch[csl.ci];
        std::vector<double> nc;
        std::vector<int> ptr;
        nc.reserve(cands[i].size());
        for (auto& cc : cands[i]) {
            double best = INF;
            int arg = -1;
            for (size_t j = 0; j < cands[i - 1].size(); ++j) {
                if (cost[j] >= best) continue;
                double t = cost[j] + transitionCost(cands[i - 1][j].v, cc.v, psl, csl, pch, cch, st, tonic);
                if (t < best) best = t, arg = (int)j;
            }
            nc.push_back(best + cc.vc);
            ptr.push_back(arg);
        }
        cost.swap(nc);
        back.push_back(ptr);
    }
    int k = 0;
    for (int x = 1; x < (int)cost.size(); ++x)
        if (cost[x] < cost[k]) k = x;
    std::vector<int> idx{k};
    for (auto it = back.rbegin(); it != back.rend(); ++it) {
        k = (*it)[k];
        idx.push_back(k);
    }
    std::reverse(idx.begin(), idx.end());
    path.clear();
    for (size_t i = 0; i < idx.size(); ++i) path.push_back(cands[i][idx[i]].v);
    return true;
}

using Parts = std::array<std::vector<Note>, 4>;   // by voice index B T A S

Parts toParts(const std::vector<Slice>& slices, const std::vector<Voicing>& vs, bool homorhythm, int melodyVoice = VS) {
    Parts parts;
    for (int vi = 0; vi < 4; ++vi) {
        auto& notes = parts[vi];
        bool havePrev = false;
        int prevId = 0;
        for (size_t k = 0; k < slices.size(); ++k) {
            const Slice& sl = slices[k];
            int p = vs[k][vi];
            bool sameNote = (vi != melodyVoice && !homorhythm) || sl.melId < 0 || (havePrev && sl.melId == prevId);
            prevId = sl.melId;
            havePrev = true;
            if (!notes.empty() && notes.back().pitch == p && notes.back().end == sl.start && sameNote)
                notes.back().end = sl.end;
            else
                notes.push_back(Note{sl.start, sl.end, p, 80});
        }
    }
    return parts;
}

// ------------------------------------------------------------------ ornaments
const std::map<std::string, int>& figureTable() {
    static const std::map<std::string, int> t = {
        {"pass8", 1},      {"neighbor", 3}, {"pass8x2", 4},         {"suspension", 5},
        {"arpeggio", 6},   {"anticipation", 6}, {"suspension_long", 7}, {"run16", 8},
        {"chromatic", 9},  {"fill", 8}};
    return t;
}

int budgetPerBar(int L) { return (int)std::ceil(0.8 * L); }
int voicesPerBeat(const Levels& lv) {
    int m = lv.max();
    return m <= 3 ? 1 : m <= 6 ? 2 : m <= 9 ? 3 : 4;
}

const Note* noteAt(const std::vector<Note>& ns, int t) {
    for (auto& n : ns)
        if (n.start <= t && t < n.end) return &n;
    return nullptr;
}
int pitchAt(const std::vector<Note>& ns, int t) {
    auto n = noteAt(ns, t);
    return n ? n->pitch : -1000;
}

std::array<bool, 12> localScale(int tonic, bool minor, const std::set<int>& chordPcs) {
    std::array<bool, 12> sc{};
    const int* s = minor ? SCALE_MIN : SCALE_MAJ;
    for (int i = 0; i < 7; ++i) sc[(tonic + s[i]) % 12] = true;
    for (int pc : chordPcs) {          // ascending, like sorted() in the prototype
        if (!sc[pc]) {
            const int nbs[2] = {mod12(pc - 1), mod12(pc + 1)};
            for (int nb : nbs)
                if (sc[nb] && !chordPcs.count(nb)) {
                    sc[nb] = false;
                    break;
                }
            sc[pc] = true;
        }
    }
    return sc;
}

bool parallelP(int a0, int a1, int b0, int b1) {
    if (a0 == a1 || b0 == b1) return false;
    int i0 = std::abs(b0 - a0) % 12, i1 = std::abs(b1 - a1) % 12;
    return i0 == i1 && (i1 == 0 || i1 == 7);
}

struct Ctx {
    Parts& parts;
    const std::vector<SChord>& ch;
    int tonic;
    bool minor;
    const StyleP& st;
    int barLen;
    Levels lv;
    std::set<int> chordStarts;
    int finalStart;
    std::map<std::pair<int, int>, int> usedBar, susBar;
    std::map<int, std::set<int>> beatVoices;
    std::map<int, int> lastBeat;
    int cap;
    std::map<std::string, int> count;

    Ctx(Parts& p, const std::vector<SChord>& c, int tn, bool mn, const StyleP& s, int bl)
        : parts(p), ch(c), tonic(tn), minor(mn), st(s), barLen(bl), lv(s.levels) {
        for (auto& x : c) chordStarts.insert(x.start);
        finalStart = c.back().start;
        cap = voicesPerBeat(lv);
    }

    const SChord& chordAt(int t) const {
        for (auto& c : ch)
            if (c.start <= t && t < c.end) return c;
        return ch.back();
    }
    std::set<int> pcsOf(const SChord& c) const {
        std::set<int> s;
        for (int pc = 0; pc < 12; ++pc)
            if (c.role[pc] != R_NONE) s.insert(pc);
        return s;
    }
    std::array<bool, 12> scaleBetween(int t1, int t2) const {
        auto a = pcsOf(chordAt(t1));
        auto b = pcsOf(chordAt(t2));
        a.insert(b.begin(), b.end());
        return localScale(tonic, minor, a);
    }

    bool allowed(int v, int t, const std::string& fig) {
        int L = lv.v[v];
        int bar = floordiv(t, barLen), beat = floordiv(t, Q);
        if (fig.find("suspension") != std::string::npos) {
            auto it = susBar.find({v, bar});
            if ((it == susBar.end() ? 0 : it->second) >= (L >= 7 ? 2 : 1)) return false;
        } else {
            auto it = usedBar.find({v, bar});
            if ((it == usedBar.end() ? 0 : it->second) >= budgetPerBar(L)) return false;
        }
        int gap = L <= 3 ? 1 : 0;
        auto lb = lastBeat.find(v);
        if (beat - (lb == lastBeat.end() ? -99 : lb->second) <= gap) return false;
        int others = 0;
        auto bv = beatVoices.find(beat);
        if (bv != beatVoices.end())
            for (int w : bv->second)
                if (w != v) ++others;
        return others < cap;
    }

    void mark(int v, int t, const std::string& fig, int added) {
        int bar = floordiv(t, barLen), beat = floordiv(t, Q);
        if (fig.find("suspension") != std::string::npos) {
            susBar[{v, bar}] += 1;
        } else {
            usedBar[{v, bar}] += std::max(1, added);
            beatVoices[beat].insert(v);
            lastBeat[v] = beat;
        }
        count[fig] += 1;
    }

    bool valid(int v, int ta, int tb) {
        for (auto& n : parts[v])
            if (ta <= n.start && n.start < tb && !(st.rangeLo[v] <= n.pitch && n.pitch <= st.rangeHi[v]))
                return false;
        std::set<int> pts{ta - 1, tb};
        for (int w = 0; w < 4; ++w)
            for (auto& n : parts[w])
                if (ta <= n.start && n.start <= tb) pts.insert(n.start);
        bool havePrev = false;
        std::array<const Note*, 4> prev{};
        for (int t : pts) {
            std::array<const Note*, 4> cur;
            for (int w = 0; w < 4; ++w) cur[w] = noteAt(parts[w], t);
            const Note* mine = cur[v];
            if (cur[0] && cur[1] && cur[2] && cur[3]) {
                int pS = cur[VS]->pitch, pA = cur[VA]->pitch, pT = cur[VT]->pitch, pB = cur[VB]->pitch;
                if (pS - pA > 12 || pA - pT > 12 || pT - pB > 19) return false;
            }
            if (mine) {
                for (int w = 0; w < 4; ++w) {
                    const Note* o = cur[w];
                    if (w == v || !o) continue;
                    if (v < w && mine->pitch > o->pitch) return false;
                    if (v > w && mine->pitch < o->pitch) return false;
                    int iv = std::abs(mine->pitch - o->pitch) % 12;
                    if (mine->orn && o->orn && (iv == 1 || iv == 2 || iv == 6 || iv == 10 || iv == 11))
                        return false;
                }
            }
            if (havePrev) {
                for (int w = 0; w < 4; ++w) {
                    if (w == v) continue;
                    if (!prev[v] || !cur[v] || !prev[w] || !cur[w]) continue;
                    if (parallelP(prev[v]->pitch, cur[v]->pitch, prev[w]->pitch, cur[w]->pitch))
                        return false;
                }
            }
            prev = cur;
            havePrev = true;
        }
        return true;
    }

    bool tryRewrite(int v, int i, int j, const std::vector<Note>& nw, const std::string& fig) {
        if (fig.find("suspension") == std::string::npos)
            for (auto& n : nw)
                if (n.start > finalStart) return false;
        auto& notes = parts[v];
        std::vector<Note> old(notes.begin() + i, notes.begin() + j);
        notes.erase(notes.begin() + i, notes.begin() + j);
        notes.insert(notes.begin() + i, nw.begin(), nw.end());
        int ta = nw.front().start, tb = nw.back().end;
        if (valid(v, ta, tb)) {
            int firstOrn = ta;
            for (auto& n : nw)
                if (n.orn) {
                    firstOrn = n.start;
                    break;
                }
            mark(v, firstOrn, fig, (int)nw.size() - (j - i));
            return true;
        }
        notes.erase(notes.begin() + i, notes.begin() + i + (int)nw.size());
        notes.insert(notes.begin() + i, old.begin(), old.end());
        return false;
    }
};

struct Rewrite {
    int i = 0, j = 0;
    std::vector<std::vector<Note>> alts;
};

const Note* nextOf(const std::vector<Note>& ns, int i) {
    if (i + 1 < (int)ns.size() && ns[i + 1].start == ns[i].end) return &ns[i + 1];
    return nullptr;
}

Note with(const Note& n, int start, int end, int pitch, bool orn) {
    Note m = n;
    m.start = start, m.end = end, m.pitch = pitch, m.orn = orn;
    return m;
}

std::optional<Rewrite> figRun(Ctx& ctx, int v, const std::vector<Note>& ns, int i, int kmax, int step) {
    const Note& n = ns[i];
    const Note* nx = nextOf(ns, i);
    if (!nx || nx->pitch == n.pitch) return std::nullopt;
    auto sc = ctx.scaleBetween(n.start, nx->start);
    int lo = std::min(n.pitch, nx->pitch), hi = std::max(n.pitch, nx->pitch);
    std::vector<int> mids;
    for (int p = lo + 1; p < hi; ++p)
        if (sc[mod12(p)]) mids.push_back(p);
    if (nx->pitch < n.pitch) std::reverse(mids.begin(), mids.end());
    int k = (int)mids.size();
    if (!(1 <= k && k <= kmax)) return std::nullopt;
    std::vector<int> seq{n.pitch};
    seq.insert(seq.end(), mids.begin(), mids.end());
    seq.push_back(nx->pitch);
    for (size_t q = 0; q + 1 < seq.size(); ++q)
        if (std::abs(seq[q + 1] - seq[q]) > 2) return std::nullopt;
    if (n.dur() - k * step < std::max(step, E)) return std::nullopt;
    int t = n.end - k * step;
    std::vector<Note> nw{with(n, n.start, t, n.pitch, n.orn)};
    for (int p : mids) {
        nw.push_back(with(n, t, t + step, p, true));
        t += step;
    }
    return Rewrite{i, i + 1, {nw}};
}

std::optional<Rewrite> figChromatic(Ctx&, int, const std::vector<Note>& ns, int i) {
    const Note& n = ns[i];
    const Note* nx = nextOf(ns, i);
    if (!nx || std::abs(nx->pitch - n.pitch) != 2 || n.dur() < Q) return std::nullopt;
    int p = floordiv(n.pitch + nx->pitch, 2);
    return Rewrite{i, i + 1, {{with(n, n.start, n.end - E, n.pitch, n.orn), with(n, n.end - E, n.end, p, true)}}};
}

std::optional<Rewrite> figNeighbor(Ctx& ctx, int v, const std::vector<Note>& ns, int i) {
    const Note& n = ns[i];
    bool fine = ctx.lv.v[v] >= 8;
    if (n.dur() < (fine ? Q : 2 * Q)) return std::nullopt;
    int grid = fine ? E : Q;
    int m = n.start + (n.dur() / 2) / grid * grid;
    int ln = n.dur() < 2 * Q ? X : E;
    if (m - ln <= n.start) return std::nullopt;
    auto sc = ctx.scaleBetween(n.start, n.start);
    bool up = v == VS || v == VA;
    const int su[4] = {1, 2, -1, -2}, sd[4] = {-1, -2, 1, 2};
    for (int q = 0; q < 4; ++q) {
        int d = up ? su[q] : sd[q];
        int p = n.pitch + d;
        if (sc[mod12(p)])
            return Rewrite{i, i + 1, {{with(n, n.start, m - ln, n.pitch, n.orn), with(n, m - ln, m, p, true),
                                       with(n, m, n.end, n.pitch, n.orn)}}};
    }
    return std::nullopt;
}

std::optional<Rewrite> figSuspension(Ctx& ctx, int v, const std::vector<Note>& ns, int i, int length) {
    const Note& n = ns[i];
    const Note* nx = nextOf(ns, i);
    if (!nx || !ctx.chordStarts.count(nx->start) || nx->start % Q) return std::nullopt;
    int d = nx->pitch - n.pitch;
    if ((d != -1 && d != -2) || nx->dur() < length + E) return std::nullopt;
    for (int w = 0; w < 4; ++w) {
        if (w == v || w == VB) continue;
        int p = pitchAt(ctx.parts[w], nx->start);
        if (p != -1000 && mod12(p) == mod12(nx->pitch)) return std::nullopt;
    }
    Note held = with(n, n.start, nx->start + length, n.pitch, true);
    Note res = with(*nx, nx->start + length, nx->end, nx->pitch, nx->orn);
    return Rewrite{i, i + 2, {{held, res}}};
}

std::optional<Rewrite> figAnticipation(Ctx& ctx, int, const std::vector<Note>& ns, int i) {
    const Note& n = ns[i];
    const Note* nx = nextOf(ns, i);
    if (!nx || nx->pitch == n.pitch || n.dur() < Q || !ctx.chordStarts.count(nx->start)) return std::nullopt;
    if (std::abs(nx->pitch - n.pitch) > 4) return std::nullopt;
    return Rewrite{i, i + 1, {{with(n, n.start, n.end - E, n.pitch, n.orn), with(n, n.end - E, n.end, nx->pitch, true)}}};
}

std::optional<Rewrite> figArpeggio(Ctx& ctx, int v, const std::vector<Note>& ns, int i) {
    const Note& n = ns[i];
    bool fine = ctx.lv.v[v] >= 8;
    if (n.dur() < (fine ? Q : 2 * Q)) return std::nullopt;
    int grid = fine ? E : Q;
    int m = n.start + (n.dur() / 2) / grid * grid;
    if (m <= n.start) return std::nullopt;
    const SChord& ch = ctx.chordAt(m);
    const Note* nx = nextOf(ns, i);
    bool present[12] = {false};
    for (int w = 0; w < 4; ++w) {
        if (w == v) continue;
        int p = pitchAt(ctx.parts[w], m);
        if (p != -1000) present[mod12(p)] = true;
    }
    int leading = (ctx.tonic + 11) % 12;
    std::vector<std::tuple<int, int, int>> options;
    const int ds[8] = {3, 4, 5, -3, -4, -5, 7, -7};
    for (int d : ds) {
        int p = n.pitch + d, pc = mod12(p);
        int role = ch.role[pc];
        if (role != R_ROOT && role != R_THIRD && role != R_FIFTH) continue;
        if (pc == leading && ch.dominant) continue;
        if (nx && std::abs(nx->pitch - p) > 7) continue;
        options.push_back({present[pc] ? 1 : 0, std::abs(d), p});
    }
    if (options.empty()) return std::nullopt;
    std::sort(options.begin(), options.end());
    int p = std::get<2>(options[0]);
    return Rewrite{i, i + 1, {{with(n, n.start, m, n.pitch, n.orn), with(n, m, n.end, p, n.orn)}}};
}

std::optional<Rewrite> figFill(Ctx& ctx, int v, const std::vector<Note>& ns, int i) {
    const Note& n = ns[i];
    const Note* nx = nextOf(ns, i);
    int L = ctx.lv.v[v];
    if (n.dur() < (L >= 10 ? Q : 2 * Q)) return std::nullopt;
    int kcap = L == 8 ? 2 : L == 9 ? 3 : 4;
    int k = std::min(n.dur() / E, kcap);
    int t0 = n.end - k * E;
    int lo = ctx.st.rangeLo[v], hi = ctx.st.rangeHi[v];
    auto sc = ctx.scaleBetween(n.start, n.end - 1);
    std::vector<std::pair<int, std::vector<int>>> best;
    auto chordTone = [&](int p, int t) { return ctx.chordAt(t).has(mod12(p)); };
    std::vector<int> seq{n.pitch};
    std::function<void(int)> walk = [&](int t) {
        if ((int)seq.size() == k) {
            int last = seq.back();
            if (nx && (std::abs(nx->pitch - last) > 4 || nx->pitch == last)) return;
            int sum = 0, big = 0;
            for (size_t q = 0; q + 1 < seq.size(); ++q) {
                int m = std::abs(seq[q + 1] - seq[q]);
                sum += m;
                if (m > 2) ++big;
            }
            std::set<int> uniq(seq.begin(), seq.end());
            int score = sum + 3 * big + 4 * (uniq.size() < seq.size() ? 1 : 0);
            best.push_back({score, seq});
            return;
        }
        int prev = seq.back();
        const int ds[10] = {-2, -1, 1, 2, -5, -4, -3, 3, 4, 5};
        for (int d : ds) {
            int p = prev + d;
            if (!(lo <= p && p <= hi) || (!sc[mod12(p)] && !chordTone(p, t))) continue;
            if (std::abs(d) > 2 && !chordTone(p, t)) continue;
            if (t % Q == 0 && !chordTone(p, t)) continue;
            seq.push_back(p);
            walk(t + E);
            seq.pop_back();
        }
    };
    walk(t0 + E);
    if (best.empty()) return std::nullopt;
    std::sort(best.begin(), best.end());
    Rewrite rw{i, i + 1, {}};
    for (size_t q = 0; q < best.size() && q < 12; ++q) {
        std::vector<Note> nw{with(n, n.start, t0 + E, n.pitch, n.orn)};
        int t = t0 + E;
        for (size_t z = 1; z < best[q].second.size(); ++z) {
            int p = best[q].second[z];
            nw.push_back(with(n, t, t + E, p, !chordTone(p, t)));
            t += E;
        }
        rw.alts.push_back(nw);
    }
    return rw;
}

std::map<std::string, int> ornament(Parts& parts, const std::vector<SChord>& ch, int tonic, bool minor,
                                    const StyleP& st, int barLen) {
    Ctx ctx(parts, ch, tonic, minor, st, barLen);
    using Fn = std::function<std::optional<Rewrite>(int, const std::vector<Note>&, int)>;
    std::vector<std::pair<std::string, Fn>> plan = {
        {"fill", [&](int v, auto& ns, int i) { return figFill(ctx, v, ns, i); }},
        {"pass8", [&](int v, auto& ns, int i) { return figRun(ctx, v, ns, i, 1, E); }},
        {"pass8x2", [&](int v, auto& ns, int i) { return figRun(ctx, v, ns, i, 2, E); }},
        {"run16", [&](int v, auto& ns, int i) { return figRun(ctx, v, ns, i, 3, X); }},
        {"chromatic", [&](int v, auto& ns, int i) { return figChromatic(ctx, v, ns, i); }},
        {"suspension_long", [&](int v, auto& ns, int i) { return figSuspension(ctx, v, ns, i, Q); }},
        {"suspension", [&](int v, auto& ns, int i) { return figSuspension(ctx, v, ns, i, E); }},
        {"neighbor", [&](int v, auto& ns, int i) { return figNeighbor(ctx, v, ns, i); }},
        {"arpeggio", [&](int v, auto& ns, int i) { return figArpeggio(ctx, v, ns, i); }},
        {"anticipation", [&](int v, auto& ns, int i) { return figAnticipation(ctx, v, ns, i); }},
    };
    std::vector<int> order{VB, VT, VA, VS};
    std::stable_sort(order.begin(), order.end(),
                     [&](int a, int b) { return ctx.lv.v[a] > ctx.lv.v[b]; });
    int top = ctx.lv.max();
    int rounds = 1 + (top >= 6) + (top >= 8) + (top >= 10);
    for (int r = 0; r < rounds; ++r) {
        for (auto& [fig, fn] : plan) {
            int need = st.figureMin.empty() ? figureTable().at(fig)
                                            : (st.figureMin.count(fig) ? st.figureMin.at(fig) : 99);
            for (int v : order) {
                bool voiceOk = std::find(st.figureVoices.begin(), st.figureVoices.end(), v) != st.figureVoices.end();
                if (ctx.lv.v[v] < need || !voiceOk) continue;
                auto& notes = parts[v];
                int i = 0;
                while (i < (int)notes.size()) {
                    const Note n = notes[i];
                    if (n.orn || !ctx.allowed(v, n.end - E, fig)) {
                        ++i;
                        continue;
                    }
                    auto rw = fn(v, notes, i);
                    bool done = false;
                    if (rw) {
                        for (auto& alt : rw->alts)
                            if (ctx.tryRewrite(v, rw->i, rw->j, alt, fig)) {
                                i = rw->i + (int)alt.size();
                                done = true;
                                break;
                            }
                    }
                    if (!done) ++i;
                }
            }
        }
    }
    return ctx.count;
}

// ------------------------------------------------------------------ check
std::vector<Issue> checkParts(const Parts& parts, int num, const StyleP& st) {
    std::vector<Issue> issues;
    std::vector<std::pair<int, std::string>> raw;
    std::set<int> times;
    for (int v = 0; v < 4; ++v)
        for (auto& n : parts[v]) times.insert(n.start);
    // ranges, in the prototype's voice order
    for (int v = 0; v < 4; ++v)
        for (auto& n : parts[v])
            if (v != st.dropVoice && !(st.rangeLo[v] <= n.pitch && n.pitch <= st.rangeHi[v]))
                raw.push_back({n.start, std::string(1, VOICE_CH[v]) + " " + std::to_string(n.pitch) +
                                            " вне диапазона"});
    bool havePrev = false;
    std::array<int, 4> prev{};
    for (int t : times) {
        std::array<int, 4> cur;
        bool full = true;
        for (int v = 0; v < 4; ++v) {
            cur[v] = pitchAt(parts[v], t);
            if (cur[v] == -1000) full = false;
        }
        if (!full) {
            havePrev = false;
            continue;
        }
        for (int a = 0; a < 3; ++a)
            if (a != st.dropVoice && a + 1 != st.dropVoice && cur[a] > cur[a + 1])
                raw.push_back({t, std::string(1, VOICE_CH[a]) + " выше " + VOICE_CH[a + 1]});
        if (cur[3] - cur[2] > 12 || (st.dropVoice != VT && cur[2] - cur[1] > 12))
            raw.push_back({t, "расстояние больше октавы в верхних голосах"});
        if (havePrev && st.checkParallels)
            for (int i = 0; i < 4; ++i)
                for (int j = i + 1; j < 4; ++j) {
                    if (prev[i] == cur[i] || prev[j] == cur[j]) continue;
                    if (i == st.dropVoice || j == st.dropVoice) continue;
                    int a = mod12(prev[j] - prev[i]), b = mod12(cur[j] - cur[i]);
                    if (a == b && (b == 0 || b == 7))
                        raw.push_back({t, std::string("параллельные ") + (b == 7 ? "квинты " : "октавы ") +
                                              VOICE_CH[i] + "-" + VOICE_CH[j]});
                }
        prev = cur;
        havePrev = true;
    }
    for (auto& r : raw) issues.push_back({issuePos(r.first, num), r.second});
    return issues;
}

// ------------------------------------------------------------------ obikhod recitation
std::vector<double> recitePattern(int level, int d) {
    int key = level >= 9 ? 9 : level >= 7 ? 7 : level >= 4 ? 4 : level >= 1 ? 1 : 0;
    std::vector<double> r;
    switch (key) {
        case 0: r = {(double)d}; break;
        case 1:
            if (d >= 4) {
                r.assign(d / 2, 2.0);
                if (d % 2) r.push_back(d % 2);
            } else r = {(double)d};
            break;
        case 4:
            if (d >= 3) { r.assign(d - 2, 1.0); r.push_back(2); }
            else if (d == 2) r = {1, 1};
            else r = {(double)d};
            break;
        case 7:
            if (d >= 3) { r = {0.5, 0.5}; for (int q = 0; q < d - 3; ++q) r.push_back(1); r.push_back(2); }
            else if (d == 2) r = {0.5, 0.5, 1};
            else r = {(double)d};
            break;
        default:
            if (d >= 3) { r.assign(2 * (d - 2), 0.5); r.push_back(2); }
            else if (d == 2) r = {0.5, 0.5, 1};
            else r = {(double)d};
    }
    return r;
}

std::vector<Note> recite(const std::vector<Note>& melody, const std::vector<SChord>& ch, int level) {
    if (level <= 0 || ch.size() < 3) return melody;
    int cadenceFrom = ch[ch.size() - 2].start;
    std::vector<Note> out;
    for (auto& n : melody) {
        if (n.start >= cadenceFrom || n.dur() % 480 != 0 || n.dur() / 480 < 2) {
            out.push_back(n);
            continue;
        }
        int t = n.start;
        for (double b : recitePattern(level, n.dur() / 480)) {
            int d = (int)(b * 480);
            out.push_back(Note{t, t + d, n.pitch, n.vel});
            t += d;
        }
    }
    return out;
}

#include "StylesWave2.inc"
#include "StylesWave3.inc"

}  // namespace

// ------------------------------------------------------------------ public: chordal styles
Result arrangeGregorian(const Analysis& an, const Meta& meta, const Settings& s);   // Gregorian.cpp

Result arrangeModal(const Analysis& an, const Meta& meta, const Settings& s);      // Gregorian.cpp

static int fifthsOf(int pc) {   // key signature of the major key on pc
    static const int f[12] = {0, -5, 2, -3, 4, -1, 6, 1, -4, 3, -2, 5};
    return f[mod12(pc)];
}

// choir make-up: ranges, comfortable tessitura and names by voice index (B T A S)
void applyChoir(StyleP& st, const Settings& s) {
    if (st.id == "barbershop") return;   // barbershop is its own TTBB
    auto set = [&](std::array<int, 4> lo, std::array<int, 4> hi, std::array<int, 4> clo, std::array<int, 4> chi,
                   std::array<const char*, 4> names) {
        for (int i = 0; i < 4; ++i) {
            st.rangeLo[i] = lo[i], st.rangeHi[i] = hi[i], st.comfortLo[i] = clo[i], st.comfortHi[i] = chi[i];
            st.partNames[i] = names[i];
        }
    };
    switch (s.choir) {
        case Choir::SATB: break;
        case Choir::SSAA:
            set({53, 55, 58, 60}, {69, 72, 76, 79}, {55, 57, 60, 64}, {65, 69, 72, 76},
                {"Alto 2", "Alto 1", "Soprano 2", "Soprano 1"});
            st.maxTB = 12;
            break;
        case Choir::TTBB:
            set({38, 43, 48, 50}, {60, 62, 67, 70}, {41, 45, 50, 53}, {55, 59, 64, 67},
                {"Bass 2", "Bass 1", "Tenor 2", "Tenor 1"});
            st.melLo = 50, st.melHi = 70, st.melCLo = 53, st.melCHi = 67;
            st.maxTB = 14;
            break;
        case Choir::SAB:
            set({45, 48, 55, 60}, {64, 67, 74, 79}, {48, 52, 57, 65}, {60, 64, 72, 77},
                {"Baritone", "-", "Alto", "Soprano"});
            st.dropVoice = VT;
            break;
        case Choir::SSA:
            set({41, 55, 59, 62}, {62, 69, 74, 77}, {45, 57, 60, 64}, {57, 67, 71, 74},
                {"-", "Alto", "Soprano 2", "Soprano 1"});
            st.melLo = 62, st.melHi = 77, st.melCLo = 64, st.melCHi = 74;
            st.dropVoice = VB;
            st.maxTB = 28;
            break;
    }
    if (s.amateur)
        for (int i = 0; i < 4; ++i) {
            st.rangeLo[i] += 2, st.rangeHi[i] -= 2;
            st.comfortLo[i] += 1, st.comfortHi[i] -= 1;
        }
    if (s.amateur) st.melHi -= 2, st.melCHi -= 1;
}

// another ensemble on the same four-voice search (strings)
static void applySkeleton(StyleP& st, const Skeleton& k) {
    if (!k.on) return;
    for (int i = 0; i < 4; ++i) {
        st.rangeLo[i] = k.rangeLo[i], st.rangeHi[i] = k.rangeHi[i];
        st.comfortLo[i] = k.comfortLo[i], st.comfortHi[i] = k.comfortHi[i];
        st.partNames[i] = k.names[i];
    }
    st.melLo = k.melLo, st.melHi = k.melHi, st.melCLo = k.melCLo, st.melCHi = k.melCHi;
    st.maxSA = k.maxSA, st.maxAT = k.maxAT, st.maxTB = k.maxTB;
    if (k.freeParallels) st.parallels = 0, st.checkParallels = false, st.wHidden = 0;
}

// parts that sit long near the top (or bottom) of their range
std::vector<Issue> tessituraIssues(const Parts& parts, const StyleP& st) {
    std::vector<Issue> out;
    for (int v = 0; v < 4; ++v) {
        if (v == st.dropVoice || parts[v].empty()) continue;
        double tot = 0, hi = 0, lo = 0;
        for (auto& n : parts[v]) {
            tot += n.dur();
            if (n.pitch > st.comfortHi[v] + 1) hi += n.dur();
            if (n.pitch < st.comfortLo[v] - 2) lo += n.dur();
        }
        if (tot <= 0) continue;
        if (hi / tot > 0.35)
            out.push_back({"тесситура", std::string(st.partNames[v]) + " долго поёт высоко (" +
                                            std::to_string((int)std::lround(100 * hi / tot)) + "% времени)"});
        if (lo / tot > 0.6)
            out.push_back({"тесситура", std::string(st.partNames[v]) + " долго поёт низко (" +
                                            std::to_string((int)std::lround(100 * lo / tot)) + "% времени)"});
    }
    return out;
}

Result arrange(const std::vector<Note>& notes, const Meta& meta, const Settings& s) {
    Result res;
    Analysis an = analyse(notes);
    bool harmonized = false;
    // a bare melody (or the user's choice): the plug-in picks the chords first
    bool bare = an.chords.empty() && !an.melody.empty();
    if (s.input == InputMode::HarmonizeMelody || (s.input == InputMode::Auto && bare)) {
        std::vector<Note> mel = an.melody;
        if (mel.empty() || (bare && isMonophonic(notes))) {
            mel.clear();
            for (auto& n : notes) mel.push_back(Note{n.start, n.end, n.pitch, n.vel});
            std::sort(mel.begin(), mel.end(), [](const Note& a, const Note& b) { return a.start < b.start; });
            for (size_t i = 0; i + 1 < mel.size(); ++i) mel[i].end = std::min(mel[i].end, mel[i + 1].start);
        }
        int lvl = clamp10(s.global);
        an.chords = harmonize(mel, meta, s.style, lvl, s.variant, an.tonic, an.minor);
        an.melody = mel;
        an.source = MelodySource::SeparateLine;
        harmonized = !an.chords.empty();
    }
    if (an.chords.empty()) {
        res.error = "в файле не найдено аккордов";
        return res;
    }
    std::string melodyLabel = harmonized ? "только мелодия, аккорды подобраны плагином"
                              : an.source == MelodySource::SeparateTrack ? "отдельная дорожка"
                              : an.source == MelodySource::SeparateLine ? "отдельная линия над аккордами"
                                                                        : "верхние ноты аккордов";
    auto finish = [&](Result r) {
        r.melodyLabel = melodyLabel;
        r.harmonized = harmonized;
        if (r.keyFifths == 99) {
            r.keyFifths = fifthsOf(an.minor ? an.tonic + 3 : an.tonic);
            r.keyMinor = an.minor;
        }
        // silent helper voices of three-part choirs are not parts
        std::vector<Part> kept;
        for (auto& p : r.parts)
            if (p.name.empty() || p.name[0] != '-') kept.push_back(p);
        r.parts = kept;
        // locked parts come back exactly as they were
        for (auto& p : r.parts) {
            auto it = s.locks.find(p.name);
            if (it == s.locks.end()) continue;
            p.notes = it->second;
            r.locked.push_back(p.name);
        }
        return r;
    };
    if (s.style == Style::Gregorian) return finish(arrangeGregorian(an, meta, s));
    if (s.style == Style::Znamenny || s.style == Style::Byzantine) return finish(arrangeModal(an, meta, s));
    if (s.style == Style::Epic) return finish(arrangeEpic(an, meta, s));
    if (s.style == Style::SacredHarp) return finish(arrangeSacredHarp(an, meta, s));
    if (s.style == Style::Bulgarian) return finish(arrangeBulgarian(an, meta, s));
    if (s.style == Style::Georgian) return finish(arrangeGeorgian(an, meta, s));
    if (s.style == Style::Tintinnabuli) return finish(arrangeTintinnabuli(an, meta, s));
    if (s.style == Style::CloseHarmony) return finish(arrangeCloseHarmony(an, meta, s));
    if (s.style == Style::AcappellaPop) return finish(arrangeAcappellaPop(an, meta, s));
    if (s.style == Style::Tenore) return finish(arrangeTenore(an, meta, s));
    if (s.style == Style::Soviet) return finish(arrangeSoviet(an, meta, s));

    StyleP st = s.style == Style::Obikhod        ? obikhodStyle()
                : s.style == Style::Contemporary ? contemporaryStyle()
                : s.style == Style::Gospel       ? gospelStyle()
                : s.style == Style::Barbershop   ? barbershopStyle()
                : s.style == Style::Renaissance  ? renaissanceStyle()
                : s.style == Style::Partes       ? partesStyle()
                : s.style == Style::Spiritual    ? spiritualStyle()
                : s.style == Style::Anglican     ? anglicanStyle()
                                                 : bachStyle();
    Levels lv = resolveLevels(s, an.realMelody());
    if (st.melodyVoice == VA && an.realMelody()) {      // protect the voice that actually sings the melody
        lv.v[VA] = s.A >= 0 ? clamp10(s.A) : 0;
        lv.v[VS] = s.S >= 0 ? clamp10(s.S) : clamp10(s.global);
    }
    tuneStyle(st, lv);
    applyChoir(st, s);
    applySkeleton(st, s.skeleton);
    st.variant = s.variant;
    for (int v = 0; v < 4; ++v) {
        auto it = s.locks.find(st.partNames[v]);
        if (it != s.locks.end()) st.locked[v] = true, st.lockNotes[v] = it->second;
    }
    std::vector<Chord> chords = an.chords;
    if (st.id == "gospel") chords = gospelHarmony(chords, an.tonic, lv.max());
    if (st.id == "spiritual") chords = gospelHarmony(chords, an.tonic, std::min(lv.max(), 4));
    if (st.id == "barbershop") chords = barbershopHarmony(chords, an.melody, an.tonic, lv.max());
    auto sch = translateAll(chords, an.tonic, an.minor, st);
    bool flex = an.source == MelodySource::TopOfChords;
    std::vector<Note> melody = an.melody;
    if (st.recitation && flex) melody = recite(melody, sch, lv.v[VS]);
    int shift = melodyShift(melody, st);
    auto slices = makeSlices(sch, melody, shift, st.pulse, flex);
    std::vector<Voicing> path;
    if (!viterbi(slices, sch, st, an.tonic, path, res.error)) return finish(res);
    Parts parts = toParts(slices, path, st.homorhythm, st.melodyVoice);
    res.figures = ornament(parts, sch, an.tonic, an.minor, st, meta.num * TPB);
    if (st.id == "renaissance") {
        int n = renaissanceEntries(parts, sch, an.tonic, an.minor, st, meta.num * TPB, lv.max());
        if (n) res.figures["imitation"] = n;
    }
    if (st.id == "spiritual") {
        int n = callResponse(parts, sch, melody, lv.max());
        if (n) res.figures["запев и ответ"] = n;
    }
    res.issues = checkParts(parts, meta.num, st);
    for (auto& i : tessituraIssues(parts, st)) res.issues.push_back(i);
    if (st.id == "contemporary" && s.choir == Choir::SATB && !s.skeleton.on) {
        res.parts = contemporaryDivisi(parts, sch, lv.max(), st);
    } else if (st.id == "partes") {
        int tEnd = 0;
        for (auto& c : sch) tEnd = std::max(tEnd, c.end);
        res.parts = partesChoirs(parts, meta.num * TPB * 4 / std::max(1, meta.den), tEnd, lv.max(), st);
    } else {
        res.parts = {{st.partNames[VS], parts[VS]}, {st.partNames[VA], parts[VA]},
                     {st.partNames[VT], parts[VT]}, {st.partNames[VB], parts[VB]}};
    }
    if (s.octavists && s.style == Style::Obikhod && s.choir == Choir::SATB) {
        Part oc{"Octavists", {}};
        for (auto& n : parts[VB])
            if (n.pitch - 12 >= 29) oc.notes.push_back(Note{n.start, n.end, n.pitch - 12, n.vel});
        res.parts.push_back(oc);
    }
    res.styleLabel = st.label;
    res.keyLabel = an.keyName();
    res.melodyShift = flex ? 0 : shift;
    for (auto& c : sch) res.chords.push_back(c.name() == c.srcName ? c.srcName : c.srcName + "→" + c.name());
    for (auto& c : sch) {
        Span sp{c.start, c.end, c.root, {}, c.name()};
        for (int pc = 0; pc < 12; ++pc)
            if (c.has(pc)) sp.pcs.insert(pc);
        res.spans.push_back(sp);
    }
    res.levels = {lv.v[VS], lv.v[VA], lv.v[VT], lv.v[VB]};
    return finish(res);
}

// ---------------------------------------------------------------- live voicing
namespace {
StyleP liveStyle(Style st) {
    switch (st) {
        case Style::Obikhod: case Style::Znamenny: case Style::Gregorian: case Style::Byzantine:
        case Style::Bulgarian: case Style::Georgian: case Style::Tenore: case Style::Partes:
            return obikhodStyle();
        case Style::Contemporary: case Style::Tintinnabuli: case Style::AcappellaPop: return contemporaryStyle();
        case Style::Gospel: case Style::CloseHarmony: return gospelStyle();
        case Style::Spiritual: return spiritualStyle();
        case Style::Barbershop: return barbershopStyle();
        case Style::Renaissance: return renaissanceStyle();
        case Style::Anglican: return anglicanStyle();
        default: return bachStyle();
    }
}
}  // namespace

LiveVoicing LiveVoicer::voice(const std::vector<LiveKey>& keysIn, const Settings& s, bool topIsMelody, int octaveShift) {
    LiveVoicing out;
    if (keysIn.empty()) return out;
    std::vector<LiveKey> keys = keysIn;
    std::sort(keys.begin(), keys.end(), [](const LiveKey& a, const LiveKey& b) { return a.pitch < b.pitch; });
    std::vector<int> hs;
    for (auto& k : keys) hs.push_back(k.pitch);
    std::set<int> pcs;
    for (int p : hs) pcs.insert(mod12(p));
    Chord c;
    // re-fingering inside the chord that is already sounding (an inversion, a note let go, the bass moved
    // an octave) keeps the chord: only the bass and the top follow the hands
    bool inside = !memPcs_.empty() && std::includes(memPcs_.begin(), memPcs_.end(), pcs.begin(), pcs.end());
    if (inside) {
        c.root = memRoot_, c.quality = memQuality_;
        c.bassPc = mod12(hs.front());
        c.pcs = memPcs_;
    } else if (pcs.size() >= 2) {
        std::vector<Note> ns;
        for (int p : hs) ns.push_back(Note{0, TPB * 4, p, 80});
        Analysis an = analyse(ns);
        if (!an.chords.empty()) c = an.chords.front();
    }
    if (c.pcs.empty()) {   // one key: a major triad on it
        c.root = mod12(hs.front()), c.quality = "";
        c.bassPc = c.root;
        c.pcs = {c.root};
    }
    if (!inside) memPcs_ = pcs, memRoot_ = c.root, memQuality_ = c.quality;
    c.start = 0, c.end = TPB * 4;
    StyleP st = liveStyle(s.style);
    Levels lv = resolveLevels(s, topIsMelody);
    tuneStyle(st, lv);
    applyChoir(st, s);
    applySkeleton(st, s.skeleton);
    st.variant = s.variant;
    int tonic = c.root;
    bool minor = c.quality == "m" || c.quality == "m7";
    auto sch = translateAll({c}, tonic, minor, st);
    SChord prevCh = havePrev_ ? translateAll({[&] {
        Chord p;
        p.root = prevRoot_, p.quality = prevQuality_, p.bassPc = prevRoot_, p.start = 0, p.end = TPB;
        return p;
    }()}, tonic, minor, st)[0] : sch[0];

    // what the hands say: span, register, number of keys, force
    int span = hs.back() - hs.front();
    int nKeys = (int)hs.size();
    double avgVel = 0;
    for (auto& k : keys) avgVel += k.vel;
    avgVel /= nKeys;
    // spacing follows the hands when a full chord is played: close position stays close, a wide one opens up
    if (nKeys >= 3 && span <= 12) st.maxSA = std::min(st.maxSA, 9), st.maxAT = std::min(st.maxAT, 9);
    else if (nKeys >= 3 && span >= 28) st.maxSA = std::max(st.maxSA, 14), st.maxAT = std::max(st.maxAT, 14);
    // register: the top voice sits where the top key is, as far as the voice's range allows
    int topKey = hs.back();
    int mv = st.melodyVoice;
    int wantTop = topKey;
    while (wantTop < st.rangeLo[mv]) wantTop += 12;
    while (wantTop > st.rangeHi[mv]) wantTop -= 12;
    int mel = topIsMelody ? wantTop : -1;
    st.bassAlt.clear();   // the bass sings the bass note the player holds
    Slice sl{TPB, TPB * 2, 1, mel, -1, false, true, false, false};
    Slice psl{0, TPB, 0, -1, -1, false, true, false, false};
    auto cands = candidates(sl, sch[0], st, tonic);
    if (cands.empty()) {
        StyleP wide = st;
        wide.maxSA = wide.maxAT = 24, wide.maxTB = 28, wide.maxST = 99;
        cands = candidates(sl, sch[0], wide, tonic);
    }
    if (cands.empty() && mel >= 0) {
        sl.mel = -1;
        cands = candidates(sl, sch[0], st, tonic);
    }
    if (cands.empty()) {
        StyleP wide = st;
        wide.maxSA = wide.maxAT = 24, wide.maxTB = 28, wide.maxST = 99, wide.relaxed = true;
        cands = candidates(sl, sch[0], wide, tonic);
    }
    if (cands.empty()) return out;
    double best = INF;
    Voicing pick = cands.front().v;
    for (auto& cd : cands) {
        double t = cd.vc;
        if (havePrev_) t += transitionCost(prev_, cd.v, psl, sl, prevCh, sch[0], st, tonic);
        if (!topIsMelody) t += 0.35 * std::abs(cd.v[mv] - wantTop);          // free top: still near the hands
        // a low left hand pulls the bass down, a high one lets it rise
        int wantBass = hs.front();
        while (wantBass < st.rangeLo[VB]) wantBass += 12;
        while (wantBass > st.rangeHi[VB]) wantBass -= 12;
        t += 0.25 * std::abs(cd.v[VB] - wantBass);
        if (t < best) best = t, pick = cd.v;
    }
    prev_ = pick;
    prevRoot_ = c.root;
    prevQuality_ = sch[0].quality;
    havePrev_ = true;

    // velocity of a sung part: the keys nearest to it in the hands weigh most; outer parts follow outer keys
    auto velFor = [&](int rank, int total) {   // rank 0 = top part
        if (nKeys == 1) return keys[0].vel;
        double pos = total <= 1 ? 0.0 : (double)rank / (total - 1);          // 0 top .. 1 bottom
        double x = (1.0 - pos) * (nKeys - 1);                                // position among the keys, bottom = 0
        int i0 = (int)std::floor(x), i1 = std::min(nKeys - 1, i0 + 1);
        double f = x - i0;
        return (int)std::lround(keys[i0].vel * (1 - f) + keys[i1].vel * f);
    };
    struct V { std::string name; int pitch, grp; };
    std::vector<V> vs;
    for (int v = 3; v >= 0; --v) {
        if (v == st.dropVoice) continue;
        vs.push_back({st.partNames[v], pick[v], 3 - v});
    }
    // density: five or more keys add a second soprano on a tone the choir is not singing yet
    if (nKeys >= 5 && st.dropVoice < 0) {
        int S = pick[VS], A = pick[VA];
        int add = -1;
        double bs = 1e9;
        for (int p = S - 1; p > A; --p) {
            int pc = mod12(p);
            if (!pcs.count(pc)) continue;
            bool sung = false;
            for (int q : pick) sung = sung || mod12(q) == pc;
            double cost = (sung ? 2.0 : 0.0) + (S - p < 3 ? 1.5 : 0.0) + 0.1 * (S - p);
            if (cost < bs) bs = cost, add = p;
        }
        if (add >= 0) vs.insert(vs.begin() + 1, {std::string(st.partNames[VS]) + " 2", add, 0});
    }
    // force: the bass played in octaves, or the whole chord struck hard, doubles the bass an octave down
    bool bassOctave = nKeys >= 2 && hs[1] - hs[0] == 12;
    if ((bassOctave && keys[0].vel >= 80) || avgVel >= 108) {
        int low = pick[VB] - 12;
        if (low >= 31 && st.dropVoice != VB)
            vs.push_back({std::string(st.partNames[VB]) + " 2", low, 3});
    }
    int total = (int)vs.size();
    for (int i = 0; i < total; ++i) {
        out.names.push_back(vs[i].name);
        out.pitches.push_back(vs[i].pitch + 12 * octaveShift);
        out.velocities.push_back(std::max(1, std::min(127, velFor(i, total))));
        out.group.push_back(vs[i].grp);
    }
    out.chord = sch[0].name();
    return out;
}

std::string Result::report() const {
    std::ostringstream o;
    if (!error.empty()) return "Ошибка: " + error;
    o << "Стиль: " << styleLabel << "\nЛад/тональность: " << keyLabel << "\nМелодия: " << melodyLabel;
    if (melodyShift) o << " (перенос " << (melodyShift > 0 ? "+" : "") << melodyShift << " полутонов)";
    o << "\nАккорды: ";
    for (size_t i = 0; i < chords.size(); ++i) o << (i ? " | " : "") << chords[i];
    o << "\nСложность: S" << levels[0] << " A" << levels[1] << " T" << levels[2] << " B" << levels[3];
    if (!figures.empty()) {
        static const std::map<std::string, std::string> ru = {
            {"pass8", "проходящие"}, {"pass8x2", "двойные проходящие"}, {"run16", "пассажи шестнадцатыми"},
            {"chromatic", "хроматические проходящие"}, {"neighbor", "вспомогательные"},
            {"suspension", "задержания"}, {"suspension_long", "долгие задержания"}, {"arpeggio", "скачки по аккорду"},
            {"anticipation", "предъёмы"}, {"fill", "линии восьмыми"}, {"imitation", "имитационные вступления"}};
        o << "\nФигуры:";
        bool firstF = true;
        for (auto& f : figures) {
            auto it = ru.find(f.first);
            o << (firstF ? " " : ", ") << (it == ru.end() ? f.first : it->second) << " " << f.second;
            firstF = false;
        }
    }
    if (!locked.empty()) {
        o << "\nЗакреплены:";
        for (size_t i = 0; i < locked.size(); ++i) o << (i ? ", " : " ") << locked[i];
    }
    if (issues.empty())
        o << "\nПроверка: нарушений нет";
    else
        for (auto& i : issues) o << "\n! " << i.where << ": " << i.what;
    return o.str();
}

}  // namespace choir
