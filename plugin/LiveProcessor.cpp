#include "LiveProcessor.h"
#include "LiveEditor.h"
#include "PianoRoll.h"
#include "Look.h"

namespace {
constexpr double SETTLE_MS = 14.0;   // keys of one chord never land at the same instant
}

juce::AudioProcessorValueTreeState::ParameterLayout LiveProcessor::layout() {
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;
    l.add(std::make_unique<AudioParameterChoice>(ParameterID{"manner", 1}, "Manner", names::manners(), 0));
    l.add(std::make_unique<AudioParameterBool>(ParameterID{"basses", 1}, "Basses an octave under the cellos", true));
    l.add(std::make_unique<AudioParameterBool>(ParameterID{"topmelody", 1}, "Top note is the melody", true));
    l.add(std::make_unique<AudioParameterChoice>(ParameterID{"channels", 1}, "Channels",
                                                 StringArray{U("Все партии в один канал"), U("Каждая партия в свой канал")}, 0));
    l.add(std::make_unique<AudioParameterInt>(ParameterID{"variant", 1}, "Variant", 0, 99, 0));
    l.add(std::make_unique<AudioParameterChoice>(ParameterID{"register", 1}, "Register",
                                                 StringArray{U("Обычный"), U("На октаву ниже"), U("На две октавы ниже")}, 0));
    l.add(std::make_unique<AudioParameterBool>(ParameterID{"velocity", 1}, "Velocity per voice", true));
    return l;
}

LiveProcessor::LiveProcessor()
    : AudioProcessor(BusesProperties()), params(*this, nullptr, "StringsArrangerLive", layout()) {}

void LiveProcessor::prepareToPlay(double sr, int) {
    sampleRate_ = sr;
}

choir::Settings LiveProcessor::settings() const {
    // classical voice leading, or the modern one that keeps added tones and allows parallels
    bool modern = (int)*params.getRawParameterValue("manner") == 1;
    choir::Settings s = strings::liveSettings(modern ? strings::Composer::Barber : strings::Composer::Mozart);
    s.variant = (int)*params.getRawParameterValue("variant");
    s.global = 2;
    return s;
}

void LiveProcessor::revoice(juce::MidiBuffer& out, int sample, double beats) {
    std::vector<choir::LiveKey> keys;
    for (int k = 0; k < 128; ++k) {
        if (held_[(size_t)k] > 0 && !stale_[(size_t)k]) keys.push_back({k, held_[(size_t)k]});
        else if (sustained_[(size_t)k]) keys.push_back({k, sustainVel_[(size_t)k]});
    }
    bool perVoice = (int)*params.getRawParameterValue("channels") == 1;
    bool useVel = *params.getRawParameterValue("velocity") > 0.5f;
    int shift = -(int)*params.getRawParameterValue("register");
    choir::LiveVoicing v;
    if (!keys.empty()) v = voicer_.voice(keys, settings(), *params.getRawParameterValue("topmelody") > 0.5f, shift);
    else voicer_.released();
    // the engine doubles a hard bass with a second bass voice: for strings that voice is the double basses,
    // an octave under the cellos, and it is there whenever the switch is on
    {
        choir::LiveVoicing w;
        w.chord = v.chord;
        int cello = -1, celloVel = 90;
        for (size_t i = 0; i < v.names.size(); ++i) {
            if (v.names[i] == "Cello 2") continue;
            if (v.names[i] == "Cello") cello = v.pitches[i], celloVel = v.velocities[i];
            w.names.push_back(v.names[i]), w.pitches.push_back(v.pitches[i]);
            w.velocities.push_back(v.velocities[i]), w.group.push_back(v.group[i]);
        }
        if (cello >= 0 && *params.getRawParameterValue("basses") > 0.5f) {
            int p = cello - 12 >= 28 ? cello - 12 : cello;
            w.names.push_back("Contrabass"), w.pitches.push_back(p), w.velocities.push_back(celloVel), w.group.push_back(4);
        }
        v = w;
    }
    int avg = 90;
    if (!keys.empty()) {
        avg = 0;
        for (auto& k : keys) avg += k.vel;
        avg /= (int)keys.size();
    }
    // voices are matched by part name, so an extra voice coming or going does not retrigger the others
    std::vector<int> nextPitch, nextChan;
    std::vector<std::string> nextNames;
    const juce::SpinLock::ScopedLockType rl(recLock_);
    std::vector<int> nextOpen;
    bool changed = false;
    for (size_t i = 0; i < v.pitches.size(); ++i) {
        int ch = perVoice ? juce::jlimit(1, 16, v.group[i] + 1) : 1;
        int vel = useVel ? v.velocities[i] : avg;
        int old = -1;
        for (size_t j = 0; j < soundingNames_.size(); ++j)
            if (soundingNames_[j] == v.names[i]) old = (int)j;
        if (old >= 0 && sounding_[(size_t)old] == v.pitches[i] && channels_[(size_t)old] == ch) {
            nextOpen.push_back(recOpen_[(size_t)old]);
            recOpen_[(size_t)old] = -2;      // carried over
            sounding_[(size_t)old] = -1;
        } else {
            out.addEvent(juce::MidiMessage::noteOn(ch, v.pitches[i], (juce::uint8)juce::jlimit(1, 127, vel)), sample);
            if (rec_.size() < 40000) {
                rec_.push_back({beats, -1.0, v.pitches[i], vel, v.names[i]});
                nextOpen.push_back((int)rec_.size() - 1);
            } else {
                nextOpen.push_back(-1);
            }
            changed = true;
        }
        nextPitch.push_back(v.pitches[i]);
        nextChan.push_back(ch);
        nextNames.push_back(v.names[i]);
    }
    for (size_t j = 0; j < sounding_.size(); ++j) {
        if (sounding_[j] >= 0) out.addEvent(juce::MidiMessage::noteOff(channels_[j], sounding_[j]), sample);
        if (recOpen_[j] >= 0 && recOpen_[j] < (int)rec_.size()) rec_[(size_t)recOpen_[j]].off = beats;
    }
    sounding_ = nextPitch, channels_ = nextChan, soundingNames_ = nextNames, recOpen_ = nextOpen;
    if (changed && !v.pitches.empty()) recChords_++;
    const juce::SpinLock::ScopedLockType l(snapLock_);
    snap_.names = v.names;
    snap_.pitches = v.pitches;
    snap_.velocities = v.velocities;
    snap_.chord = v.chord;
}

void LiveProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) {
    buffer.clear();
    const int n = buffer.getNumSamples() > 0 ? buffer.getNumSamples() : 512;
    const long long settle = (long long)(SETTLE_MS * 0.001 * sampleRate_);
    // time base of the recording: the host's beats while it plays, the plug-in's own clock otherwise
    double bpm = 120, ppq0 = 0;
    bool playing = false;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition()) {
            playing = pos->getIsPlaying();
            if (auto b = pos->getBpm()) bpm = *b;
            if (auto p = pos->getPpqPosition()) ppq0 = *p;
            if (auto ts = pos->getTimeSignature()) recNum_ = ts->numerator, recDen_ = ts->denominator;
        }
    recBpm_ = bpm;
    auto beatsAt = [&](long long at) {
        double rel = (double)(at - clock_) / sampleRate_ * bpm / 60.0;
        return playing ? ppq0 + rel : (double)clock_ / sampleRate_ * bpm / 60.0 + rel + 1.0e6;   // far from host time
    };
    juce::MidiBuffer out;
    for (const auto m : midi) {
        auto msg = m.getMessage();
        const long long at = clock_ + m.samplePosition;
        if (pendingAt_ >= 0 && at >= pendingAt_) {
            revoice(out, (int)juce::jlimit<long long>(0, n - 1, pendingAt_ - clock_), beatsAt(pendingAt_));
            pendingAt_ = -1;
        }
        if (msg.isNoteOn()) {
            int k = msg.getNoteNumber();
            if (at - groupStart_ > settle) {             // a new onset group
                groupStart_ = at;
                groupCount_ = 0;
                beforeGroup_.reset();
                for (int q = 0; q < 128; ++q)
                    if (held_[(size_t)q] > 0) beforeGroup_.set((size_t)q);
                if (beforeGroup_.none() && pedal_) sustained_.reset();   // a new chord under the pedal replaces the old
            }
            ++groupCount_;
            // two or more keys together are a new chord: keys still down from the previous one no longer count
            if (groupCount_ >= 2) {
                stale_ |= beforeGroup_;
                sustained_.reset();
            }
            held_[(size_t)k] = juce::jmax(1, (int)msg.getVelocity());
            stale_.reset((size_t)k);
            pendingAt_ = at + settle;
        } else if (msg.isNoteOff()) {
            int k = msg.getNoteNumber();
            bool wasStale = stale_[(size_t)k];
            if (pedal_ && held_[(size_t)k] > 0 && !wasStale) {
                sustained_.set((size_t)k);
                sustainVel_[(size_t)k] = held_[(size_t)k];
            }
            held_[(size_t)k] = 0;
            stale_.reset((size_t)k);
            if (!wasStale && !pedal_) pendingAt_ = at + settle;
        } else if (msg.isSustainPedalOn()) {
            pedal_ = true;
        } else if (msg.isSustainPedalOff()) {
            pedal_ = false;
            if (sustained_.any()) {
                sustained_.reset();
                pendingAt_ = at + settle;
            }
        } else if (msg.isAllNotesOff() || msg.isAllSoundOff()) {
            held_.fill(0);
            stale_.reset();
            sustained_.reset();
            pendingAt_ = at;
        } else {
            // controllers, pitch bend, aftertouch: to every channel the voices use
            if (msg.getChannel() > 0 && (int)*params.getRawParameterValue("channels") == 1) {
                for (int ch = 1; ch <= 5; ++ch) {
                    auto c = msg;
                    c.setChannel(ch);
                    out.addEvent(c, m.samplePosition);
                }
            } else {
                out.addEvent(msg, m.samplePosition);
            }
        }
    }
    if (pendingAt_ >= 0 && pendingAt_ < clock_ + n) {
        revoice(out, (int)juce::jlimit<long long>(0, n - 1, pendingAt_ - clock_), beatsAt(pendingAt_));
        pendingAt_ = -1;
    }
    clock_ += n;
    midi.swapWith(out);
}

LiveProcessor::Snapshot LiveProcessor::snapshot() const {
    const juce::SpinLock::ScopedLockType l(snapLock_);
    return snap_;
}

void LiveProcessor::clearRecording() {
    const juce::SpinLock::ScopedLockType l(recLock_);
    rec_.clear();
    for (auto& o : recOpen_) o = -1;
    recChords_ = 0;
}

juce::File LiveProcessor::writeRecordingFile() const {
    std::vector<RecNote> notes;
    {
        const juce::SpinLock::ScopedLockType l(recLock_);
        notes = rec_;
    }
    if (notes.empty()) return {};
    // a take may mix host time and the plug-in's own clock, or hold long silences: close every gap over four bars
    std::sort(notes.begin(), notes.end(), [](const RecNote& x, const RecNote& y) { return x.on < y.on; });
    {
        double barLen = recNum_ * 4.0 / juce::jmax(1, recDen_), shift = 0, prevEnd = notes.front().on;
        for (auto& r : notes) {
            double gap = r.on - shift - prevEnd;
            if (gap > 4 * barLen) shift += gap - barLen;
            r.on -= shift;
            if (r.off >= 0) r.off -= shift;
            prevEnd = std::max(prevEnd, std::max(r.on, r.off));
        }
    }
    double first = notes.front().on, last = 0;
    for (auto& r : notes) first = std::min(first, r.on), last = std::max(last, std::max(r.on, r.off));
    choir::Meta meta;
    meta.tempo = (int)std::lround(60000000.0 / juce::jmax(20.0, recBpm_));
    meta.num = recNum_, meta.den = recDen_;
    double bar = meta.num * 4.0 / juce::jmax(1, meta.den);
    double origin = std::floor(first / bar) * bar;          // keep the place inside the bar
    std::vector<choir::Part> parts;
    auto partOf = [&](const std::string& name) -> choir::Part& {
        for (auto& p : parts)
            if (p.name == name) return p;
        parts.push_back({name, {}});
        return parts.back();
    };
    // score order
    for (auto* nm : {"Violin I", "Violin I 2", "Violin II", "Viola", "Cello", "Contrabass"})
        for (auto& r : notes)
            if (r.part == nm) { partOf(nm); break; }
    for (auto& r : notes) {
        double off = r.off > r.on ? r.off : last + 0.25;    // still sounding: ends with the take
        int a = (int)std::lround((r.on - origin) * choir::TPB), b = (int)std::lround((off - origin) * choir::TPB);
        if (b - a < 30) b = a + 30;
        partOf(r.part).notes.push_back(choir::Note{a, b, r.pitch, juce::jlimit(1, 127, r.vel), 0});
    }
    auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                   .getChildFile("StringsArranger")
                   .getChildFile("live" + juce::String(juce::Time::currentTimeMillis()));
    dir.createDirectory();
    auto f = dir.getChildFile("Strings Live.mid");
    strings::Result take;
    take.parts = parts;
    auto bytes = strings::writeMidi(take, meta, strings::MidiOptions{});
    f.replaceWithData(bytes.data(), bytes.size());
    return f;
}

void LiveProcessor::getStateInformation(juce::MemoryBlock& dest) {
    auto x = params.copyState().createXml();
    copyXmlToBinary(*x, dest);
}

void LiveProcessor::setStateInformation(const void* data, int size) {
    if (auto x = getXmlFromBinary(data, size))
        if (x->hasTagName(params.state.getType())) params.replaceState(juce::ValueTree::fromXml(*x));
}

juce::AudioProcessorEditor* LiveProcessor::createEditor() { return new LiveEditor(*this); }

#ifndef STRINGS_NO_PLUGIN_ENTRY
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new LiveProcessor(); }
#endif
