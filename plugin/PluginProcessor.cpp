#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "PianoRoll.h"
#include "Look.h"
#include <cstring>

namespace {

// ---------------------------------------------------------------- preview voice (string pad)
struct AahSound : juce::SynthesiserSound {
    bool appliesToNote(int) override { return true; }
    bool appliesToChannel(int) override { return true; }
};

struct AahVoice : juce::SynthesiserVoice {
    using juce::SynthesiserVoice::renderNextBlock;
    bool canPlaySound(juce::SynthesiserSound* s) override { return dynamic_cast<AahSound*>(s) != nullptr; }

    void startNote(int note, float velocity, juce::SynthesiserSound*, int) override {
        f0 = juce::MidiMessage::getMidiNoteInHertz(note);
        gain = 0.12f * velocity;
        phase = 0;
        vibPhase = juce::Random::getSystemRandom().nextDouble() * juce::MathConstants<double>::twoPi;
        env.setSampleRate(getSampleRate());
        env.setParameters({0.035f, 0.1f, 0.9f, 0.16f});
        env.noteOn();
        // a bowed-string spectrum: every harmonic, falling off, with the body resonance near 500 Hz
        for (int h = 0; h < kH; ++h) {
            double fr = (h + 1) * f0;
            double body = 1.0 + 0.8 * std::exp(-std::pow((fr - 500) / 260, 2)) + 0.4 * std::exp(-std::pow((fr - 2600) / 700, 2));
            amp[h] = fr < getSampleRate() / 2 - 1000 ? (float)(body / std::pow((double)(h + 1), 1.15)) : 0.0f;
        }
        float norm = 0;
        for (float a : amp) norm += a * a;
        norm = std::sqrt(norm);
        if (norm > 0) for (float& a : amp) a /= norm;
        age = 0;
    }
    void stopNote(float, bool allowTail) override {
        if (allowTail) env.noteOff();
        else { env.reset(); clearCurrentNote(); }
    }
    void pitchWheelMoved(int) override {}
    void controllerMoved(int, int) override {}
    void renderNextBlock(juce::AudioBuffer<float>& out, int start, int num) override {
        if (!isVoiceActive()) return;
        const double sr = getSampleRate();
        for (int i = 0; i < num; ++i) {
            double vibDepth = 0.004 * juce::jlimit(0.0, 1.0, age / (0.35 * sr));
            double f = f0 * (1.0 + vibDepth * std::sin(vibPhase));
            vibPhase += juce::MathConstants<double>::twoPi * 5.2 / sr;
            phase += juce::MathConstants<double>::twoPi * f / sr;
            if (phase > juce::MathConstants<double>::twoPi * 1000) phase -= juce::MathConstants<double>::twoPi * 1000;
            float s = 0;
            for (int h = 0; h < kH; ++h)
                if (amp[h] != 0) s += amp[h] * (float)std::sin((h + 1) * phase);
            float e = env.getNextSample();
            float v = s * e * gain;
            for (int ch = 0; ch < out.getNumChannels(); ++ch) out.addSample(ch, start + i, v);
            ++age;
            if (!env.isActive()) { clearCurrentNote(); break; }
        }
    }
    static constexpr int kH = 24;
    float amp[kH]{};
    double f0 = 220, phase = 0, vibPhase = 0, age = 0;
    float gain = 0.1f;
    juce::ADSR env;
};

// ---------------------------------------------------------------- preview voice (piano)
// additive piano: slightly inharmonic partials, each decaying on its own, upper partials faster,
// two detuned "strings" per partial and a damper release on note-off.
struct PianoVoice : juce::SynthesiserVoice {
    using juce::SynthesiserVoice::renderNextBlock;
    bool canPlaySound(juce::SynthesiserSound* s) override { return dynamic_cast<AahSound*>(s) != nullptr; }

    void startNote(int note, float velocity, juce::SynthesiserSound*, int) override {
        const double sr = getSampleRate();
        double f0 = juce::MidiMessage::getMidiNoteInHertz(note);
        double B = 0.00012 * std::pow(2.0, (note - 60) / 24.0);          // inharmonicity grows up the keyboard
        double base = 0.55 + 0.9 * std::pow(2.0, (60 - note) / 18.0);      // seconds of sustain at the fundamental
        double bright = 0.6 + 0.8 * velocity;
        float norm = 0;
        for (int k = 0; k < kH; ++k) {
            int h = k + 1;
            double fr = h * f0 * std::sqrt(1 + B * h * h);
            inc[k] = juce::MathConstants<double>::twoPi * fr / sr;
            inc2[k] = inc[k] * 1.0007;
            ph[k] = ph2[k] = 0;
            float a = fr < sr / 2 - 500 ? (float)(std::pow(h, -1.0 - 0.6 / bright) * (h == 1 ? 1.0 : 0.8)) : 0.0f;
            if (fr > 5000) a *= (float)std::exp(-(fr - 5000) / 3000);
            amp[k] = a;
            norm += a;
            double tau = base / (1 + 0.35 * (h - 1));
            decay[k] = (float)std::exp(-1.0 / (tau * sr));
            lvl[k] = 1.0f;
        }
        if (norm > 0) for (float& a : amp) a /= norm;
        gain = 0.32f * velocity;
        attack = 0;
        attackInc = 1.0f / (float)(0.003 * sr);
        release = 1.0f;
        releaseMul = 1.0f;
        releaseStep = (float)std::exp(-1.0 / (0.12 * sr));
    }
    void stopNote(float, bool allowTail) override {
        if (allowTail) releaseMul = releaseStep;
        else clearCurrentNote();
    }
    void pitchWheelMoved(int) override {}
    void controllerMoved(int, int) override {}
    void renderNextBlock(juce::AudioBuffer<float>& out, int start, int num) override {
        if (!isVoiceActive()) return;
        for (int i = 0; i < num; ++i) {
            float s = 0;
            for (int k = 0; k < kH; ++k) {
                if (amp[k] == 0) continue;
                s += amp[k] * lvl[k] * (float)(std::sin(ph[k]) + 0.6 * std::sin(ph2[k]));
                ph[k] += inc[k];
                ph2[k] += inc2[k];
                lvl[k] *= decay[k];
            }
            if (attack < 1) attack = std::min(1.0f, attack + attackInc);
            release *= releaseMul;
            float v = s * gain * attack * release * 0.62f;
            for (int ch = 0; ch < out.getNumChannels(); ++ch) out.addSample(ch, start + i, v);
            if (release < 0.0005f || lvl[0] < 0.0005f) { clearCurrentNote(); break; }
        }
        for (int k = 0; k < kH; ++k) {
            ph[k] = std::fmod(ph[k], juce::MathConstants<double>::twoPi);
            ph2[k] = std::fmod(ph2[k], juce::MathConstants<double>::twoPi);
        }
    }
    static constexpr int kH = 14;
    double inc[kH]{}, inc2[kH]{}, ph[kH]{}, ph2[kH]{};
    float amp[kH]{}, decay[kH]{}, lvl[kH]{};
    float gain = 0.1f, attack = 0, attackInc = 0.01f, release = 1, releaseMul = 1, releaseStep = 0.999f;
};

const char* VOICE_IDS[strings::NPARTS] = {"voiceV1", "voiceV2", "voiceVa", "voiceVc", "voiceCb"};
// parameters that change the arrangement
const char* PARAM_IDS[] = {"style", "complexity", "voiceV1", "voiceV2", "voiceVa", "voiceVc", "voiceCb",
                           "section", "input", "variant"};
// what a saved variant remembers (everything that shapes the arrangement inside a style)
const char* SLOT_IDS[] = {"complexity", "voiceV1", "voiceV2", "voiceVa", "voiceVc", "voiceCb", "section", "variant"};

}  // namespace

// ---------------------------------------------------------------- setup
juce::StringArray StringsProcessor::styleNames() { return names::styles(); }

juce::AudioProcessorValueTreeState::ParameterLayout StringsProcessor::layout() {
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;
    l.add(std::make_unique<AudioParameterChoice>(ParameterID{"style", 1}, "Style", styleNames(),
                                                 (int)strings::Composer::Mozart));
    l.add(std::make_unique<AudioParameterInt>(ParameterID{"complexity", 1}, "Complexity", 0, 10, 4));
    auto voiceAttr = AudioParameterIntAttributes().withStringFromValueFunction(
        [](int v, int) { return v < 0 ? String::fromUTF8("общая") : String(v); });
    for (int i = 0; i < strings::NPARTS; ++i)
        l.add(std::make_unique<AudioParameterInt>(ParameterID{VOICE_IDS[i], 1}, String("Complexity ") + strings::PART_NAMES[i],
                                                  -1, 10, -1, voiceAttr));
    l.add(std::make_unique<AudioParameterChoice>(ParameterID{"section", 1}, "Section", names::sections(), 0));
    // the library the MIDI is written for, and phrase dynamics on the mod wheel for it
    l.add(std::make_unique<AudioParameterChoice>(ParameterID{"library", 1}, "Library", names::libraries(), 0));
    l.add(std::make_unique<AudioParameterBool>(ParameterID{"dynamics", 1}, "Dynamics CC1", true));
    l.add(std::make_unique<AudioParameterChoice>(ParameterID{"input", 1}, "Input",
                                                 StringArray{U("Авто"), U("Аккорды"), U("Гармонизовать мелодию")}, 0));
    l.add(std::make_unique<AudioParameterInt>(ParameterID{"variant", 1}, "Variant", 0, 9999, 0));
    l.add(std::make_unique<AudioParameterChoice>(ParameterID{"sound", 1}, "Preview sound",
                                                 StringArray{U("Пианино"), U("Струнные")}, 0));
    return l;
}

StringsProcessor::StringsProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      params(*this, nullptr, "StringsArranger", layout()) {
    for (auto* id : PARAM_IDS) params.addParameterListener(id, this);
    for (int i = 0; i < 24; ++i) synth_.addVoice(new AahVoice());
    synth_.addSound(new AahSound());
    for (int i = 0; i < 32; ++i) piano_.addVoice(new PianoVoice());
    piano_.addSound(new AahSound());
    params.addParameterListener("sound", this);
}

void StringsProcessor::handleAsyncUpdate() {
    if (!wasPlaying_.load() && captureArmed_.load() && captureCount_.load() > 0) {
        finishCapture();   // calls regenerate()
        return;
    }
    regenerate();
}

StringsProcessor::~StringsProcessor() {
    for (auto* id : PARAM_IDS) params.removeParameterListener(id, this);
    params.removeParameterListener("sound", this);
    cancelPendingUpdate();
}

bool StringsProcessor::isBusesLayoutSupported(const BusesLayout& l) const {
    auto out = l.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

void StringsProcessor::prepareToPlay(double sr, int) {
    sampleRate_ = sr;
    synth_.setCurrentPlaybackSampleRate(sr);
    piano_.setCurrentPlaybackSampleRate(sr);
}

juce::AudioProcessorEditor* StringsProcessor::createEditor() { return new StringsEditor(*this); }

void StringsProcessor::parameterChanged(const juce::String& id, float) {
    if (id == "sound") {
        killNotes_ = true;   // the other instrument takes over from the next note
        return;
    }
    triggerAsyncUpdate();
}

// ---------------------------------------------------------------- refresh, saved variants, clipboard
int StringsProcessor::styleIndex() const { return (int)*params.getRawParameterValue("style"); }

void StringsProcessor::refreshAll() {
    setPreview(false);
    locks_.clear();
    skeletonLocks_.clear();
    variantByStyle_.clear();
    juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("StringsArranger").deleteRecursively();
    auto* p = params.getParameter("variant");
    p->setValueNotifyingHost(p->convertTo0to1(0.0f));
    regenerate();
}

StringsProcessor::Slot StringsProcessor::snapshot() const {
    Slot s;
    for (auto* id : SLOT_IDS) s[id] = params.getRawParameterValue(id)->load();
    return s;
}

int StringsProcessor::numSlots() const {
    auto it = slots_.find(styleIndex());
    return it == slots_.end() ? 0 : (int)it->second.size();
}

int StringsProcessor::currentSlot() const {
    auto it = slots_.find(styleIndex());
    if (it == slots_.end()) return -1;
    auto now = snapshot();
    for (size_t i = 0; i < it->second.size(); ++i)
        if (it->second[i] == now) return (int)i;
    return -1;
}

void StringsProcessor::saveSlot() {
    if (currentSlot() >= 0) return;
    auto& v = slots_[styleIndex()];
    if (v.size() >= 8) v.erase(v.begin());
    v.push_back(snapshot());
    if (onResultChanged) onResultChanged();
}

void StringsProcessor::loadSlot(int index) {
    auto it = slots_.find(styleIndex());
    if (it == slots_.end() || index < 0 || index >= (int)it->second.size()) return;
    Slot s = it->second[(size_t)index];
    for (auto& kv : s) {
        auto* p = params.getParameter(kv.first);
        if (p) p->setValueNotifyingHost(p->convertTo0to1(kv.second));
    }
}

void StringsProcessor::removeSlot(int index) {
    auto it = slots_.find(styleIndex());
    if (it == slots_.end() || index < 0 || index >= (int)it->second.size()) return;
    it->second.erase(it->second.begin() + index);
    if (onResultChanged) onResultChanged();
}

bool stringsReadMidiFromClipboard(juce::MemoryBlock& midi, juce::String& report);   // Clipboard_mac.mm / Clipboard.cpp

bool StringsProcessor::pasteFromClipboard(juce::String& message) {
    juce::MemoryBlock midi;
    juce::String report;
    if (!stringsReadMidiFromClipboard(midi, report)) {
        auto f = juce::File::getSpecialLocation(juce::File::userDesktopDirectory).getChildFile("Strings Arranger paste report.txt");
        f.replaceWithText(report);
        message = U("В буфере обмена нет MIDI, который плагин может прочитать.\n\n") + report.substring(0, 700) +
                  U("\n\nОтчёт сохранён на рабочий стол: «Strings Arranger paste report.txt».");
        return false;
    }
    juce::String err;
    if (!loadMidiBytes(midi.getData(), midi.getSize(), U("буфер обмена"), err)) {
        message = err;
        return false;
    }
    return true;
}

// ---------------------------------------------------------------- locks and variants
void StringsProcessor::toggleLock(const juce::String& part) {
    auto name = part.toStdString();
    if (locks_.count(name)) {
        locks_.erase(name);
        skeletonLocks_.erase(name);
    } else {
        for (auto& p : result_.parts)
            if (p.name == name) locks_[name] = p.notes;
        // the other parts are built around the locked part's line in the skeleton
        for (auto& p : result_.skeleton)
            if (p.name == name) skeletonLocks_[name] = p.notes;
    }
    regenerate();
}

bool StringsProcessor::isLocked(const juce::String& part) const { return locks_.count(part.toStdString()) > 0; }

static bool sameParts(const strings::Result& a, const strings::Result& b) {
    if (a.parts.size() != b.parts.size()) return false;
    for (size_t i = 0; i < a.parts.size(); ++i) {
        auto &x = a.parts[i].notes, &y = b.parts[i].notes;
        if (x.size() != y.size()) return false;
        for (size_t k = 0; k < x.size(); ++k)
            if (x[k].start != y[k].start || x[k].end != y[k].end || x[k].pitch != y[k].pitch) return false;
    }
    return true;
}

void StringsProcessor::stepVariant(int dir) {
    int cur = (int)*params.getRawParameterValue("variant");
    int next = cur;
    if (dir < 0) {
        next = std::max(0, cur - 1);
    } else if (!input_.empty()) {
        // skip variants that sound the same as the one on screen
        auto s = currentSettings();
        for (int k = 1; k <= 24; ++k) {
            s.variant = cur + k;
            if (!sameParts(strings::arrange(input_, meta_, s), result_)) { next = cur + k; break; }
            next = cur + k;
        }
    } else {
        next = cur + 1;
    }
    auto* p = params.getParameter("variant");
    p->setValueNotifyingHost(p->convertTo0to1((float)juce::jlimit(0, 9999, next)));
}

strings::Settings StringsProcessor::currentSettings() const {
    strings::Settings s;
    s.composer = (strings::Composer)juce::jlimit(0, strings::NUM_COMPOSERS - 1, (int)*params.getRawParameterValue("style"));
    s.global = (int)*params.getRawParameterValue("complexity");
    for (int i = 0; i < strings::NPARTS; ++i) s.part[i] = (int)*params.getRawParameterValue(VOICE_IDS[i]);
    s.section = (strings::Section)juce::jlimit(0, (int)strings::Section::Symphonic, (int)*params.getRawParameterValue("section"));
    int in = (int)*params.getRawParameterValue("input");
    s.input = in == 1 ? choir::InputMode::Chords : in == 2 ? choir::InputMode::HarmonizeMelody : choir::InputMode::Auto;
    s.variant = (int)*params.getRawParameterValue("variant");
    s.locks = locks_;
    s.skeletonLocks = skeletonLocks_;
    return s;
}

strings::MidiOptions StringsProcessor::midiOptions() const {
    strings::MidiOptions o;
    o.library = (strings::Library)juce::jlimit(0, (int)strings::Library::SplitTracks, (int)*params.getRawParameterValue("library"));
    o.dynamics = *params.getRawParameterValue("dynamics") > 0.5f;
    return o;
}

// ---------------------------------------------------------------- input
bool StringsProcessor::loadMidiFile(const juce::File& f, juce::String& err) {
    juce::MemoryBlock mb;
    if (!f.loadFileAsData(mb)) {
        err = U("не удалось прочитать файл");
        return false;
    }
    return loadMidiBytes(mb.getData(), mb.getSize(), f.getFileName(), err);
}

bool StringsProcessor::loadMidiBytes(const void* data, size_t size, const juce::String& source, juce::String& err) {
    std::vector<unsigned char> bytes((const unsigned char*)data, (const unsigned char*)data + size);
    if (!isMidiData(bytes)) {
        err = U("это не MIDI-файл");
        return false;
    }
    std::vector<choir::Note> notes;
    choir::Meta meta;
    std::string e;
    if (!choir::readSmfBytes(bytes, notes, meta, e)) {
        err = juce::String::fromUTF8(e.c_str());
        return false;
    }
    if (notes.empty()) {
        err = U("в файле нет нот");
        return false;
    }
    setInput(std::move(notes), meta, source);
    return true;
}

void StringsProcessor::setInput(std::vector<choir::Note> notes, choir::Meta meta, const juce::String& source) {
    setPreview(false);
    locks_.clear();
    skeletonLocks_.clear();
    input_ = std::move(notes);
    meta_ = meta;
    inputSource_ = source;
    regenerate();
}

void StringsProcessor::regenerate() {
    // a new style, section or input mode changes what the parts are: the locks no longer apply
    int key = (int)*params.getRawParameterValue("style") * 100 + (int)*params.getRawParameterValue("section") * 10 +
              (int)*params.getRawParameterValue("input");
    if (lockKey_ >= 0 && key != lockKey_) locks_.clear(), skeletonLocks_.clear();
    lockKey_ = key;
    // each style keeps its own variant: coming back to a style returns to the variant left there
    int style = styleIndex();
    if (lastStyle_ >= 0 && style != lastStyle_) {
        int cur = (int)*params.getRawParameterValue("variant");
        variantByStyle_[lastStyle_] = cur;
        int want = variantByStyle_.count(style) ? variantByStyle_[style] : 0;
        lastStyle_ = style;
        if (want != cur) {
            auto* p = params.getParameter("variant");
            p->setValueNotifyingHost(p->convertTo0to1((float)want));   // comes back here through the listener
        }
    }
    lastStyle_ = style;
    if (input_.empty()) {
        result_ = strings::Result();
    } else {
        result_ = strings::arrange(input_, meta_, currentSettings());
    }
    rebuildPreviewEvents();
    if (onResultChanged) onResultChanged();
}

juce::File StringsProcessor::writePartFile(int idx) const {
    // a new folder for every drag: a host that remembers a file by its path never gets an old arrangement
    auto base = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("StringsArranger");
    auto dir = base.getChildFile("drag" + juce::String(juce::Time::currentTimeMillis()) + "_" + juce::String(++dragCounter_));
    dir.createDirectory();
    juce::String name = idx < 0 ? juce::String("Strings All Parts")
                                : "Strings " + juce::String::fromUTF8(result_.parts[(size_t)idx].name.c_str());
    auto opt = midiOptions();
    opt.only = idx;
    auto bytes = strings::writeMidi(result_, meta_, opt);
    auto f = dir.getChildFile(name + ".mid");
    f.replaceWithData(bytes.data(), bytes.size());
    return f;
}

bool StringsProcessor::isMidiData(const std::vector<unsigned char>& b) {
    return b.size() >= 14 && b[0] == 'M' && b[1] == 'T' && b[2] == 'h' && b[3] == 'd';
}

bool StringsProcessor::isMidiFile(const juce::File& f) {
    juce::FileInputStream in(f);
    if (!in.openedOk()) return false;
    char h[4]{};
    return in.read(h, 4) == 4 && std::memcmp(h, "MThd", 4) == 0;
}

juce::String StringsProcessor::scoreTitle() const {
    auto src = inputSource_.upToLastOccurrenceOf(".", false, false);
    if (src.isEmpty() || inputSource_.startsWith(U("захват"))) src = "Strings";
    return src + " - " + juce::String::fromUTF8(result_.styleLabel.c_str());
}

juce::String StringsProcessor::scoreXml() const {
    return juce::String::fromUTF8(strings::toMusicXml(result_, meta_, scoreTitle().toStdString()).c_str());
}

juce::File StringsProcessor::writeScoreFile() const {
    auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("StringsArranger");
    dir.createDirectory();
    auto f = dir.getChildFile("Strings Score.musicxml");
    auto x = strings::toMusicXml(result_, meta_, scoreTitle().toStdString());
    f.replaceWithData(x.data(), x.size());
    return f;
}

// ---------------------------------------------------------------- capture
void StringsProcessor::setCapture(bool armed) {
    {
        const juce::SpinLock::ScopedLockType l(captureLock_);
        captured_.clear();
    }
    captureCount_ = 0;
    wasPlaying_ = false;
    captureArmed_ = armed;
}

void StringsProcessor::finishCapture() {
    std::vector<RawEvent> ev;
    {
        const juce::SpinLock::ScopedLockType l(captureLock_);
        ev.swap(captured_);
    }
    captureCount_ = 0;
    captureArmed_ = false;
    std::stable_sort(ev.begin(), ev.end(), [](const RawEvent& a, const RawEvent& b) { return a.ppq < b.ppq; });
    std::map<int, std::pair<int, int>> open;
    std::vector<choir::Note> notes;
    for (auto& e : ev) {
        int t = (int)std::lround(e.ppq * choir::TPB);
        auto it = open.find(e.note);
        if (it != open.end()) {
            if (t > it->second.first) notes.push_back({it->second.first, t, e.note, it->second.second, 0});
            open.erase(it);
        }
        if (e.on) open[e.note] = {t, e.vel};
    }
    int lastT = ev.empty() ? 0 : (int)std::lround(ev.back().ppq * choir::TPB);
    for (auto& kv : open)
        if (lastT > kv.second.first) notes.push_back({kv.second.first, lastT, kv.first, kv.second.second, 0});
    if (notes.empty()) return;
    choir::Meta meta;
    meta.tempo = (int)std::lround(60000000.0 / juce::jmax(20.0, captureBpm_.load()));
    meta.num = captureNum_.load();
    meta.den = captureDen_.load();
    int barTicks = meta.num * choir::TPB * 4 / juce::jmax(1, meta.den);
    int first = notes.front().start;
    for (auto& n : notes) first = std::min(first, n.start);
    int offset = (first / barTicks) * barTicks;
    for (auto& n : notes) n.start -= offset, n.end -= offset;
    std::stable_sort(notes.begin(), notes.end(), [](const choir::Note& a, const choir::Note& b) {
        return a.start != b.start ? a.start < b.start : a.pitch < b.pitch;
    });
    setInput(std::move(notes), meta, U("захват с дорожки"));
}

// ---------------------------------------------------------------- preview
void StringsProcessor::rebuildPreviewEvents() {
    std::vector<PEvent> ev;
    double len = 0;
    for (auto& p : result_.parts)
        for (auto& n : p.notes) {
            // short bowings and pizzicato sound short in the preview too
            ev.push_back({(double)n.start, n.pitch, true, n.vel});
            ev.push_back({(double)strings::soundingEnd(n), n.pitch, false, 0});
            len = std::max(len, (double)n.end);
        }
    std::stable_sort(ev.begin(), ev.end(), [](const PEvent& a, const PEvent& b) {
        return a.tick != b.tick ? a.tick < b.tick : (!a.on && b.on);
    });
    const juce::SpinLock::ScopedLockType l(previewLock_);
    previewEvents_.swap(ev);
    previewLenTicks_ = len;
    previewIdx_ = 0;
}

void StringsProcessor::setPreview(bool on) {
    const juce::SpinLock::ScopedLockType l(previewLock_);
    previewTicks_ = 0;
    previewIdx_ = 0;
    previewOn_ = on && !previewEvents_.empty();
    killNotes_ = true;
}

// ---------------------------------------------------------------- audio thread
void StringsProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) {
    juce::ScopedNoDenormals nd;
    buffer.clear();
    const int n = buffer.getNumSamples();

    if (captureArmed_.load()) {
        bool playing = false;
        double ppq0 = 0, bpm = 120;
        if (auto* ph = getPlayHead())
            if (auto pos = ph->getPosition()) {
                playing = pos->getIsPlaying();
                if (auto p = pos->getPpqPosition()) ppq0 = *p;
                if (auto b = pos->getBpm()) bpm = *b;
                if (auto ts = pos->getTimeSignature()) {
                    captureNum_ = ts->numerator;
                    captureDen_ = ts->denominator;
                }
            }
        if (playing) {
            captureBpm_ = bpm;
            wasPlaying_ = true;
            const juce::SpinLock::ScopedLockType l(captureLock_);
            for (const auto m : midi) {
                auto msg = m.getMessage();
                if (!msg.isNoteOnOrOff()) continue;
                double ppq = ppq0 + m.samplePosition / sampleRate_ * bpm / 60.0;
                captured_.push_back({ppq, msg.getNoteNumber(), msg.getVelocity(), msg.isNoteOn()});
                ++captureCount_;
            }
        } else if (wasPlaying_.load()) {
            wasPlaying_ = false;
            if (captureCount_.load() > 0) triggerAsyncUpdate();
        }
    }
    midi.clear();

    juce::MidiBuffer synthMidi;
    if (killNotes_.exchange(false)) {
        synth_.allNotesOff(0, false);
        piano_.allNotesOff(0, false);
        midi.addEvent(juce::MidiMessage::allNotesOff(1), 0);
    }
    if (previewOn_.load()) {
        const juce::SpinLock::ScopedTryLockType l(previewLock_);
        if (l.isLocked()) {
            double ticksPerSample = choir::TPB * (1e6 / meta_.tempo) / sampleRate_;
            double t0 = previewTicks_.load(), t1 = t0 + n * ticksPerSample;
            while (previewIdx_ < previewEvents_.size() && previewEvents_[previewIdx_].tick < t1) {
                auto& e = previewEvents_[previewIdx_++];
                int s = juce::jlimit(0, n - 1, (int)((e.tick - t0) / ticksPerSample));
                auto m = e.on ? juce::MidiMessage::noteOn(1, e.note, (juce::uint8)juce::jlimit(1, 127, e.vel)) : juce::MidiMessage::noteOff(1, e.note);
                synthMidi.addEvent(m, s);
                midi.addEvent(m, s);
            }
            previewTicks_ = t1;
            if (t1 > previewLenTicks_ + choir::TPB) {
                previewOn_ = false;
                synthMidi.addEvent(juce::MidiMessage::allNotesOff(1), n - 1);
            }
        }
    }
    bool usePiano = (int)*params.getRawParameterValue("sound") == 0;
    juce::MidiBuffer empty;
    (usePiano ? piano_ : synth_).renderNextBlock(buffer, synthMidi, 0, n);
    (usePiano ? synth_ : piano_).renderNextBlock(buffer, empty, 0, n);   // lets released notes ring out
}

// ---------------------------------------------------------------- state
void StringsProcessor::getStateInformation(juce::MemoryBlock& dest) {
    auto state = params.copyState();
    auto xml = std::make_unique<juce::XmlElement>("StringsArrangerState");
    xml->addChildElement(state.createXml().release());
    auto* in = xml->createNewChildElement("Input");
    in->setAttribute("tempo", meta_.tempo);
    in->setAttribute("num", meta_.num);
    in->setAttribute("den", meta_.den);
    in->setAttribute("source", inputSource_);
    juce::String s;
    for (auto& n : input_) s << n.start << ' ' << n.end << ' ' << n.pitch << ' ' << n.vel << ' ' << n.track << ';';
    in->setAttribute("notes", s);
    auto writeLocks = [&](const char* tag, const std::map<std::string, std::vector<choir::Note>>& locks) {
        auto* lk = xml->createNewChildElement(tag);
        for (auto& kv : locks) {
            auto* e = lk->createNewChildElement("Lock");
            e->setAttribute("part", juce::String::fromUTF8(kv.first.c_str()));
            juce::String ns;
            for (auto& n : kv.second) ns << n.start << ' ' << n.end << ' ' << n.pitch << ' ' << n.vel << ' ' << n.art << ';';
            e->setAttribute("notes", ns);
        }
    };
    writeLocks("Locks", locks_);
    writeLocks("SkeletonLocks", skeletonLocks_);
    auto* sl = xml->createNewChildElement("Slots");
    for (auto& kv : slots_)
        for (auto& slot : kv.second) {
            auto* e = sl->createNewChildElement("Slot");
            e->setAttribute("style", kv.first);
            for (auto& v : slot) e->setAttribute(v.first, (double)v.second);
        }
    copyXmlToBinary(*xml, dest);
}

void StringsProcessor::setStateInformation(const void* data, int size) {
    auto xml = getXmlFromBinary(data, size);
    if (!xml) return;
    if (auto* st = xml->getChildByName(params.state.getType())) params.replaceState(juce::ValueTree::fromXml(*st));
    if (auto* in = xml->getChildByName("Input")) {
        choir::Meta m;
        m.tempo = in->getIntAttribute("tempo", 500000);
        m.num = in->getIntAttribute("num", 4);
        m.den = in->getIntAttribute("den", 4);
        std::vector<choir::Note> notes;
        for (auto& tok : juce::StringArray::fromTokens(in->getStringAttribute("notes"), ";", "")) {
            auto f = juce::StringArray::fromTokens(tok, " ", "");
            if (f.size() >= 5)
                notes.push_back({f[0].getIntValue(), f[1].getIntValue(), f[2].getIntValue(), f[3].getIntValue(), f[4].getIntValue()});
        }
        input_ = notes;
        meta_ = m;
        inputSource_ = in->getStringAttribute("source");
    }
    auto readLocks = [&](const char* tag, std::map<std::string, std::vector<choir::Note>>& locks) {
        locks.clear();
        if (auto* lk = xml->getChildByName(tag))
            for (auto* e : lk->getChildIterator()) {
                std::vector<choir::Note> ns;
                for (auto& tok : juce::StringArray::fromTokens(e->getStringAttribute("notes"), ";", "")) {
                    auto f = juce::StringArray::fromTokens(tok, " ", "");
                    if (f.size() < 5) continue;
                    choir::Note n;
                    n.start = f[0].getIntValue(), n.end = f[1].getIntValue(), n.pitch = f[2].getIntValue();
                    n.vel = f[3].getIntValue(), n.art = f[4].getIntValue();
                    ns.push_back(n);
                }
                locks[e->getStringAttribute("part").toStdString()] = ns;
            }
    };
    readLocks("Locks", locks_);
    readLocks("SkeletonLocks", skeletonLocks_);
    lockKey_ = -1;   // the restored parameters may arrive after this: keep the restored locks
    lastStyle_ = -1;
    slots_.clear();
    if (auto* sl = xml->getChildByName("Slots"))
        for (auto* e : sl->getChildIterator()) {
            Slot slot;
            for (auto* id : SLOT_IDS)
                if (e->hasAttribute(id)) slot[id] = (float)e->getDoubleAttribute(id);
            if (slot.size() == (size_t)std::size(SLOT_IDS)) slots_[e->getIntAttribute("style")].push_back(slot);
        }
    triggerAsyncUpdate();   // the host may call this off the message thread
}

#ifndef STRINGS_NO_PLUGIN_ENTRY
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new StringsProcessor(); }
#endif
