// Building blocks of the iPhone screens: tappable cards, a full-screen list to choose from, the live whistle with
// its finger holes, level meters, and sliders and switches that follow a desktop control.

#pragma once

#include "PhoneStyle.h"
#include "../Whistle/WhistleGeometry.h"

namespace phone
{
inline int textHeight (const juce::String& text, const juce::Font& f, int width)
{
    if (text.isEmpty() || width <= 0)
        return 0;
    juce::AttributedString s;
    s.append (text, f);
    s.setWordWrap (juce::AttributedString::byWord);
    juce::TextLayout layout;
    layout.createLayout (s, (float) width);
    return (int) std::ceil (layout.getHeight());
}

inline void drawWrapped (juce::Graphics& g, const juce::String& text, const juce::Font& f, juce::Colour colour, juce::Rectangle<int> area)
{
    juce::AttributedString s;
    s.append (text, f, colour);
    s.setWordWrap (juce::AttributedString::byWord);
    juce::TextLayout layout;
    layout.createLayout (s, (float) area.getWidth());
    layout.draw (g, area.toFloat());
}

inline void drawChevron (juce::Graphics& g, juce::Rectangle<float> area, juce::Colour colour)
{
    const auto c = area.getCentre();
    juce::Path p;
    p.startNewSubPath (c.x - 3.0f, c.y - 7.0f);
    p.lineTo (c.x + 4.0f, c.y);
    p.lineTo (c.x - 3.0f, c.y + 7.0f);
    g.setColour (colour);
    g.strokePath (p, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

// A tap: a press and release on the same component without dragging (a drag scrolls the screen instead).
inline bool isTap (const juce::MouseEvent& e, const juce::Component& c)
{
    return ! e.mouseWasDraggedSinceMouseDown() && c.getLocalBounds().contains (e.getPosition());
}

// A round button with a chevron pointing left or right (previous / next).
class ArrowButton : public juce::Button
{
public:
    explicit ArrowButton (bool pointsRight) : juce::Button (pointsRight ? "Next" : "Previous"), right (pointsRight) {}

    void paintButton (juce::Graphics& g, bool highlighted, bool down) override
    {
        const auto circle = getLocalBounds().toFloat().withSizeKeepingCentre (40.0f, 40.0f);
        g.setColour (down ? colours::separator.brighter (0.2f) : (highlighted ? colours::separator.brighter (0.1f) : colours::separator));
        g.fillEllipse (circle);
        const auto c = circle.getCentre();
        const float d = right ? 1.0f : -1.0f;
        juce::Path p;
        p.startNewSubPath (c.x - 3.5f * d, c.y - 8.0f);
        p.lineTo (c.x + 4.5f * d, c.y);
        p.lineTo (c.x - 3.5f * d, c.y + 8.0f);
        g.setColour (isEnabled() ? colours::text : colours::secondary);
        g.strokePath (p, juce::PathStrokeType (2.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

private:
    bool right;
};

// A rounded card with a small caption, a large title and a chevron: tapping it opens something.
class TapCard : public juce::Component
{
public:
    explicit TapCard (juce::String captionToShow) : caption (std::move (captionToShow)) { setRepaintsOnMouseActivity (true); }

    std::function<void()> onTap;
    float titleSize = sizes::title;
    int rightSpace = 0;  // Kept free for buttons placed over the card's right end

    void setTitle (const juce::String& t)
    {
        if (t != title)
        {
            title = t;
            repaint();
        }
    }

    void paint (juce::Graphics& g) override
    {
        auto area = getLocalBounds().toFloat();
        g.setColour (isMouseButtonDown() ? colours::cardPressed : colours::card);
        g.fillRoundedRectangle (area, (float) sizes::cardRadius);
        auto inner = getLocalBounds().reduced (sizes::gutter, 10).withTrimmedRight (rightSpace);
        drawChevron (g, inner.removeFromRight (16).toFloat(), colours::secondary);
        inner.removeFromRight (8);
        g.setColour (colours::secondary);
        g.setFont (font (sizes::caption));
        g.drawText (caption, inner.removeFromTop (18), juce::Justification::centredLeft);
        g.setColour (colours::text);
        g.setFont (font (titleSize, true));
        g.drawFittedText (title, inner, juce::Justification::centredLeft, 2, 0.8f);
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (isTap (e, *this) && onTap != nullptr)
            onTap();
    }

private:
    juce::String caption, title;
};

// A screen that slides over the others (here: appears) and goes away by itself. The host sets `dismiss`, which
// removes and deletes it later, so calling it from the sheet's own handlers is safe.
class Sheet : public juce::Component
{
public:
    std::function<void()> dismiss;
    juce::BorderSize<int> insets;  // The screen's safe area
};

// A full-screen list to choose one item from (presets, rooms), with section headings and a tick at the current one.
class ChoiceSheet : public Sheet
{
public:
    struct Item
    {
        juce::String text;
        int id = 0;
        bool heading = false;
    };

    ChoiceSheet (juce::String titleToShow, const std::vector<Item>& items, int selectedId, std::function<void (int)> onChooseItem)
        : title (std::move (titleToShow)), onChoose (std::move (onChooseItem))
    {
        doneButton.onClick = [this] { if (dismiss) dismiss(); };
        addAndMakeVisible (doneButton);
        for (const auto& item : items)
        {
            auto row = std::make_unique<Row> (item, item.id == selectedId && ! item.heading);
            row->onTap = [this, id = item.id]
            {
                onChoose (id);
                if (dismiss)
                    dismiss();
            };
            list.addAndMakeVisible (*row);
            rows.push_back (std::move (row));
        }
        viewport.setViewedComponent (&list, false);
        viewport.setScrollBarsShown (false, false, true, false);
        viewport.setScrollOnDragMode (juce::Viewport::ScrollOnDragMode::all);
        addAndMakeVisible (viewport);
        setOpaque (true);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (colours::background);
        g.setColour (colours::text);
        g.setFont (font (sizes::body, true));
        g.drawText (title, header, juce::Justification::centred);
        g.setColour (colours::separator);
        g.fillRect (header.getX(), header.getBottom() - 1, header.getWidth(), 1);
    }

    void resized() override
    {
        auto area = insets.subtractedFrom (getLocalBounds());
        header = area.removeFromTop (52);
        doneButton.setBounds (header.withLeft (header.getRight() - 90).reduced (8, 8));
        viewport.setBounds (area);
        int y = 8;
        for (auto& row : rows)
        {
            const int h = row->item.heading ? 40 : sizes::rowHeight;
            row->setBounds (0, y, area.getWidth(), h);
            y += h;
        }
        list.setSize (area.getWidth(), y + 24);
        // Show the current item.
        for (auto& row : rows)
            if (row->selected && row->getBottom() > viewport.getHeight())
                viewport.setViewPosition (0, std::min (row->getY() - viewport.getHeight() / 2, list.getHeight() - viewport.getHeight()));
    }

private:
    struct Row : public juce::Component
    {
        Row (Item i, bool isSelected) : item (std::move (i)), selected (isSelected) { setRepaintsOnMouseActivity (true); }

        void paint (juce::Graphics& g) override
        {
            auto area = getLocalBounds().reduced (sizes::gutter, 0);
            if (item.heading)
            {
                g.setColour (colours::secondary);
                g.setFont (font (sizes::caption, true));
                g.drawText (item.text.toUpperCase(), area.removeFromBottom (24), juce::Justification::centredLeft);
                return;
            }
            if (isMouseButtonDown())
                g.fillAll (colours::cardPressed);
            if (selected)
            {
                auto tick = area.removeFromRight (24).toFloat().withSizeKeepingCentre (14.0f, 10.0f);
                juce::Path p;
                p.startNewSubPath (tick.getX(), tick.getCentreY());
                p.lineTo (tick.getX() + 5.0f, tick.getBottom());
                p.lineTo (tick.getRight(), tick.getY());
                g.setColour (colours::accent);
                g.strokePath (p, juce::PathStrokeType (2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }
            g.setColour (selected ? colours::accent : colours::text);
            g.setFont (font (sizes::body));
            g.drawFittedText (item.text, area, juce::Justification::centredLeft, 2, 0.85f);
            g.setColour (colours::separator);
            g.fillRect (sizes::gutter, getHeight() - 1, getWidth() - sizes::gutter, 1);
        }

        void mouseUp (const juce::MouseEvent& e) override
        {
            if (! item.heading && isTap (e, *this) && onTap != nullptr)
                onTap();
        }

        Item item;
        bool selected = false;
        std::function<void()> onTap;
    };

    juce::String title;
    std::function<void (int)> onChoose;
    juce::TextButton doneButton { "Done" };
    juce::Rectangle<int> header;
    juce::Component list;
    juce::Viewport viewport;
    std::vector<std::unique_ptr<Row>> rows;
};

// Asks for a name (saving a preset): a text field with the keyboard up, Save and Cancel.
class NameSheet : public Sheet
{
public:
    NameSheet (juce::String titleToShow, juce::String messageToShow, const juce::String& initialName, std::function<void (const juce::String&)> onSaveName)
        : title (std::move (titleToShow)), message (std::move (messageToShow)), onSave (std::move (onSaveName))
    {
        editor.setFont (font (20.0f));
        editor.setText (initialName, false);
        editor.setIndents (12, 12);
        editor.setColour (juce::TextEditor::backgroundColourId, colours::card);
        editor.setColour (juce::TextEditor::textColourId, colours::text);
        editor.setColour (juce::TextEditor::outlineColourId, colours::separator);
        editor.setColour (juce::TextEditor::focusedOutlineColourId, colours::accent);
        editor.setColour (juce::CaretComponent::caretColourId, colours::accent);
        editor.onReturnKey = [this] { save(); };
        addAndMakeVisible (editor);
        saveButton.setColour (juce::TextButton::buttonColourId, colours::accent);
        saveButton.onClick = [this] { save(); };
        cancelButton.onClick = [this] { if (dismiss) dismiss(); };
        addAndMakeVisible (saveButton);
        addAndMakeVisible (cancelButton);
        setOpaque (true);
        // The keyboard comes up once the field is on screen.
        juce::Timer::callAfterDelay (150, [safe = juce::Component::SafePointer<NameSheet> (this)]
        {
            if (safe != nullptr)
            {
                safe->editor.grabKeyboardFocus();
                safe->editor.selectAll();
            }
        });
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (colours::background);
        g.setColour (colours::text);
        g.setFont (font (sizes::body, true));
        g.drawText (title, header, juce::Justification::centred);
        drawWrapped (g, message, font (sizes::caption), colours::secondary, messageArea);
    }

    void resized() override
    {
        auto area = insets.subtractedFrom (getLocalBounds()).reduced (sizes::gutter, 0);
        header = area.removeFromTop (52);
        cancelButton.setBounds (header.withWidth (90).reduced (0, 8));
        saveButton.setBounds (header.withLeft (header.getRight() - 90).reduced (0, 8));
        area.removeFromTop (12);
        messageArea = area.removeFromTop (textHeight (message, font (sizes::caption), area.getWidth()) + 4);
        area.removeFromTop (8);
        editor.setBounds (area.removeFromTop (50));
    }

private:
    void save()
    {
        const auto name = editor.getText().trim();
        if (name.isEmpty())
            return;
        onSave (name);
        if (dismiss)
            dismiss();
    }

    juce::String title, message;
    std::function<void (const juce::String&)> onSave;
    juce::TextEditor editor;
    juce::TextButton saveButton { "Save" }, cancelButton { "Cancel" };
    juce::Rectangle<int> header, messageArea;
};

// A question with two answers (e.g. "Delete preset?"), as a card in the middle of the dimmed screen. The action
// button is red when `destructive`. Tapping outside the card cancels.
class ConfirmSheet : public Sheet
{
public:
    ConfirmSheet (juce::String titleToShow, juce::String messageToShow, const juce::String& actionName, bool destructive,
                  std::function<void()> onConfirmed)
        : title (std::move (titleToShow)), message (std::move (messageToShow)), onConfirm (std::move (onConfirmed))
    {
        actionButton.setButtonText (actionName);
        actionButton.setColour (juce::TextButton::buttonColourId, destructive ? colours::record : colours::accent);
        actionButton.onClick = [this]
        {
            onConfirm();
            if (dismiss)
                dismiss();
        };
        cancelButton.setColour (juce::TextButton::buttonColourId, colours::cardPressed);
        cancelButton.onClick = [this] { if (dismiss) dismiss(); };
        addAndMakeVisible (actionButton);
        addAndMakeVisible (cancelButton);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colours::black.withAlpha (0.6f));
        g.setColour (colours::card);
        g.fillRoundedRectangle (card.toFloat(), 18.0f);
        auto area = card.reduced (20, 18);
        g.setColour (colours::text);
        g.setFont (font (sizes::body + 1.0f, true));
        g.drawText (title, area.removeFromTop (26), juce::Justification::centredLeft);
        area.removeFromTop (6);
        drawWrapped (g, message, font (sizes::body - 2.0f), colours::secondary, area.withHeight (messageHeight + 4));
    }

    void resized() override
    {
        const auto area = insets.subtractedFrom (getLocalBounds());
        const int width = std::min (340, area.getWidth() - 2 * sizes::gutter);
        messageHeight = textHeight (message, font (sizes::body - 2.0f), width - 40);
        const int height = 18 + 26 + 6 + messageHeight + 18 + 46 + 18;
        card = juce::Rectangle<int> (width, height).withCentre (area.getCentre());
        auto buttons = card.reduced (20, 18).removeFromBottom (46);
        cancelButton.setBounds (buttons.removeFromLeft (buttons.getWidth() / 2).withTrimmedRight (6));
        actionButton.setBounds (buttons.withTrimmedLeft (6));
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (! card.contains (e.getPosition()) && dismiss)
            dismiss();
    }

private:
    juce::String title, message;
    std::function<void()> onConfirm;
    juce::TextButton actionButton, cancelButton { "Cancel" };
    juce::Rectangle<int> card;
    int messageHeight = 0;
};

// The items of a desktop ComboBox (with its section headings), for a ChoiceSheet.
inline std::vector<ChoiceSheet::Item> itemsOf (juce::ComboBox& box, const juce::String& firstHeading = {})
{
    std::vector<ChoiceSheet::Item> items;
    if (firstHeading.isNotEmpty())
        items.push_back ({ firstHeading, 0, true });
    if (const auto* menu = box.getRootMenu())
        for (juce::PopupMenu::MenuItemIterator it (*menu); it.next();)
        {
            const auto& item = it.getItem();
            if (item.isSectionHeader)
                items.push_back ({ item.text, 0, true });
            else if (! item.isSeparator && item.itemID != 0)
                items.push_back ({ item.text, item.itemID, false });
        }
    return items;
}

// The whistle seen from the front, upright, mouthpiece at the top: each finger hole fills as it is covered
// (from the sensors, as the model gets it), the thumb hole is drawn beside the body, and the foot shows the
// bell sensor when the instrument uses it. Plugged holes are small dots.
class FingerView : public juce::Component
{
public:
    std::array<float, whistle::kNumHoles> openness {};  // 1 = open
    std::array<bool, whistle::kNumHoles> enabled {};
    float footOpenness = 1.0f;
    bool bell = false;

    FingerView()
    {
        openness.fill (1.0f);
        enabled.fill (true);
        setInterceptsMouseClicks (false, false);
    }

    void paint (juce::Graphics& g) override
    {
        const auto area = getLocalBounds().toFloat().reduced (4.0f);
        const float height = area.getHeight();
        const float tubeWidth = juce::jlimit (30.0f, 62.0f, std::min (area.getWidth() * 0.3f, height * 0.11f));
        const float cx = area.getCentreX() + tubeWidth * 0.35f;  // Room for the thumb hole on the left
        const float top = area.getY(), bottom = area.getBottom();

        // Body and mouthpiece (a narrower beak with the window just below it).
        const float headLength = height * 0.17f;
        juce::Path body;
        body.addRoundedRectangle (cx - tubeWidth / 2.0f, top + headLength * 0.45f, tubeWidth, bottom - top - headLength * 0.45f, tubeWidth * 0.2f);
        body.addRoundedRectangle (cx - tubeWidth * 0.32f, top, tubeWidth * 0.64f, headLength * 0.6f, tubeWidth * 0.25f);
        g.setColour (colours::card.brighter (0.12f));
        g.fillPath (body);
        g.setColour (colours::separator.brighter (0.2f));
        g.strokePath (body, juce::PathStrokeType (1.0f));
        g.setColour (colours::background);
        g.fillRoundedRectangle (juce::Rectangle<float> (tubeWidth * 0.5f, headLength * 0.16f).withCentre ({ cx, top + headLength * 0.82f }), 2.0f);

        // Front holes L1-L3, a gap, R1-R4.
        const float radius = tubeWidth * 0.3f;
        const float first = top + headLength + height * 0.1f, last = bottom - height * 0.08f;
        const float step = (last - first) / 7.5f;
        for (int hole = 1; hole < whistle::kNumHoles; ++hole)
        {
            const float y = first + step * (float) (hole - 1) + (hole >= 4 ? step * 0.5f : 0.0f);
            drawHole (g, hole, { cx, y }, radius);
        }
        // Thumb (on the back), beside the body level with L1's upper edge.
        drawHole (g, 0, { cx - tubeWidth / 2.0f - radius * 1.6f, first - step * 0.35f }, radius * 0.85f);
        g.setColour (colours::secondary);
        g.setFont (font (11.0f));
        g.drawText ("thumb", juce::Rectangle<float> (60.0f, 14.0f).withCentre ({ cx - tubeWidth / 2.0f - radius * 1.6f, first - step * 0.35f + radius * 1.6f }),
                    juce::Justification::centred);

        if (bell)
        {
            const float covered = 1.0f - juce::jlimit (0.0f, 1.0f, footOpenness);
            const auto foot = juce::Rectangle<float> (tubeWidth * 0.9f, 6.0f).withCentre ({ cx, bottom - 3.0f });
            g.setColour (colours::separator.brighter (0.3f));
            g.drawRoundedRectangle (foot, 3.0f, 1.0f);
            if (covered > 0.02f)
            {
                g.setColour (colours::finger.withAlpha (0.25f + 0.75f * covered));
                g.fillRoundedRectangle (foot, 3.0f);
            }
        }
    }

private:
    void drawHole (juce::Graphics& g, int hole, juce::Point<float> centre, float radius)
    {
        const auto circle = juce::Rectangle<float> (2.0f * radius, 2.0f * radius).withCentre (centre);
        if (! enabled[(size_t) hole])
        {
            g.setColour (colours::separator.brighter (0.3f));
            g.fillEllipse (circle.withSizeKeepingCentre (5.0f, 5.0f));
            return;
        }
        g.setColour (colours::background);
        g.fillEllipse (circle);
        const float covered = 1.0f - juce::jlimit (0.0f, 1.0f, openness[(size_t) hole]);
        if (covered > 0.02f)
        {
            // The finger as a disc that grows and darkens as it covers the hole.
            g.setColour (colours::finger.withAlpha (0.35f + 0.65f * covered));
            g.fillEllipse (circle.withSizeKeepingCentre (2.0f * radius * (0.45f + 0.55f * covered), 2.0f * radius * (0.45f + 0.55f * covered)));
        }
        g.setColour (colours::secondary.withAlpha (0.6f));
        g.drawEllipse (circle, 1.2f);
    }
};

// A vertical level bar with its name underneath.
class Meter : public juce::Component
{
public:
    explicit Meter (juce::String nameToShow, juce::Colour colourToUse) : name (std::move (nameToShow)), colour (colourToUse)
    {
        setInterceptsMouseClicks (false, false);
    }

    void setLevel (float newLevel)  // 0..1
    {
        newLevel = juce::jlimit (0.0f, 1.0f, newLevel);
        if (std::abs (newLevel - level) > 0.002f)
        {
            level = newLevel;
            repaint();
        }
    }

    void paint (juce::Graphics& g) override
    {
        auto area = getLocalBounds().toFloat();
        const auto label = area.removeFromBottom (18.0f);
        const auto bar = area.withSizeKeepingCentre (10.0f, area.getHeight() - 4.0f);
        g.setColour (colours::card);
        g.fillRoundedRectangle (bar, 5.0f);
        g.setColour (colour);
        g.fillRoundedRectangle (bar.withTop (bar.getBottom() - bar.getHeight() * level), 5.0f);
        g.setColour (colours::secondary);
        g.setFont (font (11.0f));
        g.drawText (name, label, juce::Justification::centred);
    }

private:
    juce::String name;
    juce::Colour colour;
    float level = 0.0f;
};

// A setting as a touch slider: its name and value above, an optional explanation below. It follows a desktop
// slider: dragging sets that slider (which does the work and keeps the value), and changes made there show here.
class SettingSlider : public juce::Component
{
public:
    SettingSlider (juce::String nameToShow, juce::Slider& desktopSlider, juce::String descriptionToShow = {})
        : name (std::move (nameToShow)), description (std::move (descriptionToShow)), desktop (desktopSlider)
    {
        slider.setSliderStyle (juce::Slider::LinearHorizontal);
        slider.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
        slider.setRange (desktop.getMinimum(), desktop.getMaximum(), desktop.getInterval());
        slider.setDoubleClickReturnValue (false, 0.0);
        slider.setScrollWheelEnabled (false);
        slider.setViewportIgnoreDragFlag (true);  // Dragging it moves it, not the screen
        slider.setValue (desktop.getValue(), juce::dontSendNotification);
        slider.onValueChange = [this]
        {
            desktop.setValue (slider.getValue(), juce::sendNotificationSync);
            repaint();
        };
        addAndMakeVisible (slider);

        // Fine steps: a finger can't place a slider to 0.1 mm. Held, they repeat.
        for (auto* b : { &lessButton, &moreButton })
        {
            b->setRepeatSpeed (400, 70);
            b->setColour (juce::TextButton::buttonColourId, colours::card);
            addAndMakeVisible (b);
        }
        lessButton.onClick = [this] { nudge (-1); };
        moreButton.onClick = [this] { nudge (1); };
    }

    void refresh()
    {
        if (! slider.isMouseButtonDown() && ! juce::exactlyEqual (slider.getValue(), desktop.getValue()))
        {
            slider.setValue (desktop.getValue(), juce::dontSendNotification);
            repaint();
        }
        if (isEnabled() != desktop.isEnabled())  // e.g. a plugged hole's sliders
        {
            setEnabled (desktop.isEnabled());
            setAlpha (desktop.isEnabled() ? 1.0f : 0.4f);
        }
    }

    int preferredHeight (int width) const
    {
        return 22 + 40 + (description.isEmpty() ? 0 : textHeight (description, font (sizes::caption), width) + 4);
    }

    void paint (juce::Graphics& g) override
    {
        auto top = getLocalBounds().removeFromTop (22);
        g.setFont (font (sizes::body));
        g.setColour (colours::text);
        g.drawText (name, top, juce::Justification::centredLeft);
        g.setColour (colours::secondary);
        g.drawText (desktop.getTextFromValue (slider.getValue()), top, juce::Justification::centredRight);
        if (description.isNotEmpty())
            drawWrapped (g, description, font (sizes::caption), colours::secondary, getLocalBounds().withTrimmedTop (22 + 40 + 2));
    }

    void resized() override
    {
        auto row = getLocalBounds().withTrimmedTop (22).withHeight (40);
        lessButton.setBounds (row.removeFromLeft (40).reduced (2, 4));
        moreButton.setBounds (row.removeFromRight (40).reduced (2, 4));
        slider.setBounds (row.reduced (4, 0));
    }

private:
    void nudge (int direction)
    {
        const double step = desktop.getInterval() > 0.0 ? desktop.getInterval() : (desktop.getMaximum() - desktop.getMinimum()) / 200.0;
        desktop.setValue (desktop.getValue() + direction * step, juce::sendNotificationSync);
        slider.setValue (desktop.getValue(), juce::dontSendNotification);
        repaint();
    }

    juce::String name, description;
    juce::Slider& desktop;
    juce::Slider slider;
    juce::TextButton lessButton { juce::String::fromUTF8 ("\xe2\x88\x92") }, moreButton { "+" };
};

// An on/off setting as a switch that follows a desktop toggle button (setting it does what clicking that does).
class SettingSwitch : public juce::Component
{
public:
    SettingSwitch (juce::String nameToShow, juce::ToggleButton& desktopButton, juce::String descriptionToShow = {})
        : description (std::move (descriptionToShow)), desktop (desktopButton)
    {
        button.setButtonText (nameToShow);
        button.setToggleState (desktop.getToggleState(), juce::dontSendNotification);
        button.onClick = [this] { desktop.setToggleState (button.getToggleState(), juce::sendNotificationSync); };
        addAndMakeVisible (button);
    }

    void refresh() { button.setToggleState (desktop.getToggleState(), juce::dontSendNotification); }

    int preferredHeight (int width) const
    {
        return 44 + (description.isEmpty() ? 0 : textHeight (description, font (sizes::caption), width) + 4);
    }

    void paint (juce::Graphics& g) override
    {
        if (description.isNotEmpty())
            drawWrapped (g, description, font (sizes::caption), colours::secondary, getLocalBounds().withTrimmedTop (46));
    }

    void resized() override { button.setBounds (getLocalBounds().removeFromTop (44)); }

private:
    juce::String description;
    juce::ToggleButton& desktop;
    juce::ToggleButton button;
};
} // namespace phone
