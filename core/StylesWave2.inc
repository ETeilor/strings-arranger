// Second wave of chordal styles. Included into Chordal.cpp inside its anonymous namespace,
// so it shares the voicing search, the ornament pass and the checks.

// ------------------------------------------------------------------ profiles
const std::map<std::string, std::pair<std::string, std::vector<int>>>& colorTable() {
    static const std::map<std::string, std::pair<std::string, std::vector<int>>> t = {
        {"add9", {"", {2}}},     {"madd9", {"m", {2}}},  {"9", {"7", {2}}},    {"maj9", {"maj7", {2}}},
        {"m9", {"m7", {2}}},     {"6", {"", {9}}},       {"m6", {"m", {9}}},   {"sus2", {"?", {2}}},
        {"sus4", {"?", {5}}},    {"mmaj7", {"m", {11}}}, {"5", {"?", {}}}};
    return t;
}

StyleP contemporaryStyle() {
    StyleP s = bachStyle();
    s.id = "contemporary";
    s.label = "Contemporary (Уитакр, Лауридсен)";
    s.keepColors = true;
    s.colorMap = colorTable();
    s.wColor = -1.0;
    s.sus4AsSuspension = false;
    s.parallels = 0;
    s.checkParallels = false;
    s.wHidden = 0, s.wOverlap = 1.0, s.wLeadOuter = 0, s.wLeadInner = 0, s.wSeventh = 0.5;
    s.wMoveA = 3.0, s.wMoveT = 3.0, s.wMoveB = 1.5;     // a chord change moves as few voices as it can
    s.dThird = 1.0, s.dThirdInv = 0.5, s.wNoFifth = 0.5;
    s.figureMin = {{"suspension", 3}, {"suspension_long", 6}, {"pass8", 7}};
    s.figureVoices = {VA, VT};
    return s;
}

StyleP gospelStyle() {
    StyleP s = bachStyle();
    s.id = "gospel";
    s.label = "Госпел";
    s.keepColors = true;
    s.colorMap = colorTable();
    s.wColor = -1.5;
    s.sus4AsSuspension = false;
    s.parallels = 0;
    s.checkParallels = false;
    s.wHidden = 0, s.wLeadOuter = 1.0, s.wLeadInner = 1.0, s.wSeventh = 1.0;
    s.maxST = 12;                                     // soprano, alto, tenor inside one octave
    int lo[4] = {40, 55, 60, 64}, hi[4] = {55, 67, 72, 76};
    for (int i = 0; i < 4; ++i) s.comfortLo[i] = lo[i], s.comfortHi[i] = hi[i];
    s.wMoveA = 1.0, s.wMoveT = 1.0, s.wMoveB = 0.4;
    s.dThird = 1.0, s.wNoFifth = 0.3;
    s.figureMin = {{"anticipation", 3}, {"pass8", 2}, {"neighbor", 5}, {"chromatic", 7}, {"run16", 8}, {"fill", 9}};
    return s;
}

StyleP barbershopStyle() {
    StyleP s = bachStyle();
    s.id = "barbershop";
    s.label = "Барбершоп (TTBB)";
    int rl[4] = {40, 47, 50, 57}, rh[4] = {60, 65, 69, 74};
    int cl[4] = {43, 50, 53, 60}, ch[4] = {55, 62, 66, 72};
    for (int i = 0; i < 4; ++i) s.rangeLo[i] = rl[i], s.rangeHi[i] = rh[i], s.comfortLo[i] = cl[i], s.comfortHi[i] = ch[i];
    s.melodyVoice = VA;                               // the lead sings the melody, the tenor above it
    s.melLo = 50, s.melHi = 69, s.melCLo = 53, s.melCHi = 66;
    s.keep = {"", "m", "dim", "7", "m7", "dim7", "m7b5"};
    s.reduce = {{"6", ""}, {"m6", "m"}, {"add9", ""}, {"madd9", "m"}, {"9", "7"}, {"maj9", ""}, {"m9", "m7"},
                {"maj7", ""}, {"mmaj7", "m"}, {"aug", ""}, {"sus2", "?"}, {"sus4", "?"}, {"5", "?"}};
    s.sus4AsSuspension = false;
    s.maxSA = 9, s.maxAT = 12, s.maxTB = 14;          // close, ringing chords
    s.wNoFifth = 4.0, s.wNoSeventh = 8.0;             // a seventh chord sounds all four notes
    s.parallels = 30.0;                               // avoided hard, reported when unavoidable
    s.wHidden = 0;
    s.homorhythm = true;
    s.partNames[VB] = "Bass", s.partNames[VT] = "Baritone", s.partNames[VA] = "Lead", s.partNames[VS] = "Tenor";
    s.figureMin = {{"pass8", 4}, {"neighbor", 6}};
    s.figureVoices = {VB, VT, VS};
    return s;
}

StyleP renaissanceStyle() {
    StyleP s = bachStyle();
    s.id = "renaissance";
    s.label = "Ренессансная полифония";
    s.keep = {"", "m", "dim"};
    s.reduce = {{"6", ""},  {"m6", "m"},  {"add9", ""},   {"madd9", "m"}, {"9", ""},     {"maj9", ""},
                {"m9", "m"}, {"mmaj7", "m"}, {"7", ""},   {"maj7", ""},   {"m7", "m"},   {"m7b5", "dim"},
                {"dim7", "dim"}, {"aug", ""}, {"sus2", "?"}, {"sus4", "?"}, {"5", "?"}};
    // strict style: dissonance only as passing tone, neighbour or prepared suspension
    s.figureMin = {{"pass8", 1}, {"neighbor", 3}, {"suspension", 4}, {"pass8x2", 5}, {"suspension_long", 6}};
    return s;
}

// ------------------------------------------------------------------ harmony enrichment
Chord makeChord(int start, int end, int root, const std::string& q, int bassPc) {
    Chord c;
    c.start = start, c.end = end, c.root = root, c.quality = q, c.bassPc = bassPc;
    c.pcs = {root};
    return c;
}

// gospel: sevenths everywhere, ninths higher up the knob, passing diminished chords
std::vector<Chord> gospelHarmony(const std::vector<Chord>& in, int tonic, int level) {
    std::vector<Chord> out;
    for (size_t i = 0; i < in.size(); ++i) {
        Chord c = in[i];
        bool last = i + 1 == in.size();
        const Chord* nx = last ? nullptr : &in[i + 1];
        if (level >= 2) {
            if (c.quality == "") {
                bool dominantFn = mod12(c.root - tonic) == 7 || (level >= 4 && nx && mod12(nx->root - c.root) == 5 &&
                                                                 mod12(c.root - tonic) != 0);
                c.quality = dominantFn ? "7" : last ? "6" : "maj7";
            } else if (c.quality == "m") {
                c.quality = "m7";
            }
        }
        if (level >= 5 && !last) {
            if (c.quality == "7") c.quality = "9";
            else if (c.quality == "maj7") c.quality = "maj9";
            else if (c.quality == "m7") c.quality = "m9";
        }
        if (level >= 4 && nx && c.end - c.start >= 960 && mod12(nx->root - c.root) == 2) {
            int mid = c.end - 480;
            Chord dim = makeChord(mid, c.end, (c.root + 1) % 12, "dim7", (c.root + 1) % 12);
            c.end = mid;
            out.push_back(c);
            out.push_back(dim);
            continue;
        }
        out.push_back(c);
    }
    return out;
}

// barbershop: dominant sevenths ("barbershop sevenths"), and swipes: the harmony moves under
// a long melody note to the dominant seventh of the next chord
std::vector<Chord> barbershopHarmony(const std::vector<Chord>& in, const std::vector<Note>& melody, int tonic,
                                     int level) {
    auto pcsOf7 = [](int r) { return std::set<int>{r, (r + 4) % 12, (r + 7) % 12, (r + 10) % 12}; };
    std::vector<Chord> out;
    for (size_t i = 0; i < in.size(); ++i) {
        Chord c = in[i];
        bool last = i + 1 == in.size();
        const Chord* nx = last ? nullptr : &in[i + 1];
        if (!last && c.quality == "" &&
            (mod12(c.root - tonic) == 7 || (level >= 3 && nx && mod12(nx->root - c.root) == 5)))
            c.quality = "7";
        if (level >= 5 && nx && c.end - c.start >= 960) {
            int x = (nx->root + 7) % 12;
            int mid = c.start + ((c.end - c.start) / 2) / 480 * 480;
            auto xp = pcsOf7(x);
            bool fits = x != c.root;
            for (auto& n : melody)
                if (n.start < c.end && n.end > mid && !xp.count(n.pitch % 12)) fits = false;
            if (fits && mid > c.start) {
                Chord sw = makeChord(mid, c.end, x, "7", x);
                c.end = mid;
                out.push_back(c);
                out.push_back(sw);
                continue;
            }
        }
        out.push_back(c);
    }
    return out;
}

// ------------------------------------------------------------------ contemporary divisi
std::vector<Part> contemporaryDivisi(const Parts& parts, const std::vector<SChord>& ch, int level, const StyleP& st) {
    auto chordAt = [&](int t) -> const SChord& {
        for (auto& c : ch)
            if (c.start <= t && t < c.end) return c;
        return ch.back();
    };
    const int order[4] = {VS, VA, VT, VB};
    const char* base[4] = {"Soprano", "Alto", "Tenor", "Bass"};
    std::vector<Part> out;
    bool splitAll = level >= 4;
    for (int k = 0; k < 4; ++k) {
        int v = order[k];
        bool split = level >= 1 && (splitAll || v == VS || v == VA);
        if (!split) {
            out.push_back({base[k], parts[v]});
            continue;
        }
        std::vector<Note> second;
        int prev = -1;
        for (auto& n : parts[v]) {
            int q = n.pitch;
            if (v == VB) {
                q = n.pitch - 12 >= 36 ? n.pitch - 12 : n.pitch;           // basses divide into octaves
            } else {
                const SChord& c = chordAt(n.start);
                int lowerVoice = v - 1;
                const Note* below = nullptr;
                for (auto& m : parts[lowerVoice])
                    if (m.start <= n.start && n.start < m.end) below = &m;
                int floorP = below ? below->pitch : st.rangeLo[v] - 3;
                double best = 1e9;
                for (int p = n.pitch - 1; p >= n.pitch - 7; --p) {
                    if (p < st.rangeLo[v] - 3) break;
                    int pc = mod12(p);
                    int gap = n.pitch - p;
                    if (gap <= 2 && !(level >= 7 || (level >= 4 && p >= 60))) continue;   // seconds: high and bright only
                    if (p < 55 && gap < 3) continue;                       // low register: no seconds
                    if (p < 50 && gap < 5) continue;
                    double score;
                    bool isColor = std::find(c.colors.begin(), c.colors.end(), pc) != c.colors.end();
                    if (isColor) score = 0;
                    else if (pc == (c.root + 2) % 12 && c.quality != "dim" && c.quality != "dim7") score = 1;
                    else if (c.has(pc) && c.role[pc] != R_ROOT) score = 2;
                    else if (pc == (c.root + 9) % 12 && c.seventh < 0) score = 2.5;
                    else if (c.has(pc)) score = 3;
                    else continue;
                    if (prev >= 0) score += 0.3 * std::abs(p - prev);
                    if (p <= floorP) score += 1.5;                         // under the next part: allowed, not preferred
                    if (score < best) best = score, q = p;
                }
            }
            prev = q;
            if (!second.empty() && second.back().pitch == q && second.back().end == n.start)
                second.back().end = n.end;
            else
                second.push_back(Note{n.start, n.end, q, n.vel});
        }
        out.push_back({std::string(base[k]) + " 1", parts[v]});
        out.push_back({std::string(base[k]) + " 2", second});
    }
    // the choir blooms: from the bottom up, one group per beat, if the first chord is long enough
    if (level >= 5 && !ch.empty() && ch.front().end - ch.front().start >= 1920) {
        for (auto& p : out) {
            int delay = 0;
            if (p.name.rfind("Tenor", 0) == 0) delay = 480;
            if (p.name.rfind("Alto", 0) == 0) delay = 960;
            if (p.name.rfind("Soprano", 0) == 0) delay = 1440;
            if (!delay || p.notes.empty() || p.name == "Soprano 1") continue;   // the melody keeps its start
            auto& f = p.notes.front();
            if (f.start == ch.front().start && f.end - f.start > delay + 240) f.start += delay;
        }
    }
    return out;
}

// ------------------------------------------------------------------ renaissance: imitative entries
int renaissanceEntries(Parts& parts, const std::vector<SChord>& ch, int tonic, bool minor, const StyleP& st,
                       int barLen, int level) {
    if (level < 3 || ch.empty()) return 0;
    int t0 = ch.front().start, total = ch.back().end - t0;
    int d = level >= 7 ? 1920 : 960;
    while (d > 480 && total < 5 * d) d /= 2;
    if (total < 5 * d) return 0;
    std::vector<Note> motif;
    for (auto& n : parts[VS])
        if (n.start < t0 + d) {
            Note m = n;
            m.end = std::min(m.end, t0 + d);
            m.orn = false;
            motif.push_back(m);
        }
    if (motif.empty()) return 0;
    Ctx ctx(parts, ch, tonic, minor, st, barLen);
    int made = 0;
    const int order[3] = {VA, VT, VB};
    const int shifts[8] = {-5, -7, -12, -17, -19, -24, -29, -31};
    for (int k = 0; k < 3; ++k) {
        int v = order[k], e = t0 + (k + 1) * d;
        auto& notes = parts[v];
        std::vector<Note> rest;                          // what the voice sings after its entry
        for (auto& n : notes) {
            if (n.end <= e + d) continue;
            Note m = n;
            if (m.start < e + d) m.start = e + d;
            rest.push_back(m);
        }
        std::vector<Note> fallback;                      // entry without the motif: just enter late
        for (auto& n : notes) {
            if (n.end <= e) continue;
            Note m = n;
            if (m.start < e) m.start = e;
            fallback.push_back(m);
        }
        bool done = false;
        for (int x : shifts) {
            std::vector<Note> entry;
            bool ok = true;
            for (auto& m : motif) {
                Note q = m;
                q.start = m.start - t0 + e, q.end = m.end - t0 + e, q.pitch = m.pitch + x;
                if (q.pitch < st.rangeLo[v] || q.pitch > st.rangeHi[v]) ok = false;
                const SChord& c = ctx.chordAt(q.start);
                if (q.start % 480 == 0 && !c.has(mod12(q.pitch))) ok = false;
                if (!c.has(mod12(q.pitch))) q.orn = true;
                entry.push_back(q);
            }
            if (!ok) continue;
            auto saved = notes;
            notes = entry;
            notes.insert(notes.end(), rest.begin(), rest.end());
            if (ctx.valid(v, e, e + d)) {
                done = true;
                ++made;
                break;
            }
            notes = saved;
        }
        if (!done) notes = fallback;
    }
    return made;
}

// ------------------------------------------------------------------ cinematic epic
std::vector<int> epicPattern(int level) {          // in eighths, repeated through a chord
    if (level <= 0) return {};
    if (level <= 3) return {2};
    if (level <= 6) return {3, 1, 2, 2};
    if (level <= 8) return {1, 1, 2, 1, 1, 2};
    return {1};
}

Result arrangeEpic(const Analysis& an, const Meta& meta, const Settings& s) {
    Result res;
    Levels lv = resolveLevels(s, an.realMelody());
    StyleP st = bachStyle();
    st.keep = {"", "m", "dim"};
    st.reduce = {{"7", ""}, {"maj7", ""}, {"m7", "m"}, {"m7b5", "dim"}, {"dim7", "dim"}, {"aug", ""},
                 {"6", ""}, {"m6", "m"}, {"add9", ""}, {"madd9", "m"}, {"9", ""}, {"maj9", ""},
                 {"m9", "m"}, {"mmaj7", "m"}, {"sus2", "?"}, {"sus4", "?"}, {"5", "?"}};
    st.sus4AsSuspension = false;
    auto sch = translateAll(an.chords, an.tonic, an.minor, st);
    bool real = an.realMelody();
    int shift = real ? melodyShift(an.melody, st) : 0;
    int level = lv.max();
    int finalStart = sch.back().start;

    // event grid: rhythm hits of the ostinato, chord starts, melody starts
    std::set<int> hits;
    auto pat = epicPattern(level);
    for (auto& c : sch) {
        hits.insert(c.start);
        if (pat.empty() || c.start >= finalStart) continue;
        size_t k = 0;
        for (int t = c.start; t < c.end;) {
            hits.insert(t);
            t += pat[k % pat.size()] * E;
            ++k;
        }
    }
    std::set<int> melStarts;
    for (auto& n : an.melody) melStarts.insert(n.start);
    std::set<int> events(hits);
    events.insert(melStarts.begin(), melStarts.end());
    int tEnd = sch.back().end;
    events.insert(tEnd);
    std::vector<int> ev(events.begin(), events.end());

    auto chordAt = [&](int t) -> const SChord& {
        for (auto& c : sch)
            if (c.start <= t && t < c.end) return c;
        return sch.back();
    };
    auto near = [](int pc, int lo, int hi, int prev) {
        int best = -1;
        for (int p = lo; p <= hi; ++p)
            if (mod12(p) == pc && (best < 0 || std::abs(p - prev) < std::abs(best - prev))) best = p;
        return best;
    };
    Parts parts;
    int pS = 72, pA = 64, pT = 55, pB = 45;
    for (size_t i = 0; i + 1 < ev.size(); ++i) {
        int a = ev[i], b = ev[i + 1];
        if (a < sch.front().start) continue;
        const SChord& c = chordAt(a);
        int mel = -1;
        for (auto& n : an.melody)
            if (n.start <= a && a < n.end) mel = n.pitch;
        int S;
        if (real && mel >= 0) S = mel + shift;
        else S = near(mel >= 0 ? mod12(mel) : c.root, 64, 77, pS);
        int B = near(c.root, 40, 52, pB);                                   // the bass: always the root
        int T = (S - 12 >= 50 && S - 12 <= 64) ? S - 12                     // men double the melody
                                               : near(c.fifth, 48, 64, pT);
        int A = -1;
        double best = 1e9;
        for (int p = 55; p <= 72; ++p) {                                     // open sound under the melody
            int pc = mod12(p), gap = S - p;
            if (gap < 3 || gap > 9 || !c.has(pc)) continue;
            double sc = (pc == c.fifth || pc == c.root ? 0 : 1.5) + 0.2 * std::abs(p - pA) + 1.5 * vnoise(s.variant, a, p);
            if (sc < best) best = sc, A = p;
        }
        if (A < 0) A = near(c.root, 55, 72, pA);
        int vals[4] = {B, T, A, S};
        for (int v = 0; v < 4; ++v) {
            bool rearticulate = hits.count(a) || (v == VS && real && melStarts.count(a)) ||
                                (v == VT && real && melStarts.count(a) && T == S - 12);
            auto& ns = parts[v];
            if (!ns.empty() && ns.back().end == a && ns.back().pitch == vals[v] && !rearticulate)
                ns.back().end = b;
            else
                ns.push_back(Note{a, b, vals[v], 80});
        }
        pS = S, pA = A, pT = T, pB = B;
    }
    StyleP chk = st;
    chk.checkParallels = false;
    chk.rangeLo[VB] = 38;                                                    // low basses, down to D2
    res.issues = checkParts(parts, meta.num, chk);
    res.parts = {{"Soprano", parts[VS]}, {"Alto", parts[VA]}, {"Tenor", parts[VT]}, {"Bass", parts[VB]}};
    res.styleLabel = "Кинематографический эпик";
    res.keyLabel = an.keyName();
    for (auto& c : sch) res.chords.push_back(c.name() == c.srcName ? c.srcName : c.srcName + "→" + c.name());
    res.levels = {lv.v[VS], lv.v[VA], lv.v[VT], lv.v[VB]};
    if (!pat.empty()) res.figures["скандирование"] = (int)hits.size();
    return res;
}
