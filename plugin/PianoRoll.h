#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "Strings.h"

// Cyrillic literals must reach JUCE as UTF-8
inline juce::String U(const char* s) { return juce::String::fromUTF8(s); }

namespace ui {
// palette shared by both editors: light theme, warm paper neutrals, one accent, part colours dark
// enough to read on a light ground
const juce::Colour bg{0xfff4f2ed}, panel{0xfffbfaf7}, panelHi{0xffe9e6df}, line{0xffd9d5cc},
    text{0xff1c1b1f}, textDim{0xff7b7872}, accent{0xff5b45e0}, onAccent{0xffffffff},
    danger{0xffe0483a}, whiteKey{0xffffffff}, blackKey{0xff2c2b30}, switchOff{0xffc9c5bb};
// hover and pressed states: a step darker on the light ground
inline juce::Colour lift(juce::Colour c, float amount) { return c.darker(amount * 0.9f); }
juce::Colour partColour(const std::string& name);
juce::Font font(float size, bool medium = false);
}  // namespace ui

class PianoRoll : public juce::Component {
public:
    void setData(const std::vector<choir::Part>* parts, const std::vector<choir::Note>* input, const choir::Meta& meta);
    void setPlayhead(double ticks) { playhead = ticks; repaint(); }
    void setEmptyText(const juce::String& t) { emptyText = t; repaint(); }
    void paint(juce::Graphics&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override { dragging = false; }
    void mouseEnter(const juce::MouseEvent&) override { hover = true; repaint(); }
    void mouseExit(const juce::MouseEvent&) override { hover = false; repaint(); }
    std::function<juce::File()> makeDragFile;   // dragging the roll carries all parts out as one .mid

    static constexpr float keyboardWidth = 44.0f;

private:
    bool hover = false, dragging = false;
    const std::vector<choir::Part>* parts = nullptr;
    const std::vector<choir::Note>* input = nullptr;
    choir::Meta meta;
    double playhead = -1;
    juce::String emptyText;
};
