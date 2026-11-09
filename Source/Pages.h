#pragma once

#include <JuceHeader.h>
#include "UiStyle.h"
#include "Controls.h"
#include "StereoField.h"
#include "EnvelopeEditor.h"
#include "KeyboardDisplay.h"
#include "Scales.h"

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

class CorrectionPage : public juce::Component
{
public:
    explicit CorrectionPage(juce::AudioProcessorValueTreeState& stateToUse)
        : state(stateToUse),
        amount(stateToUse, "correctAmount", "Amount", " %"),
        snap(stateToUse, "snap", "Snapping", " %")
    {
        addAndMakeVisible(amount);
        addAndMakeVisible(snap);
        addAndMakeVisible(keyBox);
        addAndMakeVisible(scaleBox);
        addAndMakeVisible(keyboard);

        addAndMakeVisible(resetButton);
        resetButton.setButtonText("Reset");
        resetButton.onClick = [this]
            {
                clearNotes();
                setParameter("correctKey", 0.0f); // Off
            };

        amount.setArcColour(ui::correctionTheme);
        snap.setArcColour(ui::correctionTheme);
        keyboard.setThemeColour(ui::correctionTheme);

        // items first, then the attachments (choice index i = item id i + 1)
        keyBox.addItemList(scales::keyChoices, 1);
        scaleBox.addItemList(scales::scaleNames, 1);
        keyBoxAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(state, "correctKey", keyBox);
        scaleBoxAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(state, "correctScale", scaleBox);

        // keyboard and scale box follow the parameters, also when automation or presets change them
        watch("correctKey", [this](float value)
            {
                keyChoice = juce::roundToInt(value);
                scaleBox.setAlpha(keyChoice == 0 ? 0.4f : 1.0f); // scale does nothing while key is Off

                // choosing a key drops any hand picked notes, so going back to Off starts empty
                if (keyChoice > 0)
                    clearNotes();

                refreshKeyboard();
            });

        watch("correctScale", [this](float value) { scale = juce::roundToInt(value); refreshKeyboard(); });

        for (int i = 0; i < 12; ++i)
            watch("note" + juce::String(i), [this, i](float value) { handPicked[(std::size_t)i] = value > 0.5f; refreshKeyboard(); });

        // fade snapping while correction is off
        watch("correctAmount", [this](float value) { snap.setAlpha(value <= 0.0f ? 0.35f : 1.0f); });

        keyboard.onNoteClicked = [this](int pitchClass) { toggleNote(pitchClass); };
    }

    // lead pitch as a fractional midi note, below 0 hides the dot
    void setPitch(float midiNote) { keyboard.setPitch(midiNote); }

    void resized() override
    {
        auto area = getLocalBounds();

        // same dial positions as the synth page
        const auto fullRowWidth = 5 * dialWidth + 4 * dialGap;
        const auto rowLeft = juce::jmax(0, (area.getWidth() - fullRowWidth) / 2);

        auto dials = area.withTrimmedLeft(rowLeft).removeFromLeft(2 * dialWidth + dialGap);
        layoutDialRow(dials, { &amount, &snap });

        // right side uses the page's full height: dropdowns at the top, the pitch rail at the bottom
        area.setLeft(dials.getRight() + 20);
        auto right = area.reduced(0, 2);

        auto top = right.removeFromTop(26);
        keyBox.setBounds(top.removeFromLeft(80));
        top.removeFromLeft(10);
        resetButton.setBounds(top.removeFromRight(70));
        top.removeFromRight(10);
        scaleBox.setBounds(top);

        right.removeFromTop(10);
        keyboard.setBounds(right);
    }

private:
    void watch(const juce::String& parameterId, std::function<void(float)> callback)
    {
        auto attachment = std::make_unique<juce::ParameterAttachment>(*state.getParameter(parameterId), std::move(callback));
        attachment->sendInitialUpdate();
        watchers.push_back(std::move(attachment));
    }

    // same notes the processor snaps to
    juce::uint16 currentMask() const
    {
        if (keyChoice > 0)
            return scales::maskFor(keyChoice - 1, scale);

        juce::uint16 mask = 0;
        for (int i = 0; i < 12; ++i)
            if (handPicked[(std::size_t)i])
                mask = static_cast<juce::uint16>(mask | (1u << i));

        return mask;
    }

    void refreshKeyboard() { keyboard.setNotes(currentMask(), keyChoice > 0 ? keyChoice - 1 : -1); }

    // clicking picks notes by hand: a chosen key's scale becomes the starting selection, then key goes to Off
    void toggleNote(int pitchClass)
    {
        const auto mask = static_cast<juce::uint16> (currentMask() ^ (1u << pitchClass));

        for (int i = 0; i < 12; ++i)
            setParameter("note" + juce::String(i), ((mask >> i) & 1u) ? 1.0f : 0.0f);

        if (keyChoice > 0)
            setParameter("correctKey", 0.0f); // index 0 = Off
    }

    void clearNotes()
    {
        for (int i = 0; i < 12; ++i)
            setParameter("note" + juce::String(i), 0.0f);
    }

    void setParameter(const juce::String& parameterId, float normalisedValue)
    {
        if (auto* parameter = state.getParameter(parameterId); parameter != nullptr && parameter->getValue() != normalisedValue)
        {
            parameter->beginChangeGesture();
            parameter->setValueNotifyingHost(normalisedValue);
            parameter->endChangeGesture();
        }
    }

    juce::AudioProcessorValueTreeState& state;
    LabelledDial amount, snap;
    juce::ComboBox keyBox, scaleBox;
    KeyboardDisplay keyboard;
    juce::TextButton resetButton;

    int keyChoice = 0, scale = 1;
    std::array<bool, 12> handPicked{};

    // last, destroyed first
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> keyBoxAttachment, scaleBoxAttachment;
    std::vector<std::unique_ptr<juce::ParameterAttachment>> watchers;
};


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
