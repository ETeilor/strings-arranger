#pragma once
#include "PianoRoll.h"

// Shared look of both plug-ins: flat, quiet, one accent.
// A button may carry an icon: button.getProperties().set("icon", "play") and so on (see Look.cpp).
class StringsLook : public juce::LookAndFeel_V4 {
public:
    StringsLook();
    void drawRotarySlider(juce::Graphics&, int x, int y, int w, int h, float pos, float a0, float a1,
                          juce::Slider&) override;
    void drawLinearSlider(juce::Graphics&, int x, int y, int w, int h, float pos, float minPos, float maxPos,
                          juce::Slider::SliderStyle, juce::Slider&) override;
    void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour&, bool hi, bool down) override;
    void drawButtonText(juce::Graphics&, juce::TextButton&, bool hi, bool down) override;
    void drawToggleButton(juce::Graphics&, juce::ToggleButton&, bool hi, bool down) override;
    void drawComboBox(juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
    juce::Font getComboBoxFont(juce::ComboBox&) override { return ui::font(13.5f); }
    juce::Font getPopupMenuFont() override { return ui::font(13.5f); }
    juce::Font getTextButtonFont(juce::TextButton&, int) override { return ui::font(13.0f); }
    void positionComboBoxText(juce::ComboBox&, juce::Label&) override;
    void drawPopupMenuBackground(juce::Graphics&, int w, int h) override;
    static void drawIcon(juce::Graphics&, const juce::String& icon, juce::Rectangle<float> box, juce::Colour);
    // small caps section label
    static void sectionLabel(juce::Label&, const juce::String& text);
};

namespace names {
juce::StringArray styles();      // order of strings::Composer
juce::StringArray sections();    // order of strings::Section
juce::StringArray libraries();   // order of strings::Library
juce::StringArray manners();     // live mode: classical voice leading or the modern one with colours
}
