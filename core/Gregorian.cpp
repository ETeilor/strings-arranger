// Family B: Gregorian chant composed from a chord progression (see the knowledge base).
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <sstream>
#include "Internal.h"

namespace choir {

namespace {

const int WHITE[7] = {0, 2, 4, 5, 7, 9, 11};

struct ModeInfo {
    const char *auth, *plag, *name;
    int recA, recP;
};

bool modeInfo(int off, ModeInfo& m) {
    switch (off) {
        case 2: m = {"I", "II", "дорийский", 7, 3}; return true;
        case 4: m = {"III", "IV", "фригийский", 8, 5}; return true;
        case 5: m = {"V", "VI", "лидийский", 7, 4}; return true;
        case 7: m = {"VII", "VIII", "миксолидийский", 7, 5}; return true;
        case 0: m = {"V", "VI", "ионийский звукоряд (лидийский с пониженной IV ступенью)", 7, 4}; return true;
        case 9: m = {"I", "II", "эолийский звукоряд (дорийский с пониженной VI ступенью)", 7, 3}; return true;
    }
    return false;
}

using Col = std::array<bool, 12>;

Col collection(int t) {
    Col c{};
    for (int x : WHITE) c[(x + t) % 12] = true;
    return c;
}

void findMode(const std::vector<Chord>& chords, const std::vector<Note>& melody, int finalis, int& tOut,
              int& offOut) {
    double w[12] = {0};
    for (auto& c : chords)
        for (int pc : c.pcs) w[pc] += (double)(c.end - c.start);
    for (auto& n : melody) w[n.pitch % 12] += n.dur() * 0.5;
    bool have = false;
    double best = 0;
    for (int t = 0; t < 12; ++t) {
        int off = mod12(finalis - t);
        ModeInfo mi;
        if (!modeInfo(off, mi)) continue;
        Col col = collection(t);
        double score = 0;
        for (int pc = 0; pc < 12; ++pc)
            if (col[pc]) score += w[pc];
        score += (off == 2 || off == 4 || off == 5 || off == 7) ? 1 : 0;
        if (!have || score > best) {
            have = true, best = score, tOut = t, offOut = off;
        }
    }
}

struct MChord {
    int start, end, root;
    std::set<int> pcs;
    std::string src;
    bool changed, skipped;
};

std::vector<MChord> modalChords(const std::vector<Chord>& chords, const Col& col, OutOfMode pol) {
    std::vector<int> scale;
    for (int pc = 0; pc < 12; ++pc)
        if (col[pc]) scale.push_back(pc);
    std::vector<MChord> out;
    for (auto& c : chords) {
        bool inside = true;
        for (int pc : c.pcs)
            if (!col[pc]) inside = false;
        if (inside || pol == OutOfMode::Substitute) {
            int root = c.root;
            if (!col[root]) {
                int best = -1, bd = 99;
                for (int pc : scale) {   // ascending: ties go to the lower pitch class
                    int d = std::min(mod12(pc - root), mod12(root - pc));
                    if (d < bd) bd = d, best = pc;
                }
                root = best;
            }
            int k = (int)(std::find(scale.begin(), scale.end(), root) - scale.begin());
            std::set<int> pcs{scale[k % 7], scale[(k + 2) % 7], scale[(k + 4) % 7]};
            out.push_back({c.start, c.end, root, pcs, c.name(), !inside, false});
        } else {
            out.push_back({c.start, c.end, c.root, c.pcs, c.name(), false, true});
        }
    }
    std::vector<MChord> merged;
    for (auto& mc : out) {
        if (mc.skipped && !merged.empty()) {
            merged.back().end = mc.end;
            merged.back().src += " " + mc.src + "(пропущен)";
        } else {
            merged.push_back(mc);
        }
    }
    if (!merged.empty() && merged[0].skipped) {
        MChord first = merged.front();
        merged.erase(merged.begin());
        if (!merged.empty()) {
            merged[0].start = first.start;
            merged[0].src = first.src + "(пропущен) " + merged[0].src;
        } else {
            first.skipped = false;
            merged = {first};
        }
    }
    return merged;
}

double jitter(int a, int b, int c) {
    uint32_t h = (uint32_t)a * 73856093u ^ (uint32_t)b * 19349663u ^ (uint32_t)c * 83492791u;
    return (double)((h >> 8) & 0xFF) / 255.0;
}

std::vector<std::vector<int>> phrases(const std::vector<MChord>& m, int finalis) {
    std::vector<std::vector<int>> out;
    std::vector<int> cur;
    for (int i = 0; i < (int)m.size(); ++i) {
        cur.push_back(i);
        bool last = i == (int)m.size() - 1;
        bool cadence = m[i].root == finalis && m[i].end - m[i].start >= 960 && cur.size() >= 3;
        if (last || cadence || cur.size() >= 8) {
            out.push_back(cur);
            cur.clear();
        }
    }
    return out;
}

enum PRole { INITIUM, TENOR, PRE, FINAL, MEDIANT };

std::vector<int> chooseAnchors(const std::vector<MChord>& m, const std::vector<int>& topPcs, int finP, int recP,
                               int lo, int hi, std::vector<PRole>& role) {
    auto phr = phrases(m, mod12(finP));
    role.assign(m.size(), TENOR);
    for (size_t pi = 0; pi < phr.size(); ++pi) {
        bool finalPhrase = pi == phr.size() - 1;
        auto& ph = phr[pi];
        for (size_t k = 0; k < ph.size(); ++k) {
            int i = ph[k];
            if (k == ph.size() - 1) role[i] = finalPhrase ? FINAL : MEDIANT;
            else if (k == 0) role[i] = INITIUM;
            else if (k == ph.size() - 2) role[i] = PRE;
            else role[i] = TENOR;
        }
    }
    std::vector<std::vector<std::pair<int, double>>> cands;
    for (size_t i = 0; i < m.size(); ++i) {
        std::vector<std::pair<int, double>> cs;
        for (int p = lo; p <= hi; ++p) {
            if (!m[i].pcs.count(mod12(p))) continue;
            double v;
            switch (role[i]) {
                case INITIUM: v = 0.5 * std::abs(p - finP); break;
                case TENOR: v = 0.7 * std::abs(p - recP); break;
                case PRE:
                    v = 0.7 * std::min({std::abs(p - (finP + 2)), std::abs(p - (finP - 2)) + 1, std::abs(p - (finP + 4))});
                    break;
                case FINAL: v = p == finP ? 0 : 30 + std::abs(p - finP); break;
                default:
                    v = 0.5 * std::min({std::abs(p - recP), std::abs(p - finP - 3), std::abs(p - finP - 4), std::abs(p - finP)});
            }
            if (topPcs[i] >= 0 && mod12(p) == topPcs[i]) v -= 1.5;
            cs.push_back({p, v});
        }
        if (cs.empty()) cs.push_back({finP, 50.0});
        cands.push_back(cs);
    }
    auto trans = [](int a, int b) {
        int d = std::abs(a - b);
        return 0.3 * d + (d > 7 ? 10 : 0) + (d == 6 ? 10 : 0);
    };
    std::vector<double> cost;
    for (auto& c : cands[0]) cost.push_back(c.second);
    std::vector<std::vector<int>> back;
    for (size_t i = 1; i < cands.size(); ++i) {
        std::vector<double> nc;
        std::vector<int> ptr;
        for (auto& pv : cands[i]) {
            int best = 0;
            double bk = cost[0] + trans(cands[i - 1][0].first, pv.first);
            for (int j = 1; j < (int)cands[i - 1].size(); ++j) {
                double kk = cost[j] + trans(cands[i - 1][j].first, pv.first);
                if (kk < bk) bk = kk, best = j;
            }
            nc.push_back(cost[best] + trans(cands[i - 1][best].first, pv.first) + pv.second);
            ptr.push_back(best);
        }
        cost = nc;
        back.push_back(ptr);
    }
    int k = 0;
    for (int x = 1; x < (int)cost.size(); ++x)
        if (cost[x] < cost[k]) k = x;
    std::vector<int> path{k};
    for (auto it = back.rbegin(); it != back.rend(); ++it) {
        k = (*it)[k];
        path.push_back(k);
    }
    std::reverse(path.begin(), path.end());
    std::vector<int> out;
    for (size_t i = 0; i < path.size(); ++i) out.push_back(cands[i][path[i]].first);
    return out;
}

std::vector<int> scalePitches(const Col& col, int lo, int hi) {
    std::vector<int> r;
    for (int p = lo; p <= hi; ++p)
        if (col[mod12(p)]) r.push_back(p);
    return r;
}

std::vector<int> fillSpan(int anchor, int target, int nNotes, const std::vector<int>& scale, int lo, int hi,
                          int recP, int spanId) {
    if (nNotes <= 1) return {anchor};
    auto idxOf = [&](int p) {
        for (int i = 0; i < (int)scale.size(); ++i)
            if (scale[i] == p) return i;
        return -1;
    };
    int a = idxOf(anchor);
    int tgt = target >= 0 ? idxOf(target) : -1;
    bool hasTgt = target >= 0 && tgt >= 0;
    std::vector<std::pair<double, std::vector<int>>> beam{{0.0, {a}}};
    for (int pos = 1; pos < nNotes; ++pos) {
        std::vector<std::pair<double, std::vector<int>>> nxt;
        for (auto& [sc, seq] : beam) {
            int cur = seq.back();
            const int ds[9] = {-1, 1, -2, 2, 0, -3, 3, -4, 4};
            for (int d : ds) {
                int j = cur + d;
                if (j < 0 || j >= (int)scale.size()) continue;
                int p = scale[j], q = scale[cur];
                if (!(lo <= p && p <= hi) || std::abs(p - q) == 6 || std::abs(p - q) > 7) continue;
                double s = sc + std::abs(d) * 1.0;
                if (d == 0) s += q == recP ? 0.6 : 2.5;
                size_t L = seq.size();
                if (std::abs(d) >= 2 && L >= 2 && (seq[L - 1] - seq[L - 2]) * d > 0) s += 3.0;
                if (L >= 2 && std::abs(seq[L - 1] - seq[L - 2]) >= 2 && (seq[L - 1] - seq[L - 2]) * d >= 0) s += 2.0;
                if (L >= 2 && j == seq[L - 2] && d != 0) s += 0.8;
                if (hasTgt) {
                    int left = nNotes - 1 - pos;
                    int need = std::abs(tgt - j) - 1;
                    if (need > left + 1) s += 4.0 * (need - left - 1);
                }
                s += 0.4 * jitter(spanId, pos, j);
                auto ns = seq;
                ns.push_back(j);
                nxt.push_back({s, ns});
            }
        }
        std::stable_sort(nxt.begin(), nxt.end(), [](auto& x, auto& y) { return x.first < y.first; });
        if (nxt.size() > 60) nxt.resize(60);
        beam = nxt;
    }
    bool have = false;
    double bestS = 0;
    std::vector<int> bestSeq;
    for (auto& [sc0, seq] : beam) {
        double sc = sc0;
        if (hasTgt) {
            int gap = std::abs(tgt - seq.back());
            if (gap == 0) sc += 3.0;
            else if (gap > 2) sc += 4.0 * (gap - 2);
            if (std::abs(scale[tgt] - scale[seq.back()]) == 6) sc += 20;
        }
        if (!have || sc < bestS) have = true, bestS = sc, bestSeq = seq;
    }
    std::vector<int> out;
    for (int j : bestSeq) out.push_back(scale[j]);
    return out;
}

std::vector<int> durations(int n, int units) {
    if (n == 1) return {units};
    std::vector<int> d(n, 1);
    int extra = units - n;
    std::vector<int> order{n - 1, 0};
    for (int k = n - 2; k > 0; --k)
        if ((k + 1) % 3 == 0) order.push_back(k);
    for (int k = 1; k < n - 1; ++k) order.push_back(k);
    int k = 0;
    while (extra > 0) {
        d[order[k % order.size()]] += 1;
        --extra;
        ++k;
    }
    return d;
}

std::vector<Note> renderSpans(const std::vector<std::pair<int, int>>& spans, const std::vector<int>& anchors,
                              int level, const std::vector<int>& scale, int lo, int hi, int rec,
                              const std::vector<PRole>* roles, double densMax = 0.9, double densPerLevel = 0.09) {
    std::vector<Note> notes;
    for (size_t i = 0; i < spans.size(); ++i) {
        int st = spans[i].first, en = spans[i].second;
        int units = std::max(1, (en - st) / E);
        bool last = i == spans.size() - 1;
        bool contiguous = !last && spans[i + 1].first == en;
        double dens = std::min(densMax, densPerLevel * level);
        int n = last ? 1 : std::max(1, std::min(units, 1 + (int)std::nearbyint(dens * (units - 1))));
        int target = contiguous ? anchors[i + 1] : -1;
        auto line = fillSpan(anchors[i], target, n, scale, lo, hi, rec, (int)i);
        auto ds = durations((int)line.size(), units);
        PRole role = roles ? (*roles)[i] : TENOR;
        if (3 <= level && level <= 6 && line[0] == rec && ds[0] >= 4 && role == TENOR) {
            std::vector<int> head(ds[0] / 2, rec), hd(ds[0] / 2, 2);
            if (ds[0] % 2) hd.back() += 1;
            head.insert(head.end(), line.begin() + 1, line.end());
            hd.insert(hd.end(), ds.begin() + 1, ds.end());
            line = head, ds = hd;
        }
        int tt = st;
        for (size_t q = 0; q < line.size(); ++q) {
            notes.push_back(Note{tt, tt + ds[q] * E, line[q], 80});
            tt += ds[q] * E;
        }
        notes.back().end = en;
    }
    return notes;
}

std::vector<int> placeMelody(const std::vector<Note>& mel, const Col& col, int finP) {
    int lo = finP - 9, hi = finP + 16, clo = finP - 5, chi = finP + 12;
    std::vector<std::vector<int>> opts;
    for (size_t k = 0; k < mel.size(); ++k) {
        int pc = mel[k].pitch % 12;
        if (!col[pc]) pc = col[mod12(pc - 1)] ? mod12(pc - 1) : mod12(pc + 1);
        std::vector<int> o;
        for (int p = lo; p <= hi; ++p)
            if (mod12(p) == pc) o.push_back(p);
        if (k == mel.size() - 1 && pc == mod12(finP)) o = {finP};
        opts.push_back(o);
    }
    auto node = [&](int p) { return (double)(std::max(0, clo - p) + std::max(0, p - chi)); };
    auto edge = [&](size_t k, int a, int b) {
        int orig = mel[k].pitch - mel[k - 1].pitch;
        int d = b - a;
        return (std::abs(d) > 7 || std::abs(d) == 6 ? 10 : 0) + 0.2 * std::abs(orig - d);
    };
    std::vector<double> cost;
    for (int p : opts[0]) cost.push_back(node(p));
    std::vector<std::vector<int>> back;
    for (size_t k = 1; k < opts.size(); ++k) {
        std::vector<double> nc;
        std::vector<int> ptr;
        for (int b : opts[k]) {
            int j = 0;
            double bk = cost[0] + edge(k, opts[k - 1][0], b);
            for (int q = 1; q < (int)opts[k - 1].size(); ++q) {
                double kk = cost[q] + edge(k, opts[k - 1][q], b);
                if (kk < bk) bk = kk, j = q;
            }
            nc.push_back(cost[j] + edge(k, opts[k - 1][j], b) + node(b));
            ptr.push_back(j);
        }
        cost = nc;
        back.push_back(ptr);
    }
    int j = 0;
    for (int x = 1; x < (int)cost.size(); ++x)
        if (cost[x] < cost[j]) j = x;
    std::vector<int> path{j};
    for (auto it = back.rbegin(); it != back.rend(); ++it) {
        j = (*it)[j];
        path.push_back(j);
    }
    std::reverse(path.begin(), path.end());
    std::vector<int> out;
    for (size_t k = 0; k < path.size(); ++k) out.push_back(opts[k][path[k]]);
    return out;
}

int below(int p, int steps, const Col& col) {
    int q = p, k = 0;
    while (k < steps) {
        --q;
        if (col[mod12(q)]) ++k;
    }
    return q;
}

std::vector<Note> organum(const std::vector<Note>& chant, const Col& col, Texture kind, int finP, bool plagal,
                          int floor = 41) {
    std::vector<Note> out;
    bool havePrev = false;
    int prev = 0;
    int limit = !plagal ? finP : below(finP, 3, col);
    for (auto& n : chant) {
        int p = n.pitch, q;
        if (kind == Texture::Organum4) {
            q = below(p, 3, col);
            if (p - q != 5) q = (havePrev && prev <= p) ? prev : p;
            q = std::max(q, limit);
            q = std::min(q, p);
        } else {
            q = below(p, 4, col);
            if (p - q != 7) q = (havePrev && prev <= p) ? prev : p - 7;
        }
        if (q < floor) q = (havePrev && floor <= prev && prev <= p) ? prev : p;
        out.push_back(Note{n.start, n.end, q, n.vel});
        prev = q;
        havePrev = true;
    }
    std::vector<Note> merged;
    for (auto& n : out) {
        if (!merged.empty() && merged.back().pitch == n.pitch && merged.back().end == n.start)
            merged.back().end = n.end;
        else
            merged.push_back(n);
    }
    return merged;
}


// The chant shared by the Gregorian, Byzantine and Znamenny styles: mode, modal chords, anchors,
// the line itself in the men's register.
struct Chant {
    std::vector<Note> notes;
    Col col{};
    int t = 0, off = 2, finalis = 0, finP = 48, rec = 0, ambLo = 0, ambHi = 0;
    ModeInfo mi{};
    bool plagal = false;
    std::set<int> kept;
    std::vector<MChord> mch;
};

Chant composeChant(const Analysis& an, OutOfMode policy, int level, double densMax = 0.9, double densPerLevel = 0.09) {
    Chant ch;
    const auto& chords = an.chords;
    bool real = an.realMelody();
    ch.finalis = real ? an.melody.back().pitch % 12 : chords.back().root;
    findMode(chords, an.melody, ch.finalis, ch.t, ch.off);
    ch.col = collection(ch.t);
    modeInfo(ch.off, ch.mi);
    ch.mch = modalChords(chords, ch.col, policy);
    ch.finP = 48;
    for (int p = 48; p <= 59; ++p)
        if (p % 12 == ch.finalis) {
            ch.finP = p;
            break;
        }
    const Col& col = ch.col;
    const ModeInfo& mi = ch.mi;
    int finP = ch.finP;
    if (real) {
        auto pitches = placeMelody(an.melody, col, finP);
        int mn = *std::min_element(pitches.begin(), pitches.end());
        int mx = *std::max_element(pitches.begin(), pitches.end());
        ch.plagal = mn < finP - 2;
        ch.rec = finP + (ch.plagal ? mi.recP : mi.recA);
        ch.ambLo = std::min(mn, ch.plagal ? finP - 5 : finP - 2);
        ch.ambHi = std::max(mx, ch.plagal ? finP + 7 : finP + 12);
        auto scale = scalePitches(col, ch.ambLo - 2, ch.ambHi + 2);
        std::vector<std::pair<int, int>> spans;
        for (auto& n : an.melody) spans.push_back({n.start, n.end});
        ch.notes = renderSpans(spans, pitches, level, scale, ch.ambLo, ch.ambHi, ch.rec, nullptr, densMax, densPerLevel);
        for (size_t k = 0; k + 1 < pitches.size(); ++k)
            if (std::abs(pitches[k + 1] - pitches[k]) > 7) ch.kept.insert(an.melody[k + 1].start);
    } else {
        std::vector<int> topPcs;
        for (auto& c : ch.mch) {
            int tp = -1;
            for (auto& n : an.melody)
                if (n.start <= c.start && c.start < n.end) {
                    tp = n.pitch % 12;
                    break;
                }
            topPcs.push_back(tp);
        }
        ch.rec = finP + mi.recA;
        ch.ambLo = finP - 2, ch.ambHi = finP + 12;
        auto scale = scalePitches(col, ch.ambLo - 2, ch.ambHi + 2);
        std::vector<PRole> roles;
        auto anchors = chooseAnchors(ch.mch, topPcs, finP, ch.rec, ch.ambLo, ch.ambHi, roles);
        std::vector<std::pair<int, int>> spans;
        for (auto& c : ch.mch) spans.push_back({c.start, c.end});
        ch.notes = renderSpans(spans, anchors, level, scale, ch.ambLo, ch.ambHi, ch.rec, &roles, densMax, densPerLevel);
    }
    return ch;
}

// register: men in unison sound best around G3; leave room for a lower voice `gap` below
void placeRegister(Chant& ch, int gap) {
    auto& chant = ch.notes;
    int loC = 999, hiC = -999;
    long long num = 0, den = 0;
    for (auto& n : chant) {
        loC = std::min(loC, n.pitch), hiC = std::max(hiC, n.pitch);
        num += (long long)n.pitch * n.dur();
        den += n.dur();
    }
    double mean = (double)num / (double)den;
    int best = 0;
    bool have = false;
    double bk = 0;
    for (int k : {-12, 0, 12}) {
        if (!(loC + k - gap >= 41 && hiC + k <= 67)) continue;
        double key = std::abs(mean + k - 55);
        if (!have || key < bk) have = true, bk = key, best = k;
    }
    if (best) {
        for (auto& n : chant) n.pitch += best;
        ch.finP += best;
        ch.rec += best;
    }
}

int fifthsOfCol(int t) {   // the white-key collection moved by t = the major key on t
    static const int f[12] = {0, -5, 2, -3, 4, -1, 6, 1, -4, 3, -2, 5};
    return f[mod12(t)];
}

void chantIssues(const Chant& ch, const Meta& meta, std::vector<Issue>& issues) {
    const auto& chant = ch.notes;
    for (size_t q = 0; q + 1 < chant.size(); ++q) {
        int d = std::abs(chant[q + 1].pitch - chant[q].pitch);
        if (d == 6) issues.push_back({issuePos(chant[q].start, meta.num), "мелодический тритон"});
        else if (d > 7 && d != 12)
            issues.push_back({issuePos(chant[q].start, meta.num),
                              "скачок " + std::to_string(d) + " полутонов" +
                                  (ch.kept.count(chant[q + 1].start) ? " (скачок вашей мелодии, сложить его в квинту не удалось)" : "")});
    }
    for (auto& n : chant)
        if (!ch.col[mod12(n.pitch)]) issues.push_back({issuePos(n.start, meta.num), "звук вне лада"});
    if (mod12(chant.back().pitch) != mod12(ch.finP))
        issues.push_back({issuePos(chant.back().start, meta.num), "последний звук не финалис"});
}

std::vector<std::string> modalChordNames(const Chant& ch) {
    std::vector<std::string> out;
    for (auto& c : ch.mch) {
        std::string x = c.src;
        if (c.changed) x += std::string("→") + NAMES[c.root] + (ch.col[(c.root + 4) % 12] ? "" : "m");
        out.push_back(x);
    }
    return out;
}

}  // namespace

Result arrangeGregorian(const Analysis& an, const Meta& meta, const Settings& s) {
    Result res;
    Levels lv = resolveLevels(s, false);
    int level = lv.v[VS];
    Chant chs = composeChant(an, s.outOfMode, level);
    int gap = s.texture == Texture::Unison ? 0 : s.texture == Texture::Organum4 ? 5 : 7;
    placeRegister(chs, gap);
    std::vector<Note>& chant = chs.notes;
    const Col& col = chs.col;
    int finP = chs.finP;
    bool plagal = chs.plagal;
    const ModeInfo& mi = chs.mi;
    int finalis = chs.finalis;
    chantIssues(chs, meta, res.issues);

    std::vector<Note> low;
    if (s.texture == Texture::Unison)
        low = chant;
    else {
        low = organum(chant, col, s.texture, finP, plagal);
        for (auto& a : chant)
            for (auto& b : low)
                if (b.start <= a.start && a.start < b.end && b.pitch > a.pitch)
                    res.issues.push_back({issuePos(a.start, meta.num), "органальный голос выше напева"});
    }
    if (s.mixed) {
        Part sp{"Soprano", {}}, ap{"Alto", {}};
        for (auto& n : chant) sp.notes.push_back(Note{n.start, n.end, n.pitch + 12, n.vel});
        for (auto& n : low) ap.notes.push_back(Note{n.start, n.end, n.pitch + 12, n.vel});
        res.parts.push_back(sp);
        res.parts.push_back(ap);
    }
    res.parts.push_back({"Tenor", chant});
    res.parts.push_back({"Bass", low});
    const char* tex = s.texture == Texture::Unison ? "унисон" : s.texture == Texture::Organum4 ? "органум в кварту" : "органум в квинту";
    res.styleLabel = std::string("Григорианский хорал, ") + tex;
    res.keyLabel = std::string(plagal ? mi.plag : mi.auth) + " лад, " + mi.name + " от " + NAMES[finalis];
    res.chords = modalChordNames(chs);
    res.figures["нот в напеве"] = (int)chant.size();
    res.levels = {level, level, level, level};
    res.keyFifths = fifthsOfCol(chs.t);
    return res;
}

namespace {

// a pitch of class pc in [lo, hi], nearest to `prev`
int nearPc(int pc, int lo, int hi, int prev) {
    int best = -1;
    for (int p = lo; p <= hi; ++p)
        if (mod12(p) == pc && (best < 0 || std::abs(p - prev) < std::abs(best - prev))) best = p;
    return best;
}

std::vector<Note> mergeRepeats(const std::vector<Note>& in) {
    std::vector<Note> out;
    for (auto& n : in) {
        if (!out.empty() && out.back().pitch == n.pitch && out.back().end == n.start) out.back().end = n.end;
        else out.push_back(n);
    }
    return out;
}

const MChord& modalAt(const Chant& ch, int t) {
    for (auto& c : ch.mch)
        if (c.start <= t && t < c.end) return c;
    return ch.mch.back();
}

bool consonant(int a, int b) {
    int iv = std::abs(a - b) % 12;
    return iv == 0 || iv == 3 || iv == 4 || iv == 5 || iv == 7 || iv == 8 || iv == 9;
}

}  // namespace

// Znamenny chant harmonised (Kastalsky, Rachmaninov): the chant in the tenors and, an octave
// higher, in the sopranos; an under-voice (podgolosok) in thirds and sixths in the altos;
// the bass on the roots of the modal chords, holding the finalis as a pedal; parallel fifths
// and octaves are part of the style. Byzantine chant: the melody over an ison (drone) that
// follows the support of the chant.
Result arrangeModal(const Analysis& an, const Meta& meta, const Settings& s) {
    Result res;
    Levels lv = resolveLevels(s, false);
    int level = lv.v[VS];
    bool zn = s.style == Style::Znamenny;
    Chant chs = zn ? composeChant(an, s.outOfMode, level, 0.5, 0.05) : composeChant(an, s.outOfMode, level);
    placeRegister(chs, zn ? 0 : 7);
    const auto& chant = chs.notes;
    chantIssues(chs, meta, res.issues);
    res.keyFifths = fifthsOfCol(chs.t);
    res.chords = modalChordNames(chs);
    res.levels = {level, level, level, level};
    res.figures["нот в напеве"] = (int)chant.size();
    std::string mode = std::string(chs.plagal ? chs.mi.plag : chs.mi.auth) + " лад, " + chs.mi.name + " от " + NAMES[chs.finalis];

    if (!zn) {
        // ison: the finalis, or (higher on the knob) the root of the modal chord when it agrees
        // with every chant note above it
        std::vector<Note> ison;
        int prev = chs.finP - 12;
        for (auto& c : chs.mch) {
            int lowC = 999;
            std::vector<int> above;
            for (auto& n : chant)
                if (n.start < c.end && n.end > c.start) {
                    lowC = std::min(lowC, n.pitch);
                    if (n.start <= c.start || n.start % 480 == 0) above.push_back(n.pitch);   // the supporting notes
                }
            int pc = chs.finalis;
            if (level >= 3) {
                bool ok = true;
                for (int p : above) ok = ok && consonant(p, c.root);
                if (ok) pc = c.root;
            }
            int hi = std::min(60, lowC - 3);
            int p = nearPc(pc, 41, hi, prev);
            if (p < 0) p = nearPc(pc, 41, 60, prev);
            if (p > lowC) p = lowC;
            ison.push_back(Note{c.start, c.end, p, 80});
            prev = p;
        }
        ison = mergeRepeats(ison);
        if (s.mixed) {
            Part sp{"Soprano", {}}, ap{"Alto", {}};
            for (auto& n : chant) sp.notes.push_back(Note{n.start, n.end, n.pitch + 12, n.vel});
            for (auto& n : ison) ap.notes.push_back(Note{n.start, n.end, n.pitch + 12, n.vel});
            res.parts.push_back(sp);
            res.parts.push_back(ap);
        }
        res.parts.push_back({"Tenor", chant});
        res.parts.push_back({"Ison", ison});
        res.styleLabel = "Византийский напев с исоном (диатоническое приближение)";
        res.keyLabel = mode;
        return res;
    }

    // znamenny harmonisation
    std::vector<Note> sop, alto, bass;
    int prevA = chs.finP + 4 + 12, prevB = chs.finP - 12;
    int lastBassPc = -1;
    for (auto& n : chant) {
        sop.push_back(Note{n.start, n.end, n.pitch + 12, n.vel});
        const MChord& c = modalAt(chs, n.start);
        bool onBeat = n.start % 480 == 0;
        int best = -1;
        double bs = 1e9;
        for (int p = 55; p <= 72; ++p) {
            int pc = mod12(p), iv = p - n.pitch;
            if (p >= n.pitch + 12) continue;                       // stays under the soprano
            bool chordTone = c.pcs.count(pc) > 0;
            if (!chordTone && (onBeat || !chs.col[pc])) continue;
            double sc;
            if (iv == 3 || iv == 4) sc = 0;                       // a third above the chant
            else if (iv == 8 || iv == 9) sc = 1.0;                // a sixth
            else if (iv == 7 || iv == 5) sc = 2.5;
            else continue;
            if (!chordTone) sc += 1.0;
            sc += 0.3 * std::abs(p - prevA);
            if (sc < bs) bs = sc, best = p;
        }
        if (best < 0) best = nearPc(*c.pcs.begin(), 55, std::min(72, n.pitch + 11), prevA);
        if (best < 0) best = n.pitch + 7;
        alto.push_back(Note{n.start, n.end, best, n.vel});
        prevA = best;
    }
    for (auto& c : chs.mch) {
        int lowC = 999;
        for (auto& n : chant)
            if (n.start < c.end && n.end > c.start) lowC = std::min(lowC, n.pitch);
        int pc = c.root;
        if (level >= 2 && c.pcs.count(chs.finalis) && lastBassPc == chs.finalis) pc = chs.finalis;   // pedal
        int p = nearPc(pc, 41, std::min(55, lowC - 3), prevB);
        if (p < 0) p = nearPc(pc, 41, 55, prevB);
        bass.push_back(Note{c.start, c.end, p, 80});
        prevB = p;
        lastBassPc = pc;
    }
    alto = mergeRepeats(alto);
    bass = mergeRepeats(bass);
    res.parts = {{"Soprano", sop}, {"Alto", alto}, {"Tenor", chant}, {"Bass", bass}};
    if (s.octavists) {
        Part oc{"Octavists", {}};
        for (auto& n : bass)
            if (n.pitch - 12 >= 29) oc.notes.push_back(Note{n.start, n.end, n.pitch - 12, n.vel});
        res.parts.push_back(oc);
    }
    res.styleLabel = "Знаменный распев (гармонизация)";
    res.keyLabel = mode;
    return res;
}

}  // namespace choir
