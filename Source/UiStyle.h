#pragma once

#include <JuceHeader.h>

// shared colours and fonts for the editor
namespace ui
{
    inline constexpr int maxVoices = 8; // must match VocalHarmonizerAudioProcessor::maximumVoices

    inline const juce::Colour paper             { 0xfffbfaf6 }; // window background
    inline const juce::Colour panel             { 0xffffffff }; // tab box, meter backgrounds
    inline const juce::Colour ink               { 0xff1a1a1a }; // text and outlines
    inline const juce::Colour subtle            { 0xff5a5a5a }; // secondary text
    inline const juce::Colour divider           { 0xffd9d6cf }; // section lines
    inline const juce::Colour gridLine          { 0xffebe8e1 }; // pitch trace note lines
    inline const juce::Colour dialTrack         { 0xffebe9e4 };
    inline const juce::Colour dialArc           { 0xff2b2e35 };
    inline const juce::Colour accent            { 0xff3b6fd8 };
    inline const juce::Colour tabOff            { 0xffece9e2 };
    inline const juce::Colour faderTrack        { 0xffefe9df };
    inline const juce::Colour meterFill         { 0xff619dc6 }; // fader meters, input above the gate
    inline const juce::Colour meterBelowGate    { 0xffa6c8e2 }; // input level below the gate
    inline const juce::Colour inputLine         { 0xff9a9a9a }; // sung pitch in the trace, dry dot
    inline const juce::Colour correctionTheme   { 0xff4f79b0 };
    inline const juce::Colour voiceTheme        { 0xffb5677d };
    inline const juce::Colour toneTheme         { 0xff8c6a4f };
    inline const juce::Colour synthTheme        { 0xffb8862b };
    inline const juce::Colour envelopeTheme     { 0xff3d4a57 };
    inline const juce::Colour movementTheme     { 0xff8ba489 };
    inline const juce::Colour chorusTheme       { 0xff8ba489 };
    inline const juce::Colour spreadArc         { 0xff6f8f6d }; // spread macro dial
    inline const juce::Colour holdTheme         { 0xff907092 };
    inline const juce::Colour clip              { 0xffd2475b }; // peak readout above 0 dB

    inline constexpr float outline = 1.5f;

    // one colour per voice slot, used in the pitch trace and the stereo field
    inline juce::Colour voiceColour (int slot)
    {
        static const juce::uint32 colours[] = { 0xff3b6fd8, 0xffd9732b, 0xff2f9e5a, 0xffb0479c,
                                                0xffc9a227, 0xff2a9bb5, 0xffd2475b, 0xff6f5bd0 };
        return juce::Colour (colours[((slot % 8) + 8) % 8]);
    }

    // Short Stack (SIL Open Font License, see Assets/OFL.txt), compiled into the plugin through BinaryData
    // so it looks the same on every machine; loaded once, the first time any text is drawn
    inline juce::Typeface::Ptr shortStackTypeface()
    {
        static const auto typeface = juce::Typeface::createSystemTypefaceFor (BinaryData::ShortStackRegular_ttf,
                                                                             BinaryData::ShortStackRegular_ttfSize);
        return typeface;
    }

    // Short Stack only has a regular weight, so bold requests come out regular
    inline juce::Font font (float height, bool bold = false)
    {
        juce::ignoreUnused (bold);
        return juce::Font (juce::FontOptions (shortStackTypeface()).withHeight (height));
    }
}
