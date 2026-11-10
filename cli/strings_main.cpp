// Command-line front end of the strings engine; every automatic check goes through it.
//   strings_cli in.mid [out.mid] [--style=mozart] [--complexity=4|V1=0,V2=6,Va=4,Vc=4,Cb=4]
//               [--section=auto|quartet|baroque|classical|romantic|symphonic]
//               [--library=none|uacc|split] [--no-dynamics] [--variant=N]
//               [--input=auto|chords|harmonize] [--xml=score.musicxml] [--dump] [--list]
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include "../core/Strings.h"

using namespace strings;

int main(int argc, char** argv) {
    std::vector<std::string> pos;
    std::string style = "mozart", cx = "4", section = "auto", library = "none", input = "auto", xml;
    bool dump = false, dynamics = true;
    int variant = 0;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto val = [&](const char* k) { return a.substr(std::strlen(k)); };
        if (a.rfind("--style=", 0) == 0) style = val("--style=");
        else if (a.rfind("--complexity=", 0) == 0) cx = val("--complexity=");
        else if (a.rfind("--section=", 0) == 0) section = val("--section=");
        else if (a.rfind("--library=", 0) == 0) library = val("--library=");
        else if (a.rfind("--input=", 0) == 0) input = val("--input=");
        else if (a.rfind("--variant=", 0) == 0) variant = std::stoi(val("--variant="));
        else if (a.rfind("--xml=", 0) == 0) xml = val("--xml=");
        else if (a == "--no-dynamics") dynamics = false;
        else if (a == "--dump") dump = true;
        else if (a == "--list") {
            for (int k = 0; k < NUM_COMPOSERS; ++k) std::cout << composerId((Composer)k) << "\n";
            return 0;
        } else pos.push_back(a);
    }
    if (pos.empty()) {
        std::cerr << "usage: strings_cli in.mid [out.mid] [options]\n";
        return 2;
    }
    Settings s;
    bool found = false;
    for (int k = 0; k < NUM_COMPOSERS; ++k)
        if (style == composerId((Composer)k)) s.composer = (Composer)k, found = true;
    if (!found) {
        std::cerr << "unknown style " << style << "\n";
        return 2;
    }
    // "4" or "V1=0,V2=6": a bare number is the overall level
    std::stringstream ss(cx);
    std::string item;
    while (std::getline(ss, item, ',')) {
        auto eq = item.find('=');
        if (eq == std::string::npos) {
            s.global = std::stoi(item);
            continue;
        }
        for (int k = 0; k < NPARTS; ++k)
            if (item.substr(0, eq) == PART_SHORT[k]) s.part[k] = std::stoi(item.substr(eq + 1));
    }
    const char* secs[] = {"auto", "quartet", "baroque", "classical", "romantic", "symphonic"};
    for (int k = 0; k < 6; ++k)
        if (section == secs[k]) s.section = (Section)k;
    s.variant = variant;
    s.input = input == "chords" ? InputMode::Chords : input == "harmonize" ? InputMode::HarmonizeMelody : InputMode::Auto;

    std::vector<Note> notes;
    Meta meta;
    std::string err;
    if (!choir::readSmf(pos[0], notes, meta, err)) {
        std::cerr << err << "\n";
        return 1;
    }
    Result r = arrange(notes, meta, s);
    if (!r.ok()) {
        std::cerr << r.report() << "\n";
        return 1;
    }
    MidiOptions mo;
    mo.library = library == "uacc" ? Library::SpitfireUACC : library == "split" ? Library::SplitTracks : Library::None;
    mo.dynamics = dynamics;
    if (pos.size() > 1) {
        auto b = writeMidi(r, meta, mo);
        std::ofstream f(pos[1], std::ios::binary);
        f.write((const char*)b.data(), (std::streamsize)b.size());
    }
    if (!xml.empty()) {
        std::ofstream f(xml, std::ios::binary);
        f << toMusicXml(r, meta, "Strings");
    }
    if (dump) {
        std::cout << "ISSUES " << r.issues.size() << "\n";
        for (auto& p : r.parts) {
            std::cout << "TRACK " << p.name << " " << p.notes.size() << "\n";
            for (auto& n : p.notes) std::cout << n.start << " " << n.end << " " << n.pitch << " " << n.art << "\n";
        }
    } else {
        std::cout << r.report() << "\n";
    }
    return 0;
}
