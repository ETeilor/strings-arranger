#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include <bitset>
#include "Strings.h"

// Strings Arranger Live: a MIDI FX. Sits in the MIDI FX slot in front of a string library, voices the
// chord being played in real time and sends the string parts on, all on one channel or one per part.
// It listens to how the chord is played: velocities go to the voices nearest to each key, the bass
// follows the lowest key (inversions stay inversions), the register and the spacing follow the hands.
class LiveProcessor : public juce::AudioProcessor {
public:
    LiveProcessor();
    void prepareToPlay(double sampleRate, int) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout&) const override { return true; }
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Strings Arranger Live"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return true; }
    double getTailLengthSeconds() const override { return 0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    juce::AudioProcessorValueTreeState params;

    // what sounds now, for the editor (message thread reads a copy)
    struct Snapshot {
        std::vector<std::string> names;
        std::vector<int> pitches, velocities;
        std::string chord;
    };
    Snapshot snapshot() const;

    // everything voiced since the last clear, for dragging to a track as MIDI
    int recordedChords() const { return recChords_.load(); }
    void clearRecording();
    juce::File writeRecordingFile() const;   // one track per part; empty File when nothing was played

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout layout();
    choir::Settings settings() const;
    void revoice(juce::MidiBuffer& out, int sample, double beats);

    choir::LiveVoicer voicer_;
    std::array<int, 128> held_{};        // velocity of a held key, 0 = up
    std::bitset<128> stale_;             // keys of the previous chord still down under a new one (legato)
    std::bitset<128> sustained_;         // keys released under the pedal
    std::array<int, 128> sustainVel_{};
    bool pedal_ = false;
    std::vector<int> sounding_, channels_;
    std::vector<std::string> soundingNames_;
    double sampleRate_ = 44100;
    long long pendingAt_ = -1;           // sample counter when the chord settles
    long long clock_ = 0;
    // onset grouping: two or more keys landing together start a new chord
    long long groupStart_ = -1000000;
    int groupCount_ = 0;
    std::bitset<128> beforeGroup_;

    mutable juce::SpinLock snapLock_;
    Snapshot snap_;

    // recording
    struct RecNote { double on, off; int pitch, vel; std::string part; };
    mutable juce::SpinLock recLock_;
    std::vector<RecNote> rec_;
    std::vector<int> recOpen_;           // index into rec_ per sounding voice, -1 none
    std::atomic<int> recChords_{0};
    double recOrigin_ = -1, recBpm_ = 120;
    int recNum_ = 4, recDen_ = 4;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LiveProcessor)
};
