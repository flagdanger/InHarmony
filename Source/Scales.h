#pragma once

#include <JuceHeader.h>
#include <array>
#include <initializer_list>


// keys and scales for pitch correction
// shared by the processor and the keyboard display
namespace scales
{
	inline const juce::StringArray keyNames{ "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };

    // key dropdown "Off" means the notes are picked by user
    inline const juce::StringArray keyChoices = []
        {
            juce::StringArray choices{ "Off" };
            choices.addArray(keyNames);
            return choices;
        }();

	// order has to match scaleMasks
	inline const juce::StringArray scaleNames{ "Chromatic", "Major", "Minor", "Harmonic Minor",
												"Major Pentatonic", "Minor Pentatonic", "Blues" };

    // semitones above the root as bits
    constexpr juce::uint16 bits(std::initializer_list<int> degrees)
    {
        juce::uint16 mask = 0;
        for (auto degree : degrees)
            mask = static_cast<juce::uint16> (mask | (1u << degree));
        return mask;
    }

    inline constexpr std::array<juce::uint16, 7> scaleMasks{
        bits({ 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 }), // chromatic
        bits({ 0, 2, 4, 5, 7, 9, 11 }),                 // major
        bits({ 0, 2, 3, 5, 7, 8, 10 }),                 // minor
        bits({ 0, 2, 3, 5, 7, 8, 11 }),                 // harmonic minor
        bits({ 0, 2, 4, 7, 9 }),                        // major pentatonic
        bits({ 0, 3, 5, 7, 10 }),                       // minor pentatonic
        bits({ 0, 3, 5, 6, 7, 10 })                     // blues
    };

    // a key's scale as pitch classes
    inline juce::uint16 maskFor(int key, int scale)
    {
        const auto scaleMask = scaleMasks[(std::size_t)juce::jlimit(0, (int)scaleMasks.size() - 1, scale)];
        juce::uint16 mask = 0;

        for (int degree = 0; degree < 12; ++degree)
            if ((scaleMask >> degree) & 1u)
                mask = static_cast<juce::uint16>(mask | (1u << ((key + degree) % 12)));

        return mask;
    }

    // true if note is in the mask
    inline bool contains(juce::uint16 mask, int note)
    {
        return ((mask >> (((note % 12) + 12) % 12)) & 1u) != 0;
    }

    // nearest note in the scale to a sung pitch, in fractional midi notes
    // stays on the current note until another is clearly closer
    inline int nearestNote(float sungNote, juce::uint16 mask, int currentNote)
    {
        if (mask == 0)
            return -1;

        int nearest = -1;
        float nearestDistance = 100.0f;
        const auto center = juce::roundToInt(sungNote);

        for (int note = center - 6; note <= center + 6; ++note)
        {
            const auto distance = std::abs(sungNote - (float)note);

            if (contains(mask, note) && distance < nearestDistance)
            {
                nearest = note;
                nearestDistance = distance;
            }
        }

        constexpr float hysteresis = 0.15f; // semitones
        if (currentNote >= 0 && nearest != currentNote && contains(mask, currentNote)
            && std::abs(sungNote - (float)currentNote) < nearestDistance + hysteresis)
            return currentNote;

        return nearest;
    }

}