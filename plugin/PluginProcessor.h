#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "Strings.h"

// The processor keeps the input progression, the settings and the last arrangement.
// Input arrives two ways: a .mid file dropped on the window, or "capture": the plug-in sits on
// an instrument track and records the region while the host plays it.
// Audio output is only the built-in preview voice (piano or a rough string pad), used for auditioning.
class StringsProcessor : public juce::AudioProcessor,
                      private juce::AudioProcessorValueTreeState::Listener,
                      private juce::AsyncUpdater {
public:
    StringsProcessor();
    ~StringsProcessor() override;

    // ---- AudioProcessor
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout&) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Strings Arranger"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 1.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    // ---- model, message thread only
    juce::AudioProcessorValueTreeState params;
    bool loadMidiFile(const juce::File& f, juce::String& err);
    void setInput(std::vector<choir::Note> notes, choir::Meta meta, const juce::String& source);
    void regenerate();
    const strings::Result& result() const { return result_; }
    const std::vector<choir::Note>& input() const { return input_; }
    const choir::Meta& meta() const { return meta_; }
    juce::String inputSource() const { return inputSource_; }
    juce::File writePartFile(int partIndex) const;      // -1 = all parts in one file
    juce::File writeScoreFile() const;                  // MusicXML score in the temp folder
    juce::String scoreXml() const;
    juce::String scoreTitle() const;
    static bool isMidiFile(const juce::File& f);        // by content (MThd), any extension
    static bool isMidiData(const std::vector<unsigned char>& bytes);
    static juce::StringArray styleNames();
    // locks and variants
    void toggleLock(const juce::String& part);
    bool isLocked(const juce::String& part) const;
    void stepVariant(int dir);     // +1: the next variant that differs, -1: back
    strings::Settings settingsForTest() const { return currentSettings(); }
    // full recompute from the input alone: locks dropped, back to the main variant
    void refreshAll();
    // saved variants, per style: a slot holds every setting that shapes the arrangement
    int numSlots() const;
    int currentSlot() const;            // slot equal to the current settings, or -1
    void saveSlot();
    void loadSlot(int index);
    void removeSlot(int index);
    // clipboard: Cmd+C on a region in the DAW, then paste here
    bool pasteFromClipboard(juce::String& message);
    bool loadMidiBytes(const void* data, size_t size, const juce::String& source, juce::String& err);
    std::function<void()> onResultChanged;

    // capture
    void setCapture(bool armed);
    bool isCapturing() const { return captureArmed_.load(); }
    bool captureHasData() const { return captureCount_.load() > 0; }

    // preview
    void setPreview(bool on);
    bool isPreviewing() const { return previewOn_.load(); }
    double previewPositionTicks() const { return previewTicks_.load(); }

private:
    void parameterChanged(const juce::String&, float) override;
    void handleAsyncUpdate() override;
    strings::Settings currentSettings() const;   // (used by tests through settingsForTest)
    strings::MidiOptions midiOptions() const;
    void finishCapture();
    void rebuildPreviewEvents();
    static juce::AudioProcessorValueTreeState::ParameterLayout layout();

    std::vector<choir::Note> input_;
    choir::Meta meta_;
    juce::String inputSource_;
    strings::Result result_;
    std::map<std::string, std::vector<choir::Note>> locks_;          // finished parts
    std::map<std::string, std::vector<choir::Note>> skeletonLocks_;  // their lines in the four-voice skeleton
    int lockKey_ = -1;
    using Slot = std::map<juce::String, float>;
    std::map<int, std::vector<Slot>> slots_;      // style index -> saved variants
    std::map<int, int> variantByStyle_;           // the variant last used in each style
    int lastStyle_ = -1;
    Slot snapshot() const;
    int styleIndex() const;
    mutable int dragCounter_ = 0;

    // capture (audio thread writes, message thread reads after stop)
    struct RawEvent { double ppq; int note, vel; bool on; };
    juce::SpinLock captureLock_;
    std::vector<RawEvent> captured_;
    std::atomic<bool> captureArmed_{false}, wasPlaying_{false};
    std::atomic<int> captureCount_{0};
    std::atomic<double> captureBpm_{120.0};
    std::atomic<int> captureNum_{4}, captureDen_{4};

    // preview synth
    struct PEvent { double tick; int note; bool on; int vel; bool pluck; };
    juce::SpinLock previewLock_;
    std::vector<PEvent> previewEvents_;
    double previewLenTicks_ = 0;
    std::atomic<bool> previewOn_{false}, killNotes_{false};
    std::atomic<double> previewTicks_{0};
    size_t previewIdx_ = 0;
    juce::Synthesiser synth_, piano_;   // string pad and piano; the "sound" parameter picks one
    double sampleRate_ = 44100.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StringsProcessor)
};
