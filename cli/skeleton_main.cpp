// Command-line front end of the engine: same options as the Python prototype.
//   choir_cli in.mid out.mid [--style=bach|obikhod|gregorian|znamenny|byzantine|renaissance|
//                                       contemporary|gospel|barbershop|epic] [--complexity=4|S0,A5,T5,B7]
//             [--octavists] [--texture=unison|organum4|organum5] [--outofmode=substitute|skip]
//             [--mixed] [--dump]
#include <cstdio>
#include <cstring>
#include <iostream>
#include <map>
#include <sstream>
#include "../core/ChoirEngine.h"

using namespace choir;

int main(int argc, char** argv) {
    std::vector<std::string> pos;
    std::string style = "bach", cx, texture = "unison", policy = "substitute";
    bool octav = false, mixed = false, dump = false;
    std::string xml, choirS = "satb", input = "auto";
    int variant = 0;
    bool amateur = false;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto val = [&](const char* k) { return a.substr(std::strlen(k)); };
        if (a.rfind("--style=", 0) == 0) style = val("--style=");
        else if (a.rfind("--complexity=", 0) == 0) cx = val("--complexity=");
        else if (a.rfind("--texture=", 0) == 0) texture = val("--texture=");
        else if (a.rfind("--outofmode=", 0) == 0) policy = val("--outofmode=");
        else if (a == "--octavists") octav = true;
        else if (a == "--mixed") mixed = true;
        else if (a == "--dump") dump = true;
        else if (a.rfind("--xml=", 0) == 0) xml = val("--xml=");
        else if (a.rfind("--choir=", 0) == 0) choirS = val("--choir=");
        else if (a.rfind("--input=", 0) == 0) input = val("--input=");
        else if (a.rfind("--variant=", 0) == 0) variant = std::stoi(val("--variant="));
        else if (a == "--amateur") amateur = true;
        else pos.push_back(a);
    }
    // --live=60,64,67;62,65,69,71;... voices a sequence of held chords like the MIDI FX does
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a.rfind("--live=", 0) != 0) continue;
        Settings ls = Settings::fromSpec(Style::Bach, "4");
        for (int j = 1; j < argc; ++j) {
            std::string b = argv[j];
            if (b.rfind("--style=", 0) == 0) {
                const char* names[] = {"bach", "obikhod", "gregorian", "znamenny", "byzantine", "renaissance",
                                       "contemporary", "gospel", "barbershop", "epic", "partes", "spiritual",
                                       "anglican", "sacredharp", "bulgarian", "georgian", "tintinnabuli",
                                       "closeharmony", "apop", "tenore", "soviet"};
                for (int k = 0; k < 21; ++k)
                    if (b.substr(8) == names[k]) ls.style = (Style)k;
            }
            if (b == "--choir=sab") ls.choir = Choir::SAB;
            if (b == "--choir=ttbb") ls.choir = Choir::TTBB;
        }
        LiveVoicer lv;
        std::stringstream ss(a.substr(7));
        std::string chord;
        while (std::getline(ss, chord, ';')) {
            // a key is pitch or pitch:velocity
            std::vector<LiveKey> held;
            std::stringstream cs(chord);
            std::string x;
            while (std::getline(cs, x, ',')) {
                auto col = x.find(':');
                held.push_back({std::stoi(x.substr(0, col)), col == std::string::npos ? 90 : std::stoi(x.substr(col + 1))});
            }
            auto r = lv.voice(held, ls, true, 0);
            std::cout << r.chord << ":";
            for (size_t k = 0; k < r.pitches.size(); ++k)
                std::cout << " " << r.names[k] << "=" << r.pitches[k] << "/" << r.velocities[k];
            std::cout << "\n";
        }
        return 0;
    }
    if (pos.empty()) {
        std::cerr << "usage: choir_cli in.mid [out.mid] [options]\n";
        return 2;
    }
    const std::map<std::string, Style> styles = {
        {"bach", Style::Bach},           {"obikhod", Style::Obikhod},       {"gregorian", Style::Gregorian},
        {"znamenny", Style::Znamenny},   {"byzantine", Style::Byzantine},   {"renaissance", Style::Renaissance},
        {"contemporary", Style::Contemporary}, {"gospel", Style::Gospel},  {"barbershop", Style::Barbershop},
        {"epic", Style::Epic},           {"partes", Style::Partes},         {"spiritual", Style::Spiritual},
        {"anglican", Style::Anglican},   {"sacredharp", Style::SacredHarp}, {"bulgarian", Style::Bulgarian},
        {"georgian", Style::Georgian},   {"tintinnabuli", Style::Tintinnabuli},
        {"closeharmony", Style::CloseHarmony}, {"apop", Style::AcappellaPop}, {"tenore", Style::Tenore}, {"soviet", Style::Soviet}};
    if (!styles.count(style)) {
        std::cerr << "unknown style " << style << "\n";
        return 2;
    }
    Style st = styles.at(style);
    Settings s = Settings::fromSpec(st, cx);
    s.octavists = octav;
    s.mixed = mixed;
    s.texture = texture == "organum4" ? Texture::Organum4 : texture == "organum5" ? Texture::Organum5 : Texture::Unison;
    s.outOfMode = policy == "skip" ? OutOfMode::Skip : OutOfMode::Substitute;
    const std::map<std::string, Choir> choirs = {{"satb", Choir::SATB}, {"ssaa", Choir::SSAA}, {"ttbb", Choir::TTBB},
                                                 {"sab", Choir::SAB}, {"ssa", Choir::SSA}};
    if (choirs.count(choirS)) s.choir = choirs.at(choirS);
    s.amateur = amateur;
    s.variant = variant;
    s.input = input == "chords" ? InputMode::Chords : input == "harmonize" ? InputMode::HarmonizeMelody : InputMode::Auto;
    std::vector<Note> notes;
    Meta meta;
    std::string err;
    if (!readSmf(pos[0], notes, meta, err)) {
        std::cerr << err << "\n";
        return 1;
    }
    Result r = arrange(notes, meta, s);
    if (!r.ok()) {
        std::cerr << r.report() << "\n";
        return 1;
    }
    if (pos.size() > 1) writeSmfFile(pos[1], r.parts, meta);
    if (!xml.empty()) {
        FILE* f = std::fopen(xml.c_str(), "wb");
        if (f) {
            std::string x = toMusicXml(r, meta, "Choir");
            std::fwrite(x.data(), 1, x.size(), f);
            std::fclose(f);
        }
    }
    if (dump) {
        std::cout << "ISSUES " << r.issues.size() << "\n";
        for (auto& p : r.parts) {
            std::cout << "TRACK " << p.name << " " << p.notes.size() << "\n";
            for (auto& n : p.notes) std::cout << n.start << " " << n.end << " " << n.pitch << "\n";
        }
    } else {
        std::cout << r.report() << "\n";
    }
    return 0;
}
