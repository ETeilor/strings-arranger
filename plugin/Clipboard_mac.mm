// Clipboard reader for macOS. A region copied in a DAW may arrive as a MIDI file reference, as raw
// Standard MIDI File data under some pasteboard type, or as a path in text. Everything found is listed
// in the report, so an unknown format can be added later.
#include <juce_gui_basics/juce_gui_basics.h>
#import <AppKit/AppKit.h>

static bool stringsTakeMidi(NSData* d, juce::MemoryBlock& midi) {
    if (d == nil || [d length] < 14) return false;
    const char* b = (const char*)[d bytes];
    const NSUInteger n = std::min((NSUInteger)4096, [d length] - 4);
    for (NSUInteger i = 0; i < n; ++i)
        if (std::memcmp(b + i, "MThd", 4) == 0) {
            midi.replaceAll(b + i, (size_t)([d length] - i));
            return true;
        }
    return false;
}

bool stringsReadMidiFromClipboard(juce::MemoryBlock& midi, juce::String& report) {
    @autoreleasepool {
        NSPasteboard* pb = [NSPasteboard generalPasteboard];
        report = "Strings Arranger paste report\npasteboard types:\n";
        bool found = false;
        for (NSPasteboardItem* item in [pb pasteboardItems]) {
            for (NSString* t in [item types]) {
                NSData* d = [item dataForType:t];
                juce::String head;
                if (d != nil) {
                    const unsigned char* b = (const unsigned char*)[d bytes];
                    for (NSUInteger i = 0; i < std::min((NSUInteger)24, [d length]); ++i)
                        head << (b[i] >= 32 && b[i] < 127 ? juce::String::charToString((juce::juce_wchar)b[i]) : juce::String("."));
                }
                report << "  " << juce::String::fromUTF8([t UTF8String]) << "  "
                       << (d != nil ? juce::String((int)[d length]) : juce::String("-")) << " bytes  '" << head << "'\n";
                if (!found && stringsTakeMidi(d, midi)) found = true;
            }
        }
        if (found) return true;
        // file references
        NSArray* urls = [pb readObjectsForClasses:@[[NSURL class]] options:nil];
        for (NSURL* url in urls) {
            if (![url isFileURL]) continue;
            NSString* path = [[url filePathURL] path];
            if (path == nil) path = [url path];
            report << "  file: " << juce::String::fromUTF8([path UTF8String]) << "\n";
            NSData* d = [NSData dataWithContentsOfFile:path];
            if (stringsTakeMidi(d, midi)) return true;
        }
        NSString* s = [pb stringForType:NSPasteboardTypeString];
        if (s != nil) {
            NSString* p = [s stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]];
            if ([p hasPrefix:@"/"]) {
                NSData* d = [NSData dataWithContentsOfFile:p];
                if (stringsTakeMidi(d, midi)) return true;
            }
        }
        return false;
    }
}
