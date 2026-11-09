#pragma once

#include <JuceHeader.h>
#include "UiStyle.h"
#include "Scales.h"

// selected notes are the theme color, the rest the light blue
// the root gets a dot and the note being snapped to gets an outline
class KeyboardDisplay : public juce::Component
{
public:
    KeyboardDisplay() { setMouseCursor(juce::MouseCursor::PointingHandCursor); }

    std::function<void(int)> onNoteClicked; // pitch class, 0 = C

    // selected notes as pitch classes (bit 0 = C); root is -1 when the notes are hand-picked
    void setNotes(juce::uint16 newMask, int newRoot)
    {
        mask = newMask;
        root = newRoot;
        repaint();
    }

    void setThemeColour(juce::Colour color) { theme = color; repaint(); }

    // lead pitch as a fractional midi note, below 0 hides the dot
    void setPitch(float midiNote)
    {
        const auto changed = (midiNote < 0.0f) != (pitch < 0.0f) || std::abs(midiNote - pitch) > 0.005f;
        pitch = midiNote;

        if (changed)
            repaint(trackArea().expanded(2.0f).getSmallestIntegerContainer());
    }

    void paint(juce::Graphics& g) override
    {
        const auto area = keyArea();

        // the line the keys hang from
        g.setColour(ui::ink);
        g.fillRoundedRectangle(area.getX(), area.getY(), area.getWidth(), lidHeight, lidHeight * 0.5f);

        for (auto note : whiteNotes)
            drawKey(g, note);

        for (auto note : blackNotes)
            drawKey(g, note);

        drawTrack(g);
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        const auto note = noteAt(e.position);

        if (note >= 0 && onNoteClicked)
            onNoteClicked(note);
    }

private:
    static constexpr int whiteNotes[] = { 0, 2, 4, 5, 7, 9, 11 };
    static constexpr int blackNotes[] = { 1, 3, 6, 8, 10 };
    static constexpr float lidHeight = 4.0f;
    static constexpr float trackHeight = 14.0f;
    static constexpr float dotSize = 10.0f;

    static bool isBlack(int note) { return note == 1 || note == 3 || note == 6 || note == 8 || note == 10; }

    juce::Rectangle<float> keyArea() const
    {
        return getLocalBounds().toFloat().reduced(ui::outline).withTrimmedBottom(trackHeight + 4.0f);
    }

    // rail under the keys, same width as the keyboard so wrapping lines up with its edges
    juce::Rectangle<float> trackArea() const
    {
        const auto area = keyArea();
        return { area.getX(), area.getBottom() + 4.0f, area.getWidth(), trackHeight };
    }

    juce::Rectangle<float> keyBounds(int note) const
    {
        const auto area = keyArea();
        const auto top = area.getY() + lidHeight - 1.0f;
        const auto height = area.getBottom() - top;
        const auto whiteWidth = area.getWidth() / 7.0f;

        if (!isBlack(note))
        {
            int index = 0;
            while (whiteNotes[index] != note)
                ++index;

            return { area.getX() + (float)index * whiteWidth + 1.5f, top, whiteWidth - 3.0f, height };
        }

        // black keys sit on the edge between two white keys
        const auto edge = note == 1 ? 1.0f : note == 3 ? 2.0f : note == 6 ? 4.0f : note == 8 ? 5.0f : 6.0f;
        const auto blackWidth = whiteWidth * 0.6f;

        return { area.getX() + edge * whiteWidth - blackWidth * 0.5f, top, blackWidth, height * 0.6f };
    }

    void drawKey(juce::Graphics& g, int note)
    {
        const auto r = keyBounds(note);
        const auto radius = juce::jmin(isBlack(note) ? 5.0f : 7.0f, r.getWidth() * 0.4f);

        juce::Path path;
        path.addRoundedRectangle(r.getX(), r.getY(), r.getWidth(), r.getHeight(), radius, radius,
            false, false, true, true); // rounded bottom corners only

        g.setColour(scales::contains(mask, note) ? theme : ui::meterBelowGate);
        g.fillPath(path);
        g.setColour(ui::ink);
        g.strokePath(path, juce::PathStrokeType(ui::outline));

        if (note == root)
            g.fillEllipse(juce::Rectangle<float>(7.0f, 7.0f).withCentre({ r.getCentreX(), r.getBottom() - 11.0f }));
    }

    // x under the keys for a pitch: key centre to key centre, B runs on past the right edge toward the next C
    float pitchToX(float midiNote) const
    {
        auto pitchClass = std::fmod(midiNote, 12.0f);
        if (pitchClass < 0.0f)
            pitchClass += 12.0f;

        const auto lower = juce::jlimit(0, 11, (int)std::floor(pitchClass));
        const auto fraction = pitchClass - (float)lower;

        const auto fromX = keyBounds(lower).getCentreX();
        const auto toX = lower == 11 ? keyBounds(0).getCentreX() + keyArea().getWidth()
            : keyBounds(lower + 1).getCentreX();

        return fromX + fraction * (toX - fromX);
    }

    void drawTrack(juce::Graphics& g)
    {
        const auto track = trackArea();
        const auto centreY = track.getCentreY();

        // rail and the two walls
        g.setColour(ui::divider);
        g.drawHorizontalLine(juce::roundToInt(centreY), track.getX(), track.getRight());

        g.setColour(ui::ink);
        g.fillRoundedRectangle(track.getX() - 1.0f, track.getY(), 2.0f, track.getHeight(), 1.0f);
        g.fillRoundedRectangle(track.getRight() - 1.0f, track.getY(), 2.0f, track.getHeight(), 1.0f);

        if (pitch < 0.0f)
            return;

        // clipped between the walls, drawn a keyboard-width either side so it comes out the other end
        juce::Graphics::ScopedSaveState saved(g);
        g.reduceClipRegion(track.reduced(1.0f, -2.0f).getSmallestIntegerContainer());

        const auto x = pitchToX(pitch);
        const auto width = track.getWidth();

        g.setColour(ui::inputLine);

        for (auto offset : { -width, 0.0f, width })
            g.fillEllipse(juce::Rectangle<float>(dotSize, dotSize).withCentre({ x + offset, centreY }));
    }

    // black keys are on top, so they're checked first
    int noteAt(juce::Point<float> position) const
    {
        for (auto note : blackNotes)
            if (keyBounds(note).contains(position))
                return note;

        for (auto note : whiteNotes)
            if (keyBounds(note).expanded(1.5f, 0.0f).contains(position))
                return note;

        return -1;
    }

    juce::uint16 mask = 0;
    int root = -1;
    float pitch = -1.0f;
    juce::Colour theme{ ui::accent };
};