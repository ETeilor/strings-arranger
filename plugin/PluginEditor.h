#pragma once
#include "Look.h"
#include "PianoRoll.h"
#include "PluginProcessor.h"

// A part chip: drag carries the part to a DAW track as a .mid file; the padlock locks the part.
class DragChip : public juce::Component, public juce::SettableTooltipClient {
public:
    std::function<juce::File()> makeFile;
    std::function<void()> onLock;      // a click on the padlock
    juce::String label, shortLabel;    // the short one is used when the chip is narrow
    juce::Colour colour;
    bool locked = false, lockable = false;
    void paint(juce::Graphics&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseEnter(const juce::MouseEvent&) override { hover = true; repaint(); }
    void mouseExit(const juce::MouseEvent&) override { hover = false; repaint(); }
    void mouseUp(const juce::MouseEvent& e) override;

private:
    juce::Rectangle<int> lockZone() const { return getLocalBounds().removeFromRight(26); }
    bool hover = false, dragging = false;
};

// Saved variants of the current style: numbered pills, the active one filled.
class SlotBar : public juce::Component, public juce::SettableTooltipClient {
public:
    int count = 0, active = -1;
    std::function<void(int)> onPick, onRemove;
    void paint(juce::Graphics&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent& e) override { hoverIndex = indexAt(e.getPosition()); repaint(); }
    void mouseExit(const juce::MouseEvent&) override { hoverIndex = -1; repaint(); }

private:
    int indexAt(juce::Point<int>) const;
    juce::Rectangle<float> pill(int i) const;
    int hoverIndex = -1;
};

class StringsEditor : public juce::AudioProcessorEditor, public juce::FileDragAndDropTarget, private juce::Timer {
public:
    explicit StringsEditor(StringsProcessor&);
    ~StringsEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    bool isInterestedInFileDrag(const juce::StringArray&) override;
    void filesDropped(const juce::StringArray&, int, int) override;
    void fileDragEnter(const juce::StringArray&, int, int) override { dropHover = true; repaint(); }
    void fileDragExit(const juce::StringArray&) override { dropHover = false; repaint(); }
    bool keyPressed(const juce::KeyPress&) override;
    void mouseDown(const juce::MouseEvent&) override { grabKeyboardFocus(); }

private:
    void timerCallback() override;
    void refresh();
    void updateStyleControls();
    void openFile();
    void exportAll();
    void exportScore();
    void paste();
    void showError(const juce::String& err);

    StringsProcessor& proc;
    StringsLook look;
    using SA = juce::AudioProcessorValueTreeState::SliderAttachment;
    using CA = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using BA = juce::AudioProcessorValueTreeState::ButtonAttachment;

    juce::TextButton captureBtn{U("Захват")}, openBtn{U("Открыть")}, pasteBtn{U("Вставить")}, refreshBtn{U("Обновить")},
        playBtn{U("Слушать")}, exportBtn{U("MIDI")}, scoreBtn{U("Ноты")}, prevVarBtn, nextVarBtn, saveVarBtn;
    juce::ComboBox styleBox, sectionBox, libraryBox, soundBox, inputBox;
    juce::ToggleButton dynamicsBtn{U("Динамика CC1")};
    juce::Label inputLbl, styleLbl, sectionLbl, sectionVal, libraryLbl, complexityLbl, complexityVal, voicesLbl, soundLbl,
        variantsLbl, variantLbl, titleLbl, sourceLbl, report;
    juce::Slider complexity;
    juce::Slider voice[strings::NPARTS];          // Violin I .. Contrabass
    juce::Label voiceLbl[strings::NPARTS], voiceVal[strings::NPARTS];
    std::unique_ptr<CA> styleA, sectionA, libraryA, soundA, inputA;
    std::unique_ptr<BA> dynamicsA;
    std::unique_ptr<SA> complexityA, voiceA[strings::NPARTS];

    PianoRoll roll;
    SlotBar slots;
    juce::OwnedArray<DragChip> chips;
    std::unique_ptr<DragChip> allChip, scoreChip;
    std::unique_ptr<juce::FileChooser> chooser;
    bool dropHover = false;
    juce::TooltipWindow tips{this, 700};
};
