// String section arranger: five parts (violins I and II, violas, cellos, basses) on top of the
// four-voice search of the choir engine. The search gives the skeleton, the texture layer turns it
// into string writing, the performance layer writes MIDI for a sample library.
#pragma once
#include "ChoirEngine.h"

namespace strings {

using choir::InputMode;
using choir::Meta;
using choir::Note;
using choir::Part;
constexpr int TPB = choir::TPB;

// articulation of a note (Note::art)
enum Art { A_LONG = 0, A_LEGATO, A_STACC, A_SPICC, A_PIZZ, A_TREM, A_MARC };

// the order is the order of the style menu and is saved in projects: add new names at the end only
enum class Composer { Vivaldi, Bach, Haydn, Mozart, Beethoven, Tchaikovsky, RimskyKorsakov, Dvorak,
                      Stravinsky, Barber, Herrmann, Zimmer, Sviridov };
constexpr int NUM_COMPOSERS = 13;

// desks of the section: Auto takes the size the style is usually played with
enum class Section { Auto, Quartet, Baroque, Classical, Romantic, Symphonic };

// what the MIDI is written for; add new libraries at the end only
enum class Library { None, SpitfireUACC, SplitTracks };

enum { P_V1 = 0, P_V2, P_VA, P_VC, P_CB, NPARTS };
extern const char* PART_NAMES[NPARTS];    // "Violin I" ... "Contrabass"
extern const char* PART_SHORT[NPARTS];    // "V1" ... "Cb"

struct Settings {
    Composer composer = Composer::Mozart;
    int global = 4;                                   // complexity 0..10
    int part[NPARTS] = {-1, -1, -1, -1, -1};          // per part; -1 = as the global one
    Section section = Section::Auto;
    InputMode input = InputMode::Auto;
    int variant = 0;
    std::map<std::string, std::vector<Note>> locks;           // finished parts kept as they are
    std::map<std::string, std::vector<Note>> skeletonLocks;   // their lines in the four-voice skeleton
};

struct Hint {
    int start = 0;          // tick the advice refers to
    std::string part;       // part name, empty = the whole section
    std::string text;
};

struct Result : choir::Result {
    std::vector<Part> skeleton;          // Violin I, Violin II, Viola, Cello before the texture layer
    std::vector<Hint> hints;             // what notes cannot say: bowing, patches, doublings
    std::array<int, NPARTS> lv{};
    std::array<std::string, NPARTS> textures;   // texture of each part, for the report
    std::string sectionLabel;            // "6-6-4-3-2"
    std::string report() const;
};

Result arrange(const std::vector<Note>& notes, const Meta& meta, const Settings& s);

struct MidiOptions {
    Library library = Library::None;
    bool dynamics = true;      // CC1 phrase curves (libraries only)
    int only = -1;             // one part, or -1 for all
};
std::vector<unsigned char> writeMidi(const Result& r, const Meta& meta, const MidiOptions& o);
std::string toMusicXml(const Result& r, const Meta& meta, const std::string& title);
// where a note stops sounding once its bowing is played: short bowings end early (preview, piano roll)
int soundingEnd(const Note& n);

// names for menus
const char* composerLabel(Composer c);     // "Моцарт"
const char* composerId(Composer c);        // "mozart"
const char* sectionLabel(Section s);
const char* libraryLabel(Library l);
const char* sectionDesks(Section s, Composer c);

// live mode: the four skeleton voices plus basses an octave under the cellos
choir::Settings liveSettings(Composer c);

}  // namespace strings
