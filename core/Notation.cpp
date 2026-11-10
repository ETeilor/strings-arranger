// MusicXML export: one staff per part, key signature from the analysis, ties across bar lines.
#include <algorithm>
#include <cmath>
#include <map>
#include <sstream>
#include "ChoirEngine.h"

namespace choir {
namespace {

constexpr int DIV = 4;                    // divisions per quarter: a 16th grid
constexpr int TICK = TPB / DIV;           // 120 ticks per division

std::string esc(const std::string& s) {
    std::string o;
    for (char c : s) {
        if (c == '&') o += "&amp;";
        else if (c == '<') o += "&lt;";
        else if (c == '>') o += "&gt;";
        else if (c == '"') o += "&quot;";
        else o += c;
    }
    return o;
}

const char LETTERS[7] = {'C', 'D', 'E', 'F', 'G', 'A', 'B'};
const int LETTER_PC[7] = {0, 2, 4, 5, 7, 9, 11};

// alteration the key signature gives each letter
std::array<int, 7> keyAlters(int fifths) {
    std::array<int, 7> a{};
    const int sharpOrder[7] = {3, 0, 4, 1, 5, 2, 6};   // F C G D A E B
    const int flatOrder[7] = {6, 2, 5, 1, 4, 0, 3};    // B E A D G C F
    for (int i = 0; i < std::min(7, std::abs(fifths)); ++i) {
        if (fifths > 0) a[sharpOrder[i]] = 1;
        else a[flatOrder[i]] = -1;
    }
    return a;
}

struct Spelled { int letter, alter, octave; };

Spelled spell(int pitch, int fifths, const std::array<int, 7>& ka) {
    int pc = ((pitch % 12) + 12) % 12;
    Spelled best{0, 0, 4};
    int bestScore = 99;
    for (int L = 0; L < 7; ++L)
        for (int alt = -1; alt <= 1; ++alt) {
            if (((LETTER_PC[L] + alt) % 12 + 12) % 12 != pc) continue;
            int sc = alt == ka[L] ? 0 : alt == 0 ? 1 : (alt > 0) == (fifths >= 0) ? 2 : 3;
            if (sc < bestScore) {
                bestScore = sc;
                // octave from the written letter: B#3 sounds as C4, Cb4 as B3
                int oct = (pitch - alt - LETTER_PC[L]) / 12 - 1;
                best = {L, alt, oct};
            }
        }
    return best;
}

struct Clef { char sign; int line, octave; };

Clef clefFor(const Part& p) {
    auto starts = [&](const char* s) { return p.name.rfind(s, 0) == 0; };
    if (starts("Violin")) return {'G', 2, 0};
    if (starts("Viola")) return {'C', 3, 0};
    if (starts("Cello") || starts("Contrabass")) return {'F', 4, 0};
    if (starts("Octavist")) return {'F', 4, -1};
    if (starts("Beatbox")) return {'P', 2, 0};
    if (starts("Bassu") || starts("Contra")) return {'F', 4, 0};
    if (starts("Boghe") || starts("Mesu")) return {'G', 2, -1};
    if (starts("Treble")) return {'G', 2, 0};
    if (starts("Bass") || starts("Baritone") || starts("Ison")) return {'F', 4, 0};
    if (starts("Tenor")) return {'G', 2, -1};
    if (starts("Soprano") || starts("Alto") || starts("Lead")) return {'G', 2, 0};
    std::vector<int> ps;
    for (auto& n : p.notes) ps.push_back(n.pitch);
    if (ps.empty()) return {'G', 2, 0};
    std::nth_element(ps.begin(), ps.begin() + ps.size() / 2, ps.end());
    int med = ps[ps.size() / 2];
    return med >= 60 ? Clef{'G', 2, 0} : med >= 50 ? Clef{'G', 2, -1} : Clef{'F', 4, 0};
}

// note values a 16th grid can write in one symbol, longest first
const int VALUES[] = {16, 12, 8, 6, 4, 3, 2, 1};
const char* typeOf(int d, bool& dot) {
    dot = (d == 12 || d == 6 || d == 3);
    int base = dot ? d * 2 / 3 : d;
    switch (base) {
        case 16: return "whole";
        case 8: return "half";
        case 4: return "quarter";
        case 2: return "eighth";
        default: return "16th";
    }
}

// split a span [pos, pos+len) inside one bar into writable values; a value never crosses a beat
// it does not start on unless it is beat-aligned itself (keeps rhythms readable)
std::vector<int> splitSpan(int pos, int len, int beatDiv) {
    std::vector<int> out;
    while (len > 0) {
        int chosen = 1;
        for (int v : VALUES) {
            if (v > len) continue;
            bool onBeat = pos % beatDiv == 0;
            if (!onBeat && pos / beatDiv != (pos + v - 1) / beatDiv) continue;
            if (onBeat && v > beatDiv && v % beatDiv != 0 && v != 6 && v != 12) continue;
            chosen = v;
            break;
        }
        out.push_back(chosen);
        pos += chosen;
        len -= chosen;
    }
    return out;
}

struct Ev { int start, end; std::vector<int> pitches; int art = 0; };   // in divisions; empty pitches = rest

// instrument of a part for the score: name, General MIDI program (1-based)
std::pair<const char*, int> instrumentFor(const Part& p) {
    auto starts = [&](const char* s) { return p.name.rfind(s, 0) == 0; };
    if (starts("Violin")) return {"Violin", 41};
    if (starts("Viola")) return {"Viola", 42};
    if (starts("Cello")) return {"Violoncello", 43};
    if (starts("Contrabass")) return {"Contrabass", 44};
    return {"Choir Aahs", 53};
}

}  // namespace

std::string toMusicXml(const Result& r, const Meta& meta, const std::string& title) {
    return toMusicXml(r, meta, title, {}, "Choir Arranger");
}

std::string toMusicXml(const Result& r, const Meta& meta, const std::string& title,
                       const std::vector<ScoreWord>& words, const std::string& software) {
    int fifths = r.keyFifths == 99 ? 0 : std::max(-7, std::min(7, r.keyFifths));
    auto ka = keyAlters(fifths);
    int num = meta.num > 0 ? meta.num : 4, den = meta.den > 0 ? meta.den : 4;
    int bar = num * DIV * 4 / den;
    if (bar <= 0) bar = 16;
    int beatDiv = den == 8 && num % 3 == 0 ? 6 : DIV * 4 / den;
    if (beatDiv <= 0) beatDiv = DIV;

    int endDiv = 0;
    for (auto& p : r.parts)
        for (auto& n : p.notes) endDiv = std::max(endDiv, (n.end + TICK / 2) / TICK);
    int bars = std::max(1, (endDiv + bar - 1) / bar);
    double bpm = 60000000.0 / std::max(1, meta.tempo);

    std::ostringstream o;
    o << "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"no\"?>\n"
      << "<!DOCTYPE score-partwise PUBLIC \"-//Recordare//DTD MusicXML 3.1 Partwise//EN\" "
         "\"http://www.musicxml.org/dtds/partwise.dtd\">\n"
      << "<score-partwise version=\"3.1\">\n"
      << "  <work><work-title>" << esc(title) << "</work-title></work>\n"
      << "  <identification><encoding><software>" << esc(software) << "</software></encoding></identification>\n"
      << "  <part-list>\n";
    for (size_t i = 0; i < r.parts.size(); ++i)
        o << "    <score-part id=\"P" << i + 1 << "\"><part-name>" << esc(r.parts[i].name)
          << "</part-name><score-instrument id=\"P" << i + 1 << "-I1\"><instrument-name>"
          << instrumentFor(r.parts[i]).first
          << "</instrument-name></score-instrument><midi-instrument id=\"P" << i + 1
          << "-I1\"><midi-channel>" << (i % 15) + 1 << "</midi-channel><midi-program>"
          << instrumentFor(r.parts[i]).second << "</midi-program>"
          << "</midi-instrument></score-part>\n";
    o << "  </part-list>\n";

    for (size_t pi = 0; pi < r.parts.size(); ++pi) {
        const Part& part = r.parts[pi];
        // quantize and make the part a single line of events (equal starts become chords)
        std::vector<Note> ns = part.notes;
        std::sort(ns.begin(), ns.end(), [](const Note& a, const Note& b) {
            return a.start != b.start ? a.start < b.start : a.pitch < b.pitch;
        });
        std::vector<Ev> evs;
        for (auto& n : ns) {
            int s = (n.start + TICK / 2) / TICK, e = (n.end + TICK / 2) / TICK;
            if (e <= s) e = s + 1;
            if (!evs.empty() && evs.back().start == s) {
                if (std::find(evs.back().pitches.begin(), evs.back().pitches.end(), n.pitch) ==
                    evs.back().pitches.end())
                    evs.back().pitches.push_back(n.pitch);
                continue;
            }
            if (!evs.empty() && evs.back().end > s) evs.back().end = s;
            evs.push_back({s, e, {n.pitch}, n.art});
        }
        // rests between notes
        std::vector<Ev> line;
        int t = 0;
        for (auto& e : evs) {
            if (e.start > t) line.push_back({t, e.start, {}, 0});
            line.push_back(e);
            t = e.end;
        }
        if (t < bars * bar) line.push_back({t, bars * bar, {}, 0});

        Clef clef = clefFor(part);
        // the double bass is written an octave above its sound
        bool octaveUp = part.name.rfind("Contrabass", 0) == 0;
        int written = octaveUp ? 12 : 0;
        // words over this staff: the advice for the part, general advice over the top staff
        std::vector<ScoreWord> mine;
        for (auto& w : words)
            if (w.part == part.name || (w.part.empty() && pi == 0)) mine.push_back(w);
        size_t wi = 0;
        bool plucked = false;
        o << "  <part id=\"P" << pi + 1 << "\">\n";
        size_t li = 0;
        for (int m = 0; m < bars; ++m) {
            int m0 = m * bar, m1 = m0 + bar;
            o << "    <measure number=\"" << m + 1 << "\">\n";
            if (m == 0) {
                o << "      <attributes><divisions>" << DIV << "</divisions><key><fifths>" << fifths
                  << "</fifths><mode>" << (r.keyMinor ? "minor" : "major") << "</mode></key><time><beats>"
                  << num << "</beats><beat-type>" << den << "</beat-type></time><clef><sign>" << (clef.sign == 'P' ? "percussion" : std::string(1, clef.sign))
                  << "</sign><line>" << clef.line << "</line>";
                if (clef.octave) o << "<clef-octave-change>" << clef.octave << "</clef-octave-change>";
                o << "</clef>";
                if (octaveUp) o << "<transpose><diatonic>0</diatonic><chromatic>0</chromatic><octave-change>-1</octave-change></transpose>";
                o << "</attributes>\n";
                if (pi == 0)
                    o << "      <direction placement=\"above\"><direction-type><metronome><beat-unit>quarter"
                      << "</beat-unit><per-minute>" << (int)std::lround(bpm) << "</per-minute></metronome>"
                      << "</direction-type><sound tempo=\"" << (int)std::lround(bpm) << "\"/></direction>\n";
            }
            std::map<int, int> shown;   // letter*16+octave -> alteration in force in this bar
            while (li < line.size() && line[li].end <= m0) ++li;
            for (size_t k = li; k < line.size() && line[k].start < m1; ++k) {
                const Ev& e = line[k];
                int s = std::max(e.start, m0), en = std::min(e.end, m1);
                if (en <= s) continue;
                bool wholeBarRest = e.pitches.empty() && s == m0 && en == m1;
                if (wholeBarRest) {
                    o << "      <note><rest measure=\"yes\"/><duration>" << bar << "</duration></note>\n";
                    continue;
                }
                auto direction = [&](const std::string& text) {
                    o << "      <direction placement=\"above\"><direction-type><words>" << esc(text)
                      << "</words></direction-type></direction>\n";
                };
                while (wi < mine.size() && (mine[wi].start + TICK / 2) / TICK <= s) direction(mine[wi++].text);
                if (!e.pitches.empty() && s == e.start) {
                    bool pz = e.art == 4;
                    if (pz != plucked) direction(pz ? "pizz." : "arco");
                    plucked = pz;
                }
                // short bowings are written at their full value with a dot over the note
                auto pieces = splitSpan(s - m0, en - s, beatDiv);
                int pos = s;
                for (size_t q = 0; q < pieces.size(); ++q) {
                    int d = pieces[q];
                    bool dot;
                    const char* ty = typeOf(d, dot);
                    bool tieFromPrev = pos > e.start, tieToNext = pos + d < e.end;
                    if (e.pitches.empty()) {
                        o << "      <note><rest/><duration>" << d << "</duration><type>" << ty << "</type>"
                          << (dot ? "<dot/>" : "") << "</note>\n";
                    } else {
                        for (size_t c = 0; c < e.pitches.size(); ++c) {
                            Spelled sp = spell(e.pitches[c] + written, fifths, ka);
                            int key = sp.letter * 16 + sp.octave;
                            auto it = shown.find(key);
                            int inForce = it != shown.end() ? it->second : ka[sp.letter];
                            bool needAcc = sp.alter != inForce && !tieFromPrev;
                            if (!tieFromPrev) shown[key] = sp.alter;
                            o << "      <note>" << (c ? "<chord/>" : "") << "<pitch><step>" << LETTERS[sp.letter]
                              << "</step>";
                            if (sp.alter) o << "<alter>" << sp.alter << "</alter>";
                            o << "<octave>" << sp.octave << "</octave></pitch><duration>" << d << "</duration>";
                            if (tieFromPrev) o << "<tie type=\"stop\"/>";
                            if (tieToNext) o << "<tie type=\"start\"/>";
                            o << "<voice>1</voice><type>" << ty << "</type>" << (dot ? "<dot/>" : "");
                            if (needAcc)
                                o << "<accidental>"
                                  << (sp.alter == 1 ? "sharp" : sp.alter == -1 ? "flat" : "natural")
                                  << "</accidental>";
                            bool firstPiece = !tieFromPrev;
                            bool dotOver = firstPiece && (e.art == 2 || e.art == 3), accentOver = firstPiece && e.art == 6;
                            bool trem = e.art == 5;
                            if (tieFromPrev || tieToNext || dotOver || accentOver || trem) {
                                o << "<notations>";
                                if (tieFromPrev) o << "<tied type=\"stop\"/>";
                                if (tieToNext) o << "<tied type=\"start\"/>";
                                if (trem) o << "<ornaments><tremolo type=\"single\">3</tremolo></ornaments>";
                                if (dotOver) o << "<articulations><staccato/></articulations>";
                                if (accentOver) o << "<articulations><accent/></articulations>";
                                o << "</notations>";
                            }
                            o << "</note>\n";
                        }
                    }
                    pos += d;
                }
            }
            if (m == bars - 1) o << "      <barline location=\"right\"><bar-style>light-heavy</bar-style></barline>\n";
            o << "    </measure>\n";
        }
        o << "  </part>\n";
    }
    o << "</score-partwise>\n";
    return o.str();
}

}  // namespace choir
