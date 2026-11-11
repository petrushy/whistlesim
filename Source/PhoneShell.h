// iOS, for now: the desktop window (1200 x 910) scaled to the width of the screen's safe area (clear of the notch
// and the home indicator), scrolling vertically. A phone layout of its own comes later; this gets the whole app
// running on the phone first.

#pragma once

#include "MainComponent.h"

class PhoneShell : public juce::Component
{
public:
    explicit PhoneShell (std::unique_ptr<MainComponent> mainToShow) : main (std::move (mainToShow))
    {
        holder.addAndMakeVisible (*main);
        viewport.setViewedComponent (&holder, false);
        viewport.setScrollBarsShown (false, false, true, false);
        viewport.setScrollOnDragMode (juce::Viewport::ScrollOnDragMode::all);
        addAndMakeVisible (viewport);

        // A drag on a slider moves the slider, not the page.
        keepSliderDrags (*main);
        setOpaque (true);
    }

    MainComponent& getMain() { return *main; }

    void paint (juce::Graphics& g) override { g.fillAll (juce::Colour (0xff1e1f22)); }

    void resized() override
    {
        auto area = getLocalBounds();
        if (const auto* display = juce::Desktop::getInstance().getDisplays().getDisplayForRect (getScreenBounds()))
            area = display->safeAreaInsets.subtractedFrom (area);
        viewport.setBounds (area);

        const float scale = (float) area.getWidth() / (float) main->getWidth();
        main->setTransform (juce::AffineTransform::scale (scale));
        holder.setSize (area.getWidth(), juce::roundToInt ((float) main->getHeight() * scale));
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

    std::unique_ptr<MainComponent> main;  // Outlives holder and viewport, which only show it.
    juce::Component holder;
    juce::Viewport viewport;
};
