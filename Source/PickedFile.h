// Files chosen with a juce::FileChooser, on every platform. On iOS the chooser gives a link to a file in another
// place (iCloud Drive, another app, On My iPhone), which can only be read through its URL, and only while that
// URL's security-scoped access lasts. So the file is copied into the app's own storage, where it stays readable
// (an impulse response is reopened from its path on the next start).

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// The chosen file, ready to read: the file itself on the desktop, or a copy in `folder` on iOS (replacing one of
// the same name). An empty File if nothing was chosen or the copy failed.
inline juce::File readableChosenFile (const juce::FileChooser& chooser, const juce::File& folder)
{
   #if JUCE_IOS
    auto url = chooser.getURLResult();
    if (url.isEmpty())
        return {};
    auto in = url.createInputStream (juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress));
    if (in == nullptr || ! folder.createDirectory())
        return {};
    const auto copy = folder.getChildFile (juce::File::createLegalFileName (juce::URL::removeEscapeChars (url.getFileName())));
    copy.deleteFile();
    juce::FileOutputStream out (copy);
    if (! out.openedOk() || out.writeFromInputStream (*in, -1) < 0)
        return {};
    out.flush();
    return out.getStatus().wasOk() ? copy : juce::File();
   #else
    juce::ignoreUnused (folder);
    return chooser.getResult();
   #endif
}

// The user-visible name of the chosen file, without its extension (on iOS, from the URL).
inline juce::String chosenFileName (const juce::FileChooser& chooser)
{
   #if JUCE_IOS
    return juce::File::createLegalFileName (juce::URL::removeEscapeChars (chooser.getURLResult().getFileName()))
               .upToLastOccurrenceOf (".", false, false);
   #else
    return chooser.getResult().getFileNameWithoutExtension();
   #endif
}
