// Minimal Standard MIDI File reader/writer (format 0/1), engine-side.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <map>
#include "ChoirEngine.h"

namespace choir {

namespace {

struct Reader {
    const std::vector<unsigned char>& b;
    size_t p = 0;
    explicit Reader(const std::vector<unsigned char>& bytes) : b(bytes) {}
    bool ok(size_t n) const { return p + n <= b.size(); }
    uint32_t u32() { uint32_t v = (b[p] << 24) | (b[p + 1] << 16) | (b[p + 2] << 8) | b[p + 3]; p += 4; return v; }
    uint16_t u16() { uint16_t v = (uint16_t)((b[p] << 8) | b[p + 1]); p += 2; return v; }
    uint32_t vlq(size_t end) {
        uint32_t v = 0;
        while (p < end) {
            unsigned char c = b[p++];
            v = (v << 7) | (c & 0x7F);
            if (!(c & 0x80)) break;
        }
        return v;
    }
};

void putVlq(std::vector<unsigned char>& out, uint32_t v) {
    unsigned char buf[5];
    int n = 0;
    buf[n++] = v & 0x7F;
    while (v >>= 7) buf[n++] = (unsigned char)((v & 0x7F) | 0x80);
    while (n) out.push_back(buf[--n]);
}

void put32(std::vector<unsigned char>& o, uint32_t v) {
    for (int s = 24; s >= 0; s -= 8) o.push_back((unsigned char)(v >> s));
}

void putTrack(std::vector<unsigned char>& out, const std::vector<unsigned char>& body) {
    out.insert(out.end(), {'M', 'T', 'r', 'k'});
    put32(out, (uint32_t)body.size());
    out.insert(out.end(), body.begin(), body.end());
}

}  // namespace

bool readSmfBytes(const std::vector<unsigned char>& bytes, std::vector<Note>& notes, Meta& meta, std::string& err) {
    notes.clear();
    Reader r(bytes);
    if (!r.ok(14) || std::string(bytes.begin(), bytes.begin() + 4) != "MThd") {
        err = "это не MIDI-файл";
        return false;
    }
    r.p = 4;
    uint32_t hl = r.u32();
    r.u16();
    int ntr = r.u16();
    int div = r.u16();
    if (div & 0x8000) {
        err = "SMPTE-разметка времени не поддерживается";
        return false;
    }
    r.p = 8 + hl;
    double scale = (double)TPB / div;
    bool tempoSet = false, tsSet = false;
    for (int ti = 0; ti < ntr; ++ti) {
        if (!r.ok(8)) break;
        std::string id(bytes.begin() + r.p, bytes.begin() + r.p + 4);
        r.p += 4;
        uint32_t len = r.u32();
        size_t end = std::min(bytes.size(), r.p + len);
        if (id != "MTrk") {
            r.p = end;
            continue;
        }
        long long tick = 0;
        unsigned char status = 0;
        std::map<std::pair<int, int>, std::pair<int, int>> open;   // (ch,note) -> (start, vel)
        auto at = [&](long long tk) { return (int)std::nearbyint(tk * scale); };
        while (r.p < end) {
            tick += r.vlq(end);
            if (r.p >= end) break;
            unsigned char c = bytes[r.p];
            if (c == 0xFF) {
                r.p++;
                unsigned char type = bytes[r.p++];
                uint32_t l = r.vlq(end);
                if (type == 0x51 && l == 3 && !tempoSet) {
                    meta.tempo = (bytes[r.p] << 16) | (bytes[r.p + 1] << 8) | bytes[r.p + 2];
                    tempoSet = true;
                } else if (type == 0x58 && l >= 2 && !tsSet) {
                    meta.num = bytes[r.p];
                    meta.den = 1 << bytes[r.p + 1];
                    tsSet = true;
                }
                r.p += l;
                continue;
            }
            if (c == 0xF0 || c == 0xF7) {
                r.p++;
                r.p += r.vlq(end);
                continue;
            }
            if (c & 0x80) {
                status = c;
                r.p++;
            }
            int hi = status & 0xF0, ch = status & 0x0F;
            int nd = (hi == 0xC0 || hi == 0xD0) ? 1 : 2;
            if (r.p + nd > end) break;
            int d1 = bytes[r.p], d2 = nd == 2 ? bytes[r.p + 1] : 0;
            r.p += nd;
            if ((hi != 0x90 && hi != 0x80) || ch == 9) continue;
            int t = at(tick);
            auto k = std::make_pair(ch, d1);
            if (hi == 0x90 && d2 > 0) {
                auto it = open.find(k);
                if (it != open.end()) {
                    if (t > it->second.first) notes.push_back(Note{it->second.first, t, d1, it->second.second, ti});
                    open.erase(it);
                }
                open[k] = {t, d2};
            } else {
                auto it = open.find(k);
                if (it != open.end()) {
                    if (t > it->second.first) notes.push_back(Note{it->second.first, t, d1, it->second.second, ti});
                    open.erase(it);
                }
            }
        }
        for (auto& kv : open) notes.push_back(Note{kv.second.first, at(tick), kv.first.second, kv.second.second, ti});
        r.p = end;
    }
    std::stable_sort(notes.begin(), notes.end(), [](const Note& a, const Note& b) {
        return a.start != b.start ? a.start < b.start : a.pitch < b.pitch;
    });
    return true;
}

bool readSmf(const std::string& path, std::vector<Note>& notes, Meta& meta, std::string& err) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        err = "не удалось открыть файл";
        return false;
    }
    std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    return readSmfBytes(bytes, notes, meta, err);
}

std::vector<unsigned char> writeSmf(const std::vector<Part>& parts, const Meta& meta) {
    std::vector<unsigned char> out{'M', 'T', 'h', 'd', 0, 0, 0, 6, 0, 1};
    out.push_back((unsigned char)((parts.size() + 1) >> 8));
    out.push_back((unsigned char)(parts.size() + 1));
    out.push_back((unsigned char)(TPB >> 8));
    out.push_back((unsigned char)(TPB & 0xFF));
    // conductor
    std::vector<unsigned char> c;
    std::string cn = "Conductor";
    c.insert(c.end(), {0x00, 0xFF, 0x03, (unsigned char)cn.size()});
    c.insert(c.end(), cn.begin(), cn.end());
    int dpow = 0;
    while ((1 << dpow) < meta.den) ++dpow;
    c.insert(c.end(), {0x00, 0xFF, 0x58, 0x04, (unsigned char)meta.num, (unsigned char)dpow, 24, 8});
    c.insert(c.end(), {0x00, 0xFF, 0x51, 0x03, (unsigned char)(meta.tempo >> 16), (unsigned char)(meta.tempo >> 8),
                       (unsigned char)meta.tempo});
    c.insert(c.end(), {0x00, 0xFF, 0x2F, 0x00});
    putTrack(out, c);
    for (size_t ch = 0; ch < parts.size(); ++ch) {
        std::vector<unsigned char> t;
        const std::string& nm = parts[ch].name;
        t.push_back(0);
        t.insert(t.end(), {0xFF, 0x03});
        putVlq(t, (uint32_t)nm.size());
        t.insert(t.end(), nm.begin(), nm.end());
        struct Ev { int t, on, pitch, vel; };
        std::vector<Ev> ev;
        for (auto& n : parts[ch].notes) {
            ev.push_back({n.start, 1, n.pitch, n.vel});
            ev.push_back({n.end, 0, n.pitch, 0});
        }
        std::stable_sort(ev.begin(), ev.end(), [](const Ev& a, const Ev& b) {
            return a.t != b.t ? a.t < b.t : a.on < b.on;
        });
        int last = 0;
        // channel 10 is drums in General MIDI: voices skip it, the beatbox part goes there
        bool drums = nm.rfind("Beatbox", 0) == 0;
        int cidx = (int)ch < 9 ? (int)ch : std::min(15, (int)ch + 1);
        unsigned char chan = (unsigned char)(drums ? 9 : cidx);
        if (!drums) {
            // Acoustic Grand Piano: bank 0, program 0, so Logic opens the track with a piano patch
            std::string inst = "Acoustic Grand Piano";
            t.insert(t.end(), {0x00, 0xFF, 0x04});
            putVlq(t, (uint32_t)inst.size());
            t.insert(t.end(), inst.begin(), inst.end());
            t.insert(t.end(), {0x00, (unsigned char)(0xB0 | chan), 0x00, 0x00});
            t.insert(t.end(), {0x00, (unsigned char)(0xB0 | chan), 0x20, 0x00});
            t.insert(t.end(), {0x00, (unsigned char)(0xC0 | chan), 0x00});
        }
        for (auto& e : ev) {
            putVlq(t, (uint32_t)(e.t - last));
            last = e.t;
            t.push_back((unsigned char)((e.on ? 0x90 : 0x80) | chan));
            t.push_back((unsigned char)e.pitch);
            t.push_back((unsigned char)e.vel);
        }
        t.insert(t.end(), {0x00, 0xFF, 0x2F, 0x00});
        putTrack(out, t);
    }
    return out;
}

bool writeSmfFile(const std::string& path, const std::vector<Part>& parts, const Meta& meta) {
    auto b = writeSmf(parts, meta);
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    f.write((const char*)b.data(), (std::streamsize)b.size());
    return (bool)f;
}

}  // namespace choir
