#pragma once

#include <JuceHeader.h>
#include "UiStyle.h"

#include <array>
#include <cmath>

// scrolling graph of pitch over time with newest at the right edge
// grey sung pitch, harmonies are colored
class PitchTrace : public juce::Component
{
public:
    static constexpr int historyLength = 140; // ~4.7 s at 30 frames per second

    PitchTrace()
    {
        for (auto& frame : frames)
        {
            frame.input = -1.0f;
            frame.voices.fill (-1.0f);
        }
    }

    // called by the editor's timer; 0 Hz = silent
    void pushFrame (float inputHz, const std::array<float, ui::maxVoices>& voiceHz)
    {
        auto& frame = frames[(size_t) writeIndex];
        frame.input = hzToNote (inputHz);

        for (size_t i = 0; i < (size_t) ui::maxVoices; ++i)
            frame.voices[i] = hzToNote (voiceHz[i]);

        writeIndex = (writeIndex + 1) % historyLength;
        count = juce::jmin (count + 1, historyLength);

        updateRange();
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto area = getLocalBounds().toFloat();
        auto header = area.removeFromTop (26.0f);
        const auto labels = area.removeFromLeft (40.0f);
        const auto plot = area.reduced (0.0f, 6.0f);

        const auto& latest = frameAtAge (0);

        // header: sung note on the left, held voices on the right
        if (count > 0 && latest.input >= 0.0f)
        {
            g.setColour (ui::ink);
            g.setFont (ui::font (18.0f, true));
            g.drawText (noteName (juce::roundToInt (latest.input)), header.removeFromLeft (52.0f), juce::Justification::centredLeft);

            g.setColour (ui::subtle);
            g.setFont (ui::font (14.0f));
            g.drawText (juce::String (noteToHz (latest.input), 1) + " Hz", header.removeFromLeft (90.0f), juce::Justification::centredLeft);
        }
        else
        {
            g.setColour (ui::subtle);
            g.setFont (ui::font (14.0f));
            g.drawText ("sing something...", header.removeFromLeft (200.0f), juce::Justification::centredLeft);
        }

        g.setFont (ui::font (14.0f));
        for (int i = ui::maxVoices - 1; i >= 0; --i)
        {
            const auto note = latest.voices[(size_t) i];
            if (note < 0.0f)
                continue;

            auto slot = header.removeFromRight (56.0f);
            g.setColour (ui::voiceColour (i));
            g.fillRect (slot.getX(), slot.getCentreY() - 1.5f, 14.0f, 3.0f);
            g.setColour (ui::ink);
            g.drawText (noteName (juce::roundToInt (note)), slot.withTrimmedLeft (18.0f), juce::Justification::centredLeft);
        }

        // note grid: every C, plus whatever is sounding right now
        const auto low = centre - span * 0.5f;
        const auto high = centre + span * 0.5f;
        auto yOf = [&] (float note) { return plot.getBottom() - (note - low) / span * plot.getHeight(); };

        juce::Array<int> marked;
        for (int n = (int) std::ceil (low); n <= (int) std::floor (high); ++n)
            if (n % 12 == 0)
                marked.addIfNotAlreadyThere (n);

        if (latest.input >= 0.0f)
            marked.addIfNotAlreadyThere (juce::roundToInt (latest.input));

        for (auto note : latest.voices)
            if (note >= 0.0f)
                marked.addIfNotAlreadyThere (juce::roundToInt (note));

        g.setFont (ui::font (13.0f));
        for (auto n : marked)
        {
            if ((float) n < low || (float) n > high)
                continue;

            const auto y = yOf ((float) n);
            g.setColour (ui::gridLine);
            g.drawHorizontalLine (juce::roundToInt (y), plot.getX(), plot.getRight());
            g.setColour (ui::subtle);
            g.drawText (noteName (n), labels.withY (y - 8.0f).withHeight (16.0f), juce::Justification::centredLeft);
        }

        // one path per series, lifting the pen wherever it was silent
        const auto step = plot.getWidth() / (float) (historyLength - 1);

        auto drawSeries = [&] (auto valueOf, juce::Colour colour, float thickness)
        {
            juce::Path path;
            bool penDown = false;

            for (int age = count - 1; age >= 0; --age)
            {
                const auto note = valueOf (frameAtAge (age));

                if (note < 0.0f)
                {
                    penDown = false;
                    continue;
                }

                const auto x = plot.getRight() - (float) age * step;
                const auto y = juce::jlimit (plot.getY(), plot.getBottom(), yOf (note));

                if (penDown)
                    path.lineTo (x, y);
                else
                    path.startNewSubPath (x, y);

                penDown = true;
            }

            g.setColour (colour);
            g.strokePath (path, juce::PathStrokeType (thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        };

        drawSeries ([] (const Frame& f) { return f.input; }, ui::inputLine, 2.0f);

        for (int i = 0; i < ui::maxVoices; ++i)
            drawSeries ([i] (const Frame& f) { return f.voices[(size_t) i]; }, ui::voiceColour (i), 2.5f);
    }

private:
    struct Frame
    {
        float input = -1.0f;                         // MIDI note number (fractional), < 0 = silent
        std::array<float, ui::maxVoices> voices {};
    };

    const Frame& frameAtAge (int age) const
    {
        return frames[(size_t) ((writeIndex - 1 - age + 2 * historyLength) % historyLength)];
    }

    // follow the notes on screen so lines don't run off the edge
    void updateRange()
    {
        float lowest = 1000.0f, highest = -1000.0f;

        auto include = [&] (float note)
        {
            if (note >= 0.0f)
            {
                lowest = juce::jmin (lowest, note);
                highest = juce::jmax (highest, note);
            }
        };

        for (int age = 0; age < count; ++age)
        {
            const auto& frame = frameAtAge (age);
            include (frame.input);

            for (auto note : frame.voices)
                include (note);
        }

        if (highest < lowest)
            return; // nothing on screen, keep the last view

        const auto targetCentre = 0.5f * (lowest + highest);
        const auto targetSpan = juce::jlimit (12.0f, 40.0f, highest - lowest + 4.0f);

        centre += 0.1f * (targetCentre - centre);
        span += 0.1f * (targetSpan - span);
    }

    static float hzToNote (float hz) { return hz > 20.0f ? 69.0f + 12.0f * std::log2 (hz / 440.0f) : -1.0f; }
    static float noteToHz (float note) { return 440.0f * std::pow (2.0f, (note - 69.0f) / 12.0f); }

    static juce::String noteName (int note)
    {
        static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        return juce::String (names[((note % 12) + 12) % 12]) + juce::String (note / 12 - 1);
    }

    std::array<Frame, historyLength> frames;
    int writeIndex = 0;
    int count = 0;

    float centre = 57.0f; // A3
    float span = 14.0f;   // semitones visible
};
