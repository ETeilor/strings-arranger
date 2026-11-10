// String section arranger. Three layers:
//   1. skeleton: the four-voice search of the choir engine with string ranges (Violin I, Violin II,
//      Viola, Cello);
//   2. texture: every part turns its skeleton line into string writing by the rule its composer
//      profile gives for the part's complexity level; the basses are derived from the cellos;
//   3. advice: what notes cannot say (bowing, patches, doublings) goes to the report as text.
// Everything is deterministic: the same input and settings always give the same parts.
#include "Strings.h"
#include <algorithm>
#include <climits>
#include <cstdint>
#include <cmath>
#include <sstream>

namespace strings {

const char* PART_NAMES[NPARTS] = {"Violin I", "Violin II", "Viola", "Cello", "Contrabass"};
const char* PART_SHORT[NPARTS] = {"V1", "V2", "Va", "Vc", "Cb"};

namespace {

constexpr int Q = 480, E = 240, X = 120;

// sounding limits of the instruments: lowest note (an open string), comfortable top, hard limit
const int LO[NPARTS] = {55, 55, 48, 36, 28};
const int TOP[NPARTS] = {88, 88, 81, 69, 55};
const int LIMIT[NPARTS] = {100, 100, 88, 81, 67};

enum class Tex { Sustain, Detache, Repeat8, Repeat16, Tremolo, Alberti8, Alberti16, Arp8, Offbeat, PizzBeat,
                 Ostinato, Augurs, Stabs, OctaveUp, Double8vb, Roots, RootsPizz };

const char* texLabel(Tex t) {
    switch (t) {
        case Tex::Sustain: return "выдержанные ноты";
        case Tex::Detache: return "деташе по долям";
        case Tex::Repeat8: return "репетиции восьмыми";
        case Tex::Repeat16: return "репетиции шестнадцатыми";
        case Tex::Tremolo: return "тремоло";
        case Tex::Alberti8: return "альбертиевы фигуры восьмыми";
        case Tex::Alberti16: return "альбертиевы фигуры шестнадцатыми";
        case Tex::Arp8: return "ломаные аккорды";
        case Tex::Offbeat: return "аккорды на слабую долю";
        case Tex::PizzBeat: return "пиццикато по долям";
        case Tex::Ostinato: return "остинато 3+3+2";
        case Tex::Augurs: return "остинато со сбитыми акцентами";
        case Tex::Stabs: return "акцентированные удары";
        case Tex::OctaveUp: return "в октаву (divisi)";
        case Tex::Double8vb: return "октавой ниже виолончелей";
        case Tex::Roots: return "выдержанный бас";
        case Tex::RootsPizz: return "пиццикато на сильные доли";
    }
    return "";
}

struct Step {
    int from;
    Tex tex;
};

struct Profile {
    const char* id;
    const char* label;
    choir::Style base;            // harmonic vocabulary and voice-leading manner of the skeleton
    Section section;              // the size the style is usually played with
    std::vector<Step> ladder[NPARTS];
    int ornCap;                   // sustained lines are never ornamented above this level
    bool legato;                  // sustained notes are slurred
    bool freeParallels;
    bool accents;                 // sforzando on chord changes from level 5
    bool bite;                    // a second is added to accented ostinato chords from level 7
};

using T = Tex;

const Profile& profile(Composer c) {
    static const Profile P[NUM_COMPOSERS] = {
        // motor rhythm: repeated notes in every part, basses with the cellos
        {"vivaldi", "Вивальди", choir::Style::Bach, Section::Baroque,
         {{{0, T::Sustain}, {7, T::Repeat16}},
          {{0, T::Sustain}, {3, T::Repeat8}, {7, T::Repeat16}},
          {{0, T::Sustain}, {3, T::Repeat8}, {9, T::Repeat16}},
          {{0, T::Sustain}, {2, T::Detache}, {5, T::Repeat8}},
          {{0, T::Double8vb}}},
         4, false, false, false, false},
        // counterpoint: four singing lines, the complexity goes into passing notes and suspensions
        {"bach", "Бах", choir::Style::Bach, Section::Baroque,
         {{{0, T::Sustain}},
          {{0, T::Sustain}},
          {{0, T::Sustain}},
          {{0, T::Sustain}},
          {{0, T::Double8vb}}},
         10, false, false, false, false},
        // quartet manner: drum eighths in the middle, detached bass
        {"haydn", "Гайдн", choir::Style::Bach, Section::Classical,
         {{{0, T::Sustain}},
          {{0, T::Sustain}, {3, T::Repeat8}},
          {{0, T::Sustain}, {4, T::Repeat8}},
          {{0, T::Sustain}, {2, T::Detache}, {8, T::Repeat8}},
          {{0, T::Double8vb}}},
         6, false, false, false, false},
        // singing melody over Alberti figures of the second violins
        {"mozart", "Моцарт", choir::Style::Bach, Section::Classical,
         {{{0, T::Sustain}},
          {{0, T::Sustain}, {3, T::Alberti8}, {7, T::Alberti16}},
          {{0, T::Sustain}, {5, T::Repeat8}},
          {{0, T::Sustain}, {2, T::Detache}},
          {{0, T::Double8vb}}},
         7, true, false, false, false},
        // agitation: repeated sixteenths, sforzando on chord changes, melody in octaves at the top
        {"beethoven", "Бетховен", choir::Style::Bach, Section::Classical,
         {{{0, T::Sustain}, {8, T::OctaveUp}},
          {{0, T::Sustain}, {3, T::Repeat8}, {6, T::Repeat16}},
          {{0, T::Sustain}, {3, T::Repeat8}, {7, T::Repeat16}},
          {{0, T::Sustain}, {2, T::Detache}, {5, T::Repeat8}},
          {{0, T::Double8vb}}},
         5, false, false, true, false},
        // melody in octaves, after-beat chords in the middle, singing cellos, pizzicato basses
        {"tchaikovsky", "Чайковский", choir::Style::Bach, Section::Symphonic,
         {{{0, T::Sustain}, {6, T::OctaveUp}},
          {{0, T::Sustain}, {4, T::Offbeat}},
          {{0, T::Sustain}, {4, T::Offbeat}, {8, T::Arp8}},
          {{0, T::Sustain}},
          {{0, T::Double8vb}, {3, T::RootsPizz}}},
         8, true, false, false, false},
        // colour: tremolo under the theme, broken chords in the cellos
        {"rimsky", "Римский-Корсаков", choir::Style::Bach, Section::Symphonic,
         {{{0, T::Sustain}, {7, T::OctaveUp}},
          {{0, T::Sustain}, {3, T::Tremolo}, {8, T::Repeat16}},
          {{0, T::Sustain}, {4, T::Tremolo}},
          {{0, T::Sustain}, {5, T::Arp8}},
          {{0, T::Double8vb}, {3, T::RootsPizz}}},
         6, true, false, false, false},
        // dance: pizzicato bass, after-beats in the middle
        {"dvorak", "Дворжак", choir::Style::Bach, Section::Romantic,
         {{{0, T::Sustain}, {8, T::OctaveUp}},
          {{0, T::Sustain}, {3, T::Offbeat}},
          {{0, T::Sustain}, {3, T::Offbeat}, {7, T::Repeat8}},
          {{0, T::Sustain}, {3, T::PizzBeat}, {8, T::Arp8}},
          {{0, T::Double8vb}, {3, T::RootsPizz}}},
         6, true, false, false, false},
        // hammered chords with displaced accents, seconds added at the top levels
        {"stravinsky", "Стравинский", choir::Style::Contemporary, Section::Symphonic,
         {{{0, T::Sustain}, {4, T::Augurs}},
          {{0, T::Sustain}, {3, T::Augurs}},
          {{0, T::Sustain}, {3, T::Augurs}},
          {{0, T::Sustain}, {3, T::Augurs}},
          {{0, T::Double8vb}}},
         3, false, true, false, true},
        // slow stepwise lines and suspensions, nothing but long bows
        {"barber", "Барбер", choir::Style::Contemporary, Section::Romantic,
         {{{0, T::Sustain}},
          {{0, T::Sustain}},
          {{0, T::Sustain}},
          {{0, T::Sustain}},
          {{0, T::Roots}}},
         10, true, true, false, false},
        // nervous eighths in the middle, tremolo on top, marcato low strings
        {"herrmann", "Херрманн", choir::Style::Contemporary, Section::Classical,
         {{{0, T::Sustain}, {7, T::Tremolo}},
          {{0, T::Sustain}, {3, T::Repeat8}, {8, T::Repeat16}},
          {{0, T::Sustain}, {3, T::Repeat8}},
          {{0, T::Sustain}, {4, T::Stabs}},
          {{0, T::Double8vb}}},
         3, false, true, true, false},
        // long top line, 3+3+2 ostinato in the middle, low pedal
        {"zimmer", "Циммер", choir::Style::Bach, Section::Symphonic,
         {{{0, T::Sustain}, {8, T::OctaveUp}},
          {{0, T::Sustain}, {3, T::Ostinato}},
          {{0, T::Sustain}, {2, T::Repeat8}, {5, T::Ostinato}},
          {{0, T::Sustain}, {4, T::Repeat8}},
          {{0, T::Roots}}},
         1, true, true, false, false},
        // pulsing chords in the middle, broken chords in the cellos, pizzicato basses
        {"sviridov", "Свиридов", choir::Style::Bach, Section::Symphonic,
         {{{0, T::Sustain}, {6, T::OctaveUp}},
          {{0, T::Sustain}, {3, T::Repeat8}},
          {{0, T::Sustain}, {3, T::Repeat8}, {8, T::Repeat16}},
          {{0, T::Sustain}, {4, T::Arp8}},
          {{0, T::Double8vb}, {3, T::RootsPizz}}},
         5, true, false, false, false},
    };
    int i = std::max(0, std::min(NUM_COMPOSERS - 1, (int)c));
    return P[i];
}

int clamp10(int x) { return std::max(0, std::min(10, x)); }
int clampVel(int v) { return std::max(1, std::min(127, v)); }

Tex texFor(const Profile& p, int part, int level) {
    Tex t = p.ladder[part].front().tex;
    for (auto& s : p.ladder[part])
        if (level >= s.from) t = s.tex;
    return t;
}

bool sustainedTex(Tex t) { return t == Tex::Sustain || t == Tex::Tremolo || t == Tex::OctaveUp; }

int pitchAt(const std::vector<Note>& ns, int t) {
    for (auto& n : ns)
        if (n.start <= t && t < n.end) return n.pitch;
    return -1;
}

int velAt(const std::vector<Note>& ns, int t) {
    for (auto& n : ns)
        if (n.start <= t && t < n.end) return n.vel;
    return 80;
}

struct Ctx {
    const std::vector<choir::Span>& spans;
    int bar, beat, num;
    int holdFrom;        // the last harmony is held, not figured
    bool legato;

    const choir::Span* spanAt(int t) const {
        for (auto& s : spans)
            if (s.start <= t && t < s.end) return &s;
        return spans.empty() ? nullptr : &spans.back();
    }
    bool chordTone(int pitch, int t) const {
        auto s = spanAt(t);
        return s && s->pcs.count(((pitch % 12) + 12) % 12) > 0;
    }
    // chord tones of the harmony at t, nearest first, going down (dir -1) or up (+1) from pitch
    std::vector<int> tones(int pitch, int t, int dir, int lo, int hi, int count) const {
        std::vector<int> out;
        for (int p = pitch + dir; p >= lo && p <= hi && (int)out.size() < count; p += dir)
            if (chordTone(p, t)) out.push_back(p);
        return out;
    }
};

Note mk(int s, int e, int pitch, int vel, int art) {
    Note n;
    n.start = s, n.end = e, n.pitch = pitch, n.vel = clampVel(vel), n.art = art;
    return n;
}

// the part of a line that is held at the end: its notes from holdFrom on, as they are
void holdTail(const std::vector<Note>& line, const Ctx& c, std::vector<Note>& out, int art) {
    for (auto& n : line) {
        if (n.end <= c.holdFrom) continue;
        out.push_back(mk(std::max(n.start, c.holdFrom), n.end, n.pitch, n.vel, art));
    }
}

// walks a line in cells of `unit` on the bar grid, up to the held ending
template <typename F>
void cells(const std::vector<Note>& line, const Ctx& c, int unit, F f) {
    for (auto& n : line) {
        int end = std::min(n.end, c.holdFrom);
        int t = n.start;
        while (t < end) {
            int next = std::min(end, (t / unit) * unit + unit);
            if (end - next > 0 && end - next < unit / 2) next = end;   // no splinter at the end of a note
            f(t, next, n);
            t = next;
        }
    }
}

int accent(int t, const Ctx& c) {
    int inBar = t % c.bar;
    if (inBar == 0) return 10;
    if (inBar % c.beat == 0) return 4;
    return -8;
}

std::vector<Note> texSustain(const std::vector<Note>& line, const Ctx& c, int art) {
    std::vector<Note> out;
    for (auto& n : line) out.push_back(mk(n.start, n.end, n.pitch, n.vel, art));
    (void)c;
    return out;
}

std::vector<Note> texRepeat(const std::vector<Note>& line, const Ctx& c, int unit, int art) {
    std::vector<Note> out;
    cells(line, c, unit, [&](int t, int e, const Note& n) { out.push_back(mk(t, e, n.pitch, n.vel + accent(t, c), art)); });
    holdTail(line, c, out, A_LONG);
    return out;
}

std::vector<Note> texAlberti(const std::vector<Note>& line, const Ctx& c, int unit, int part) {
    std::vector<Note> out;
    cells(line, c, unit, [&](int t, int e, const Note& n) {
        // low - high - middle - high, the line's own note on top so the figure stays under the melody
        int high = n.pitch;
        auto below = c.tones(high, t, -1, LO[part], LIMIT[part], 2);
        int mid = below.size() > 0 ? below[0] : high, low = below.size() > 1 ? below[1] : mid;
        if (below.size() < 2) {   // near the open string: build the figure upwards
            auto above = c.tones(high, t, +1, LO[part], TOP[part], 2);
            low = high;
            mid = above.size() > 0 ? above[0] : high;
            high = above.size() > 1 ? above[1] : mid;
        }
        int k = ((t % c.bar) / unit) % 4;
        int p = k == 0 ? low : k == 2 ? mid : high;
        out.push_back(mk(t, e, p, n.vel + (k == 0 ? 6 : -8), A_STACC));
    });
    holdTail(line, c, out, A_LONG);
    return out;
}

std::vector<Note> texArp(const std::vector<Note>& line, const Ctx& c, int part) {
    std::vector<Note> out;
    cells(line, c, E, [&](int t, int e, const Note& n) {
        // root - fifth or sixth above - the next chord tone - back: an open broken chord
        int t0 = n.pitch;
        auto up = c.tones(t0 + 3, t, +1, LO[part], TOP[part], 2);
        int t1 = up.size() > 0 ? up[0] : t0, t2 = up.size() > 1 ? up[1] : t1;
        int k = ((t % c.bar) / E) % 4;
        int p = k == 0 ? t0 : k == 2 ? t2 : t1;
        out.push_back(mk(t, e, p, n.vel + (k == 0 ? 4 : -6), c.legato ? A_LEGATO : A_LONG));
    });
    holdTail(line, c, out, A_LONG);
    return out;
}

// the extent of a line as beat starts
std::vector<int> beatsOf(const std::vector<Note>& line, const Ctx& c, int step) {
    std::vector<int> out;
    if (line.empty()) return out;
    int t0 = (line.front().start / step) * step, t1 = line.back().end;
    for (auto& n : line) t1 = std::max(t1, n.end);
    for (int t = t0; t < t1; t += step) out.push_back(t);
    (void)c;
    return out;
}

std::vector<Note> texOffbeat(const std::vector<Note>& line, const Ctx& c) {
    std::vector<Note> out;
    for (int b : beatsOf(line, c, c.beat)) {
        if (b >= c.holdFrom) break;
        if (c.num == 3) {   // waltz: rest on one, chords on two and three
            if (b % c.bar == 0) continue;
            int p = pitchAt(line, b);
            if (p >= 0) out.push_back(mk(b, b + c.beat, p, velAt(line, b) - 6, A_STACC));
        } else {
            int t = b + c.beat / 2;
            int p = pitchAt(line, t);
            if (p >= 0) out.push_back(mk(t, b + c.beat, p, velAt(line, t) - 6, A_STACC));
        }
    }
    holdTail(line, c, out, A_LONG);
    return out;
}

std::vector<Note> texPizz(const std::vector<Note>& line, const Ctx& c, int step, int shift, int lo) {
    std::vector<Note> out;
    for (int b : beatsOf(line, c, step)) {
        int p = pitchAt(line, b);
        if (p < 0) continue;
        if (p + shift >= lo) p += shift;
        out.push_back(mk(b, b + c.beat / 2, p, velAt(line, b) + (b % c.bar == 0 ? 8 : 0), A_PIZZ));
    }
    return out;
}

std::vector<Note> texOstinato(const std::vector<Note>& line, const Ctx& c) {
    std::vector<Note> out;
    cells(line, c, X, [&](int t, int e, const Note& n) {
        int k = ((t % c.bar) / X) % 8;
        bool acc = k == 0 || k == 3 || k == 6;
        out.push_back(mk(t, e, n.pitch, n.vel + (acc ? 14 : -10), A_SPICC));
    });
    holdTail(line, c, out, A_LONG);
    return out;
}

std::vector<Note> texAugurs(const std::vector<Note>& line, const Ctx& c, bool bite, int part) {
    // accents fall on fixed eighths of a 32-eighth cycle, against the bar line
    static const int ACC[] = {0, 9, 11, 17, 20, 24, 29};
    std::vector<Note> out;
    cells(line, c, E, [&](int t, int e, const Note& n) {
        int k = (t / E) % 32;
        bool acc = std::find(std::begin(ACC), std::end(ACC), k) != std::end(ACC);
        out.push_back(mk(t, e, n.pitch, n.vel + (acc ? 22 : -4), acc ? A_MARC : A_STACC));
        if (acc && bite && n.pitch + 2 <= TOP[part]) out.push_back(mk(t, e, n.pitch + 2, n.vel + 16, A_MARC));
    });
    holdTail(line, c, out, A_LONG);
    return out;
}

std::vector<Note> texStabs(const std::vector<Note>& line, const Ctx& c) {
    std::vector<int> at;
    for (auto& s : c.spans) at.push_back(s.start);
    for (int b : beatsOf(line, c, c.bar)) at.push_back(b);
    std::sort(at.begin(), at.end());
    at.erase(std::unique(at.begin(), at.end()), at.end());
    std::vector<Note> out;
    for (int t : at) {
        if (t >= c.holdFrom) break;
        int p = pitchAt(line, t);
        if (p >= 0) out.push_back(mk(t, t + c.beat / 2, p, velAt(line, t) + 20, A_MARC));
    }
    holdTail(line, c, out, A_LONG);
    return out;
}

std::vector<Note> texOctave(const std::vector<Note>& line, const Ctx& c, int part, int art) {
    std::vector<Note> out;
    for (auto& n : line) {
        out.push_back(mk(n.start, n.end, n.pitch, n.vel, art));
        if (n.pitch + 12 <= TOP[part] + 5) out.push_back(mk(n.start, n.end, n.pitch + 12, n.vel, art));
    }
    (void)c;
    return out;
}

// basses: long bass notes of the harmonies, an octave under the cellos where the instrument reaches
std::vector<Note> texRoots(const std::vector<Note>& cello, const Ctx& c) {
    std::vector<Note> out;
    for (auto& s : c.spans) {
        int p = pitchAt(cello, s.start);
        if (p < 0) continue;
        if (p - 12 >= LO[P_CB]) p -= 12;
        if (!out.empty() && out.back().pitch == p && out.back().end == s.start) out.back().end = s.end;
        else out.push_back(mk(s.start, s.end, p, velAt(cello, s.start), A_LONG));
    }
    return out;
}

std::vector<Note> buildPart(Tex tex, int part, const std::vector<Note>& line, const Ctx& c, const Profile& pr, int level) {
    int longArt = pr.legato ? A_LEGATO : A_LONG;
    switch (tex) {
        case Tex::Sustain: return texSustain(line, c, longArt);
        case Tex::Detache: return texRepeat(line, c, c.beat, A_LONG);
        case Tex::Repeat8: return texRepeat(line, c, E, A_STACC);
        case Tex::Repeat16: return texRepeat(line, c, X, A_SPICC);
        case Tex::Tremolo: return texSustain(line, c, A_TREM);
        case Tex::Alberti8: return texAlberti(line, c, E, part);
        case Tex::Alberti16: return texAlberti(line, c, X, part);
        case Tex::Arp8: return texArp(line, c, part);
        case Tex::Offbeat: return texOffbeat(line, c);
        case Tex::PizzBeat: return texPizz(line, c, c.beat, 0, LO[part]);
        case Tex::Ostinato: return texOstinato(line, c);
        case Tex::Augurs: return texAugurs(line, c, pr.bite && level >= 7 && (part == P_V2 || part == P_VA), part);
        case Tex::Stabs: return texStabs(line, c);
        case Tex::OctaveUp: return texOctave(line, c, part, longArt);
        default: return texSustain(line, c, longArt);
    }
}

const char* artWord(int art) {
    switch (art) {
        case A_LEGATO: return "легато";
        case A_STACC: return "стаккато";
        case A_SPICC: return "спиккато";
        case A_PIZZ: return "пиццикато";
        case A_TREM: return "тремоло";
        case A_MARC: return "маркато";
        default: return "длинные ноты";
    }
}

const char* patchFor(Section s) {
    switch (s) {
        case Section::Quartet: return "сольные инструменты (solo strings)";
        case Section::Baroque: case Section::Classical: return "камерная секция (chamber strings)";
        default: return "большая секция (symphonic strings)";
    }
}

}  // namespace

// ---------------------------------------------------------------- names
const char* composerLabel(Composer c) { return profile(c).label; }
const char* composerId(Composer c) { return profile(c).id; }

const char* sectionLabel(Section s) {
    switch (s) {
        case Section::Auto: return "По стилю";
        case Section::Quartet: return "Квартет";
        case Section::Baroque: return "Барочный";
        case Section::Classical: return "Классический";
        case Section::Romantic: return "Романтический";
        case Section::Symphonic: return "Симфонический";
    }
    return "";
}

const char* sectionDesks(Section s, Composer c) {
    if (s == Section::Auto) s = profile(c).section;
    switch (s) {
        case Section::Quartet: return "1-1-1-1";
        case Section::Baroque: return "4-4-2-2-1";
        case Section::Classical: return "6-6-4-3-2";
        case Section::Romantic: return "12-10-8-6-4";
        default: return "16-14-12-10-8";
    }
}

const char* libraryLabel(Library l) {
    switch (l) {
        case Library::None: return "Без оптимизации";
        case Library::SpitfireUACC: return "Spitfire (UACC)";
        case Library::SplitTracks: return "Дорожка на штрих";
    }
    return "";
}

static choir::Skeleton skeletonFor(const Profile& pr) {
    choir::Skeleton k;
    k.on = true;
    k.freeParallels = pr.freeParallels;
    return k;
}

choir::Settings liveSettings(Composer c) {
    const Profile& pr = profile(c);
    choir::Settings s;
    s.style = pr.base;
    s.skeleton = skeletonFor(pr);
    return s;
}

// ---------------------------------------------------------------- arrange
Result arrange(const std::vector<Note>& notes, const Meta& meta, const Settings& s) {
    Result res;
    const Profile& pr = profile(s.composer);
    Section sec = s.section == Section::Auto ? pr.section : s.section;
    bool quartet = sec == Section::Quartet;

    // is the top line the user's own melody? then it is protected unless its level is set by hand
    choir::Analysis an = choir::analyse(notes);
    bool bare = an.chords.empty() && !an.melody.empty();
    bool real = an.realMelody() || s.input == InputMode::HarmonizeMelody || (s.input == InputMode::Auto && bare);

    int lv[NPARTS];
    for (int i = 0; i < NPARTS; ++i) lv[i] = s.part[i] >= 0 ? clamp10(s.part[i]) : clamp10(s.global);
    if (real && s.part[P_V1] < 0) lv[P_V1] = 0;
    Tex tex[NPARTS];
    for (int i = 0; i < NPARTS; ++i) {
        tex[i] = texFor(pr, i, lv[i]);
        // octave divisi and big gestures need a section
        if ((quartet || sec == Section::Baroque) && tex[i] == Tex::OctaveUp) tex[i] = Tex::Sustain;
    }

    // ---- skeleton
    choir::Settings cs;
    cs.style = pr.base;
    cs.input = s.input;
    cs.variant = s.variant;
    cs.skeleton = skeletonFor(pr);
    cs.locks = s.skeletonLocks;
    auto skLevel = [&](int part) {
        if (sustainedTex(tex[part])) return std::min(lv[part], pr.ornCap);
        return std::min(lv[part], 2);    // a figured part needs plain chord tones under it
    };
    cs.global = skLevel(P_V2);
    cs.S = skLevel(P_V1), cs.A = skLevel(P_V2), cs.T = skLevel(P_VA), cs.B = skLevel(P_VC);
    choir::Result base = choir::arrange(notes, meta, cs);
    static_cast<choir::Result&>(res) = base;
    if (!base.ok()) return res;

    std::vector<Note> line[4];
    for (auto& p : base.parts)
        for (int i = 0; i < 4; ++i)
            if (p.name == PART_NAMES[i]) line[i] = p.notes;
    for (int i = 0; i < 4; ++i) {
        std::sort(line[i].begin(), line[i].end(), [](const Note& a, const Note& b) { return a.start < b.start; });
        res.skeleton.push_back({PART_NAMES[i], line[i]});
    }

    // ---- texture
    int num = meta.num > 0 ? meta.num : 4, den = meta.den > 0 ? meta.den : 4;
    int beat = den == 8 && num % 3 == 0 ? 3 * E : TPB * 4 / den;
    int bar = num * TPB * 4 / den;
    int hold = INT_MAX;
    if (!base.spans.empty() && base.spans.back().end - base.spans.back().start >= 2 * beat) hold = base.spans.back().start;
    Ctx ctx{base.spans, bar, beat, num, hold, pr.legato};

    std::vector<Note> out[NPARTS];
    for (int i = 0; i < 4; ++i) out[i] = buildPart(tex[i], i, line[i], ctx, pr, lv[i]);
    if (!quartet) {
        if (tex[P_CB] == Tex::Roots) {
            out[P_CB] = texRoots(line[P_VC], ctx);
        } else if (tex[P_CB] == Tex::RootsPizz) {
            int step = num == 4 ? 2 * beat : bar;     // one and three in 4/4, the downbeat elsewhere
            out[P_CB] = texPizz(line[P_VC], ctx, step, -12, LO[P_CB]);
        } else {
            for (auto n : out[P_VC]) {
                if (n.pitch - 12 >= LO[P_CB]) n.pitch -= 12;
                if (n.pitch <= LIMIT[P_CB]) out[P_CB].push_back(n);
            }
        }
    }
    // sforzando on the chord changes
    if (pr.accents) {
        std::set<int> changes;
        for (auto& sp : base.spans) changes.insert(sp.start);
        for (int i = 1; i < NPARTS; ++i) {
            if (lv[i] < 5) continue;
            for (auto& n : out[i])
                if (changes.count(n.start) && n.start < hold && n.art != A_PIZZ && n.art != A_TREM)
                    n.art = A_MARC, n.vel = clampVel(n.vel + 18);
        }
    }

    res.parts.clear();
    for (int i = 0; i < NPARTS; ++i) {
        if (i == P_CB && quartet) continue;
        std::stable_sort(out[i].begin(), out[i].end(), [](const Note& a, const Note& b) {
            return a.start != b.start ? a.start < b.start : a.pitch < b.pitch;
        });
        res.parts.push_back({PART_NAMES[i], out[i]});
        res.textures[i] = texLabel(tex[i]);
    }
    // finished parts the user locked come back as they were
    res.locked.clear();
    for (auto& p : res.parts) {
        auto it = s.locks.find(p.name);
        if (it == s.locks.end()) continue;
        p.notes = it->second;
        res.locked.push_back(p.name);
    }
    for (int i = 0; i < NPARTS; ++i) res.lv[i] = lv[i];
    res.styleLabel = pr.label;
    res.sectionLabel = std::string(sectionDesks(sec, s.composer));

    // ---- checks of the finished parts
    // the skeleton's checks speak the choir's language: rename the voices, drop what does not apply
    {
        std::vector<choir::Issue> kept;
        for (auto i : res.issues) {
            if (i.what.find("расстояние больше октавы") != std::string::npos) continue;   // wide spacing is normal for strings
            auto swap = [&](const std::string& a, const std::string& b) {
                for (size_t pos = i.what.find(a); pos != std::string::npos; pos = i.what.find(a, pos + b.size()))
                    i.what.replace(pos, a.size(), b);
            };
            swap("поёт", "играет");
            const char* from[4] = {"S", "A", "T", "B"};
            const char* to[4] = {"V1", "V2", "Va", "Vc"};
            for (int k = 0; k < 4; ++k) {
                if (i.what.rfind(std::string(from[k]) + " ", 0) == 0) i.what.replace(0, 1, to[k]);
                swap(std::string(" ") + from[k] + "-", std::string(" ") + to[k] + "-");
                swap(std::string("-") + from[k], std::string("-") + to[k]);
                swap(std::string("выше ") + from[k], std::string("выше ") + to[k]);
            }
            kept.push_back(i);
        }
        res.issues = kept;
    }
    for (auto& p : res.parts) {
        int idx = 0;
        for (int i = 0; i < NPARTS; ++i)
            if (p.name == PART_NAMES[i]) idx = i;
        int low = 0, high = 0;
        for (auto& n : p.notes) low += n.pitch < LO[idx], high += n.pitch > LIMIT[idx];
        if (low) res.issues.push_back({"диапазон", p.name + ": " + std::to_string(low) + " нот ниже нижней струны"});
        if (high) res.issues.push_back({"диапазон", p.name + ": " + std::to_string(high) + " нот выше предела инструмента"});
    }

    // ---- advice: what the notes cannot say
    res.hints.push_back({0, "", std::string("Состав ") + res.sectionLabel + ": " + patchFor(sec)});
    int shortParts = 0, pizzParts = 0;
    for (auto& p : res.parts) {
        std::map<int, int> count;
        for (auto& n : p.notes) ++count[n.art];
        int best = A_LONG, bn = -1;
        for (auto& kv : count)
            if (kv.second > bn) best = kv.first, bn = kv.second;
        if (p.notes.empty()) continue;
        res.hints.push_back({p.notes.front().start, p.name, artWord(best)});
        if (best == A_STACC || best == A_SPICC) ++shortParts;
        if (best == A_PIZZ) ++pizzParts;
    }
    if (shortParts >= 2 && pizzParts == 0)
        res.hints.push_back({0, "", "Коротким штрихам не хватает атаки: попробуй перевести альты на пиццикато"});
    if (!quartet && lv[P_V2] >= 6 && tex[P_V1] == Tex::Sustain && tex[P_V2] == Tex::Sustain)
        res.hints.push_back({0, "", "Для напряжения продублируй первые скрипки во вторых на тремоло"});
    return res;
}

std::string Result::report() const {
    std::ostringstream o;
    if (!error.empty()) return "Ошибка: " + error;
    o << "Стиль: " << styleLabel << "   Состав: " << sectionLabel << "\nТональность: " << keyLabel
      << "   Мелодия: " << melodyLabel;
    if (melodyShift) o << " (перенос " << (melodyShift > 0 ? "+" : "") << melodyShift << " полутонов)";
    o << "\nАккорды: ";
    for (size_t i = 0; i < chords.size(); ++i) o << (i ? " | " : "") << chords[i];
    o << "\nФактура:";
    bool first = true;
    for (int i = 0; i < NPARTS; ++i) {
        if (textures[i].empty()) continue;
        o << (first ? " " : ", ") << PART_SHORT[i] << " " << textures[i] << " (" << lv[i] << ")";
        first = false;
    }
    if (!locked.empty()) {
        o << "\nЗакреплены:";
        for (size_t i = 0; i < locked.size(); ++i) o << (i ? ", " : " ") << locked[i];
    }
    if (issues.empty())
        o << "\nПроверка: нарушений нет";
    else
        for (auto& i : issues) o << "\n! " << i.where << ": " << i.what;
    o << "\nСоветы:";
    bool firstH = true;
    for (auto& h : hints) {
        if (!h.part.empty()) continue;   // per-part bowing is already in the texture line
        o << (firstH ? " " : "; ") << h.text;
        firstH = false;
    }
    return o.str();
}

// ---------------------------------------------------------------- MIDI for a library
namespace {

void putVlq(std::vector<unsigned char>& o, uint32_t v) {
    unsigned char b[5];
    int n = 0;
    b[n++] = (unsigned char)(v & 0x7F);
    while ((v >>= 7) > 0) b[n++] = (unsigned char)((v & 0x7F) | 0x80);
    while (n > 0) o.push_back(b[--n]);
}

void putTrack(std::vector<unsigned char>& o, const std::vector<unsigned char>& t) {
    o.insert(o.end(), {'M', 'T', 'r', 'k'});
    uint32_t n = (uint32_t)t.size();
    o.insert(o.end(), {(unsigned char)(n >> 24), (unsigned char)(n >> 16), (unsigned char)(n >> 8), (unsigned char)n});
    o.insert(o.end(), t.begin(), t.end());
}

struct Ev {
    int t, order;                          // order at one tick: note off, controllers and text, note on
    std::vector<unsigned char> bytes;
};

int uacc(int art) {
    switch (art) {
        case A_LEGATO: return 20;   // legato
        case A_STACC: return 40;    // short
        case A_SPICC: return 42;    // short, very short (spiccato)
        case A_PIZZ: return 56;     // plucked
        case A_TREM: return 11;     // tremolo
        case A_MARC: return 52;     // short marcato
        default: return 1;          // long
    }
}

// groups for one track per bowing
int artClass(int art) { return art == A_PIZZ ? 2 : art == A_TREM ? 3 : (art == A_STACC || art == A_SPICC || art == A_MARC) ? 1 : 0; }
const char* CLASS_NAME[4] = {"long", "short", "pizz", "trem"};
const int CLASS_PROGRAM[4] = {48, 48, 45, 44};   // General MIDI: string ensemble, pizzicato, tremolo

int soundingEnd(const Note& n, bool overlap, bool nextJoins) {
    int d = n.end - n.start;
    switch (n.art) {
        case A_STACC: return n.start + std::max(30, d / 2);
        case A_SPICC: return n.start + std::max(30, d * 3 / 10);
        case A_PIZZ: return n.start + std::max(40, d / 2);
        case A_MARC: return n.start + std::max(40, d * 3 / 4);
        case A_LEGATO: return n.end + (overlap && nextJoins ? 24 : 0);   // legato patches need the notes to overlap
        default: return n.end;
    }
}

// one phrase arch over four bars, the peak a little after the middle
int dynamicsAt(int t, int phrase, int total) {
    double x = (double)(t % phrase) / phrase;
    double w = x < 0.6 ? x / 0.6 * 0.5 : 0.5 + (x - 0.6) / 0.4 * 0.5;
    double v = 46.0 + 44.0 * std::sin(3.14159265358979 * w);
    if (total - t < phrase / 4) v = std::min(v, 40.0 + 30.0 * (double)(total - t) / (phrase / 4));   // dies away at the end
    return std::max(1, std::min(127, (int)std::lround(v)));
}

std::vector<unsigned char> trackBytes(const std::string& name, int chan, const std::vector<Note>& notes,
                                      const std::vector<std::pair<int, std::string>>& texts, const MidiOptions& o,
                                      int program, bool curve, int bar, int total) {
    std::vector<Ev> ev;
    bool lib = o.library != Library::None;
    bool overlap = lib;
    int cur = -1;
    for (size_t i = 0; i < notes.size(); ++i) {
        const Note& n = notes[i];
        bool joins = false;
        for (size_t k = i + 1; k < notes.size() && notes[k].start <= n.end; ++k)
            if (notes[k].start == n.end && (notes[k].art == A_LEGATO || notes[k].art == A_LONG)) joins = true;
        int end = soundingEnd(n, overlap, joins);
        if (o.library == Library::SpitfireUACC && uacc(n.art) != cur) {
            cur = uacc(n.art);
            ev.push_back({std::max(0, n.start - 5), 1, {(unsigned char)(0xB0 | chan), 32, (unsigned char)cur}});
        }
        ev.push_back({n.start, 2, {(unsigned char)(0x90 | chan), (unsigned char)n.pitch, (unsigned char)n.vel}});
        ev.push_back({end, 0, {(unsigned char)(0x80 | chan), (unsigned char)n.pitch, 0}});
    }
    if (curve && !notes.empty()) {
        int phrase = std::max(bar, std::min(4 * bar, ((total + bar - 1) / bar) * bar));
        int last = -1;
        for (int t = notes.front().start; t < total; t += 60) {
            int v = dynamicsAt(t, phrase, total);
            if (v == last) continue;
            last = v;
            ev.push_back({t, 1, {(unsigned char)(0xB0 | chan), 1, (unsigned char)v}});
        }
    }
    for (auto& tx : texts) {
        std::vector<unsigned char> b{0xFF, 0x01};
        putVlq(b, (uint32_t)tx.second.size());
        b.insert(b.end(), tx.second.begin(), tx.second.end());
        ev.push_back({tx.first, 1, b});
    }
    std::stable_sort(ev.begin(), ev.end(), [](const Ev& a, const Ev& b) { return a.t != b.t ? a.t < b.t : a.order < b.order; });

    std::vector<unsigned char> t;
    t.insert(t.end(), {0x00, 0xFF, 0x03});
    putVlq(t, (uint32_t)name.size());
    t.insert(t.end(), name.begin(), name.end());
    // a General MIDI program so the DAW opens the track with strings; UACC uses controller 32 itself,
    // so no bank select is written there
    if (o.library != Library::SpitfireUACC && program >= 0) {
        t.insert(t.end(), {0x00, (unsigned char)(0xB0 | chan), 0x00, 0x00});
        t.insert(t.end(), {0x00, (unsigned char)(0xB0 | chan), 0x20, 0x00});
        t.insert(t.end(), {0x00, (unsigned char)(0xC0 | chan), (unsigned char)program});
    }
    int last = 0;
    for (auto& e : ev) {
        putVlq(t, (uint32_t)(e.t - last));
        last = e.t;
        t.insert(t.end(), e.bytes.begin(), e.bytes.end());
    }
    t.insert(t.end(), {0x00, 0xFF, 0x2F, 0x00});
    return t;
}

}  // namespace

std::vector<unsigned char> writeMidi(const Result& r, const Meta& meta, const MidiOptions& o) {
    int num = meta.num > 0 ? meta.num : 4, den = meta.den > 0 ? meta.den : 4;
    int bar = num * TPB * 4 / den;
    int total = 0;
    for (auto& p : r.parts)
        for (auto& n : p.notes) total = std::max(total, n.end);

    struct Tr { std::string name; std::vector<Note> notes; int program; bool curve; std::vector<std::pair<int, std::string>> texts; };
    std::vector<Tr> tracks;
    bool lib = o.library != Library::None;
    for (size_t pi = 0; pi < r.parts.size(); ++pi) {
        if (o.only >= 0 && (int)pi != o.only) continue;
        const Part& p = r.parts[pi];
        std::vector<std::pair<int, std::string>> texts;
        for (auto& h : r.hints)
            if (h.part == p.name) texts.push_back({h.start, h.text});
        if (o.library == Library::SplitTracks) {
            bool first = true;
            for (int c = 0; c < 4; ++c) {
                std::vector<Note> ns;
                for (auto& n : p.notes)
                    if (artClass(n.art) == c) ns.push_back(n);
                if (ns.empty()) continue;
                tracks.push_back({p.name + " " + CLASS_NAME[c], ns, CLASS_PROGRAM[c], o.dynamics && (c == 0 || c == 3),
                                  first ? texts : std::vector<std::pair<int, std::string>>{}});
                first = false;
            }
        } else {
            tracks.push_back({p.name, p.notes, 48, lib && o.dynamics, texts});
        }
    }

    std::vector<unsigned char> out{'M', 'T', 'h', 'd', 0, 0, 0, 6, 0, 1};
    out.push_back((unsigned char)((tracks.size() + 1) >> 8));
    out.push_back((unsigned char)(tracks.size() + 1));
    out.push_back((unsigned char)(TPB >> 8));
    out.push_back((unsigned char)(TPB & 0xFF));
    std::vector<unsigned char> c;
    std::string cn = "Conductor";
    c.insert(c.end(), {0x00, 0xFF, 0x03, (unsigned char)cn.size()});
    c.insert(c.end(), cn.begin(), cn.end());
    int dpow = 0;
    while ((1 << dpow) < den) ++dpow;
    c.insert(c.end(), {0x00, 0xFF, 0x58, 0x04, (unsigned char)num, (unsigned char)dpow, 24, 8});
    c.insert(c.end(), {0x00, 0xFF, 0x51, 0x03, (unsigned char)(meta.tempo >> 16), (unsigned char)(meta.tempo >> 8),
                       (unsigned char)meta.tempo});
    for (auto& h : r.hints) {
        if (!h.part.empty() || o.only >= 0) continue;
        c.insert(c.end(), {0x00, 0xFF, 0x01});
        putVlq(c, (uint32_t)h.text.size());
        c.insert(c.end(), h.text.begin(), h.text.end());
    }
    c.insert(c.end(), {0x00, 0xFF, 0x2F, 0x00});
    putTrack(out, c);
    for (size_t i = 0; i < tracks.size(); ++i) {
        int chan = (int)i < 9 ? (int)i : std::min(15, (int)i + 1);   // channel 10 is drums in General MIDI
        putTrack(out, trackBytes(tracks[i].name, chan, tracks[i].notes, tracks[i].texts, o, tracks[i].program,
                                 tracks[i].curve, bar, total));
    }
    return out;
}

int soundingEnd(const Note& n) { return soundingEnd(n, false, false); }

std::string toMusicXml(const Result& r, const Meta& meta, const std::string& title) {
    std::vector<choir::ScoreWord> words;
    for (auto& h : r.hints)
        if (h.part.empty()) words.push_back({h.start, "", h.text});
    return choir::toMusicXml(r, meta, title, words, "Strings Arranger");
}

}  // namespace strings
