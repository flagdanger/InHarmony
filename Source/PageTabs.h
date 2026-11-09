#pragma once

#include <JuceHeader.h>
#include "UiStyle.h"

// folder tabs sitting on top of an outlined box; the editor puts the page inside getContentBounds()
class PageTabs : public juce::Component
{
public:
    explicit PageTabs (juce::StringArray names) : tabNames (std::move (names))
    {
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    std::function<void (int)> onChange;

    int getCurrentPage() const noexcept { return current; }

    void setCurrentPage (int index)
    {
        index = juce::jlimit (0, tabNames.size() - 1, index);

        if (index == current)
            return;

        current = index;
        repaint();

        if (onChange)
            onChange (current);
    }

    // inside of the box, in this component's coordinates
    juce::Rectangle<int> getContentBounds() const
    {
        return getLocalBounds().withTrimmedTop (tabHeight).reduced (10);
    }

    void paint (juce::Graphics& g) override
    {
        const auto body = getLocalBounds().toFloat().withTrimmedTop ((float) tabHeight).reduced (ui::outline * 0.5f);

        // inactive tabs sit behind the box
        for (int i = 0; i < tabNames.size(); ++i)
            if (i != current)
                drawTab (g, i, false);

        g.setColour (ui::panel);
        g.fillRoundedRectangle (body, 6.0f);
        g.setColour (ui::ink);
        g.drawRoundedRectangle (body, 6.0f, ui::outline);

        drawTab (g, current, true);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        for (int i = 0; i < tabNames.size(); ++i)
        {
            if (tabBounds (i).contains (e.position))
            {
                setCurrentPage (i);
                return;
            }
        }
    }

private:
    static constexpr int tabHeight = 32;
    static constexpr float tabWidth = 90.0f;

    juce::Rectangle<float> tabBounds (int index) const
    {
        return { 10.0f + (float) index * (tabWidth + 4.0f), ui::outline * 0.5f, tabWidth, (float) tabHeight };
    }

    void drawTab (juce::Graphics& g, int index, bool isActive)
    {
        const auto tab = tabBounds (index);

        juce::Path shape;
        shape.addRoundedRectangle (tab.getX(), tab.getY(), tab.getWidth(), tab.getHeight(),
                                   6.0f, 6.0f, true, true, false, false);

        g.setColour (isActive ? ui::panel : ui::tabOff);
        g.fillPath (shape);
        
        if (!isActive)
        {
            g.setColour(ui::ink);
            g.strokePath(shape, juce::PathStrokeType(ui::outline));
        }
        else
        {
            const auto half = ui::outline * 0.5f;

            // hide the box's top line, exactly between the inner edges of the tab's side lines
            g.setColour(ui::panel);
            g.fillRect(tab.getX() + half, tab.getBottom() - half, tab.getWidth() - ui::outline, ui::outline + 1.0f);

            // open outline (no bottom), sides run down to the bottom of the box's line so the corners join
            constexpr float corner = 6.0f;
            const auto bottom = tab.getBottom() + half;

            juce::Path edge;
            edge.startNewSubPath(tab.getX(), bottom);
            edge.lineTo(tab.getX(), tab.getY() + corner);
            edge.quadraticTo(tab.getX(), tab.getY(), tab.getX() + corner, tab.getY());
            edge.lineTo(tab.getRight() - corner, tab.getY());
            edge.quadraticTo(tab.getRight(), tab.getY(), tab.getRight(), tab.getY() + corner);
            edge.lineTo(tab.getRight(), bottom);

            g.setColour(ui::ink);
            g.strokePath(edge, juce::PathStrokeType(ui::outline, juce::PathStrokeType::curved, juce::PathStrokeType::butt));
        }

        g.setColour (isActive ? ui::ink : ui::subtle);
        g.setFont (ui::font (16.0f, isActive));
        g.drawText (tabNames[index], tab, juce::Justification::centred);
    }

    juce::StringArray tabNames;
    int current = 0;
};
