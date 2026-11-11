// The desktop window's pages (1200 x 910) on a touch screen, scaled: to the whole screen on an iPad, or to the
// width (scrolling up and down) as the iPhone's Details tab. It only shows the window; its owner keeps it alive,
// since the pages also run the connection, the stream and the settings the phone's own screens use.

#pragma once

#include "../MainComponent.h"

class DesktopView : public juce::Component
{
public:
    enum class Fit
    {
        width,  // Fill the width and scroll vertically
        whole   // Fit entirely (an iPad shows the whole window)
    };

    DesktopView (MainComponent& mainToShow, Fit fitToUse) : main (mainToShow), fit (fitToUse)
    {
        holder.addAndMakeVisible (main);
        viewport.setViewedComponent (&holder, false);
        viewport.setScrollBarsShown (false, false, true, false);
        viewport.setScrollOnDragMode (juce::Viewport::ScrollOnDragMode::all);
        addAndMakeVisible (viewport);

        // A drag on a slider moves the slider, not the page.
        keepSliderDrags (main);
        setOpaque (true);
    }

    // Space the screen keeps for itself (notch, home indicator), in this component's coordinates.
    void setInsets (juce::BorderSize<int> newInsets)
    {
        insets = newInsets;
        resized();
    }

    void paint (juce::Graphics& g) override { g.fillAll (juce::Colour (0xff1e1f22)); }

    void resized() override
    {
        const auto area = insets.subtractedFrom (getLocalBounds());
        viewport.setBounds (area);
        if (area.isEmpty())
            return;

        float scale = (float) area.getWidth() / (float) main.getWidth();
        if (fit == Fit::whole)
            scale = std::min (scale, (float) area.getHeight() / (float) main.getHeight());
        const float width = (float) main.getWidth() * scale, height = (float) main.getHeight() * scale;
        if (fit == Fit::whole)  // Centred in the whole area (nothing to scroll)
        {
            main.setTransform (juce::AffineTransform::scale (scale).translated (((float) area.getWidth() - width) / 2.0f,
                                                                                 ((float) area.getHeight() - height) / 2.0f));
            holder.setSize (area.getWidth(), area.getHeight());
        }
        else
        {
            main.setTransform (juce::AffineTransform::scale (scale));
            holder.setSize (juce::roundToInt (width), juce::roundToInt (height));
        }
    }

private:
    static void keepSliderDrags (juce::Component& c)
    {
        if (dynamic_cast<juce::Slider*> (&c) != nullptr)
            c.setViewportIgnoreDragFlag (true);
        // Only the shown tab's page is a child, so visit every page.
        if (auto* tabs = dynamic_cast<juce::TabbedComponent*> (&c))
            for (int i = 0; i < tabs->getNumTabs(); ++i)
                if (auto* page = tabs->getTabContentComponent (i); page != nullptr && page->getParentComponent() == nullptr)
                    keepSliderDrags (*page);
        for (auto* child : c.getChildren())
            keepSliderDrags (*child);
    }

    MainComponent& main;
    const Fit fit;
    juce::BorderSize<int> insets;
    juce::Component holder;
    juce::Viewport viewport;
};

// iPad: the desktop window, scaled to fit the screen.
class TabletRoot : public juce::Component
{
public:
    explicit TabletRoot (std::unique_ptr<MainComponent> mainToShow)
        : main (std::move (mainToShow)), view (*main, DesktopView::Fit::whole)
    {
        addAndMakeVisible (view);
    }

    MainComponent& getMain() { return *main; }

    void resized() override
    {
        view.setBounds (getLocalBounds());
        if (const auto* display = juce::Desktop::getInstance().getDisplays().getDisplayForRect (getScreenBounds()))
            view.setInsets (display->safeAreaInsets);
    }

private:
    std::unique_ptr<MainComponent> main;  // Outlives the view, which only shows it.
    DesktopView view;
};
