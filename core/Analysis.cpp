// Step 1: raw notes -> chord timeline + melody line + key.
#include <algorithm>
#include <cmath>
#include "ChoirEngine.h"

namespace choir {

const char* NAMES[12] = {"C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"};

namespace {

struct Template {
    const char* q;
    std::vector<int> iv;
};

const std::vector<Template>& templates() {
    static const std::vector<Template> t = {
        {"", {0, 4, 7}},       {"m", {0, 3, 7}},        {"dim", {0, 3, 6}},
        {"aug", {0, 4, 8}},    {"sus2", {0, 2, 7}},     {"sus4", {0, 5, 7}},
        {"5", {0, 7}},         {"6", {0, 4, 7, 9}},     {"m6", {0, 3, 7, 9}},
        {"7", {0, 4, 7, 10}},  {"maj7", {0, 4, 7, 11}}, {"m7", {0, 3, 7, 10}},
        {"mmaj7", {0, 3, 7, 11}}, {"m7b5", {0, 3, 6, 10}}, {"dim7", {0, 3, 6, 9}},
        {"add9", {0, 2, 4, 7}},   {"madd9", {0, 2, 3, 7}}, {"9", {0, 2, 4, 7, 10}},
        {"maj9", {0, 2, 4, 7, 11}}, {"m9", {0, 2, 3, 7, 10}},
    };
    return t;
}

bool simple(const std::string& q) {
    return q == "" || q == "m" || q == "dim" || q == "7" || q == "m7" || q == "maj7";
}

int mod12(int x) { return ((x % 12) + 12) % 12; }

using Group = std::vector<Note>;

std::vector<Group> groupOnsets(std::vector<Note> notes, int window = TPB / 8) {
    std::stable_sort(notes.begin(), notes.end(), [](const Note& a, const Note& b) {
        return a.start != b.start ? a.start < b.start : a.pitch < b.pitch;
    });
    std::vector<Group> groups;
    Group cur;
    for (auto& n : notes) {
        if (!cur.empty() && n.start - cur[0].start > window) {
            groups.push_back(cur);
            cur.clear();
        }
        cur.push_back(n);
    }
    if (!cur.empty()) groups.push_back(cur);
    return groups;
}

int maxPolyphony(const std::vector<Note>& notes) {
    std::vector<std::pair<int, int>> ev;
    for (auto& n : notes) {
        ev.push_back({n.start, 1});
        ev.push_back({n.end, -1});
    }
    std::sort(ev.begin(), ev.end());
    int cur = 0, best = 0;
    for (auto& e : ev) {
        cur += e.second;
        best = std::max(best, cur);
    }
    return best;
}

const Note& topOf(const Group& g) {
    size_t k = 0;
    for (size_t i = 1; i < g.size(); ++i)
        if (g[i].pitch > g[k].pitch) k = i;
    return g[k];
}

std::pair<int, std::string> recognize(const std::set<int>& pcs, int bassPc) {
    bool have = false;
    double bestS = 0;
    int bestRoot = 0;
    std::string bestQ;
    for (int root = 0; root < 12; ++root) {
        for (auto& t : templates()) {
            std::set<int> tpl;
            for (int i : t.iv) tpl.insert((root + i) % 12);
            int matched = 0, extra = 0;
            for (int pc : pcs) (tpl.count(pc) ? matched : extra)++;
            double miss = 0;
            for (int pc : tpl)
                if (!pcs.count(pc)) miss += (mod12(pc - root) == 7) ? 0.5 : 1.5;
            double s = (double)(matched * 2 - extra * 3) - miss;
            if (root == bassPc) s += 0.6;
            if (simple(t.q)) s += 0.3;
            s -= 0.05 * (double)t.iv.size();
            if (!have || s > bestS) {
                have = true;
                bestS = s;
                bestRoot = root;
                bestQ = t.q;
            }
        }
    }
    return {bestRoot, bestQ};
}

const double KK_MAJ[12] = {6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88};
const double KK_MIN[12] = {6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17};

double corr(const double* a, const double* b) {
    double sa = 0, sb = 0;
    for (int i = 0; i < 12; ++i) sa += a[i];
    for (int i = 0; i < 12; ++i) sb += b[i];
    double ma = sa / 12, mb = sb / 12;
    double num = 0, da = 0, db = 0;
    for (int i = 0; i < 12; ++i) num += (a[i] - ma) * (b[i] - mb);
    for (int i = 0; i < 12; ++i) da += (a[i] - ma) * (a[i] - ma);
    for (int i = 0; i < 12; ++i) db += (b[i] - mb) * (b[i] - mb);
    da = std::pow(da, .5);
    db = std::pow(db, .5);
    return (da != 0 && db != 0) ? num / (da * db) : 0;
}

bool isMinorQuality(const std::string& q) {
    return !q.empty() && q[0] == 'm' && q.rfind("maj", 0) != 0;
}

void detectKey(const std::vector<Chord>& chords, const std::vector<Note>& melody, int& tonic,
               bool& minor) {
    double w[12] = {0};
    for (auto& c : chords)
        for (int pc : c.pcs) w[pc] += (double)(c.end - c.start);
    for (auto& n : melody) w[n.pitch % 12] += n.dur() * 0.5;
    const Chord& last = chords.back();
    const Chord& first = chords.front();
    bool have = false;
    double best = 0;
    for (int t = 0; t < 12; ++t) {
        for (int m = 0; m < 2; ++m) {
            const double* prof = m == 0 ? KK_MAJ : KK_MIN;
            double rot[12];
            for (int i = 0; i < 12; ++i) rot[i] = prof[mod12(i - t)];
            double r = corr(w, rot);
            bool lm = isMinorQuality(last.quality);
            if (last.root == t && (m == 1) == lm) r += 0.15;
            bool fm = isMinorQuality(first.quality);
            if (first.root == t && (m == 1) == fm) r += 0.08;
            if (!have || r > best) {
                have = true;
                best = r;
                tonic = t;
                minor = m == 1;
            }
        }
    }
}

}  // namespace

std::string Chord::name() const {
    std::string n = std::string(NAMES[root]) + quality;
    if (bassPc != root) n += std::string("/") + NAMES[bassPc];
    return n;
}

std::string Analysis::keyName() const {
    return std::string(NAMES[tonic]) + (minor ? " minor" : " major");
}

Analysis analyse(const std::vector<Note>& notes) {
    Analysis an;
    std::set<int> trackSet;
    for (auto& n : notes) trackSet.insert(n.track);
    std::vector<int> tracks(trackSet.begin(), trackSet.end());
    std::vector<Note> melodyNotes, chordNotes;
    bool found = false;

    if (tracks.size() > 1) {
        std::map<int, std::vector<Note>> byTrack;
        for (auto& n : notes) byTrack[n.track].push_back(n);
        std::vector<int> mono;
        for (int t : tracks)
            if (maxPolyphony(byTrack[t]) == 1 && byTrack[t].size() > 1) mono.push_back(t);
        if (!mono.empty()) {
            int mt = mono[0];
            double bestAvg = -1;
            for (int t : mono) {
                double sum = 0;
                for (auto& n : byTrack[t]) sum += n.pitch;
                double avg = sum / (double)byTrack[t].size();
                if (bestAvg < 0 || avg > bestAvg) {
                    bestAvg = avg;
                    mt = t;
                }
            }
            for (auto& n : byTrack[mt]) {
                Note m{n.start, n.end, n.pitch, n.vel};
                melodyNotes.push_back(m);
            }
            for (auto& n : notes)
                if (n.track != mt) chordNotes.push_back(n);
            an.source = MelodySource::SeparateTrack;
            found = true;
        }
    }

    if (!found) {
        auto groups = groupOnsets(notes);
        bool separate = false;
        int cur = -1;   // index into melodyNotes
        auto chordTopAt = [&](int t) {
            int best = -1;
            for (auto& n : chordNotes)
                if (n.start <= t && t < n.end) best = std::max(best, n.pitch);
            return best;
        };
        for (auto& g : groups) {
            const Note& top = topOf(g);
            bool sounding = cur >= 0 && melodyNotes[cur].end > top.start;
            if (g.size() == 1) {
                if (top.pitch <= chordTopAt(top.start)) {
                    chordNotes.insert(chordNotes.end(), g.begin(), g.end());
                    continue;
                }
                separate = true;
            } else if (sounding && top.pitch < melodyNotes[cur].pitch) {
                chordNotes.insert(chordNotes.end(), g.begin(), g.end());
                continue;
            }
            if (sounding) melodyNotes[cur].end = top.start;
            Note m{top.start, top.end, top.pitch, top.vel};
            melodyNotes.push_back(m);
            cur = (int)melodyNotes.size() - 1;
            if (g.size() > 1) chordNotes.insert(chordNotes.end(), g.begin(), g.end());
        }
        an.source = separate ? MelodySource::SeparateLine : MelodySource::TopOfChords;
    }

    std::vector<Group> groups;
    for (auto& g : groupOnsets(chordNotes)) {
        std::set<int> p;
        for (auto& n : g) p.insert(n.pitch % 12);
        if (p.size() >= 2) groups.push_back(g);
    }
    for (size_t i = 0; i < groups.size(); ++i) {
        auto& g = groups[i];
        Chord c;
        c.start = g[0].start;
        if (i + 1 < groups.size())
            c.end = groups[i + 1][0].start;
        else {
            c.end = g[0].end;
            for (auto& n : g) c.end = std::max(c.end, n.end);
        }
        for (auto& n : g) c.pcs.insert(n.pitch % 12);
        size_t lo = 0;
        for (size_t k = 1; k < g.size(); ++k)
            if (g[k].pitch < g[lo].pitch) lo = k;
        c.bassPc = g[lo].pitch % 12;
        auto rq = recognize(c.pcs, c.bassPc);
        c.root = rq.first;
        c.quality = rq.second;
        an.chords.push_back(c);
    }
    if (!an.chords.empty()) {
        int spanEnd = an.chords.back().end;
        std::vector<Note> kept;
        for (auto& n : melodyNotes)
            if (n.start < spanEnd) {
                Note m = n;
                m.end = std::min(m.end, spanEnd);
                kept.push_back(m);
            }
        melodyNotes = kept;
        detectKey(an.chords, melodyNotes, an.tonic, an.minor);
    }
    an.melody = melodyNotes;
    return an;
}

}  // namespace choir
