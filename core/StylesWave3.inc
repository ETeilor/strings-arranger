// Third wave of styles. Included into Chordal.cpp inside its anonymous namespace after
// StylesWave2.inc. Three styles reuse the voicing search (partes, spiritual, Anglican chant);
// the rest are rule-based textures built on a shared event grid.

// ------------------------------------------------------------------ voicing-search profiles
StyleP partesStyle() {
    StyleP s = bachStyle();
    s.id = "partes";
    s.label = "Партесный концерт (Бортнянский)";
    s.keep = {"", "m", "dim", "7", "dim7"};
    s.reduce = {{"6", ""},  {"m6", "m"},    {"add9", ""},  {"madd9", "m"}, {"9", "7"},      {"maj9", ""},
                {"m9", "m"}, {"mmaj7", "m"}, {"maj7", ""}, {"m7", "m"},     {"m7b5", "dim"}, {"aug", ""},
                {"sus2", "?"}, {"sus4", "?"}, {"5", "?"}};
    int lo[4] = {43, 52, 57, 64}, hi[4] = {57, 64, 70, 77};
    for (int i = 0; i < 4; ++i) s.comfortLo[i] = lo[i], s.comfortHi[i] = hi[i];
    s.figureMin = {{"pass8", 2}, {"suspension", 4}, {"neighbor", 5}, {"pass8x2", 7}};
    return s;
}

StyleP spiritualStyle() {
    StyleP s = gospelStyle();
    s.id = "spiritual";
    s.label = "Спиричуэл";
    s.wColor = -0.8;
    s.figureMin = {{"anticipation", 4}, {"pass8", 3}, {"neighbor", 6}, {"chromatic", 8}};
    return s;
}

StyleP anglicanStyle() {
    StyleP s = bachStyle();
    s.id = "anglican";
    s.label = "Англиканское пение (Anglican chant)";
    s.keep = {"", "m", "dim", "7"};
    s.reduce = {{"6", ""},  {"m6", "m"},    {"add9", ""},  {"madd9", "m"}, {"9", "7"},      {"maj9", ""},
                {"m9", "m"}, {"mmaj7", "m"}, {"maj7", ""}, {"m7", "m"},     {"m7b5", "dim"}, {"dim7", "dim"},
                {"aug", ""}, {"sus2", "?"}, {"sus4", "?"}, {"5", "?"}};
    s.homorhythm = true;
    s.recitation = true;     // a reciting chord, then the cadence
    s.figureMin = {{"pass8", 5}, {"suspension", 7}};
    return s;
}

// keep only the part of each note that lies inside [a, b)
std::vector<Note> clipNotes(const std::vector<Note>& ns, int a, int b) {
    std::vector<Note> out;
    for (auto n : ns) {
        n.start = std::max(n.start, a);
        n.end = std::min(n.end, b);
        if (n.end - n.start >= X) out.push_back(n);
    }
    return out;
}

// double choir: phrases of two bars sung in turn by choir I and choir II, the last one by both
std::vector<Part> partesChoirs(const Parts& parts, int barTicks, int tEnd, int level, const StyleP& st) {
    const char* const* names = st.partNames;
    std::vector<Part> out;
    if (level < 3) {
        for (int v = VS; v >= VB; --v) out.push_back({names[v], parts[v]});
        return out;
    }
    int phrase = 2 * barTicks;
    int nPh = std::max(1, (tEnd + phrase - 1) / phrase);
    std::array<std::vector<Note>, 4> one, two;
    for (int k = 0; k < nPh; ++k) {
        int a = k * phrase, b = std::min(tEnd, a + phrase);
        bool last = k == nPh - 1, tutti = last || (level >= 6 && k % 3 == 2);
        for (int v = 0; v < 4; ++v) {
            auto seg = clipNotes(parts[v], a, b);
            if (tutti || k % 2 == 0) one[v].insert(one[v].end(), seg.begin(), seg.end());
            if (tutti || k % 2 == 1) two[v].insert(two[v].end(), seg.begin(), seg.end());
        }
    }
    for (int v = VS; v >= VB; --v) out.push_back({std::string(names[v]) + " I", one[v]});
    for (int v = VS; v >= VB; --v) out.push_back({std::string(names[v]) + " II", two[v]});
    return out;
}

// call and response: where the melody starts a long chord, the lower voices answer in its second half
int callResponse(Parts& parts, const std::vector<SChord>& sch, const std::vector<Note>& melody, int level) {
    if (level < 3) return 0;
    int made = 0;
    for (auto& c : sch) {
        if (c.end - c.start < 960 || &c == &sch.back()) continue;
        bool call = false;
        for (auto& n : melody)
            if (n.start == c.start) call = true;
        if (!call) continue;
        int mid = c.start + ((c.end - c.start) / 2 / Q) * Q;
        for (int v = VB; v <= VA; ++v) {
            std::vector<Note> keep;
            for (auto n : parts[v]) {
                if (n.end <= c.start || n.start >= mid) { keep.push_back(n); continue; }
                if (n.start < c.start) { Note h = n; h.end = c.start; keep.push_back(h); }
                if (n.end > mid) { n.start = mid; keep.push_back(n); }
            }
            parts[v] = keep;
        }
        ++made;
    }
    return made;
}

// ------------------------------------------------------------------ rule-based textures
// The grid every rule-based style walks: chord changes, melody notes, the end.
struct Grid {
    std::vector<SChord> sch;
    std::vector<Note> mel;     // melody, untransposed
    std::vector<int> ev;       // sorted event times, last = end
    std::set<int> chordStarts, melStarts;
    int tonic = 0;
    bool minor = false, real = false;

    const SChord& chordAt(int t) const {
        for (auto& c : sch)
            if (c.start <= t && t < c.end) return c;
        return sch.back();
    }
    int melAt(int t) const {
        int p = -1;
        for (auto& n : mel)
            if (n.start <= t && t < n.end) p = n.pitch;
        return p;
    }
    int end() const { return ev.back(); }
};

Grid makeGrid(const Analysis& an, const StyleP& st, std::vector<int> extraTimes = {}) {
    Grid g;
    g.sch = translateAll(an.chords, an.tonic, an.minor, st);
    g.mel = an.melody;
    g.tonic = an.tonic, g.minor = an.minor, g.real = an.realMelody();
    std::set<int> e(extraTimes.begin(), extraTimes.end());
    for (auto& c : g.sch) e.insert(c.start), g.chordStarts.insert(c.start);
    for (auto& n : g.mel) e.insert(n.start), g.melStarts.insert(n.start);
    int tEnd = g.sch.back().end;
    for (auto& n : g.mel) tEnd = std::max(tEnd, n.end);
    std::vector<int> ev;
    for (int t : e)
        if (t >= g.sch.front().start && t < tEnd) ev.push_back(t);
    ev.push_back(tEnd);
    g.ev = ev;
    return g;
}

// octave shift that puts the melody's middle inside [lo, hi]
int fitShift(const std::vector<Note>& mel, int lo, int hi) {
    if (mel.empty()) return 0;
    int mn = 127, mx = 0;
    for (auto& n : mel) mn = std::min(mn, n.pitch), mx = std::max(mx, n.pitch);
    int mid = (mn + mx) / 2, target = (lo + hi) / 2;
    return 12 * (int)std::lround((target - mid) / 12.0);
}

int nearPc(int pc, int lo, int hi, int prev) {
    int best = -1;
    for (int p = lo; p <= hi; ++p)
        if (mod12(p) == pc && (best < 0 || std::abs(p - prev) < std::abs(best - prev))) best = p;
    if (best < 0) best = lo + mod12(pc - lo);
    return best;
}

// add a note, or extend the previous one when the pitch holds and no new attack is wanted
void putNote(std::vector<Note>& ns, int a, int b, int p, bool attack) {
    if (p < 0) return;
    if (!ns.empty() && ns.back().end == a && ns.back().pitch == p && !attack) ns.back().end = b;
    else ns.push_back(Note{a, b, p, 80});
}

std::vector<int> scalePcs(int tonic, bool minor) {
    std::vector<int> v;
    for (int i = 0; i < 12; ++i)
        if (inScale(i, tonic, minor)) v.push_back(i);
    return v;
}

Result rbResult(const Analysis& an, const Grid& g, const Levels& lv, const std::string& label) {
    Result res;
    res.styleLabel = label;
    res.keyLabel = an.keyName();
    for (auto& c : g.sch) res.chords.push_back(c.name() == c.srcName ? c.srcName : c.srcName + "→" + c.name());
    res.levels = {lv.v[VS], lv.v[VA], lv.v[VT], lv.v[VB]};
    return res;
}

StyleP triadStyle() {
    StyleP st = bachStyle();
    st.keep = {"", "m", "dim"};
    st.reduce = {{"7", ""}, {"maj7", ""}, {"m7", "m"}, {"m7b5", "dim"}, {"dim7", "dim"}, {"aug", ""},
                 {"6", ""}, {"m6", "m"}, {"add9", ""}, {"madd9", "m"}, {"9", ""}, {"maj9", ""},
                 {"m9", "m"}, {"mmaj7", "m"}, {"sus2", "?"}, {"sus4", "?"}, {"5", "?"}};
    st.sus4AsSuspension = false;
    return st;
}

// ---- Sacred Harp: melody in the tenor, open fifths, parallels welcome
Result arrangeSacredHarp(const Analysis& an, const Meta& meta, const Settings& s) {
    Levels lv = resolveLevels(s, an.realMelody());
    int level = lv.max();
    Grid g = makeGrid(an, triadStyle());
    int sh = fitShift(g.mel, 55, 69);
    Parts parts;
    int pTr = 69, pA = 62, pB = 48;
    int beat = TPB;
    for (size_t i = 0; i + 1 < g.ev.size(); ++i) {
        int a = g.ev[i], b = g.ev[i + 1];
        const SChord& c = g.chordAt(a);
        bool last = &c == &g.sch.back();
        bool move = g.chordStarts.count(a) || (level >= 3 && g.melStarts.count(a));
        int m = g.melAt(a);
        int T = m >= 0 ? m + sh : nearPc(c.root, 55, 67, 60);
        int B = nearPc(c.root, 41, 55, pB);
        auto pick = [&](int lo, int hi, int prev, int above) {
            int best = prev;
            double bs = 1e9;
            for (int p = lo; p <= hi; ++p) {
                int pc = mod12(p);
                if (!c.has(pc)) continue;
                if (last && pc == c.third) continue;                         // the final chord is open
                double sc = (pc == c.root || pc == c.fifth ? 0 : 2.0) + 0.25 * std::abs(p - prev) +
                            2.0 * vnoise(s.variant, a, p);
                if (above >= 0 && p <= above) sc += 3;
                if (sc < bs) bs = sc, best = p;
            }
            return best;
        };
        int Tr = move ? pick(62, 77, pTr, T) : pTr;
        int A = move ? pick(55, 70, pA, -1) : pA;
        putNote(parts[VT], a, b, T, g.melStarts.count(a));
        if (move || parts[VS].empty()) {
            putNote(parts[VS], a, b, Tr, g.chordStarts.count(a) || level >= 3);
            putNote(parts[VA], a, b, A, g.chordStarts.count(a) || level >= 3);
            putNote(parts[VB], a, b, B, g.chordStarts.count(a));
        } else {
            for (int v : {VS, VA, VB}) putNote(parts[v], a, b, parts[v].back().pitch, false);
        }
        pTr = Tr, pA = A, pB = B;
    }
    // fuguing entries: bass, then alto, then treble come in one beat apart
    if (level >= 6) {
        int k = 1;
        for (int v : {VA, VS}) {
            int cut = g.sch.front().start + (k++) * 2 * beat;
            parts[v] = clipNotes(parts[v], cut, g.end());
        }
    }
    Result res = rbResult(an, g, lv, "Sacred Harp (пение по фигурным нотам)");
    res.parts = {{"Treble", parts[VS]}, {"Alto", parts[VA]}, {"Tenor", parts[VT]}, {"Bass", parts[VB]}};
    if (level >= 6) res.figures["fuguing entries"] = 2;
    (void)meta;
    return res;
}

// ---- Bulgarian women's choir: melody, a drone, seconds rubbing against the tune
Result arrangeBulgarian(const Analysis& an, const Meta& meta, const Settings& s) {
    Levels lv = resolveLevels(s, an.realMelody());
    int level = lv.max();
    Grid g = makeGrid(an, triadStyle());
    int sh = fitShift(g.mel, 64, 76);
    auto sc = scalePcs(g.tonic, g.minor);
    auto diat = [&](int p) { return std::find(sc.begin(), sc.end(), mod12(p)) != sc.end(); };
    int drone = nearPc(g.tonic, 55, 62, 57);
    std::vector<Note> mel, sec, dr;
    int seconds = 0;
    for (size_t i = 0; i + 1 < g.ev.size(); ++i) {
        int a = g.ev[i], b = g.ev[i + 1];
        const SChord& c = g.chordAt(a);
        int m = g.melAt(a);
        int M = m >= 0 ? m + sh : -1;
        putNote(mel, a, b, M, g.melStarts.count(a));
        // drone: tonic; from level 4 it steps down to the subtonic under a chord built there
        int D = drone;
        if (level >= 4 && mod12(c.root - g.tonic) == 10) D = drone - 2;
        putNote(dr, a, b, D, false);
        if (level >= 3 && M >= 0) {
            int S2 = -1;
            if (level >= 5 && b - a >= Q) {
                // a second below the tune: the rub this singing is known for
                bool half = vnoise(s.variant, a, 1) > 0.5;
                for (int d : {half ? 1 : 2, half ? 2 : 1}) if (diat(M - d) && M - d > D) { S2 = M - d; break; }
            }
            if (S2 < 0) {
                for (int d : {3, 4}) if (diat(M - d) && M - d > D) { S2 = M - d; break; }
            }
            if (S2 < 0) S2 = M;
            if (S2 == M - 1 || S2 == M - 2) ++seconds;
            putNote(sec, a, b, S2, g.melStarts.count(a));
        }
    }
    Result res = rbResult(an, g, lv, "Болгарский женский хор");
    res.parts = {{"Soprano 1", mel}};
    if (!sec.empty()) res.parts.push_back({"Soprano 2", sec});
    res.parts.push_back({"Alto", dr});
    if (seconds) res.figures["секунды"] = seconds;
    (void)meta;
    return res;
}

// ---- Georgian three-part singing: top melody, a middle voice in fourths and fifths, a drone bass
Result arrangeGeorgian(const Analysis& an, const Meta& meta, const Settings& s) {
    Levels lv = resolveLevels(s, an.realMelody());
    int level = lv.max();
    Grid g = makeGrid(an, triadStyle());
    int sh = fitShift(g.mel, 59, 71);
    auto sc = scalePcs(g.tonic, g.minor);
    auto diat = [&](int p) { return std::find(sc.begin(), sc.end(), mod12(p)) != sc.end(); };
    std::vector<Note> top, mid, bass;
    int pM = 57, pB = nearPc(g.tonic, 43, 52, 45);
    for (size_t i = 0; i + 1 < g.ev.size(); ++i) {
        int a = g.ev[i], b = g.ev[i + 1];
        const SChord& c = g.chordAt(a);
        bool last = &c == &g.sch.back();
        int m = g.melAt(a);
        int T = m >= 0 ? m + sh : nearPc(c.root, 59, 71, 64);
        int B = level >= 3 ? nearPc(c.root, 43, 52, pB) : nearPc(g.tonic, 43, 52, pB);
        if (last) B = nearPc(g.tonic, 43, 52, pB);
        int best = pM;
        double bs = 1e9;
        for (int p = B + 3; p <= T + (level >= 7 ? 2 : -1); ++p) {
            if (!diat(p)) continue;
            int up = T - p, dn = p - B;
            double cost = 0.3 * std::abs(p - pM) + 1.5 * vnoise(s.variant, a, p);
            int iu = ((up % 12) + 12) % 12, id = ((dn % 12) + 12) % 12;
            cost += (iu == 5 || iu == 7) ? 0 : (iu == 2 && level >= 5) ? 0.5 : iu == 0 ? 1.5 : 2.5;
            cost += (id == 7 || id == 5 || id == 0) ? 0 : 1.5;
            if (last) cost += (id == 7 ? -3 : 3);
            if (cost < bs) bs = cost, best = p;
        }
        putNote(top, a, b, T, g.melStarts.count(a));
        putNote(mid, a, b, best, g.melStarts.count(a) && level >= 2);
        putNote(bass, a, b, B, g.chordStarts.count(a) && B != pB);
        pM = best, pB = B;
    }
    Result res = rbResult(an, g, lv, "Грузинское многоголосие");
    res.parts = {{"Tenor 1", top}, {"Tenor 2", mid}, {"Bass", bass}};
    (void)meta;
    return res;
}

// ---- Arvo Part, tintinnabuli: a melodic voice and a voice on the tonic triad
Result arrangeTintinnabuli(const Analysis& an, const Meta& meta, const Settings& s) {
    Levels lv = resolveLevels(s, an.realMelody());
    int level = lv.max();
    Grid g = makeGrid(an, triadStyle());
    int sh = fitShift(g.mel, 64, 76);
    std::set<int> triad = {g.tonic, mod12(g.tonic + (g.minor ? 3 : 4)), mod12(g.tonic + 7)};
    auto sc = scalePcs(g.tonic, g.minor);
    auto diat = [&](int p) { return std::find(sc.begin(), sc.end(), mod12(p)) != sc.end(); };
    auto tVoice = [&](int m, bool above) {   // the nearest triad note strictly below (or above) m
        for (int d = 1; d <= 12; ++d) {
            int p = above ? m + d : m - d;
            if (triad.count(mod12(p))) return p;
        }
        return m;
    };
    std::vector<Note> S, A, T, B;
    int pedal = nearPc(g.tonic, 41, 50, 45);
    int k = 0;
    for (size_t i = 0; i + 1 < g.ev.size(); ++i) {
        int a = g.ev[i], b = g.ev[i + 1];
        int m = g.melAt(a);
        if (m < 0) {
            continue;
        }
        int M = m + sh;
        bool att = g.melStarts.count(a);
        if (att) ++k;
        putNote(S, a, b, M, att);
        bool above = level >= 4 && k % 2 == s.variant % 2;   // alternating position from level 4
        int t = tVoice(M, above);
        if (above && t > 79) t = tVoice(M, false);
        putNote(A, a, b, t, att);
        if (level >= 5) {                             // second M-voice a tenth below
            int p = M - 15;
            if (!diat(p)) p = M - 16;
            if (!diat(p)) p = M - 17;
            putNote(T, a, b, p, att);
            if (level >= 7) putNote(B, a, b, tVoice(p, false), att);
        }
        if (level >= 2 && level < 7) putNote(B, a, b, pedal, false);
    }
    Result res = rbResult(an, g, lv, "Тинтиннабули (Арво Пярт)");
    res.parts = {{"Soprano", S}, {"Alto", A}};
    if (!T.empty()) res.parts.push_back({"Tenor", T});
    if (!B.empty()) res.parts.push_back({"Bass", B});
    res.chords.clear();
    res.chords.push_back(std::string(NAMES[g.tonic]) + (g.minor ? "m" : "") + " (тоническое трезвучие)");
    (void)meta;
    return res;
}

// ---- close harmony (vocal jazz): block chords under the lead, drop-2, a bass part
Result arrangeCloseHarmony(const Analysis& an, const Meta& meta, const Settings& s) {
    Levels lv = resolveLevels(s, an.realMelody());
    int level = lv.max();
    StyleP st = bachStyle();
    st.keepColors = true;
    st.colorMap = colorTable();
    Grid g = makeGrid(an, st);
    int sh = fitShift(g.mel, 64, 76);
    std::vector<Note> parts[5];   // lead, 3 inner, bass
    int pBass = 45;
    int blocks = 0;
    for (size_t i = 0; i + 1 < g.ev.size(); ++i) {
        int a = g.ev[i], b = g.ev[i + 1];
        const SChord& c = g.chordAt(a);
        int m = g.melAt(a);
        int L = m >= 0 ? m + sh : nearPc(c.root, 64, 76, 69);
        bool att = g.chordStarts.count(a) || (level >= 2 && g.melStarts.count(a));
        // the chord with its jazz extensions
        std::vector<int> pcs;
        bool minorQ = c.third >= 0 && mod12(c.third - c.root) == 3;
        bool dom = c.seventh >= 0 && mod12(c.seventh - c.root) == 10 && !minorQ;
        pcs.push_back(c.root);
        if (c.third >= 0) pcs.push_back(c.third);
        if (c.fifth >= 0) pcs.push_back(c.fifth);
        if (c.seventh >= 0) pcs.push_back(c.seventh);
        else if (level >= 1) pcs.push_back(mod12(c.root + (minorQ ? 10 : 9)));      // 6 or m7
        if (level >= 4) pcs.push_back(mod12(c.root + 2));                            // 9
        if (level >= 6 && dom) pcs.push_back(mod12(c.root + 9));                     // 13
        for (int col : c.colors) pcs.push_back(col);
        std::set<int> set(pcs.begin(), pcs.end());
        bool passing = !set.count(mod12(L)) && level >= 5 && !g.chordStarts.count(a);
        if (passing) set = {mod12(L), mod12(L + 3), mod12(L + 6), mod12(L + 9)};    // diminished passing chord
        // three notes under the lead, closest first; the root is the bass's, dropped when tones abound
        std::vector<int> inner;
        int p = L - 1;
        while ((int)inner.size() < 3 && p > L - 13) {
            int pc = mod12(p);
            bool skipRoot = pc == c.root && set.size() >= 5 && !passing;
            if (set.count(pc) && pc != mod12(L) && !skipRoot) inner.push_back(p);
            --p;
        }
        while ((int)inner.size() < 3) inner.push_back(inner.empty() ? L - 12 : inner.back() - 5);
        if (level >= 3) {                                   // drop 2 (drop 3 in odd variants)
            inner[s.variant % 2 == 1 ? 1 : 0] -= 12;
            std::sort(inner.rbegin(), inner.rend());
        }
        if (passing) ++blocks;
        putNote(parts[0], a, b, L, g.melStarts.count(a));
        for (int v = 0; v < 3; ++v) putNote(parts[1 + v], a, b, inner[v], att);
        // bass: the root of the chord (walking line from level 7 is built after the loop)
        int B = nearPc(c.bassPc, 40, 52, pBass);
        putNote(parts[4], a, b, B, g.chordStarts.count(a));
        pBass = B;
    }
    if (level >= 7) {                         // walking bass in quarters, chromatic approach to the next root
        parts[4].clear();
        int pb = 45;
        for (size_t ci = 0; ci < g.sch.size(); ++ci) {
            const SChord& c = g.sch[ci];
            int B = nearPc(c.bassPc, 40, 52, pb);
            bool lastChord = ci + 1 == g.sch.size();
            int nb = lastChord ? B : nearPc(g.sch[ci + 1].bassPc, 40, 52, B);
            int seq[3] = {B, nearPc(c.fifth >= 0 ? c.fifth : c.root, 40, 55, B),
                          nearPc(c.third >= 0 ? c.third : c.root, 40, 55, B)};
            int k = 0;
            for (int t = c.start; t < c.end; t += Q, ++k) {
                int e = std::min(t + Q, c.end);
                int p = (!lastChord && e == c.end && k > 0) ? nb + (nb > seq[(k - 1) % 3] ? -1 : 1) : seq[k % 3];
                if (lastChord && k > 0) { parts[4].back().end = e; continue; }
                parts[4].push_back(Note{t, e, p, 80});
            }
            pb = B;
        }
    }
    Result res = rbResult(an, g, lv, "Джазовый close harmony");
    res.parts = {{"Soprano", parts[0]}, {"Alto", parts[1]}, {"Tenor", parts[2]}, {"Baritone", parts[3]},
                 {"Bass", parts[4]}};
    if (blocks) res.figures["проходящие уменьшённые"] = blocks;
    (void)meta;
    return res;
}

// ---- a cappella pop: lead, "oo" pads, a vocal bass with a groove, beatbox
Result arrangeAcappellaPop(const Analysis& an, const Meta& meta, const Settings& s) {
    Levels lv = resolveLevels(s, an.realMelody());
    int level = lv.max();
    StyleP st = bachStyle();
    st.keepColors = true;
    st.colorMap = colorTable();
    // bass groove in eighths per bar
    std::vector<int> groove = level <= 1 ? std::vector<int>{} : level <= 3 ? std::vector<int>{4, 4}
                            : level <= 6 ? std::vector<int>{3, 3, 2} : std::vector<int>{2, 1, 1, 2, 2};
    int bar = meta.num * TPB * 4 / std::max(1, meta.den);
    std::vector<int> extra;
    Grid g0 = makeGrid(an, st);
    for (int t = g0.sch.front().start; t < g0.end(); t += bar) {
        int x = t;
        for (int e8 : groove) { extra.push_back(x); x += e8 * E; }
    }
    Grid g = makeGrid(an, st, extra);
    int sh = fitShift(g.mel, 62, 74);
    std::vector<Note> lead, pad[3], bass, bb;
    int prev[3] = {67, 64, 60};
    int pB = 40, k = 0;
    for (size_t i = 0; i + 1 < g.ev.size(); ++i) {
        int a = g.ev[i], b = g.ev[i + 1];
        const SChord& c = g.chordAt(a);
        int m = g.melAt(a);
        putNote(lead, a, b, m >= 0 ? m + sh : -1, g.melStarts.count(a));
        // pads: three chord tones in 55..70, as close to before as possible, under the lead
        std::vector<int> tones = {c.root};
        if (c.third >= 0) tones.push_back(c.third);
        if (c.seventh >= 0) tones.push_back(c.seventh);
        else if (!c.colors.empty()) tones.push_back(c.colors.front());
        else if (c.fifth >= 0) tones.push_back(c.fifth);
        std::sort(tones.begin(), tones.end());
        tones.erase(std::unique(tones.begin(), tones.end()), tones.end());
        while (tones.size() < 3) tones.push_back(c.fifth >= 0 ? c.fifth : c.root);
        if (s.variant > 0) std::rotate(tones.begin(), tones.begin() + s.variant % 3, tones.begin() + 3);
        std::vector<int> used;
        for (int v = 0; v < 3; ++v) {
            int pc = tones[v];
            int p = nearPc(pc, 55, 70, prev[v]);
            prev[v] = p;
            used.push_back(p);
        }
        std::sort(used.rbegin(), used.rend());
        bool padAttack = g.chordStarts.count(a) || (level >= 5 && std::find(extra.begin(), extra.end(), a) != extra.end());
        for (int v = 0; v < 3; ++v) putNote(pad[v], a, b, used[v], padAttack);
        // bass: root, octave jumps on off-beats at the top levels
        bool hit = std::find(extra.begin(), extra.end(), a) != extra.end() || g.chordStarts.count(a);
        int B = nearPc(c.root, 36, 48, pB);
        if (hit) ++k;
        int bp = (level >= 7 && hit && k % 2 == 0) ? B + 12 : B;
        if (groove.empty()) putNote(bass, a, b, B, g.chordStarts.count(a));
        else putNote(bass, a, b, bp, hit);
        pB = B;
    }
    // beatbox from level 3: kick, snare, hats (General MIDI drum notes)
    int beatbox = 0;
    if (level >= 3) {
        int beat = TPB * 4 / std::max(1, meta.den);
        for (int t = g.sch.front().start; t < g.end(); t += bar) {
            for (int bt = 0; bt < meta.num; ++bt) {
                int at = t + bt * beat;
                if (at >= g.end()) break;
                bool backbeat = meta.num == 4 ? (bt == 1 || bt == 3) : bt == meta.num - 1;
                bb.push_back(Note{at, at + X, backbeat ? 38 : 36, 100});
                if (level >= 6 && bt == 2 && meta.num == 4) bb.push_back(Note{at + E, at + E + X, 36, 90});
                int sub = level >= 8 ? X : level >= 5 ? E : 0;
                if (sub)
                    for (int h = at; h < at + beat; h += sub) bb.push_back(Note{h, h + X / 2, 42, 70});
                beatbox += 1;
            }
        }
        std::sort(bb.begin(), bb.end(), [](const Note& x, const Note& y) {
            return x.start != y.start ? x.start < y.start : x.pitch < y.pitch;
        });
    }
    Result res = rbResult(an, g, lv, "A cappella поп");
    res.parts = {{"Lead", lead}, {"Soprano", pad[0]}, {"Alto", pad[1]}, {"Tenor", pad[2]}, {"Bass", bass}};
    if (!bb.empty()) res.parts.push_back({"Beatbox", bb});
    if (beatbox) res.figures["долей битбокса"] = beatbox;
    return res;
}

// ---- Sardinian cantu a tenore: solo voice over bassu, contra and mesu boghe
Result arrangeTenore(const Analysis& an, const Meta& meta, const Settings& s) {
    Levels lv = resolveLevels(s, an.realMelody());
    int level = lv.max();
    Grid g0 = makeGrid(an, triadStyle());
    std::vector<int> pat = level >= 7 ? std::vector<int>{1, 1, 2} : level >= 4 ? std::vector<int>{2, 1, 1}
                                                                                : std::vector<int>{};
    std::vector<int> extra;
    for (auto& c : g0.sch) {
        if (pat.empty() || &c == &g0.sch.back()) continue;
        size_t k = 0;
        for (int t = c.start; t < c.end; t += pat[k++ % pat.size()] * E) extra.push_back(t);
    }
    Grid g = makeGrid(an, triadStyle(), extra);
    int sh = fitShift(g.mel, 60, 72);
    std::set<int> pulse(extra.begin(), extra.end());
    std::vector<Note> boghe, mesu, contra, bassu;
    int pB = 45;
    for (size_t i = 0; i + 1 < g.ev.size(); ++i) {
        int a = g.ev[i], b = g.ev[i + 1];
        const SChord& c = g.chordAt(a);
        int m = g.melAt(a);
        int M = m >= 0 ? m + sh : -1;
        int root = level >= 3 ? c.root : g.tonic;
        int B = nearPc(root, 41, 50, pB);
        int C = B + 7;
        int third = level >= 3 ? c.third : mod12(g.tonic + (g.minor ? 3 : 4));
        int Me = (level >= 2 && third >= 0) ? nearPc(third, B + 12, B + 19, B + 15) : B + 12;
        if (level >= 6 && M >= 0) {                       // mesu boghe follows the solo a third or sixth under
            const int ds[2][4] = {{3, 4, 8, 9}, {8, 9, 3, 4}};
            for (int d : ds[s.variant % 2])
                if (c.has(mod12(M - d)) && M - d > C) { Me = M - d; break; }
        }
        bool att = pulse.count(a) || g.chordStarts.count(a);
        putNote(boghe, a, b, M, g.melStarts.count(a));
        putNote(bassu, a, b, B, att);
        putNote(contra, a, b, C, att);
        putNote(mesu, a, b, Me, att || (level >= 6 && g.melStarts.count(a)));
        pB = B;
    }
    Result res = rbResult(an, g, lv, "Сардинский cantu a tenore");
    res.parts = {{"Boghe", boghe}, {"Mesu boghe", mesu}, {"Contra", contra}, {"Bassu", bassu}};
    if (!pulse.empty()) res.figures["пульсация"] = (int)pulse.size();
    (void)meta;
    return res;
}

// ---- Soviet estrada and film song (Zatsepin, Gladkov, Krylatov, Pauls; Artemyev's vocalise pads)
// Harmony: harmonic minor with a dominant seventh, the circle-of-fifths sequence, a secondary
// dominant into iv ("A7 -> Dm"), jazz colours higher up, a major final chord at the top levels.
std::vector<Chord> sovietHarmony(const std::vector<Chord>& in, int tonic, bool minor, int level, bool majorFinal) {
    std::vector<Chord> out;
    for (size_t i = 0; i < in.size(); ++i) {
        Chord c = in[i];
        bool last = i + 1 == in.size();
        const Chord* nx = last ? nullptr : &in[i + 1];
        int deg = mod12(c.root - tonic);
        if (minor && deg == 7 && (c.quality == "m" || c.quality == "")) c.quality = level >= 2 ? "7" : "";
        if (!minor && deg == 7 && c.quality == "" && level >= 2) c.quality = "7";
        if (minor && deg == 2 && (c.quality == "dim" || c.quality == "m") && level >= 3) c.quality = "m7b5";
        if (level >= 5 && !last) {
            if (c.quality == "m" && deg != 0) c.quality = "m7";
            else if (c.quality == "" && minor && deg == 10) c.quality = "7";    // VII7 -> III in the sequence
            else if (c.quality == "" && deg != 7 && deg != 0) c.quality = "maj7";
            else if (c.quality == "" && deg == 0 && !minor) c.quality = "6";
        }
        if (level >= 7 && !last && c.quality == "7") c.quality = "9";
        if (last && minor && deg == 0 && level >= 6 && majorFinal) c.quality = "";   // the major third at the end
        // secondary dominant before iv / III (minor) or ii / vi (major), in the second half of a long chord
        if (level >= 4 && nx && c.end - c.start >= 960) {
            int nd = mod12(nx->root - tonic);
            bool target = minor ? (nd == 5 || nd == 3) : (nd == 2 || nd == 9);
            int domRoot = mod12(nx->root + 7);
            if (target && domRoot != c.root) {
                int mid = c.end - std::max(480, ((c.end - c.start) / 2 / 480) * 480);
                if (mid <= c.start) mid = c.start + (c.end - c.start) / 2;
                Chord d = makeChord(mid, c.end, domRoot, "7", domRoot);
                c.end = mid;
                out.push_back(c);
                out.push_back(d);
                continue;
            } else if (target && domRoot == c.root && i > 0 &&
                       (c.quality == "m" || c.quality == "" || c.quality == "maj7")) {
                c.quality = "7";                                                 // the tonic itself turns into V7/iv
            }
        }
        out.push_back(c);
    }
    return out;
}

Result arrangeSoviet(const Analysis& anIn, const Meta& meta, const Settings& s) {
    Levels lv = resolveLevels(s, anIn.realMelody());
    int level = lv.max();
    Analysis an = anIn;
    an.chords = sovietHarmony(anIn.chords, an.tonic, an.minor, level, s.majorFinal);
    StyleP st = bachStyle();
    st.keepColors = true;
    st.colorMap = colorTable();
    st.keep.insert("m6");
    int bar = meta.num * TPB * 4 / std::max(1, meta.den);
    int beat = TPB * 4 / std::max(1, meta.den);
    // bass rhythm: whole chords, then root-fifth halves ("бас-квинта"), then quarters with a chromatic approach
    std::vector<int> extra;
    {
        Grid g0 = makeGrid(an, st);
        int step = level >= 6 ? beat : level >= 3 ? std::max(beat, bar / 2) : 0;
        if (step)
            for (auto& c : g0.sch)
                if (&c != &g0.sch.back())                                       // the final chord is held
                    for (int t = c.start; t < c.end; t += step) extra.push_back(t);
    }
    Grid g = makeGrid(an, st, extra);
    std::set<int> pulse(extra.begin(), extra.end());
    int sh = fitShift(g.mel, 64, 76);
    auto sc = scalePcs(g.tonic, g.minor);
    auto diat = [&](int p) { return std::find(sc.begin(), sc.end(), mod12(p)) != sc.end(); };
    std::vector<Note> S, A, T, B;
    int pT = 57, pB = 45, pA = 62;
    int k = 0;
    int seconds = 0;
    for (size_t i = 0; i + 1 < g.ev.size(); ++i) {
        int a = g.ev[i], b = g.ev[i + 1];
        const SChord& c = g.chordAt(a);
        bool chordStart = g.chordStarts.count(a);
        int m = g.melAt(a);
        int M = m >= 0 ? m + sh : -1;
        putNote(S, a, b, M, g.melStarts.count(a));
        // alto: the second voice of an estrada duet, a third (or sixth) under the tune
        int Av = -1;
        if (M >= 0 && level >= 1) {
            int best = -1;
            double bs = 1e9;
            for (int d : {3, 4, 8, 9, 5}) {
                int p = M - d;
                if (p < 53 || !diat(p) && !c.has(mod12(p))) continue;
                double cost = (d == 3 || d == 4 ? 0 : d == 5 ? 2.5 : 0.8) + (c.has(mod12(p)) ? 0 : 1.2) +
                              0.15 * std::abs(p - pA) + 0.8 * vnoise(s.variant, a, p);
                if (cost < bs) bs = cost, best = p;
            }
            Av = best;
        }
        if (Av < 0) Av = nearPc(c.third >= 0 ? c.third : c.root, 55, 69, pA);
        putNote(A, a, b, Av, g.melStarts.count(a) || chordStart);
        // tenor: a sustained vocalise on "a", the colour tone of the chord when there is one
        int Tv = pT;
        if (chordStart || T.empty()) {
            std::vector<int> pref;
            if (level >= 5 && !c.colors.empty()) pref.push_back(c.colors.front());
            if (c.seventh >= 0) pref.push_back(c.seventh);
            if (c.third >= 0) pref.push_back(c.third);
            pref.push_back(c.fifth >= 0 ? c.fifth : c.root);
            double bs = 1e9;
            for (size_t q = 0; q < pref.size(); ++q) {
                int p = nearPc(pref[q], 50, 65, pT);
                if (Av >= 0 && p >= Av) p -= 12;
                if (p < 48) continue;
                double cost = q * 0.8 + 0.3 * std::abs(p - pT);
                if (cost < bs) bs = cost, Tv = p;
            }
        }
        putNote(T, a, b, Tv, chordStart);
        // bass
        int root = nearPc(c.bassPc, 40, 52, pB);
        int Bv = root;
        if (pulse.count(a) && !chordStart) {
            ++k;
            bool lastBeatOfChord = a + beat >= c.end && &c != &g.sch.back();
            if (level >= 6 && lastBeatOfChord) {
                const SChord& nx = *(&c + 1);
                int target = nearPc(nx.bassPc, 40, 52, root);
                Bv = target + (target > root ? -1 : 1);                      // chromatic approach
                ++seconds;
            } else {
                Bv = (k % 2 == 1 && c.fifth >= 0) ? nearPc(c.fifth, 38, 52, root - 5) : root;
            }
        } else if (chordStart) {
            k = 0;
        }
        putNote(B, a, b, Bv, chordStart || pulse.count(a));
        pT = Tv, pA = Av, pB = root;
    }
    Result res = rbResult(an, g, lv, "Советская эстрада и кино");
    res.parts = {{"Soprano", S}, {"Alto", A}, {"Tenor", T}, {"Bass", B}};
    if (seconds) res.figures["хроматические подходы в басу"] = seconds;
    return res;
}
