#pragma once

#include <JuceHeader.h>
#include "UiStyle.h"
#include "Controls.h"
#include "StereoField.h"
#include "EnvelopeEditor.h"

#include <initializer_list>

// dial layout sizes, shared so pages can line up with each other
inline constexpr int dialWidth = 110;
inline constexpr int dialHeight = 130;
inline constexpr int dialGap = 27; // 5 * 110 + 4 * 27 = 658 (page width)

// lays dials out in a centred row
inline void layoutDialRow(juce::Rectangle<int> area, std::initializer_list<juce::Component*> dials)
{
    const auto count = (int) dials.size();
    const auto totalWidth = count * dialWidth + (count - 1) * dialGap;
    auto row = area.withSizeKeepingCentre(juce::jmin (totalWidth, area.getWidth()), juce::jmin(dialHeight, area.getHeight()));

    for (auto* dial : dials)
    {
        dial->setBounds(row.removeFromLeft(dialWidth));
        row.removeFromLeft(dialGap);
    }
}

class VoicePage : public juce::Component
{
public:
    explicit VoicePage(juce::AudioProcessorValueTreeState& state)
        : formant(state, "formant", "Formant", " %"),
        breath(state, "breath", "Breath", " %"),
        jitter(state, "jitter", "Jitter", " %"),
        lowCut(state, "lowCut", "Low Cut", " Hz"),
        highCut(state, "highCut", "High Cut", " Hz")
    {
        addAndMakeVisible(formant);
        addAndMakeVisible(breath);
        addAndMakeVisible(jitter);
        addAndMakeVisible(lowCut);
        addAndMakeVisible(highCut);

        formant.setArcColour(ui::voiceTheme);
        breath.setArcColour(ui::voiceTheme);
        jitter.setArcColour(ui::voiceTheme);
        lowCut.setArcColour(ui::toneTheme);
        highCut.setArcColour(ui::toneTheme);
    }

    void resized() override { layoutDialRow (getLocalBounds(), { &formant, &breath, &jitter, &lowCut, &highCut}); }

private:
    LabelledDial formant, breath, jitter, lowCut, highCut;
};

class SynthPage : public juce::Component
{
public:
    explicit SynthPage(juce::AudioProcessorValueTreeState& state)
        : glide (state, "glide", "Glide", ""),
        scoop(state, "scoop", "Scoop", " semitones"),
        glideMode(state, "glideMode"),
        envelope (state)
    {
        addAndMakeVisible(glide);
        addAndMakeVisible(glideMode);
        addAndMakeVisible(scoop);
        addAndMakeVisible(envelope);

        glide.setArcColour(ui::synthTheme);
        scoop.setArcColour(ui::synthTheme);
        glideMode.setAccentColour(ui::synthTheme.interpolatedWith(ui::panel, 0.45f)); // lighter synthTheme

        // fade the scoop dial while Duet is selected, since duet always glides the full distance
        if (auto* modeParameter = dynamic_cast<juce::AudioParameterChoice*>(state.getParameter("glideMode")))
        {
            const auto duetIndex = modeParameter->choices.indexOf("Duet");

            modeAttachment = std::make_unique<juce::ParameterAttachment>(*modeParameter,
                [this, duetIndex](float index) { scoop.setAlpha(juce::roundToInt(index) == duetIndex ? 0.35f : 1.0f); });
            modeAttachment->sendInitialUpdate();
        }
    }

    void resized() override 
    {
        auto area = getLocalBounds();

        // to get glide and scoop to be where the dials on the other pages are
        const auto fullRowWidth = 5 * dialWidth + 4 * dialGap;
        const auto rowLeft = juce::jmax(0, (area.getWidth() - fullRowWidth) / 2);

        // slot exactly two dials wide, so layoutDialRow doesn't do any centering
        auto dials = area.withTrimmedLeft(rowLeft).removeFromLeft(2 * dialWidth + dialGap);
        layoutDialRow(dials, { &glide, &scoop });

        // glide mode switch in the space below the two dials, same width as them
        glideMode.setBounds(dials.withTrimmedTop(dials.getHeight() - 22));

        area.setLeft(dials.getRight() + 20);
        envelope.setBounds(area);
    }

private:
    LabelledDial glide, scoop;
    ChoiceSwitch glideMode;
    EnvelopeEditor envelope;
    std::unique_ptr<juce::ParameterAttachment> modeAttachment; // last, destroyed first (it touches scoop)
};

// spread controls
class SpreadPage : public juce::Component
{
public:
    explicit SpreadPage(juce::AudioProcessorValueTreeState& state)
        : movement(state, "movement", "Movement", " %"),
        moveRate(state, "movementRate", "Rate", " Hz"),
        chorus(state, "chorus", "Chorus", " %"),
        rate(state, "chorusRate", "Rate", " Hz"),
        random(state, "chorusRandom", "Random", " %")
    {
        addAndMakeVisible(movement);
        addAndMakeVisible(moveRate);
        addAndMakeVisible(chorus);
        addAndMakeVisible(rate);
        addAndMakeVisible(random);

        movement.setArcColour(ui::movementTheme);
        moveRate.setArcColour(ui::movementTheme);

        chorus.setArcColour(ui::chorusTheme);
        rate.setArcColour(ui::chorusTheme);
        random.setArcColour(ui::chorusTheme);
    }

    void resized() override { layoutDialRow(getLocalBounds(), { &movement, &moveRate, &chorus, &rate, &random }); }

private:
    LabelledDial movement, moveRate, chorus, rate, random;
};
