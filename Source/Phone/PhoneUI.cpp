#include "PhoneUI.h"

#include "../AboutPanel.h"
#include "../OverlayPanel.h"
#include "../Recorder.h"
#include "DesktopView.h"

using namespace phone;

namespace
{
// Lines of text, each with its own size and colour, wrapped to the width.
class InfoBlock : public juce::Component
{
public:
    struct Line
    {
        juce::String text;
        float size = sizes::body;
        juce::Colour colour = colours::text;
        bool bold = false;
    };

    InfoBlock() { setInterceptsMouseClicks (false, false); }

    void setLines (std::vector<Line> newLines)
    {
        const bool same = newLines.size() == lines.size()
                          && std::equal (newLines.begin(), newLines.end(), lines.begin(), [] (const Line& a, const Line& b)
                                         { return a.text == b.text && juce::exactlyEqual (a.size, b.size) && a.colour == b.colour; });
        if (same)
            return;
        lines = std::move (newLines);
        repaint();
        if (onHeightChange != nullptr)
            onHeightChange();
    }

    std::function<void()> onHeightChange;

    int preferredHeight (int width) const
    {
        int h = 0;
        for (const auto& line : lines)
            h += textHeight (line.text, font (line.size, line.bold), width) + 4;
        return h;
    }

    void paint (juce::Graphics& g) override
    {
        int y = 0;
        for (const auto& line : lines)
        {
            const auto f = font (line.size, line.bold);
            const int h = textHeight (line.text, f, getWidth());
            drawWrapped (g, line.text, f, line.colour, { 0, y, getWidth(), h + 2 });
            y += h + 4;
        }
    }

private:
    std::vector<Line> lines;
};

class Heading : public juce::Component
{
public:
    explicit Heading (juce::String t, float s = sizes::caption) : text (std::move (t)), size (s) { setInterceptsMouseClicks (false, false); }

    void paint (juce::Graphics& g) override
    {
        const bool big = size > sizes::body;
        g.setColour (big ? colours::text : colours::secondary);
        g.setFont (font (size, true));
        g.drawText (big ? text : text.toUpperCase(), getLocalBounds(), juce::Justification::bottomLeft);
    }

private:
    juce::String text;
    float size;
};

// The WARBL connection in a few words, and a colour for its dot.
struct ConnectionState
{
    juce::String text;
    juce::Colour colour;
    bool streaming = false;
};

ConnectionState connectionState (StreamReceiver& receiver)
{
    const auto stats = receiver.getStats();
    const bool flowing = stats.framesPerSecond > 50.0;
    if (receiver.isSimulating())
        return { "Simulated WARBL", colours::accent, true };
    if (receiver.isConnected() && flowing)
        return { juce::String ("WARBL") + (stats.bluetooth ? " over Bluetooth" : " over USB"), colours::good, true };
    if (receiver.isConnected())
        return { "WARBL connected, not streaming", colours::warning, false };
    return { "No WARBL connected", colours::secondary, false };
}

// The non-heading ids of a ComboBox, in order (for stepping through presets).
std::vector<int> idsOf (juce::ComboBox& box)
{
    std::vector<int> ids;
    for (const auto& item : itemsOf (box))
        if (! item.heading)
            ids.push_back (item.id);
    return ids;
}

// A preset list as the desktop shows it, with "Built-in" over the app's own when the player has presets too.
std::vector<ChoiceSheet::Item> presetItems (juce::ComboBox& box)
{
    auto items = itemsOf (box);
    if (std::any_of (items.begin(), items.end(), [] (const ChoiceSheet::Item& i) { return i.heading; }))
        items.insert (items.begin(), { "Built-in", 0, true });
    return items;
}
} // namespace

//==============================================================================
class PhoneUI::TabBar : public juce::Component
{
public:
    std::function<void (int)> onSelect;
    int selected = 0;
    int bottomInset = 0;

    void paint (juce::Graphics& g) override
    {
        g.fillAll (colours::card);
        g.setColour (colours::separator);
        g.fillRect (0, 0, getWidth(), 1);
        static const char* names[] = { "Play", "Instrument", "Room", "Setup", "Details" };
        const float slot = (float) getWidth() / (float) numTabs;
        for (int i = 0; i < numTabs; ++i)
        {
            const auto colour = i == selected ? colours::accent : colours::secondary;
            const auto area = juce::Rectangle<float> (slot * (float) i, 0.0f, slot, (float) (getHeight() - bottomInset));
            drawIcon (g, i, area.withHeight (32.0f).withY (6.0f).withSizeKeepingCentre (24.0f, 22.0f), colour);
            g.setColour (colour);
            g.setFont (font (11.0f, i == selected));
            g.drawText (names[i], area.withTrimmedTop (32.0f).withHeight (16.0f), juce::Justification::centred);
        }
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (! getLocalBounds().contains (e.getPosition()) || onSelect == nullptr)
            return;
        onSelect (juce::jlimit (0, numTabs - 1, e.x * numTabs / std::max (1, getWidth())));
    }

private:
    static void drawIcon (juce::Graphics& g, int tab, juce::Rectangle<float> r, juce::Colour colour)
    {
        g.setColour (colour);
        const juce::PathStrokeType stroke (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
        juce::Path p;
        switch (tab)
        {
            case play:  // A whistle with three holes
            {
                const auto body = r.withSizeKeepingCentre (r.getWidth(), 8.0f);
                p.addRoundedRectangle (body, 3.0f);
                g.strokePath (p, stroke);
                for (int i = 0; i < 3; ++i)
                    g.fillEllipse (juce::Rectangle<float> (3.4f, 3.4f).withCentre ({ body.getX() + body.getWidth() * (0.45f + 0.17f * (float) i), body.getCentreY() }));
                break;
            }
            case room:  // Sound spreading into a room
            {
                const auto c = juce::Point<float> (r.getX() + 4.0f, r.getCentreY());
                g.fillEllipse (juce::Rectangle<float> (5.0f, 5.0f).withCentre (c));
                for (int i = 1; i <= 3; ++i)
                {
                    const float radius = 3.6f * (float) i + 1.0f;  // The largest stays within the icon's 22 points
                    p.addCentredArc (c.x, c.y, radius, radius, 0.0f, 0.55f, juce::MathConstants<float>::pi - 0.55f, true);
                }
                g.strokePath (p, stroke);
                break;
            }
            case setup:  // A cog
            {
                const auto c = r.getCentre();
                for (int i = 0; i < 8; ++i)
                {
                    const float angle = juce::MathConstants<float>::twoPi * (float) i / 8.0f;
                    p.startNewSubPath (c.x + 7.0f * std::sin (angle), c.y - 7.0f * std::cos (angle));
                    p.lineTo (c.x + 10.5f * std::sin (angle), c.y - 10.5f * std::cos (angle));
                }
                g.strokePath (p, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::butt));
                g.drawEllipse (juce::Rectangle<float> (14.0f, 14.0f).withCentre (c), 1.8f);
                g.fillEllipse (juce::Rectangle<float> (4.0f, 4.0f).withCentre (c));
                break;
            }
            case instrument:  // Two sliders
            {
                for (int i = 0; i < 2; ++i)
                {
                    const float y = r.getY() + r.getHeight() * (0.3f + 0.4f * (float) i);
                    p.startNewSubPath (r.getX(), y);
                    p.lineTo (r.getRight(), y);
                }
                g.strokePath (p, stroke);
                g.fillEllipse (juce::Rectangle<float> (7.0f, 7.0f).withCentre ({ r.getX() + r.getWidth() * 0.65f, r.getY() + r.getHeight() * 0.3f }));
                g.fillEllipse (juce::Rectangle<float> (7.0f, 7.0f).withCentre ({ r.getX() + r.getWidth() * 0.3f, r.getY() + r.getHeight() * 0.7f }));
                break;
            }
            default:  // Details: a list
            {
                for (int i = 0; i < 3; ++i)
                {
                    const float y = r.getY() + r.getHeight() * (0.2f + 0.3f * (float) i);
                    p.startNewSubPath (r.getX() + 6.0f, y);
                    p.lineTo (r.getRight(), y);
                    g.fillEllipse (juce::Rectangle<float> (3.0f, 3.0f).withCentre ({ r.getX() + 1.5f, y }));
                }
                g.strokePath (p, stroke);
                break;
            }
        }
    }
};

//==============================================================================
// A screen of stacked items that scrolls when they don't fit.
class ScrollingContent : public juce::Component
{
public:
    void add (juce::Component& c, std::function<int (int width)> height, int gapBefore = 0)
    {
        content.addAndMakeVisible (c);
        items.push_back ({ &c, std::move (height), gapBefore });
    }

    void attachTo (juce::Component& parent)
    {
        viewport.setViewedComponent (&content, false);
        viewport.setScrollBarsShown (false, false, true, false);
        viewport.setScrollOnDragMode (juce::Viewport::ScrollOnDragMode::all);
        parent.addAndMakeVisible (viewport);
    }

    void layout (juce::Rectangle<int> area)
    {
        viewport.setBounds (area);
        const int width = area.getWidth() - 2 * sizes::gutter;
        int y = 8;
        for (auto& item : items)
        {
            if (! item.component->isVisible())
                continue;
            y += item.gapBefore;
            const int h = item.height (width);
            item.component->setBounds (sizes::gutter, y, width, h);
            y += h + 8;
        }
        content.setSize (area.getWidth(), y + 24);
    }

private:
    struct Item
    {
        juce::Component* component;
        std::function<int (int)> height;
        int gapBefore;
    };
    juce::Component content;
    juce::Viewport viewport;
    std::vector<Item> items;
};

//==============================================================================
class PhoneUI::PlayScreen : public Screen
{
public:
    explicit PlayScreen (PhoneUI& ownerToUse) : owner (ownerToUse), c (ownerToUse.controls)
    {
        connection.onTap = [this] { owner.showTab (setup); };
        addAndMakeVisible (connection);

        presetCard.titleSize = 22.0f;
        presetCard.rightSpace = 100;
        presetCard.onTap = [this] { owner.showPresetList(); };
        addAndMakeVisible (presetCard);
        addAndMakeVisible (previousButton);
        addAndMakeVisible (nextButton);
        previousButton.onClick = [this] { stepPreset (-1); };
        nextButton.onClick = [this] { stepPreset (1); };

        addAndMakeVisible (fingers);
        addAndMakeVisible (breathMeter);
        addAndMakeVisible (soundMeter);

        roomCard.titleSize = 19.0f;
        roomCard.onTap = [this]
        {
            owner.present (std::make_unique<ChoiceSheet> ("Room", itemsOf (c.roomBox()), c.roomBox().getSelectedId(),
                                                          [this] (int id) { c.roomBox().setSelectedId (id, juce::sendNotificationSync); }));
        };
        addAndMakeVisible (roomCard);
        addAndMakeVisible (reverb);

        scaleButton.onClick = [this] { c.audio().playScale(); };
        recordButton.onClick = [this] { c.toggleRecording(); };
        addAndMakeVisible (scaleButton);
        addAndMakeVisible (recordButton);
        addAndMakeVisible (message);
    }

    void refresh() override
    {
        const auto state = connectionState (c.receiver());
        connection.set (state.text, state.colour);

        presetCard.setTitle (c.presetBox().getText());
        if (--geometryCountdown <= 0)
        {
            geometryCountdown = 15;  // Twice a second: it only changes with the instrument
            const auto geometry = c.geometry();
            for (size_t i = 0; i < (size_t) whistle::kNumHoles; ++i)
                fingers.enabled[i] = geometry.holes[i].enabled;
            fingers.bell = geometry.bellClosesFoot;
        }
        auto& audio = c.audio();
        // Nothing fingering it (no stream, no scale): the holes show open, not the engine's start-up state.
        const bool fingered = state.streaming || audio.isScaleActive();
        for (size_t i = 0; i < (size_t) whistle::kNumHoles; ++i)
            fingers.openness[i] = fingered ? audio.shownOpenness[i].load() : 1.0f;
        fingers.footOpenness = fingered ? audio.shownFootOpenness.load() : 1.0f;
        fingers.repaint();
        breathMeter.setLevel (std::sqrt (std::max (0.0f, audio.shownPressure.load()) / 3000.0f));
        soundMeter.setLevel ((juce::Decibels::gainToDecibels (audio.whistleModel.level.load(), -60.0f) + 60.0f) / 60.0f);

        roomCard.setTitle (c.roomName());
        reverb.refresh();

        const auto scaleStatus = audio.getScaleStatus();
        scaleButton.setButtonText (audio.isScaleActive() ? "Stop scale" : (scaleStatus.isNotEmpty() ? "Preparing..." : "Play scale"));
        if (c.isRecording())
        {
            recordButton.setButtonText ("Stop  " + juce::String (c.recordingSeconds(), 0) + " s");
            recordButton.setColour (juce::TextButton::buttonColourId, colours::record);
        }
        else
        {
            recordButton.setButtonText ("Record");
            recordButton.setColour (juce::TextButton::buttonColourId, colours::card);
        }
        const auto recorded = c.recordingMessage();
        message.setLines ({ { recorded.isNotEmpty() ? recorded : scaleStatus, sizes::caption, colours::secondary } });
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (sizes::gutter, 8);
        const bool wide = getWidth() > getHeight();
        auto controlsArea = wide ? area.removeFromLeft (area.getWidth() * 55 / 100) : area;
        if (wide)
            area.removeFromLeft (sizes::gutter);

        connection.setBounds (controlsArea.removeFromTop (32));
        controlsArea.removeFromTop (wide ? 6 : 10);
        presetCard.setBounds (controlsArea.removeFromTop (wide ? 80 : 92));
        auto arrows = presetCard.getBounds().removeFromRight (presetCard.rightSpace).reduced (4, 0);
        arrows = arrows.withSizeKeepingCentre (arrows.getWidth(), 46);
        previousButton.setBounds (arrows.removeFromLeft (arrows.getWidth() / 2));
        nextButton.setBounds (arrows);

        auto bottom = controlsArea.removeFromBottom (50);
        scaleButton.setBounds (bottom.removeFromLeft (bottom.getWidth() / 2).withTrimmedRight (6));
        recordButton.setBounds (bottom.withTrimmedLeft (6));
        // Sideways the left column is short: the message goes under the whistle instead.
        message.setBounds (wide ? area.removeFromBottom (32).withTrimmedTop (4) : controlsArea.removeFromBottom (36).withTrimmedTop (4));
        reverb.setBounds (controlsArea.removeFromBottom (62));
        controlsArea.removeFromBottom (6);
        roomCard.setBounds (controlsArea.removeFromBottom (66));
        controlsArea.removeFromBottom (10);

        auto live = wide ? area : controlsArea;
        auto meters = live.removeFromRight (90);
        soundMeter.setBounds (meters.removeFromRight (45));
        breathMeter.setBounds (meters);
        fingers.setBounds (live);
    }

private:
    // The connection, as a coloured dot and a few words; tapping it opens Setup.
    struct Pill : public juce::Component
    {
        std::function<void()> onTap;
        juce::String text;
        juce::Colour colour;

        void set (const juce::String& t, juce::Colour col)
        {
            if (t != text || col != colour)
            {
                text = t;
                colour = col;
                repaint();
            }
        }

        void paint (juce::Graphics& g) override
        {
            const auto f = font (sizes::caption + 1.0f, true);
            const float width = std::min ((float) getWidth(), juce::GlyphArrangement::getStringWidth (f, text) + 40.0f);
            const auto pill = getLocalBounds().toFloat().withWidth (width);
            g.setColour (colours::card);
            g.fillRoundedRectangle (pill, pill.getHeight() / 2.0f);
            g.setColour (colour);
            g.fillEllipse (juce::Rectangle<float> (9.0f, 9.0f).withCentre ({ pill.getX() + 16.0f, pill.getCentreY() }));
            g.setColour (colours::text);
            g.setFont (f);
            g.drawText (text, pill.withTrimmedLeft (28.0f), juce::Justification::centredLeft);
        }

        void mouseUp (const juce::MouseEvent& e) override
        {
            if (isTap (e, *this) && onTap != nullptr)
                onTap();
        }
    };

    void stepPreset (int direction)
    {
        auto& box = c.presetBox();
        const auto ids = idsOf (box);
        if (ids.empty())
            return;
        const auto at = std::find (ids.begin(), ids.end(), box.getSelectedId());
        int index = at == ids.end() ? (direction > 0 ? -1 : (int) ids.size()) : (int) (at - ids.begin());
        index = juce::jlimit (0, (int) ids.size() - 1, index + direction);
        box.setSelectedId (ids[(size_t) index], juce::sendNotificationSync);
    }

    PhoneUI& owner;
    DesktopControls& c;
    Pill connection;
    TapCard presetCard { "Instrument" };
    ArrowButton previousButton { false }, nextButton { true };
    FingerView fingers;
    Meter breathMeter { "Breath", colours::accent }, soundMeter { "Sound", colours::good };
    TapCard roomCard { "Room" };
    SettingSlider reverb { "Reverb", c.reverbLevel() };
    juce::TextButton scaleButton { "Play scale" }, recordButton { "Record" };
    InfoBlock message;
    int geometryCountdown = 0;
};

//==============================================================================
namespace
{
// A section heading that folds its items away: tap it to show or hide them.
class FoldingHeading : public juce::Component
{
public:
    FoldingHeading (juce::String titleToShow, juce::String subtitleToShow, bool startExpanded)
        : title (std::move (titleToShow)), subtitle (std::move (subtitleToShow)), expanded (startExpanded)
    {
        setRepaintsOnMouseActivity (true);
    }

    std::function<void()> onToggle;
    std::vector<juce::Component*> members;

    void apply()
    {
        for (auto* m : members)
            m->setVisible (expanded);
        repaint();
    }

    int preferredHeight (int width) const
    {
        return 48 + (expanded && subtitle.isNotEmpty() ? textHeight (subtitle, font (sizes::caption), width - 2 * sizes::gutter) + 10 : 0);
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (isMouseButtonDown() ? colours::cardPressed : colours::card);
        g.fillRoundedRectangle (getLocalBounds().toFloat(), (float) sizes::cardRadius);
        auto area = getLocalBounds().reduced (sizes::gutter, 0);
        auto top = area.removeFromTop (48);
        // A chevron pointing right when folded, down when open.
        const auto c = top.removeFromRight (16).toFloat().getCentre();
        juce::Path p;
        if (expanded)
        {
            p.startNewSubPath (c.x - 7.0f, c.y - 3.0f);
            p.lineTo (c.x, c.y + 4.0f);
            p.lineTo (c.x + 7.0f, c.y - 3.0f);
        }
        else
        {
            p.startNewSubPath (c.x - 3.0f, c.y - 7.0f);
            p.lineTo (c.x + 4.0f, c.y);
            p.lineTo (c.x - 3.0f, c.y + 7.0f);
        }
        g.setColour (colours::secondary);
        g.strokePath (p, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (colours::text);
        g.setFont (font (sizes::body, true));
        g.drawText (title, top, juce::Justification::centredLeft);
        if (expanded && subtitle.isNotEmpty())
            drawWrapped (g, subtitle, font (sizes::caption), colours::secondary, area.withTrimmedBottom (8));
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (! isTap (e, *this))
            return;
        expanded = ! expanded;
        apply();
        if (onToggle != nullptr)
            onToggle();
    }

private:
    juce::String title, subtitle;
    bool expanded;
};

// The tuning the geometry predicts, one fingering a line: the note, the fingering (filled, open and half circles;
// plugged holes left out) and how far the resonance is from the note.
class TuningList : public juce::Component
{
public:
    void set (const std::vector<TuningTable::Entry>& newEntries)
    {
        const bool same = newEntries.size() == entries.size()
                          && std::equal (newEntries.begin(), newEntries.end(), entries.begin(), [] (const auto& a, const auto& b)
                                         { return a.note == b.note && a.pattern == b.pattern && std::lround (a.cents) == std::lround (b.cents); });
        if (same)
            return;
        const bool resize = newEntries.size() != entries.size();
        entries = newEntries;
        repaint();
        if (resize && onHeightChange != nullptr)
            onHeightChange();
    }

    std::function<void()> onHeightChange;
    int preferredHeight() const { return 24 + 28 * (int) entries.size(); }

    void paint (juce::Graphics& g) override
    {
        auto area = getLocalBounds();
        auto header = area.removeFromTop (24);
        g.setColour (colours::secondary);
        g.setFont (font (sizes::caption));
        g.drawText ("Note", header.removeFromLeft (64), juce::Justification::centredLeft);
        g.drawText ("Off by", header.removeFromRight (70), juce::Justification::centredRight);
        g.drawText ("Fingering", header, juce::Justification::centredLeft);
        for (const auto& e : entries)
        {
            auto line = area.removeFromTop (28);
            juce::String pattern;
            for (auto ch : e.pattern)
            {
                if (ch == 'x')
                    pattern += juce::String (juce::CharPointer_UTF8 ("\xe2\x97\x8f"));
                else if (ch == 'o')
                    pattern += juce::String (juce::CharPointer_UTF8 ("\xe2\x97\x8b"));
                else if (ch == 'h' || ch == 'p')
                    pattern += juce::String (juce::CharPointer_UTF8 ("\xe2\x97\x90"));
                else if (ch == '|')
                    pattern += "  ";
            }
            const int cents = (int) std::lround (e.cents);
            g.setFont (font (sizes::body));
            g.setColour (colours::text);
            g.drawText (e.note, line.removeFromLeft (64), juce::Justification::centredLeft);
            g.setColour (std::abs (cents) > 10 ? colours::finger : colours::text);
            g.drawText ((cents > 0 ? "+" : "") + juce::String (cents) + " c", line.removeFromRight (70), juce::Justification::centredRight);
            g.setColour (colours::text);
            g.drawText (pattern, line, juce::Justification::centredLeft);
        }
    }

private:
    std::vector<TuningTable::Entry> entries;
};

// Buttons in a grid of equal cells.
class ButtonGrid : public juce::Component
{
public:
    explicit ButtonGrid (int columnsToUse) : columns (columnsToUse) {}

    int preferredHeight() const
    {
        const int rows = (getNumChildComponents() + columns - 1) / columns;
        return rows * 46 + (rows - 1) * 8;
    }

    void resized() override
    {
        const int width = (getWidth() - (columns - 1) * 8) / columns;
        for (int i = 0; i < getNumChildComponents(); ++i)
            getChildComponent (i)->setBounds ((i % columns) * (width + 8), (i / columns) * 54, width, 46);
    }

private:
    int columns;
};

// "Tone holes (unticked = plugged): ..." -> "Tone holes"; the part in brackets, if any, as a subtitle.
std::pair<juce::String, juce::String> splitHeading (const juce::String& heading)
{
    const auto title = heading.upToFirstOccurrenceOf (" (", false, false);
    auto rest = heading.fromFirstOccurrenceOf (" (", false, false).upToLastOccurrenceOf (")", false, false);
    return { title, rest.isNotEmpty() ? rest.substring (0, 1).toUpperCase() + rest.substring (1) + "." : juce::String() };
}
} // namespace

//==============================================================================
class PhoneUI::InstrumentScreen : public Screen
{
public:
    explicit InstrumentScreen (PhoneUI& ownerToUse) : owner (ownerToUse), c (ownerToUse.controls)
    {
        // The instrument and its presets
        presetCard.titleSize = 20.0f;
        presetCard.onTap = [this] { owner.showPresetList(); };
        saveButton.onClick = [this]
        {
            owner.present (std::make_unique<NameSheet> ("Save preset", "A name for this instrument. A preset of yours with the same name is replaced.",
                                                        c.instrumentName(), [this] (const juce::String& name) { c.savePreset (name); }));
        };
        deleteButton.onClick = [this] { c.deletePreset(); };
        exportButton.onClick = [this] { c.exportInstrument(); };
        importButton.onClick = [this] { c.importInstrument(); };
        for (auto* b : { &saveButton, &deleteButton, &exportButton, &importButton })
            fileButtons.addAndMakeVisible (b);
        content.add (title, [] (int) { return 36; });
        content.add (presetCard, [] (int) { return 66; }, 4);
        content.add (fileButtons, [this] (int) { return fileButtons.preferredHeight(); });
        content.add (status, [this] (int w) { return status.preferredHeight (w); });
        status.onHeightChange = [this] { resized(); };

        // Key and tuning
        addSection ("Key and tuning", "What the tuning table, the scale and Tune holes aim for. The sound never uses a chart: it comes "
                                      "from the instrument's shape.", true);
        addPicker ("Key", c.keyBox());
        addPicker ("Octave", c.octaveBox());
        addPicker ("Fingering chart", c.chartBox());
        addSlider ("Pitch standard (A4)", c.pitchStandard(), c.pitchStandard().getTooltip());
        tuneButton.onClick = [this] { c.tuneHoles(); };
        addMember (tuneButton, [] (int) { return 46; }, 4);
        addSwitch ("Tune C natural with L1's size", c.crossFingerings(), c.crossFingerings().getTooltip());

        addSection ("Predicted tuning", "Each fingering's resonance against its note, from the instrument's shape: a design aid, more "
                                        "than 10 cents off is highlighted.", false);
        tuningList.onHeightChange = [this] { resized(); };
        addMember (tuningList, [this] (int) { return tuningList.preferredHeight(); });

        // The dimension and voicing settings, in the desktop's groups.
        for (const auto& group : c.instrumentSettings())
        {
            auto [heading, subtitle] = splitHeading (group.heading);
            const bool holes = std::any_of (group.settings.begin(), group.settings.end(), [] (const auto& s) { return s.hole >= 0; });
            if (holes)
                subtitle = "Switch a hole off to plug it (its sensor is then ignored). Position is from the window's edge to the hole's "
                           "centre. Nearer the window or larger: the notes it vents are higher. Tune holes sets these.";
            addSection (heading, subtitle, false);
            if (heading.startsWith ("Bore"))
                addPicker ("Material (wall thickness and roughness)", c.materialBox());
            for (const auto& setting : group.settings)
            {
                if (setting.hole >= 0 && ! setting.diameter)
                {
                    static const char* fingers[] = { "left thumb, at the back", "left index finger", "left middle finger", "left ring finger",
                                                     "right index finger", "right middle finger", "right ring finger", "right little finger" };
                    addSwitch (setting.name + " (" + fingers[juce::jlimit (0, whistle::kNumHoles - 1, setting.hole)] + ")",
                               c.holeUsed (setting.hole), {}, 10);
                }
                if (setting.hole >= 0)
                    addSlider (setting.diameter ? "Diameter" : "Position", *setting.slider, {});
                else
                    addSlider (setting.name, *setting.slider, setting.slider->getTooltip());
            }
            if (holes)
                addSwitch ("Bell closes the foot", c.bell(), c.bell().getTooltip(), 10);
        }

        for (auto& section : sections)
            section->apply();
        content.attachTo (*this);
    }

    void refresh() override
    {
        presetCard.setTitle (c.presetBox().getText());
        deleteButton.setEnabled (c.canDeletePreset());
        status.setLines ({ { c.instrumentStatus(), sizes::caption, colours::secondary } });
        for (auto& [card, box] : pickers)
            card->setTitle (box->getText());
        for (auto& s : sliders)
            if (s->isShowing())
                s->refresh();
        for (auto& s : switches)
            if (s->isShowing())
                s->refresh();
        if (tuningList.isShowing())
            tuningList.set (c.tuningTable());
    }

    void resized() override { content.layout (getLocalBounds()); }

private:
    void addSection (const juce::String& heading, const juce::String& subtitle, bool expanded)
    {
        auto section = std::make_unique<FoldingHeading> (heading, subtitle, expanded);
        auto* s = section.get();
        s->onToggle = [this] { resized(); };
        content.add (*s, [s] (int w) { return s->preferredHeight (w); }, 10);
        sections.push_back (std::move (section));
    }

    void addMember (juce::Component& component, std::function<int (int)> height, int gap = 0)
    {
        content.add (component, std::move (height), gap);
        sections.back()->members.push_back (&component);
    }

    void addPicker (const juce::String& caption, juce::ComboBox& box)
    {
        auto card = std::make_unique<TapCard> (caption);
        card->titleSize = sizes::body;
        card->onTap = [this, caption, &box]
        {
            owner.present (std::make_unique<ChoiceSheet> (caption, itemsOf (box), box.getSelectedId(),
                                                          [&box] (int id) { box.setSelectedId (id, juce::sendNotificationSync); }));
        };
        addMember (*card, [] (int) { return 60; });
        pickers.push_back ({ std::move (card), &box });
    }

    void addSlider (const juce::String& name, juce::Slider& slider, const juce::String& description)
    {
        auto s = std::make_unique<SettingSlider> (name, slider, description);
        auto* p = s.get();
        addMember (*p, [p] (int w) { return p->preferredHeight (w); }, 4);
        sliders.push_back (std::move (s));
    }

    void addSwitch (const juce::String& name, juce::ToggleButton& button, const juce::String& description, int gap = 4)
    {
        auto s = std::make_unique<SettingSwitch> (name, button, description);
        auto* p = s.get();
        addMember (*p, [p] (int w) { return p->preferredHeight (w); }, gap);
        switches.push_back (std::move (s));
    }

    PhoneUI& owner;
    DesktopControls& c;
    ScrollingContent content;
    Heading title { "Instrument", sizes::title };
    TapCard presetCard { "Preset" };
    ButtonGrid fileButtons { 2 };
    juce::TextButton saveButton { "Save as preset..." }, deleteButton { "Delete preset" }, exportButton { "Export file..." },
        importButton { "Import file..." }, tuneButton { "Tune holes" };
    InfoBlock status;
    TuningList tuningList;
    std::vector<std::unique_ptr<FoldingHeading>> sections;
    std::vector<std::pair<std::unique_ptr<TapCard>, juce::ComboBox*>> pickers;
    std::vector<std::unique_ptr<SettingSlider>> sliders;
    std::vector<std::unique_ptr<SettingSwitch>> switches;
};

//==============================================================================
class PhoneUI::RoomScreen : public Screen
{
public:
    explicit RoomScreen (PhoneUI& ownerToUse) : owner (ownerToUse), c (ownerToUse.controls)
    {
        intro.setLines ({ { "The room you play in: one of the modelled rooms, or a measured one (an impulse response, a WAV recording of "
                            "a room).",
                            sizes::caption, colours::secondary } });
        roomCard.titleSize = 20.0f;
        roomCard.onTap = [this]
        {
            owner.present (std::make_unique<ChoiceSheet> ("Room", itemsOf (c.roomBox()), c.roomBox().getSelectedId(),
                                                          [this] (int id) { c.roomBox().setSelectedId (id, juce::sendNotificationSync); }));
        };
        irButton.onClick = [this] { c.loadImpulseResponse(); };

        content.add (title, [] (int) { return 36; });
        content.add (intro, [this] (int w) { return intro.preferredHeight (w); });
        content.add (roomCard, [] (int) { return 66; }, 4);
        content.add (irButton, [] (int) { return 46; });
        for (auto* s : { &level, &preDelay, &spread })
            content.add (*s, [s] (int w) { return s->preferredHeight (w); }, 10);
        content.attachTo (*this);
    }

    void refresh() override
    {
        roomCard.setTitle (c.roomName());
        for (auto* s : { &level, &preDelay, &spread })
            s->refresh();
    }

    void resized() override { content.layout (getLocalBounds()); }

private:
    PhoneUI& owner;
    DesktopControls& c;
    ScrollingContent content;
    Heading title { "Room", sizes::title };
    InfoBlock intro;
    TapCard roomCard { "Room" };
    juce::TextButton irButton { "Load impulse response..." };
    SettingSlider level { "Reverb level", c.reverbLevel(), c.reverbLevel().getTooltip() };
    SettingSlider preDelay { "Pre-delay", c.preDelay(), c.preDelay().getTooltip() };
    SettingSlider spread { "Spread", c.spread(), c.spread().getTooltip() };
};

//==============================================================================
class PhoneUI::SetupScreen : public Screen
{
public:
    explicit SetupScreen (PhoneUI& ownerToUse) : owner (ownerToUse), c (ownerToUse.controls)
    {
        pairButton.onClick = [] { DesktopControls::pairBluetooth(); };
        infoButton.onClick = [this] { c.requestInfo(); };
        aboutButton.onClick = [this] { owner.showAbout(); };
        recordings.setLines ({ { "Recordings (the sound as WAV, and every sensor frame as CSV) are saved in " + Recorder::getFolderDescription() + ".",
                                 sizes::caption, colours::secondary } });
        for (auto* block : { &warbl, &output })
            block->onHeightChange = [this] { resized(); };

        content.add (title, [] (int) { return 36; });
        content.add (warblHeading, [] (int) { return 28; }, 4);
        content.add (warbl, [this] (int w) { return warbl.preferredHeight (w); });
        if (DesktopControls::canPairBluetooth())
            content.add (pairButton, [] (int) { return 46; });
        content.add (infoButton, [] (int) { return 46; });
        content.add (simulate, [this] (int w) { return simulate.preferredHeight (w); }, 4);
        content.add (smooth, [this] (int w) { return smooth.preferredHeight (w); });
        content.add (outputHeading, [] (int) { return 28; }, 10);
        content.add (output, [this] (int w) { return output.preferredHeight (w); });
        content.add (bufferRow, [] (int) { return 40; });
        content.add (bufferNote, [this] (int w) { return bufferNote.preferredHeight (w); });
        content.add (recordingsHeading, [] (int) { return 28; }, 10);
        content.add (recordings, [this] (int w) { return recordings.preferredHeight (w); });
        content.add (aboutButton, [] (int) { return 46; }, 10);
        content.attachTo (*this);
        bufferNote.setLines ({ { "A smaller buffer answers sooner. If the sound crackles, choose a larger one.", sizes::caption, colours::secondary } });
    }

    void refresh() override
    {
        const auto state = connectionState (c.receiver());
        const auto stats = c.receiver().getStats();
        std::vector<InfoBlock::Line> lines { { state.text, sizes::body, state.colour, true } };
        if (state.streaming)
            lines.push_back ({ juce::String (juce::roundToInt (stats.framesPerSecond)) + " frames a second, " + juce::String ((juce::int64) stats.lostFrames) + " lost",
                               sizes::caption, colours::secondary });
        else
            lines.push_back ({ DesktopControls::canPairBluetooth() ? "Plug WARBL into the iPhone's USB port, or pair it over Bluetooth (turn on WARBL's Bluetooth first)."
                                                                   : "Connect WARBL with a USB cable.",
                               sizes::caption, colours::secondary });
        if (c.deviceInfo().isNotEmpty() && ! c.receiver().isSimulating())
            lines.push_back ({ c.deviceInfo(), sizes::caption, colours::secondary });
        warbl.setLines (lines);
        infoButton.setEnabled (c.receiver().isConnected() && ! c.receiver().isSimulating());
        simulate.refresh();
        smooth.refresh();
        refreshOutput();
    }

    void resized() override { content.layout (getLocalBounds()); }

private:
    void refreshOutput()
    {
        auto& deviceManager = c.audio().deviceManager;
        auto* device = deviceManager.getCurrentAudioDevice();
        if (device == nullptr)
        {
            output.setLines ({ { "No sound output", sizes::body, colours::warning, true } });
            setBufferSizes ({}, 0);
            return;
        }
        const double rate = device->getCurrentSampleRate();
        const int buffer = device->getCurrentBufferSizeSamples();
        const double latencyMs = (buffer + device->getOutputLatencyInSamples()) * 1000.0 / rate;
        std::vector<InfoBlock::Line> lines {
            { juce::String (latencyMs, 1) + " ms from breath to sound", sizes::body, latencyMs > 40.0 ? colours::warning : colours::text, true },
            { "Buffer " + juce::String (buffer) + " samples at " + juce::String (rate / 1000.0, 1) + " kHz, plus the output's own delay",
              sizes::caption, colours::secondary }
        };
        if (latencyMs > 40.0)
            lines.push_back ({ "The sound is probably going to Bluetooth headphones or a speaker, which delay it far too much to play. Use "
                               "the iPhone's speaker, or wired headphones (through a USB-C hub while WARBL uses the port).",
                               sizes::caption, colours::warning });
        output.setLines (lines);

        juce::Array<int> sizes;
        for (int size : device->getAvailableBufferSizes())
            if (size >= 32 && size <= 1024)
                sizes.add (size);
        setBufferSizes (sizes, buffer);
    }

    // One button per buffer size the device offers; the current one highlighted.
    void setBufferSizes (const juce::Array<int>& sizes, int current)
    {
        if (sizes != bufferSizes)
        {
            bufferSizes = sizes;
            bufferButtons.clear();
            for (int size : sizes)
            {
                auto button = std::make_unique<juce::TextButton> (juce::String (size));
                button->onClick = [this, size]
                {
                    auto& deviceManager = c.audio().deviceManager;
                    auto setup = deviceManager.getAudioDeviceSetup();
                    setup.bufferSize = size;
                    deviceManager.setAudioDeviceSetup (setup, true);
                };
                bufferRow.addAndMakeVisible (*button);
                bufferButtons.push_back (std::move (button));
            }
            bufferRow.resized();
        }
        for (size_t i = 0; i < bufferButtons.size(); ++i)
            bufferButtons[i]->setToggleState (bufferSizes[(int) i] == current, juce::dontSendNotification);
    }

    struct ButtonRow : public juce::Component
    {
        void resized() override
        {
            const int n = getNumChildComponents();
            for (int i = 0; i < n; ++i)
                getChildComponent (i)->setBounds (getWidth() * i / n, 0, getWidth() / n, getHeight());
        }
    };

    PhoneUI& owner;
    DesktopControls& c;
    ScrollingContent content;
    Heading title { "Setup", sizes::title };
    Heading warblHeading { "WARBL" }, outputHeading { "Sound output" }, recordingsHeading { "Recordings" };
    InfoBlock warbl, output, bufferNote, recordings;
    juce::TextButton pairButton { "Pair over Bluetooth..." }, infoButton { "Read WARBL's calibration" }, aboutButton { "About WhistleSim" };
    SettingSwitch simulate { "Simulate WARBL", c.simulate(), "Plays a scripted scale as if from WARBL, through the same path as the real stream." };
    SettingSwitch smooth { "Smooth Bluetooth", c.smoothBluetooth(),
                           "Over Bluetooth, WARBL's readings arrive in bursts. On: they are played evenly, a few milliseconds later." };
    ButtonRow bufferRow;
    juce::Array<int> bufferSizes;
    std::vector<std::unique_ptr<juce::TextButton>> bufferButtons;
};

//==============================================================================
class PhoneUI::DetailsScreen : public Screen
{
public:
    explicit DetailsScreen (PhoneUI& owner) : view (owner.getMain(), DesktopView::Fit::width)
    {
        hint.setLines ({ { "The desktop pages: the stream, the instrument's dimensions and voicing, the sensor response and the model "
                           "inside. Turn the phone sideways for a larger view.",
                           sizes::caption, colours::secondary } });
        addAndMakeVisible (hint);
        addAndMakeVisible (view);
    }

    void resized() override
    {
        auto area = getLocalBounds();
        auto top = area.removeFromTop (hint.preferredHeight (getWidth() - 2 * sizes::gutter) + 12).reduced (sizes::gutter, 4);
        hint.setBounds (top);
        view.setBounds (area);
    }

private:
    InfoBlock hint;
    DesktopView view;
};

//==============================================================================
PhoneUI::PhoneUI (std::unique_ptr<MainComponent> mainToShow) : main (std::move (mainToShow))
{
    setLookAndFeel (&lookAndFeel);
    main->setLookAndFeel (&desktopLookAndFeel);

    screens[play] = std::make_unique<PlayScreen> (*this);
    screens[instrument] = std::make_unique<InstrumentScreen> (*this);
    screens[room] = std::make_unique<RoomScreen> (*this);
    screens[setup] = std::make_unique<SetupScreen> (*this);
    screens[details] = std::make_unique<DetailsScreen> (*this);
    for (auto& screen : screens)
        addChildComponent (*screen);

    tabBar = std::make_unique<TabBar>();
    tabBar->onSelect = [this] (int tab) { showTab (tab); };
    addAndMakeVisible (*tabBar);

    setOpaque (true);
    setSize (393, 852);  // An iPhone 16 in points (the Mac's --phone window)
    showTab (play);
    startTimerHz (30);
}

PhoneUI::~PhoneUI()
{
    stopTimer();
    overlay.reset();
    sheet.reset();
    for (auto& screen : screens)
        screen.reset();  // Before the desktop window they show and drive
    tabBar.reset();
    main->setLookAndFeel (nullptr);
    setLookAndFeel (nullptr);
}

void PhoneUI::showTab (int tab)
{
    currentTab = juce::jlimit (0, numTabs - 1, tab);
    for (int i = 0; i < numTabs; ++i)
        screens[(size_t) i]->setVisible (i == currentTab);
    tabBar->selected = currentTab;
    tabBar->repaint();
    screens[(size_t) currentTab]->refresh();
}

void PhoneUI::present (std::unique_ptr<Sheet> newSheet)
{
    newSheet->dismiss = [this]
    {
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<PhoneUI> (this)]
        {
            if (safe != nullptr)
                safe->sheet.reset();
        });
    };
    newSheet->insets = safeArea();
    addAndMakeVisible (*newSheet);
    newSheet->setBounds (getLocalBounds());
    sheet = std::move (newSheet);
}

void PhoneUI::showPresetList()
{
    auto& box = controls.presetBox();
    present (std::make_unique<ChoiceSheet> ("Instrument", presetItems (box), box.getSelectedId(),
                                            [&box] (int id) { box.setSelectedId (id, juce::sendNotificationSync); }));
}

void PhoneUI::showAbout()
{
    auto panel = std::make_unique<OverlayPanel> (std::make_unique<AboutPanel>(), "About WhistleSim");
    panel->onClose = [this] { overlay.reset(); };
    panel->showOver (*this, safeArea().getTop() + 12);
    overlay = std::move (panel);
}

juce::BorderSize<int> PhoneUI::safeArea() const
{
   #if JUCE_IOS
    if (const auto* display = juce::Desktop::getInstance().getDisplays().getDisplayForRect (getScreenBounds()))
        return display->safeAreaInsets;
   #endif
    return {};  // A Mac's --phone window: nothing in the way
}

void PhoneUI::paint (juce::Graphics& g)
{
    g.fillAll (colours::background);
}

void PhoneUI::resized()
{
    const auto safe = safeArea();
    auto area = getLocalBounds();
    tabBar->bottomInset = safe.getBottom();
    tabBar->setBounds (area.removeFromBottom (50 + safe.getBottom()).withTrimmedLeft (safe.getLeft()).withTrimmedRight (safe.getRight()));
    area = area.withTrimmedTop (safe.getTop()).withTrimmedLeft (safe.getLeft()).withTrimmedRight (safe.getRight());
    for (auto& screen : screens)
        screen->setBounds (area);
    if (sheet != nullptr)
    {
        sheet->insets = safe;
        sheet->setBounds (getLocalBounds());
    }
    if (overlay != nullptr)
        overlay->setBounds (getLocalBounds());
}

void PhoneUI::timerCallback()
{
    if (sheet == nullptr)
        screens[(size_t) currentTab]->refresh();
}
