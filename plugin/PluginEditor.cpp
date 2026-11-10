#include "PluginEditor.h"

namespace {
constexpr int kTop = 56, kSide = 268;
}

// ---------------------------------------------------------------- drag chip
void DragChip::paint(juce::Graphics& g) {
    auto r = getLocalBounds().toFloat().reduced(0.5f);
    g.setColour(hover ? ui::lift(ui::panelHi, 0.07f) : ui::panelHi);
    g.fillRoundedRectangle(r, r.getHeight() / 2.0f);
    g.setColour(colour);
    g.fillEllipse(r.getX() + 11.0f, r.getCentreY() - 3.5f, 7.0f, 7.0f);
    g.setColour(ui::text);
    g.setFont(ui::font(12.5f));
    auto text = label;
    int room = getWidth() - 26 - (lockable ? 24 : 10);
    if (shortLabel.isNotEmpty() && juce::Font(ui::font(12.5f)).getStringWidthFloat(label) > (float)room) text = shortLabel;
    g.drawText(text, getLocalBounds().withTrimmedLeft(26).withTrimmedRight(lockable ? 24 : 8), juce::Justification::centredLeft);
    if (lockable) {
        auto z = lockZone().toFloat();
        auto b = juce::Rectangle<float>(z.getCentreX() - 5.0f, z.getCentreY() - 1.0f, 9.0f, 7.0f);
        g.setColour(locked ? ui::accent : ui::textDim.withAlpha(hover ? 0.9f : 0.35f));
        g.fillRoundedRectangle(b, 1.5f);
        juce::Path shackle;
        shackle.addCentredArc(b.getCentreX(), b.getY(), 3.0f, 3.6f, 0.0f, -juce::MathConstants<float>::halfPi,
                              juce::MathConstants<float>::halfPi, true);
        g.strokePath(shackle, juce::PathStrokeType(1.4f));
    }
    if (locked) {
        g.setColour(ui::accent.withAlpha(0.8f));
        g.drawRoundedRectangle(r, r.getHeight() / 2.0f, 1.0f);
    }
}

void DragChip::mouseDrag(const juce::MouseEvent& e) {
    if (dragging || e.getDistanceFromDragStart() < 4 || !makeFile) return;
    dragging = true;
    auto f = makeFile();
    if (f.existsAsFile())
        juce::DragAndDropContainer::performExternalDragDropOfFiles({f.getFullPathName()}, false, this);
}

void DragChip::mouseUp(const juce::MouseEvent& e) {
    // only the padlock locks: a sloppy drag over the name must not lock a part by accident
    if (!dragging && lockable && onLock && e.getDistanceFromDragStart() < 4 && lockZone().contains(e.getPosition())) onLock();
    dragging = false;
}

// ---------------------------------------------------------------- saved variants
juce::Rectangle<float> SlotBar::pill(int i) const {
    return {(float)i * 30.0f, (float)getHeight() / 2.0f - 12.0f, 24.0f, 24.0f};
}

int SlotBar::indexAt(juce::Point<int> p) const {
    for (int i = 0; i < count; ++i)
        if (pill(i).contains(p.toFloat())) return i;
    return -1;
}

void SlotBar::paint(juce::Graphics& g) {
    if (count == 0) {
        g.setColour(ui::textDim);
        g.setFont(ui::font(12.0f));
        g.drawText(U("нажмите +, чтобы запомнить вариант"), getLocalBounds(), juce::Justification::centredLeft);
        return;
    }
    for (int i = 0; i < count; ++i) {
        auto r = pill(i);
        bool on = i == active;
        g.setColour(on ? ui::accent : (i == hoverIndex ? ui::lift(ui::panelHi, 0.1f) : ui::panelHi));
        g.fillEllipse(r);
        g.setColour(on ? ui::onAccent : ui::text);
        g.setFont(ui::font(12.0f, on));
        g.drawText(juce::String(i + 1), r, juce::Justification::centred);
    }
}

void SlotBar::mouseUp(const juce::MouseEvent& e) {
    int i = indexAt(e.getPosition());
    if (i < 0) return;
    if (e.mods.isPopupMenu() || e.mods.isAltDown()) {
        if (onRemove) onRemove(i);
    } else if (onPick) {
        onPick(i);
    }
}

// ---------------------------------------------------------------- editor
StringsEditor::StringsEditor(StringsProcessor& p) : AudioProcessorEditor(p), proc(p) {
    setLookAndFeel(&look);
    juce::LookAndFeel::setDefaultLookAndFeel(&look);
    setWantsKeyboardFocus(true);

    titleLbl.setText("Strings Arranger", juce::dontSendNotification);
    titleLbl.setFont(ui::font(16.0f, true));
    addAndMakeVisible(titleLbl);
    sourceLbl.setFont(ui::font(12.5f));
    sourceLbl.setColour(juce::Label::textColourId, ui::textDim);
    sourceLbl.setJustificationType(juce::Justification::centredLeft);
    sourceLbl.setMinimumHorizontalScale(1.0f);
    addAndMakeVisible(sourceLbl);

    auto icon = [](juce::Button& b, const char* name, bool ghost) {
        b.getProperties().set("icon", name);
        if (ghost) b.getProperties().set("ghost", true);
    };
    for (auto* b : {&captureBtn, &openBtn, &pasteBtn, &refreshBtn, &playBtn, &exportBtn, &scoreBtn, &prevVarBtn,
                    &nextVarBtn, &saveVarBtn})
        addAndMakeVisible(b);
    icon(captureBtn, "rec", true);
    icon(openBtn, "open", true);
    icon(pasteBtn, "paste", true);
    icon(refreshBtn, "refresh", true);
    icon(playBtn, "play", false);
    icon(exportBtn, "export", false);
    icon(scoreBtn, "export", false);
    icon(prevVarBtn, "prev", false);
    icon(nextVarBtn, "next", false);
    icon(saveVarBtn, "plus", false);
    captureBtn.getProperties().set("danger", true);
    captureBtn.onClick = [this] { proc.setCapture(!proc.isCapturing()); timerCallback(); };
    captureBtn.setTooltip(U("Положите регион с аккордами на эту дорожку, нажмите «Захват» и запустите воспроизведение"));
    playBtn.setTooltip(U("Прослушать результат встроенным звуком; короткие штрихи звучат коротко"));
    openBtn.onClick = [this] { openFile(); };
    openBtn.setTooltip(U("Открыть MIDI-файл"));
    pasteBtn.onClick = [this] { paste(); };
    pasteBtn.setTooltip(U("Вставить MIDI из буфера обмена (Cmd+V): скопируйте регион в Logic и вставьте сюда"));
    refreshBtn.onClick = [this] { proc.refreshAll(); };
    refreshBtn.setTooltip(U("Пересчитать всё заново по входу: снять закрепления, вернуться к основному варианту"));
    playBtn.onClick = [this] { proc.setPreview(!proc.isPreviewing()); timerCallback(); };
    exportBtn.onClick = [this] { exportAll(); };
    exportBtn.setTooltip(U("Сохранить все партии одним MIDI-файлом, с оптимизацией под выбранную библиотеку"));
    scoreBtn.onClick = [this] { exportScore(); };
    scoreBtn.setTooltip(U("Партитура MusicXML для MuseScore, Sibelius, Dorico, Finale"));
    prevVarBtn.onClick = [this] { proc.stepVariant(-1); };
    nextVarBtn.onClick = [this] { proc.stepVariant(+1); };
    nextVarBtn.setTooltip(U("Другой вариант: те же настройки, другое голосоведение"));
    prevVarBtn.setTooltip(U("Предыдущий вариант"));
    saveVarBtn.onClick = [this] { proc.saveSlot(); refresh(); };
    saveVarBtn.setTooltip(U("Запомнить этот вариант в стиле"));

    StringsLook::sectionLabel(inputLbl, U("Вход"));
    StringsLook::sectionLabel(styleLbl, U("Стиль"));
    StringsLook::sectionLabel(sectionLbl, U("Состав секции"));
    StringsLook::sectionLabel(libraryLbl, U("Библиотека"));
    StringsLook::sectionLabel(complexityLbl, U("Сложность"));
    StringsLook::sectionLabel(voicesLbl, U("По партиям"));
    StringsLook::sectionLabel(variantsLbl, U("Варианты"));
    StringsLook::sectionLabel(soundLbl, U("Звук прослушивания"));
    for (auto* l : {&inputLbl, &styleLbl, &sectionLbl, &libraryLbl, &complexityLbl, &voicesLbl, &variantsLbl, &soundLbl})
        addAndMakeVisible(l);
    sectionVal.setFont(ui::font(12.0f));
    sectionVal.setColour(juce::Label::textColourId, ui::textDim);
    sectionVal.setJustificationType(juce::Justification::centredRight);
    sectionVal.setBorderSize(juce::BorderSize<int>(0));
    addAndMakeVisible(sectionVal);

    for (auto* c : {&styleBox, &sectionBox, &libraryBox, &soundBox, &inputBox}) addAndMakeVisible(c);
    styleBox.addItemList(StringsProcessor::styleNames(), 1);
    sectionBox.addItemList(names::sections(), 1);
    sectionBox.setTooltip(U("Сколько пультов в группах. «По стилю» берёт состав, обычный для композитора. В квартете нет контрабасов и divisi"));
    libraryBox.addItemList(names::libraries(), 1);
    libraryBox.setTooltip(U("Для чего пишется MIDI. Spitfire: штрихи переключаются по UACC (CC32). «Дорожка на штрих»: каждый штрих отдельной дорожкой для любой библиотеки"));
    soundBox.addItemList({U("Пианино"), U("Струнные")}, 1);
    inputBox.addItemList({U("Авто"), U("Аккорды"), U("Гармонизовать мелодию")}, 1);
    inputBox.setTooltip(U("Авто: если на входе одна мелодия без аккордов, плагин подберёт аккорды сам"));
    styleA = std::make_unique<CA>(proc.params, "style", styleBox);
    sectionA = std::make_unique<CA>(proc.params, "section", sectionBox);
    libraryA = std::make_unique<CA>(proc.params, "library", libraryBox);
    soundA = std::make_unique<CA>(proc.params, "sound", soundBox);
    inputA = std::make_unique<CA>(proc.params, "input", inputBox);
    addAndMakeVisible(dynamicsBtn);
    dynamicsA = std::make_unique<BA>(proc.params, "dynamics", dynamicsBtn);
    dynamicsBtn.setTooltip(U("Для длинных нот пишется кривая колеса модуляции: дуга на каждую фразу в четыре такта"));

    auto valueLabel = [this](juce::Label& l) {
        l.setFont(ui::font(13.0f, true));
        l.setJustificationType(juce::Justification::centredRight);
        addAndMakeVisible(l);
    };
    complexity.setSliderStyle(juce::Slider::LinearHorizontal);
    complexity.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    addAndMakeVisible(complexity);
    complexityA = std::make_unique<SA>(proc.params, "complexity", complexity);
    valueLabel(complexityVal);
    for (int i = 0; i < strings::NPARTS; ++i) {
        voice[i].setSliderStyle(juce::Slider::LinearHorizontal);
        voice[i].setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        voice[i].getProperties().set("colour", (juce::int64)ui::partColour(strings::PART_NAMES[i]).getARGB());
        voice[i].setTooltip(U("Сложность партии: от неё зависит фактура. Крайнее левое положение (=) означает «как общая»"));
        addAndMakeVisible(voice[i]);
        voiceA[i] = std::make_unique<SA>(proc.params, juce::String("voice") + strings::PART_SHORT[i], voice[i]);
        voiceLbl[i].setText(strings::PART_SHORT[i], juce::dontSendNotification);
        voiceLbl[i].setFont(ui::font(12.5f, true));
        voiceLbl[i].setColour(juce::Label::textColourId, ui::partColour(strings::PART_NAMES[i]));
        voiceLbl[i].setBorderSize(juce::BorderSize<int>(0));
        addAndMakeVisible(voiceLbl[i]);
        valueLabel(voiceVal[i]);
        voiceVal[i].setColour(juce::Label::textColourId, ui::textDim);
    }
    styleBox.onChange = [this] { updateStyleControls(); };
    sectionBox.onChange = [this] { updateStyleControls(); };
    libraryBox.onChange = [this] { updateStyleControls(); };

    variantLbl.setFont(ui::font(13.0f));
    variantLbl.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(variantLbl);
    slots.onPick = [this](int i) { proc.loadSlot(i); };
    slots.onRemove = [this](int i) { proc.removeSlot(i); refresh(); };
    slots.setTooltip(U("Запомненные варианты этого стиля. Щелчок возвращает вариант, правый щелчок удаляет"));
    addAndMakeVisible(slots);

    roll.setEmptyText(U("Перетащите сюда MIDI-файл с аккордами или мелодией,\nвставьте скопированный регион (Cmd+V)\nили нажмите «Захват» и запустите воспроизведение"));
    roll.makeDragFile = [this] { return proc.writePartFile(-1); };
    addAndMakeVisible(roll);
    allChip = std::make_unique<DragChip>();
    allChip->label = U("Все партии");
    allChip->colour = ui::accent;
    allChip->makeFile = [this] { return proc.writePartFile(-1); };
    allChip->setTooltip(U("Тяните на дорожки: все партии одним файлом"));
    addAndMakeVisible(*allChip);
    scoreChip = std::make_unique<DragChip>();
    scoreChip->label = U("Ноты");
    scoreChip->colour = ui::textDim;
    scoreChip->makeFile = [this] { return proc.writeScoreFile(); };
    scoreChip->setTooltip(U("Тяните в Finder или в нотный редактор: партитура MusicXML"));
    addAndMakeVisible(*scoreChip);

    report.setFont(ui::font(12.0f));
    report.setJustificationType(juce::Justification::topLeft);
    report.setColour(juce::Label::textColourId, ui::textDim);
    report.setMinimumHorizontalScale(1.0f);
    addAndMakeVisible(report);

    proc.onResultChanged = [this] { refresh(); };
    setResizable(true, true);
    setResizeLimits(960, 700, 1900, 1300);
    setSize(1120, 720);
    updateStyleControls();
    refresh();
    startTimerHz(30);
}

StringsEditor::~StringsEditor() {
    proc.onResultChanged = nullptr;
    juce::LookAndFeel::setDefaultLookAndFeel(nullptr);
    setLookAndFeel(nullptr);
}

void StringsEditor::updateStyleControls() {
    auto comp = (strings::Composer)juce::jmax(0, styleBox.getSelectedItemIndex());
    auto sec = (strings::Section)juce::jmax(0, sectionBox.getSelectedItemIndex());
    sectionVal.setText(strings::sectionDesks(sec, comp), juce::dontSendNotification);
    // phrase dynamics are written for a library only
    dynamicsBtn.setVisible(libraryBox.getSelectedItemIndex() > 0);
    // a quartet has no basses
    bool basses = juce::String(strings::sectionDesks(sec, comp)) != "1-1-1-1";
    voice[strings::P_CB].setVisible(basses);
    voiceLbl[strings::P_CB].setVisible(basses);
    voiceVal[strings::P_CB].setVisible(basses);
    resized();
}

void StringsEditor::refresh() {
    const auto& r = proc.result();
    roll.setData(&r.parts, &proc.input(), proc.meta());
    chips.clear();
    for (size_t i = 0; i < r.parts.size(); ++i) {
        auto* c = chips.add(new DragChip());
        c->label = juce::String::fromUTF8(r.parts[i].name.c_str());
        // "Violin II" -> "V2", "Contrabass" -> "Cb"
        for (int k = 0; k < strings::NPARTS; ++k)
            if (r.parts[i].name == strings::PART_NAMES[k]) c->shortLabel = strings::PART_SHORT[k];
        c->colour = ui::partColour(r.parts[i].name);
        int idx = (int)i;
        c->makeFile = [this, idx] { return proc.writePartFile(idx); };
        c->lockable = true;
        c->locked = proc.isLocked(c->label);
        auto nm = c->label;
        c->onLock = [this, nm] { proc.toggleLock(nm); };
        c->setTooltip(U("Тяните на дорожку. Замок справа закрепляет партию: при смене сложности и вариантов она не меняется"));
        addAndMakeVisible(c);
    }
    allChip->setVisible(!r.parts.empty());
    scoreChip->setVisible(!r.parts.empty());
    report.setText(proc.input().empty() ? juce::String() : juce::String::fromUTF8(r.report().c_str()),
                   juce::dontSendNotification);
    sourceLbl.setText(proc.input().empty() ? U("нет входа") : proc.inputSource(), juce::dontSendNotification);
    bool have = !r.parts.empty();
    for (auto* b : {&exportBtn, &scoreBtn, &playBtn, &nextVarBtn, &saveVarBtn}) b->setEnabled(have);
    int var = (int)*proc.params.getRawParameterValue("variant");
    variantLbl.setText(var == 0 ? U("Основной") : U("Вариант ") + juce::String(var), juce::dontSendNotification);
    prevVarBtn.setEnabled(var > 0);
    slots.count = proc.numSlots();
    slots.active = proc.currentSlot();
    saveVarBtn.setEnabled(have && slots.active < 0);
    slots.repaint();
    resized();
    repaint();
}

void StringsEditor::timerCallback() {
    bool cap = proc.isCapturing();
    captureBtn.setToggleState(cap, juce::dontSendNotification);
    captureBtn.setButtonText(cap ? (proc.captureHasData() ? U("Запись") : U("Жду play")) : U("Захват"));
    bool pv = proc.isPreviewing();
    playBtn.setToggleState(pv, juce::dontSendNotification);
    playBtn.setButtonText(pv ? U("Стоп") : U("Слушать"));
    playBtn.getProperties().set("icon", pv ? "stop" : "play");
    roll.setPlayhead(pv ? proc.previewPositionTicks() : -1.0);
    complexityVal.setText(juce::String((int)complexity.getValue()), juce::dontSendNotification);
    for (int i = 0; i < strings::NPARTS; ++i)
        voiceVal[i].setText(voice[i].getValue() < 0 ? juce::String("=") : juce::String((int)voice[i].getValue()),
                            juce::dontSendNotification);
}

void StringsEditor::paint(juce::Graphics& g) {
    g.fillAll(ui::bg);
    g.setColour(ui::line);
    g.drawHorizontalLine(kTop - 1, 0.0f, (float)getWidth());
    g.drawVerticalLine(kSide, (float)kTop, (float)getHeight());
    if (dropHover) {
        g.setColour(ui::accent);
        g.drawRoundedRectangle(roll.getBounds().toFloat().expanded(2), 11.0f, 1.5f);
    }
}

void StringsEditor::resized() {
    auto b = getLocalBounds();
    auto top = b.removeFromTop(kTop).reduced(18, 12);
    titleLbl.setBounds(top.removeFromLeft(140));
    scoreBtn.setBounds(top.removeFromRight(84));
    top.removeFromRight(6);
    exportBtn.setBounds(top.removeFromRight(84));
    top.removeFromRight(6);
    playBtn.setBounds(top.removeFromRight(108));
    top.removeFromRight(14);
    refreshBtn.setBounds(top.removeFromRight(106));
    pasteBtn.setBounds(top.removeFromRight(100));
    openBtn.setBounds(top.removeFromRight(96));
    captureBtn.setBounds(top.removeFromRight(100));
    sourceLbl.setBounds(top.withTrimmedLeft(8).withTrimmedRight(8));

    auto side = b.removeFromLeft(kSide).reduced(20, 16);
    auto row = [&](juce::Component& c, int h, int gap) {
        c.setBounds(side.removeFromTop(h));
        side.removeFromTop(gap);
    };
    row(inputLbl, 14, 5);
    row(inputBox, 30, 14);
    row(styleLbl, 14, 5);
    row(styleBox, 30, 14);
    {
        auto sr = side.removeFromTop(14);
        sectionVal.setBounds(sr.removeFromRight(110));
        sectionLbl.setBounds(sr);
        side.removeFromTop(5);
    }
    row(sectionBox, 30, 14);
    row(libraryLbl, 14, 5);
    row(libraryBox, 30, dynamicsBtn.isVisible() ? 8 : 14);
    if (dynamicsBtn.isVisible()) row(dynamicsBtn, 22, 14);

    // bottom up: sound, then variants
    auto snd = side.removeFromBottom(49);
    soundLbl.setBounds(snd.removeFromTop(14));
    snd.removeFromTop(5);
    soundBox.setBounds(snd);
    side.removeFromBottom(16);
    auto slotRow = side.removeFromBottom(28);
    saveVarBtn.setBounds(slotRow.removeFromRight(28));
    slotRow.removeFromRight(6);
    slots.setBounds(slotRow);
    side.removeFromBottom(8);
    auto varRow = side.removeFromBottom(30);
    prevVarBtn.setBounds(varRow.removeFromLeft(30));
    nextVarBtn.setBounds(varRow.removeFromRight(30));
    variantLbl.setBounds(varRow);
    side.removeFromBottom(5);
    variantsLbl.setBounds(side.removeFromBottom(14));

    auto crow = side.removeFromTop(14);
    complexityVal.setBounds(crow.removeFromRight(30));
    complexityLbl.setBounds(crow);
    side.removeFromTop(4);
    complexity.setBounds(side.removeFromTop(22));
    side.removeFromTop(12);
    if (voicesLbl.isVisible()) {
        voicesLbl.setBounds(side.removeFromTop(14));
        side.removeFromTop(4);
        for (int i = 0; i < strings::NPARTS; ++i) {
            if (!voice[i].isVisible()) continue;
            auto vr = side.removeFromTop(22);
            voiceLbl[i].setBounds(vr.removeFromLeft(24));
            voiceVal[i].setBounds(vr.removeFromRight(24));
            voice[i].setBounds(vr.reduced(4, 0));
        }
    }

    auto main = b.reduced(18, 0).withTrimmedTop(16).withTrimmedBottom(14);
    auto rep = main.removeFromBottom(proc.result().issues.size() > 1 ? 150 : 122);
    report.setBounds(rep.withTrimmedTop(10));
    auto chipRow = main.removeFromBottom(42).withTrimmedTop(12);
    if (scoreChip->isVisible()) {
        scoreChip->setBounds(chipRow.removeFromRight(74));
        chipRow.removeFromRight(8);
    }
    if (allChip->isVisible()) {
        allChip->setBounds(chipRow.removeFromLeft(112));
        chipRow.removeFromLeft(8);
    }
    int cw = chips.isEmpty() ? 104 : juce::jlimit(62, 124, (chipRow.getWidth() - 6 * chips.size()) / chips.size());
    for (auto* c : chips) {
        c->setBounds(chipRow.removeFromLeft(cw));
        chipRow.removeFromLeft(6);
    }
    roll.setBounds(main);
}

bool StringsEditor::keyPressed(const juce::KeyPress& k) {
    if (k == juce::KeyPress('v', juce::ModifierKeys::commandModifier, 0)) {
        paste();
        return true;
    }
    return false;   // everything else goes on to the host (space bar, transport keys)
}

void StringsEditor::paste() {
    juce::String msg;
    if (!proc.pasteFromClipboard(msg)) showError(msg);
}

// Any file is welcome: regions dragged from a DAW may arrive with an unusual name, or as a file
// promise that only becomes a real file on drop. The content (MThd header) decides.
bool StringsEditor::isInterestedInFileDrag(const juce::StringArray& files) { return !files.isEmpty(); }

void StringsEditor::showError(const juce::String& err) {
    juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Strings Arranger", err);
}

void StringsEditor::filesDropped(const juce::StringArray& files, int, int) {
    dropHover = false;
    repaint();
    for (auto& path : files) {
        juce::File f(path);
        if (f.getFileName() == "choir-drag-report.txt") {
            // nothing usable came with the drag: show what did, and keep a copy for sending to the developer
            auto copy = juce::File::getSpecialLocation(juce::File::userDesktopDirectory).getChildFile("Strings Arranger drag report.txt");
            f.copyFileTo(copy);
            showError(U("Не получилось взять MIDI из перетащенного. Пока пользуйтесь «Захватом» или "
                        "File > Export > Selection as MIDI File.\n\nОтчёт сохранён на рабочий стол: "
                        "«Strings Arranger drag report.txt».\n\n") +
                      f.loadFileAsString().substring(0, 900));
            return;
        }
        if (f.isDirectory()) {
            for (auto& c : f.findChildFiles(juce::File::findFiles, true))
                if (StringsProcessor::isMidiFile(c)) { f = c; break; }
        }
        if (!f.existsAsFile() || !StringsProcessor::isMidiFile(f)) continue;
        juce::String err;
        if (!proc.loadMidiFile(f, err)) showError(err);
        return;
    }
    showError(U("Не нашёл MIDI в том, что перетащили. Если это регион из Logic, "
                "сделайте File > Export > Selection as MIDI File или включите «Захват»."));
}

void StringsEditor::openFile() {
    chooser = std::make_unique<juce::FileChooser>(U("Файл с аккордами или мелодией"), juce::File(), "*.mid;*.midi;*.smf;*");
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                         [this](const juce::FileChooser& fc) {
                             auto f = fc.getResult();
                             if (!f.existsAsFile()) return;
                             juce::String err;
                             if (!proc.loadMidiFile(f, err)) showError(err);
                         });
}

void StringsEditor::exportScore() {
    auto tmp = proc.writeScoreFile();
    chooser = std::make_unique<juce::FileChooser>(U("Сохранить ноты"),
                                                  juce::File::getSpecialLocation(juce::File::userDesktopDirectory)
                                                      .getChildFile("Strings score.musicxml"),
                                                  "*.musicxml");
    chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                         [tmp](const juce::FileChooser& fc) {
                             auto f = fc.getResult();
                             if (f != juce::File()) tmp.copyFileTo(f.withFileExtension("musicxml"));
                         });
}

void StringsEditor::exportAll() {
    auto tmp = proc.writePartFile(-1);
    chooser = std::make_unique<juce::FileChooser>(U("Сохранить партии"),
                                                  juce::File::getSpecialLocation(juce::File::userDesktopDirectory)
                                                      .getChildFile("Strings parts.mid"),
                                                  "*.mid");
    chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                         [tmp](const juce::FileChooser& fc) {
                             auto f = fc.getResult();
                             if (f != juce::File()) tmp.copyFileTo(f.withFileExtension("mid"));
                         });
}
