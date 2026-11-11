// A panel shown over a page, with a title and a Close button: what a dialog window is on the desktop. Used on
// iOS, where a dialog window gets no title bar to close it with. Tapping the dimmed area around it closes it too.
// The owner keeps it in a std::unique_ptr and resets it in onClose (called asynchronously, so that is safe).

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

class OverlayPanel : public juce::Component
{
public:
    OverlayPanel (std::unique_ptr<juce::Component> contentToShow, const juce::String& titleToShow)
        : content (std::move (contentToShow)), title (titleToShow), contentSize (content->getWidth(), content->getHeight())
    {
        addAndMakeVisible (*content);
        closeButton.onClick = [this] { close(); };
        addAndMakeVisible (closeButton);
    }

    std::function<void()> onClose;

    // Covers `parent`; the panel sits near its top (`top` from it), where the button that opened it is.
    void showOver (juce::Component& parent, int top = 50)
    {
        topMargin = top;
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
        // Content wider than the screen (a phone) is scaled down to fit, rather than cut off.
        const float scale = juce::jmin (1.0f, (float) (getWidth() - 20) / (float) juce::jmax (1, contentSize.x));
        const int width = juce::roundToInt ((float) contentSize.x * scale);
        const int height = juce::jmin (getHeight() - topMargin - 10, juce::roundToInt ((float) contentSize.y * scale) + titleHeight);
        box = juce::Rectangle<int> ((getWidth() - width) / 2, topMargin, width, height);
        auto area = box;
        auto titleRow = area.removeFromTop (titleHeight);
        closeButton.setBounds (titleRow.removeFromRight (90).reduced (6));
        content->setBounds (0, 0, contentSize.x, juce::roundToInt ((float) area.getHeight() / scale));
        content->setTransform (juce::AffineTransform::scale (scale).translated ((float) area.getX(), (float) area.getY()));
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
    juce::Point<int> contentSize;  // As the content made itself
    int topMargin = 50;
};
