// A panel shown over a page, with a title and a Close button: what a dialog window is on the desktop. Used on
// iOS, where a dialog window gets no title bar to close it with. Tapping the dimmed area around it closes it too.
// The owner keeps it in a std::unique_ptr and resets it in onClose (called asynchronously, so that is safe).

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

class OverlayPanel : public juce::Component
{
public:
    OverlayPanel (std::unique_ptr<juce::Component> contentToShow, const juce::String& titleToShow)
        : content (std::move (contentToShow)), title (titleToShow)
    {
        addAndMakeVisible (*content);
        closeButton.onClick = [this] { close(); };
        addAndMakeVisible (closeButton);
    }

    std::function<void()> onClose;

    // Covers `parent`; the panel sits near its top, where the button that opened it is.
    void showOver (juce::Component& parent)
    {
        parent.addAndMakeVisible (this);
        setBounds (parent.getLocalBounds());
        toFront (true);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colours::black.withAlpha (0.6f));
        g.setColour (juce::Colour (0xff1e1f22));
        g.fillRect (box);
        g.setColour (juce::Colour (0xff4a4c52));
        g.drawRect (box);
        g.setColour (juce::Colour (0xffd8d8d8));
        g.setFont (juce::FontOptions (17.0f, juce::Font::bold));
        g.drawText (title, box.withHeight (titleHeight).reduced (12, 0), juce::Justification::centredLeft);
    }

    void resized() override
    {
        const int width = juce::jmin (getWidth() - 20, content->getWidth());
        const int height = juce::jmin (getHeight() - 60, content->getHeight() + titleHeight);
        box = juce::Rectangle<int> ((getWidth() - width) / 2, 50, width, height);
        auto area = box;
        auto titleRow = area.removeFromTop (titleHeight);
        closeButton.setBounds (titleRow.removeFromRight (90).reduced (6));
        content->setBounds (area);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (! box.contains (e.getPosition()))
            close();
    }

private:
    void close()
    {
        setVisible (false);
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<OverlayPanel> (this)]
        {
            if (safe != nullptr && safe->onClose != nullptr)
            {
                const auto callback = safe->onClose;  // The owner deletes the panel, and with it onClose.
                callback();
            }
        });
    }

    static constexpr int titleHeight = 40;
    std::unique_ptr<juce::Component> content;
    juce::String title;
    juce::TextButton closeButton { "Close" };
    juce::Rectangle<int> box;
};
