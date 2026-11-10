// Melody harmonization: a bare tune in, a chord progression out, chosen in the manner of the style.
// Viterbi over a harmonic-rhythm grid: each segment scores how well a chord holds the melody notes
// sounding in it, transitions follow functional harmony (or a modal vocabulary), the end cadences.
#include <algorithm>
#include <cmath>
#include "Internal.h"

namespace choir {
namespace {

const double KK_MAJ[12] = {6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88};
const double KK_MIN[12] = {6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17};

double corr12(const double* a, const double* b) {
    double ma = 0, mb = 0;
    for (int i = 0; i < 12; ++i) ma += a[i] / 12, mb += b[i] / 12;
    double num = 0, da = 0, db = 0;
    for (int i = 0; i < 12; ++i) {
        num += (a[i] - ma) * (b[i] - mb);
        da += (a[i] - ma) * (a[i] - ma);
        db += (b[i] - mb) * (b[i] - mb);
    }
    return da > 0 && db > 0 ? num / std::sqrt(da * db) : 0;
}

struct HChord {
    int degree;           // root relative to the tonic, semitones
    std::string q;        // quality as the analysis spells it
    int target = -1;      // secondary dominant: degree it must resolve to
    std::set<int> iv() const {
        if (q == "") return {0, 4, 7};
        if (q == "m") return {0, 3, 7};
        if (q == "dim") return {0, 3, 6};
        if (q == "7") return {0, 4, 7, 10};
        return {0, 4, 7};
    }
};

enum class Family { Functional, Orthodox, Modal, Jazz, Soviet };

Family familyOf(Style s) {
    switch (s) {
        case Style::Obikhod: case Style::Znamenny: case Style::Partes: return Family::Orthodox;
        case Style::Gregorian: case Style::Byzantine: case Style::Bulgarian: case Style::Georgian:
        case Style::Tintinnabuli: case Style::Tenore: case Style::SacredHarp: case Style::Renaissance:
            return Family::Modal;
        case Style::Gospel: case Style::Spiritual: case Style::CloseHarmony: case Style::Barbershop:
        case Style::AcappellaPop: case Style::Contemporary:
            return Family::Jazz;
        case Style::Soviet: return Family::Soviet;
        default: return Family::Functional;
    }
}

std::vector<HChord> vocabulary(bool minor, Family f, int level) {
    std::vector<HChord> v;
    if (f == Family::Soviet) {
        // the circle of fifths in harmonic minor, and secondary dominants into iv and III
        if (minor) {
            v = {{0, "m"}, {5, "m"}, {10, ""}, {3, ""}, {8, ""}, {2, "dim"}, {7, ""}};
            if (level >= 2) v.push_back({7, "7"});
            if (level >= 3) v.push_back({0, "7", 5}), v.push_back({10, "7", 3});
            if (level >= 6) v.push_back({2, "7", 7});
        } else {
            v = {{0, ""}, {2, "m"}, {4, "m"}, {5, ""}, {7, ""}, {9, "m"}};
            if (level >= 2) v.push_back({7, "7"});
            if (level >= 3) v.push_back({9, "7", 2}), v.push_back({4, "7", 9});
            if (level >= 5) v.push_back({0, "7", 5});
        }
        return v;
    }
    if (!minor) {
        v = {{0, ""}, {2, "m"}, {4, "m"}, {5, ""}, {7, ""}, {9, "m"}};
        if (f == Family::Functional || f == Family::Jazz) v.push_back({11, "dim"});
        if (f != Family::Modal && f != Family::Orthodox && level >= 3) v.push_back({7, "7"});
        if (f == Family::Orthodox && level >= 4) v.push_back({7, "7"});
        if ((f == Family::Functional || f == Family::Jazz) && level >= 5) {
            v.push_back({2, "7", 7});    // V of V
            v.push_back({4, "7", 9});    // V of vi
            v.push_back({0, "7", 5});    // V of IV
        }
        if (f == Family::Modal) v.push_back({10, ""});   // mixolydian colour
    } else {
        v = {{0, "m"}, {3, ""}, {5, "m"}, {8, ""}, {10, ""}};
        if (f == Family::Modal) {
            v.push_back({7, "m"});
            v.push_back({2, "m"});
        } else {
            v.push_back({7, ""});
            v.push_back({2, "dim"});
            if (level >= 3 && f != Family::Orthodox) v.push_back({7, "7"});
            if (f == Family::Orthodox && level >= 4) v.push_back({7, "7"});
            if ((f == Family::Functional || f == Family::Jazz) && level >= 5) {
                v.push_back({0, "7", 5});   // V of iv
                v.push_back({10, "7", 3});  // V of III
            }
        }
    }
    return v;
}

// functional tendency: 0 = tonic, 1 = subdominant, 2 = dominant
int function(const HChord& c, bool minor) {
    int d = c.degree;
    if (c.target >= 0) return 2;
    if (d == 0 || d == (minor ? 3 : 4) || d == (minor ? 8 : 9)) return d == 0 ? 0 : (d == (minor ? 8 : 9) ? 0 : 0);
    if (d == 5 || d == 2) return 1;
    if (d == 7 || d == 11 || (minor && d == 10)) return 2;
    return 0;
}

double transition(const HChord& a, const HChord& b, bool minor, Family f, bool acrossBar) {
    if (a.degree == b.degree && a.q == b.q) return acrossBar ? 0.9 : 0.35;
    double c = 0.4;
    if (a.target >= 0) return b.degree == a.target ? 0.0 : 5.0;
    int fa = function(a, minor), fb = function(b, minor);
    if (fa == 2 && fb == 1) c += 1.6;                        // retrogression V -> IV
    if (fa == 2 && b.degree == 0) c -= 0.5;                  // authentic resolution
    if (fa == 1 && fb == 2) c -= 0.2;
    if (fa == 0 && fb == 1) c -= 0.1;
    int up = ((b.degree - a.degree) % 12 + 12) % 12;
    if (up == 5) c -= 0.25;                                  // root down a fifth: the strongest motion
    if (up == 6) c += 1.0;
    if (f == Family::Orthodox && up == 5 && b.degree == 0 && a.degree == 5) c -= 0.3;   // plagal colour
    if (f == Family::Modal && (up == 2 || up == 10)) c -= 0.2;                          // stepwise roots
    if (f == Family::Soviet && up == 5) c -= 0.35;                                       // sequences down the fifths
    return c;
}

}  // namespace

bool isMonophonic(const std::vector<Note>& notes) {
    std::vector<Note> ns = notes;
    std::sort(ns.begin(), ns.end(), [](const Note& a, const Note& b) { return a.start < b.start; });
    int overlap = 0;
    for (size_t i = 1; i < ns.size(); ++i)
        if (ns[i].start < ns[i - 1].end - TPB / 8) ++overlap;
    return !ns.empty() && overlap <= (int)ns.size() / 20;
}

void melodyKey(const std::vector<Note>& mel, int& tonic, bool& minor) {
    double w[12] = {0};
    for (auto& n : mel) w[mod12(n.pitch)] += n.dur();
    double best = -1e9;
    for (int t = 0; t < 12; ++t)
        for (int m = 0; m < 2; ++m) {
            double rot[12];
            for (int i = 0; i < 12; ++i) rot[i] = (m ? KK_MIN : KK_MAJ)[mod12(i - t)];
            double r = corr12(w, rot);
            if (mod12(mel.back().pitch) == t) r += 0.2;        // tunes end on the tonic
            if (mod12(mel.front().pitch - t) == 0 || mod12(mel.front().pitch - t) == 7) r += 0.05;
            if (r > best) best = r, tonic = t, minor = m == 1;
        }
}

std::vector<Chord> harmonize(const std::vector<Note>& melIn, const Meta& meta, Style style, int level, int variant,
                             int& tonic, bool& minor) {
    std::vector<Note> mel = melIn;
    std::sort(mel.begin(), mel.end(), [](const Note& a, const Note& b) { return a.start < b.start; });
    std::vector<Chord> out;
    if (mel.empty()) return out;
    melodyKey(mel, tonic, minor);
    Family fam = familyOf(style);
    int bar = meta.num * TPB * 4 / std::max(1, meta.den);
    int beat = TPB * 4 / std::max(1, meta.den);
    int seg = level <= 2 ? bar : level <= 6 ? (meta.num % 2 == 0 ? bar / 2 : bar) : beat;
    int t0 = mel.front().start, t1 = mel.back().end;
    // segment grid: from the first note to the next grid line, then whole segments
    std::vector<int> cuts{t0};
    for (int t = (t0 / seg + 1) * seg; t < t1; t += seg) cuts.push_back(t);
    cuts.push_back(t1);
    // the cadence moves faster: the last two bars change at least every half bar (every beat in odd metres)
    {
        int cad = meta.num % 2 == 0 ? bar / 2 : beat;
        int from = std::max(t0, ((t1 - 1) / bar - 1) * bar);
        std::set<int> cs(cuts.begin(), cuts.end());
        for (int t = (from / cad + 1) * cad; t < t1; t += cad)
            if (t > t0 && cad < seg) cs.insert(t);
        cuts.assign(cs.begin(), cs.end());
    }
    // a last segment shorter than a beat joins the previous one
    if (cuts.size() > 2 && cuts.back() - cuts[cuts.size() - 2] < beat) cuts.erase(cuts.end() - 2);
    size_t nSeg = cuts.size() - 1;

    auto vocab = vocabulary(minor, fam, level);
    std::vector<std::vector<double>> em(nSeg, std::vector<double>(vocab.size(), 0));
    for (size_t s = 0; s < nSeg; ++s) {
        int a = cuts[s], b = cuts[s + 1];
        for (size_t k = 0; k < vocab.size(); ++k) {
            auto iv = vocab[k].iv();
            std::set<int> pcs;
            for (int i : iv) pcs.insert(mod12(tonic + vocab[k].degree + i));
            double c = 0;
            for (auto& n : mel) {
                int s0 = std::max(n.start, a), s1 = std::min(n.end, b);
                if (s1 <= s0) continue;
                double w = (double)(s1 - s0) / TPB;
                bool strong = s0 == a || s0 % beat == 0;
                if (strong) w *= 1.8;
                int pc = mod12(n.pitch);
                if (pcs.count(pc)) c -= w;
                else {
                    bool step = false;
                    for (int q : pcs) {
                        int d = mod12(pc - q);
                        if (d == 1 || d == 2 || d == 10 || d == 11) step = true;
                    }
                    c += (step && !strong ? 0.25 : 1.6) * w;
                    if (s0 == a) c += 1.5;                      // the downbeat note must be in the chord
                }
            }
            // seventh chords and diminished triads are spice, not the default
            if (vocab[k].q == "7" && vocab[k].target < 0) c += 0.3;
            if (vocab[k].target >= 0) {
                // a secondary dominant only where the tune fits it completely
                c += 1.4;
                for (auto& n : mel)
                    if (std::max(n.start, a) < std::min(n.end, b) && !pcs.count(mod12(n.pitch))) c += 2.0;
            }
            if (vocab[k].q == "dim") c += 0.6;
            if (variant > 0) c += 0.9 * vnoise(variant, (int)s, (int)k);
            em[s][k] = c;
        }
    }
    // boundaries: begin on the tonic (or the dominant before a pickup), end on V-I
    for (size_t k = 0; k < vocab.size(); ++k) {
        bool tonicChord = vocab[k].degree == 0 && vocab[k].target < 0;
        bool dom = (vocab[k].degree == 7 && vocab[k].target < 0) || (fam == Family::Modal && minor && vocab[k].degree == 10);
        bool sub = vocab[k].degree == 5 && vocab[k].target < 0;
        em[0][k] += tonicChord ? 0 : (dom && t0 % bar != 0) ? 0.3 : 1.6;
        em[nSeg - 1][k] += tonicChord ? 0 : 7.0;
        if (nSeg >= 2) {
            double pen = dom ? 0 : sub ? (fam == Family::Orthodox || fam == Family::Modal ? 0.2 : 0.9) : 2.0;
            em[nSeg - 2][k] += pen;
        }
    }
    std::vector<std::vector<double>> cost(nSeg, std::vector<double>(vocab.size(), 1e18));
    std::vector<std::vector<int>> back(nSeg, std::vector<int>(vocab.size(), -1));
    for (size_t k = 0; k < vocab.size(); ++k) cost[0][k] = em[0][k];
    for (size_t s = 1; s < nSeg; ++s) {
        bool across = cuts[s] % bar == 0;
        for (size_t k = 0; k < vocab.size(); ++k)
            for (size_t j = 0; j < vocab.size(); ++j) {
                double c = cost[s - 1][j] + transition(vocab[j], vocab[k], minor, fam, across) + em[s][k];
                if (c < cost[s][k]) cost[s][k] = c, back[s][k] = (int)j;
            }
    }
    int k = 0;
    for (size_t x = 1; x < vocab.size(); ++x)
        if (cost[nSeg - 1][x] < cost[nSeg - 1][k]) k = (int)x;
    std::vector<int> pick(nSeg);
    for (size_t s = nSeg; s-- > 0;) {
        pick[s] = k;
        if (s) k = back[s][k];
    }
    for (size_t s = 0; s < nSeg; ++s) {
        const HChord& h = vocab[pick[s]];
        int root = mod12(tonic + h.degree);
        if (!out.empty() && out.back().root == root && out.back().quality == h.q) {
            out.back().end = cuts[s + 1];
            continue;
        }
        Chord c;
        c.start = cuts[s], c.end = cuts[s + 1], c.root = root, c.bassPc = root, c.quality = h.q;
        for (int i : h.iv()) c.pcs.insert(mod12(root + i));
        out.push_back(c);
    }
    return out;
}

}  // namespace choir
