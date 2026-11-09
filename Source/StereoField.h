#pragma once

#include <JuceHeader.h>
#include "UiStyle.h"

#include <array>

// dry in the middle, a dot per voice at its current pan,
// with a faint oval behind each dot showing how far spread's drift can move it
class StereoField : public juce::Component
{
public:
    void setVoices (const std::array<float, ui::maxVoices>& newPans,
                    const std::array<float, ui::maxVoices>& newBasePans,
                    const std::array<float, ui::maxVoices>& newRanges,
                    const std::array<bool, ui::maxVoices>& newActive)
    {
        pans = newPans;
        basePans = newBasePans;
        ranges = newRanges;
        active = newActive;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto area = getLocalBounds().toFloat();
        const auto labelRow = area.removeFromTop (18.0f);

        g.setColour(ui::subtle);
        g.setFont(ui::font (13.0f));
        g.drawText("L", labelRow, juce::Justification::centredLeft);
        g.drawText("dry", labelRow, juce::Justification::centred);
        g.drawText("R", labelRow, juce::Justification::centredRight);

        const auto track = area.withSizeKeepingCentre(area.getWidth(), 26.0f);
        g.setColour(ui::faderTrack);
        g.fillRoundedRectangle(track, 13.0f);

        g.setColour(ui::divider);
        g.drawVerticalLine(juce::roundToInt(track.getCentreX()), track.getY() - 6.0f, track.getBottom() + 6.0f);


        const auto halfWidth = track.getWidth() * 0.5f - 14.0f;
        auto xOf = [&](float pan) { return track.getCentreX() + juce::jlimit(-1.0f, 1.0f, pan) * halfWidth; };

        // drift ranges first, so the dots sit on top
        for (int i = 0; i < ui::maxVoices; ++i)
        {
            const auto slot = (size_t) i;

            if (! active[slot] || ranges[slot] <= 0.005f)
                continue;

            const auto left = xOf(basePans[slot] - ranges[slot]) - 8.0f;
            const auto right = xOf(basePans[slot] + ranges[slot]) + 8.0f;
            const auto oval = juce::Rectangle<float>(left, track.getCentreY() - 11.0f, right - left, 22.0f);

            g.setColour(ui::voiceColour(i).withAlpha(0.18f));
            g.fillRoundedRectangle(oval, 11.0f);
        }

        for (int i = 0; i < ui::maxVoices; ++i)
        {
            if (! active[(size_t) i])
                continue;

            g.setColour(ui::voiceColour(i));
            g.fillEllipse(juce::Rectangle<float> (16.0f, 16.0f).withCentre({ xOf(pans[(size_t) i]), track.getCentreY() }));
        }

        // paint the dry voice dot last, so its on top 
        g.setColour(ui::inputLine);
        g.fillEllipse(juce::Rectangle<float>(12.0f, 12.0f).withCentre(track.getCentre()));
    }

private:
    std::array<float, ui::maxVoices> pans {};
    std::array<float, ui::maxVoices> basePans {};
    std::array<float, ui::maxVoices> ranges {};
    std::array<bool, ui::maxVoices> active {};
};
