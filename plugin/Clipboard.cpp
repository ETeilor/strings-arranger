// Clipboard reader for platforms other than macOS: a MIDI file path in the clipboard text.
#include <juce_gui_basics/juce_gui_basics.h>

bool stringsReadMidiFromClipboard(juce::MemoryBlock& midi, juce::String& report) {
    auto text = juce::SystemClipboard::getTextFromClipboard().trim();
    report = "clipboard text: " + text.substring(0, 200) + "\n";
    juce::File f(text.unquoted());
    if (text.isNotEmpty() && juce::File::isAbsolutePath(text.unquoted()) && f.existsAsFile() && f.loadFileAsData(midi) &&
        midi.getSize() > 14 && std::memcmp(midi.getData(), "MThd", 4) == 0)
        return true;
    midi.reset();
    return false;
}
