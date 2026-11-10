#include "Look.h"

// ---------------------------------------------------------------- look
StringsLook::StringsLook() {
   #if JUCE_MAC
    setDefaultSansSerifTypefaceName("Helvetica Neue");
   #endif
    setColour(juce::ResizableWindow::backgroundColourId, ui::bg);
    setColour(juce::ComboBox::backgroundColourId, ui::panelHi);
    setColour(juce::ComboBox::outlineColourId, juce::Colours::transparentBlack);
    setColour(juce::ComboBox::textColourId, ui::text);
    setColour(juce::ComboBox::arrowColourId, ui::textDim);
    setColour(juce::PopupMenu::backgroundColourId, ui::panel);
    setColour(juce::PopupMenu::textColourId, ui::text);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, ui::accent);
    setColour(juce::PopupMenu::highlightedTextColourId, ui::onAccent);
    setColour(juce::Label::textColourId, ui::text);
    setColour(juce::TextButton::textColourOffId, ui::text);
    setColour(juce::TextButton::textColourOnId, ui::onAccent);
    setColour(juce::ToggleButton::textColourId, ui::text);
    setColour(juce::ToggleButton::tickColourId, ui::accent);
    setColour(juce::ToggleButton::tickDisabledColourId, ui::textDim);
    setColour(juce::Slider::textBoxTextColourId, ui::text);
    setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour(juce::TooltipWindow::backgroundColourId, ui::text);
    setColour(juce::TooltipWindow::textColourId, ui::bg);
    setColour(juce::TooltipWindow::outlineColourId, ui::line);
    setColour(juce::AlertWindow::backgroundColourId, ui::panel);
    setColour(juce::AlertWindow::textColourId, ui::text);
    setColour(juce::AlertWindow::outlineColourId, ui::line);
    setColour(juce::TextButton::buttonColourId, ui::panelHi);
}

void StringsLook::sectionLabel(juce::Label& l, const juce::String& text) {
    l.setText(text.toUpperCase(), juce::dontSendNotification);
    l.setFont(ui::font(10.5f, true).withExtraKerningFactor(0.09f));
    l.setColour(juce::Label::textColourId, ui::textDim);
    l.setBorderSize(juce::BorderSize<int>(0));
    l.setJustificationType(juce::Justification::centredLeft);
}

void StringsLook::drawIcon(juce::Graphics& g, const juce::String& icon, juce::Rectangle<float> b, juce::Colour c) {
    g.setColour(c);
    auto cx = b.getCentreX(), cy = b.getCentreY();
    float r = std::min(b.getWidth(), b.getHeight()) / 2.0f;
    juce::PathStrokeType stroke(1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
    juce::Path p;
    if (icon == "play") {
        p.addTriangle(cx - r * 0.6f, cy - r * 0.8f, cx - r * 0.6f, cy + r * 0.8f, cx + r * 0.8f, cy);
        g.fillPath(p);
    } else if (icon == "stop") {
        g.fillRoundedRectangle(cx - r * 0.65f, cy - r * 0.65f, r * 1.3f, r * 1.3f, 1.5f);
    } else if (icon == "rec") {
        g.fillEllipse(cx - r * 0.6f, cy - r * 0.6f, r * 1.2f, r * 1.2f);
    } else if (icon == "refresh") {
        p.addCentredArc(cx, cy, r * 0.75f, r * 0.75f, 0.0f, 0.7f, 5.6f, true);
        g.strokePath(p, stroke);
        juce::Path head;
        auto end = juce::Point<float>(cx + r * 0.75f * std::sin(5.6f), cy - r * 0.75f * std::cos(5.6f));
        head.addTriangle(end.x - r * 0.42f, end.y - r * 0.10f, end.x + r * 0.30f, end.y - r * 0.42f, end.x + r * 0.16f, end.y + r * 0.40f);
        g.fillPath(head);
    } else if (icon == "plus" || icon == "close") {
        p.startNewSubPath(cx - r * 0.6f, cy);
        p.lineTo(cx + r * 0.6f, cy);
        p.startNewSubPath(cx, cy - r * 0.6f);
        p.lineTo(cx, cy + r * 0.6f);
        if (icon == "close") p.applyTransform(juce::AffineTransform::rotation(juce::MathConstants<float>::pi / 4, cx, cy));
        g.strokePath(p, stroke);
    } else if (icon == "prev" || icon == "next") {
        float d = icon == "prev" ? 1.0f : -1.0f;
        p.startNewSubPath(cx + d * r * 0.3f, cy - r * 0.6f);
        p.lineTo(cx - d * r * 0.3f, cy);
        p.lineTo(cx + d * r * 0.3f, cy + r * 0.6f);
        g.strokePath(p, stroke);
    } else if (icon == "paste") {
        p.addRoundedRectangle(cx - r * 0.6f, cy - r * 0.55f, r * 1.2f, r * 1.35f, 1.5f);
        g.strokePath(p, stroke);
        g.fillRoundedRectangle(cx - r * 0.3f, cy - r * 0.85f, r * 0.6f, r * 0.45f, 1.0f);
    } else if (icon == "open") {
        p.startNewSubPath(cx, cy + r * 0.2f);
        p.lineTo(cx, cy - r * 0.75f);
        p.startNewSubPath(cx - r * 0.4f, cy - r * 0.35f);
        p.lineTo(cx, cy - r * 0.75f);
        p.lineTo(cx + r * 0.4f, cy - r * 0.35f);
        p.startNewSubPath(cx - r * 0.7f, cy + r * 0.2f);
        p.lineTo(cx - r * 0.7f, cy + r * 0.7f);
        p.lineTo(cx + r * 0.7f, cy + r * 0.7f);
        p.lineTo(cx + r * 0.7f, cy + r * 0.2f);
        g.strokePath(p, stroke);
    } else if (icon == "export") {
        p.startNewSubPath(cx, cy - r * 0.75f);
        p.lineTo(cx, cy + r * 0.25f);
        p.startNewSubPath(cx - r * 0.4f, cy - r * 0.15f);
        p.lineTo(cx, cy + r * 0.25f);
        p.lineTo(cx + r * 0.4f, cy - r * 0.15f);
        p.startNewSubPath(cx - r * 0.7f, cy + r * 0.7f);
        p.lineTo(cx + r * 0.7f, cy + r * 0.7f);
        g.strokePath(p, stroke);
    }
}

void StringsLook::drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h, float pos, float a0, float a1,
                                 juce::Slider& s) {
    auto b = juce::Rectangle<int>(x, y, w, h).toFloat().reduced(4.0f);
    float r = std::min(b.getWidth(), b.getHeight()) / 2.0f;
    auto c = b.getCentre();
    juce::Path track, val;
    track.addCentredArc(c.x, c.y, r - 2, r - 2, 0, a0, a1, true);
    g.setColour(ui::line);
    g.strokePath(track, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    val.addCentredArc(c.x, c.y, r - 2, r - 2, 0, a0, a0 + pos * (a1 - a0), true);
    g.setColour(ui::accent);
    g.strokePath(val, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour(ui::text);
    g.setFont(ui::font(13.0f));
    g.drawText(juce::String((int)s.getValue()), b, juce::Justification::centred);
}

// thin track, small round thumb; a slider whose minimum is below zero shows "=" there ("same as the overall")
void StringsLook::drawLinearSlider(juce::Graphics& g, int x, int y, int w, int h, float pos, float, float,
                                 juce::Slider::SliderStyle, juce::Slider& s) {
    float cy = (float)y + (float)h / 2.0f;
    float x0 = (float)x, x1 = (float)(x + w);
    bool automatic = s.getMinimum() < 0 && s.getValue() < 0;
    auto col = s.getProperties().contains("colour") ? juce::Colour((juce::uint32)(juce::int64)s.getProperties()["colour"])
                                                     : ui::accent;
    g.setColour(ui::line);
    g.fillRoundedRectangle(x0, cy - 1.0f, x1 - x0, 2.0f, 1.0f);
    if (!automatic) {
        g.setColour(col);
        g.fillRoundedRectangle(x0, cy - 1.0f, pos - x0, 2.0f, 1.0f);
    }
    float tr = s.isMouseOverOrDragging() ? 6.5f : 5.5f;
    g.setColour(automatic ? ui::textDim : col);
    g.fillEllipse(pos - tr, cy - tr, tr * 2, tr * 2);
    g.setColour(ui::bg);
    g.fillEllipse(pos - tr + 2.5f, cy - tr + 2.5f, (tr - 2.5f) * 2, (tr - 2.5f) * 2);
}

void StringsLook::drawButtonBackground(juce::Graphics& g, juce::Button& b, const juce::Colour&, bool hi, bool down) {
    auto r = b.getLocalBounds().toFloat().reduced(0.5f);
    float rad = std::min(9.0f, r.getHeight() / 2.0f);
    bool on = b.getToggleState();
    bool danger = b.getProperties().contains("danger");
    if (on) {
        g.setColour(danger ? ui::danger : ui::accent);
        g.fillRoundedRectangle(r, rad);
        return;
    }
    if (b.getProperties().contains("ghost")) {          // no outline: icon buttons in a row
        if (hi || down) {
            g.setColour(ui::lift(ui::panelHi, down ? 0.12f : 0.0f));
            g.fillRoundedRectangle(r, rad);
        }
        return;
    }
    g.setColour(hi ? ui::lift(ui::panelHi, down ? 0.14f : 0.07f) : ui::panelHi);
    g.fillRoundedRectangle(r, rad);
}

void StringsLook::drawButtonText(juce::Graphics& g, juce::TextButton& b, bool, bool) {
    bool on = b.getToggleState();
    auto col = !b.isEnabled() ? ui::textDim.withAlpha(0.5f) : on ? ui::onAccent : ui::text;
    auto r = b.getLocalBounds().toFloat();
    auto icon = b.getProperties()["icon"].toString();
    auto text = b.getButtonText();
    g.setFont(ui::font(13.0f));
    if (icon.isEmpty()) {
        g.setColour(col);
        g.drawText(text, r.reduced(8, 0), juce::Justification::centred);
        return;
    }
    if (text.isEmpty()) {
        drawIcon(g, icon, r.withSizeKeepingCentre(15, 15), col);
        return;
    }
    float tw = juce::Font(ui::font(13.0f)).getStringWidthFloat(text);
    float total = 14.0f + 7.0f + tw;
    float x = r.getCentreX() - total / 2.0f;
    drawIcon(g, icon, {x, r.getCentreY() - 7.0f, 14.0f, 14.0f}, col);
    g.setColour(col);
    g.drawText(text, juce::Rectangle<float>(x + 21.0f, r.getY(), tw + 4.0f, r.getHeight()), juce::Justification::centredLeft);
}

// a small switch and its label
void StringsLook::drawToggleButton(juce::Graphics& g, juce::ToggleButton& b, bool hi, bool) {
    auto r = b.getLocalBounds().toFloat();
    juce::Rectangle<float> sw(r.getX(), r.getCentreY() - 8.0f, 28.0f, 16.0f);
    bool on = b.getToggleState();
    g.setColour(on ? ui::accent : (hi ? ui::lift(ui::switchOff, 0.1f) : ui::switchOff));
    g.fillRoundedRectangle(sw, 8.0f);
    g.setColour(juce::Colours::white);
    float kx = on ? sw.getRight() - 14.0f : sw.getX() + 2.0f;
    g.fillEllipse(kx, sw.getY() + 2.0f, 12.0f, 12.0f);
    g.setColour(b.isEnabled() ? ui::text : ui::textDim);
    g.setFont(ui::font(13.0f));
    g.drawText(b.getButtonText(), r.withTrimmedLeft(38.0f), juce::Justification::centredLeft);
}

void StringsLook::drawComboBox(juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box) {
    juce::Rectangle<float> r(0.5f, 0.5f, (float)w - 1.0f, (float)h - 1.0f);
    g.setColour(box.isMouseOver(true) ? ui::lift(ui::panelHi, 0.07f) : ui::panelHi);
    g.fillRoundedRectangle(r, 8.0f);
    juce::Path p;
    float cx = (float)w - 16.0f, cy = (float)h / 2.0f;
    p.startNewSubPath(cx - 4.0f, cy - 2.0f);
    p.lineTo(cx, cy + 2.0f);
    p.lineTo(cx + 4.0f, cy - 2.0f);
    g.setColour(ui::textDim);
    g.strokePath(p, juce::PathStrokeType(1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void StringsLook::positionComboBoxText(juce::ComboBox& box, juce::Label& label) {
    label.setBounds(10, 1, box.getWidth() - 34, box.getHeight() - 2);
    label.setFont(getComboBoxFont(box));
}

void StringsLook::drawPopupMenuBackground(juce::Graphics& g, int w, int h) {
    g.fillAll(ui::panel);
    g.setColour(ui::line);
    g.drawRect(0, 0, w, h, 1);
}


// ---------------------------------------------------------------- menu names (same order as the engine enums)
juce::StringArray names::styles() {
    juce::StringArray a;
    for (int i = 0; i < strings::NUM_COMPOSERS; ++i) a.add(U(strings::composerLabel((strings::Composer)i)));
    return a;
}

juce::StringArray names::sections() {
    juce::StringArray a;
    for (int i = 0; i <= (int)strings::Section::Symphonic; ++i) a.add(U(strings::sectionLabel((strings::Section)i)));
    return a;
}

juce::StringArray names::libraries() {
    juce::StringArray a;
    for (int i = 0; i <= (int)strings::Library::BBCSO; ++i) a.add(U(strings::libraryLabel((strings::Library)i)));
    return a;
}

juce::StringArray names::manners() { return {U("Классическая"), U("Современная")}; }
