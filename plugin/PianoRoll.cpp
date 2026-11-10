#include "PianoRoll.h"
#include <map>

juce::Colour ui::partColour(const std::string& n) {
    auto starts = [&](const char* s) { return n.rfind(s, 0) == 0; };
    juce::Colour c = juce::Colours::grey;
    // top to bottom: amber, a lighter amber for the seconds, red, green, blue
    if (starts("Violin II")) c = juce::Colour(0xffe2a94a);
    else if (starts("Violin I")) c = juce::Colour(0xffc67a08);
    else if (starts("Viola")) c = juce::Colour(0xffd4503f);
    else if (starts("Cello")) c = juce::Colour(0xff1a9a7e);
    else if (starts("Contrabass")) c = juce::Colour(0xff2f7fd0);
    // the second line of a divisi pair is a lighter shade of the first
    if (n.size() > 2 && n.compare(n.size() - 2, 2, " 2") == 0) c = c.brighter(0.35f).withMultipliedSaturation(0.7f);
    return c;
}

juce::Font ui::font(float size, bool medium) {
    return juce::Font(juce::FontOptions(size, medium ? juce::Font::bold : juce::Font::plain));
}

void PianoRoll::mouseDrag(const juce::MouseEvent& e) {
    if (dragging || e.getDistanceFromDragStart() < 6 || !makeDragFile || !parts || parts->empty()) return;
    dragging = true;
    auto f = makeDragFile();
    if (f.existsAsFile())
        juce::DragAndDropContainer::performExternalDragDropOfFiles({f.getFullPathName()}, false, this);
}

void PianoRoll::setData(const std::vector<choir::Part>* p, const std::vector<choir::Note>* in, const choir::Meta& m) {
    parts = p;
    input = in;
    meta = m;
    repaint();
}

void PianoRoll::paint(juce::Graphics& g) {
    auto r = getLocalBounds().toFloat();
    g.setColour(ui::panel);
    g.fillRoundedRectangle(r, 10.0f);

    bool haveParts = parts && !parts->empty();
    bool haveInput = input && !input->empty();
    if (!haveParts && !haveInput) {
        g.setColour(ui::textDim);
        g.setFont(juce::FontOptions(15.0f));
        g.drawFittedText(emptyText, getLocalBounds().reduced(40), juce::Justification::centred, 4);
        return;
    }

    int lo = 127, hi = 0, endT = 0;
    auto scan = [&](const std::vector<choir::Note>& ns) {
        for (auto& n : ns) lo = std::min(lo, n.pitch), hi = std::max(hi, n.pitch), endT = std::max(endT, n.end);
    };
    if (haveParts)
        for (auto& p : *parts) scan(p.notes);
    else
        scan(*input);
    lo -= 2, hi += 2;
    if (endT <= 0) return;

    auto area = r.reduced(12.0f, 10.0f);
    auto keys = area.removeFromLeft(keyboardWidth);
    area.removeFromLeft(4.0f);
    const float rowH = area.getHeight() / (float)(hi - lo + 1);
    auto xOf = [&](double t) { return area.getX() + (float)(t / endT) * area.getWidth(); };
    auto yOf = [&](int p) { return area.getBottom() - (float)(p - lo + 1) * rowH; };

    // what sounds at the playhead, for lighting the keys
    std::map<int, juce::Colour> sounding;
    if (playhead >= 0 && haveParts)
        for (auto& p : *parts)
            for (auto& n : p.notes)
                if (n.start <= playhead && playhead < strings::soundingEnd(n)) sounding[n.pitch] = ui::partColour(p.name);

    // keyboard: one row per semitone, black keys shorter and on top
    auto isBlack = [](int p) { int c = ((p % 12) + 12) % 12; return c == 1 || c == 3 || c == 6 || c == 8 || c == 10; };
    {
        juce::Graphics::ScopedSaveState ss(g);
        g.reduceClipRegion(keys.toNearestInt());
        for (int p = lo; p <= hi; ++p) {
            if (isBlack(p)) continue;
            // a white key spans its own row plus half of each black neighbour
            float y0 = yOf(p) - (isBlack(p + 1) ? rowH * 0.5f : 0.0f);
            float y1 = yOf(p) + rowH + (isBlack(p - 1) ? rowH * 0.5f : 0.0f);
            juce::Rectangle<float> k(keys.getX(), y0, keys.getWidth(), y1 - y0);
            auto it = sounding.find(p);
            g.setColour(it != sounding.end() ? it->second : ui::whiteKey);
            g.fillRect(k);
            g.setColour(ui::line);
            g.drawHorizontalLine((int)y1, k.getX(), k.getRight());
            if (p % 12 == 0 && rowH * 1.5f >= 8.0f) {
                g.setColour(it != sounding.end() ? juce::Colours::white : ui::textDim);
                g.setFont(juce::FontOptions(juce::jlimit(8.0f, 10.5f, rowH * 1.3f)));
                g.drawText("C" + juce::String(p / 12 - 1), k.withTrimmedRight(3), juce::Justification::centredRight);
            }
        }
        for (int p = lo; p <= hi; ++p) {
            if (!isBlack(p)) continue;
            juce::Rectangle<float> k(keys.getX(), yOf(p) + rowH * 0.08f, keys.getWidth() * 0.6f, rowH * 0.84f);
            auto it = sounding.find(p);
            g.setColour(it != sounding.end() ? it->second : ui::blackKey);
            g.fillRoundedRectangle(k, 1.5f);
        }
    }

    // black-key rows shaded, octave lines on C
    for (int p = lo; p <= hi; ++p) {
        if (isBlack(p)) {
            g.setColour(juce::Colours::black.withAlpha(0.045f));
            g.fillRect(juce::Rectangle<float>(area.getX(), yOf(p), area.getWidth(), rowH));
        }
        if (p % 12 == 0) {
            g.setColour(ui::line);
            g.drawHorizontalLine((int)(yOf(p) + rowH), area.getX(), area.getRight());
        }
    }
    // bars
    int barT = meta.num * choir::TPB * 4 / std::max(1, meta.den);
    for (int t = 0, b = 1; t <= endT; t += barT, ++b) {
        g.setColour(ui::line);
        g.drawVerticalLine((int)xOf(t), area.getY(), area.getBottom());
        g.setColour(ui::textDim);
        if (t < endT) g.drawText(juce::String(b), juce::Rectangle<float>(xOf(t) + 3, area.getY(), 24, 12), juce::Justification::topLeft);
    }
    // the user's chords, faint, behind the parts
    if (haveInput) {
        g.setColour(juce::Colours::black.withAlpha(0.07f));
        for (auto& n : *input)
            if (n.pitch >= lo && n.pitch <= hi)
                g.fillRect(juce::Rectangle<float>(xOf(n.start), yOf(n.pitch), xOf(n.end) - xOf(n.start), rowH));
    }
    if (haveParts) {
        for (auto& p : *parts) {
            auto c = ui::partColour(p.name);
            for (auto& n : p.notes) {
                // a note is as long as it sounds: short bowings and pizzicato end early
                int end = strings::soundingEnd(n);
                juce::Rectangle<float> nr(xOf(n.start) + 0.5f, yOf(n.pitch) + 0.5f,
                                          std::max(2.0f, xOf(end) - xOf(n.start) - 1.0f), std::max(2.0f, rowH - 1.0f));
                g.setColour(c.withAlpha(n.art == strings::A_TREM ? 0.55f : 0.95f));
                g.fillRoundedRectangle(nr, std::min(2.5f, rowH * 0.4f));
                if (n.art == strings::A_TREM) {   // tremolo: a hatched bar
                    g.setColour(c);
                    for (float x = nr.getX() + 2.0f; x < nr.getRight() - 1.0f; x += 4.0f)
                        g.drawLine(x, nr.getY() + 1.0f, x, nr.getBottom() - 1.0f, 1.2f);
                }
            }
        }
    }
    if (playhead >= 0 && playhead <= endT) {
        g.setColour(ui::accent);
        g.drawLine(xOf(playhead), area.getY(), xOf(playhead), area.getBottom(), 1.5f);
    }
    if (haveParts && makeDragFile && hover) {
        // the whole roll can be dragged out to a track
        juce::Rectangle<float> h(area.getRight() - 178, area.getBottom() - 28, 174, 22);
        g.setColour(ui::text.withAlpha(0.85f));
        g.fillRoundedRectangle(h, 11.0f);
        g.setColour(ui::bg);
        g.setFont(juce::FontOptions(11.5f));
        g.drawText(U("тяните ноты на дорожку"), h, juce::Justification::centred);
    }
}
