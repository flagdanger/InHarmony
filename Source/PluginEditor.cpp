#include "PluginProcessor.h"
#include "PluginEditor.h"

static_assert (ui::maxVoices == (int) VocalHarmonizerAudioProcessor::maximumVoices,
               "ui::maxVoices in UiStyle.h must match the processor's maximumVoices");

//==============================================================================
VocalHarmonizerAudioProcessorEditor::VocalHarmonizerAudioProcessorEditor (VocalHarmonizerAudioProcessor& p)
    : AudioProcessorEditor (&p),
    audioProcessor (p),
    inputMeter (p.apvts),
    correctionPage(p.apvts),
    voicePage (p.apvts),
    synthPage (p.apvts),
    spreadPage(p.apvts),
    spreadDial (p.apvts, "spread", "Spread", " %"),
    dryFader (p.apvts, "dry", "dryCeiling", "Dry"),
    wetFader (p.apvts, "wet", "wetCeiling", "Wet"),
    outFader (p.apvts, "out", "outCeiling", "Out"),
    presetBar(p.presetManager, presetDialog)
{
    logo = juce::ImageCache::getFromMemory (BinaryData::logo_png, BinaryData::logo_pngSize);

    // spread is the macro, so it gets its own color
    spreadDial.getSlider().setColour (juce::Slider::rotarySliderFillColourId, ui::spreadArc);

    for (auto* component : std::initializer_list<juce::Component*> { &presetBar, &inputMeter, &pitchTrace, &stereoField, &pageTabs,
                                                                     &correctionPage, &voicePage, &synthPage, &spreadPage,
                                                                     &spreadDial, &dryFader, &wetFader, &outFader })
        addAndMakeVisible (component);

    addChildComponent(presetDialog); // added last so it sits on top, but hidden

    // set after the children are added so they all get lookAndFeelChanged() and rebuild
    // their value boxes with these colors (sliders bake text box colours in when created)
    setLookAndFeel (&lookAndFeel);

    pageTabs.onChange = [this] (int index) { showPage (index); };
    showPage (pageTabs.getCurrentPage());

    setResizable (false, false);
    setSize (960, 620);
    startTimerHz (60);
}

VocalHarmonizerAudioProcessorEditor::~VocalHarmonizerAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void VocalHarmonizerAudioProcessorEditor::showPage (int index)
{
    correctionPage.setVisible(index == 0);
    voicePage.setVisible(index == 1);
    synthPage.setVisible(index == 2);
    spreadPage.setVisible(index == 3);
}

//==============================================================================
void VocalHarmonizerAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (ui::paper);

    // header: name on the left, logo in the center
    g.setColour(ui::ink);
    g.setFont(ui::font(26.0f, true));
    g.drawText("In Harmony", headerArea.withTrimmedBottom(4).removeFromLeft(200), juce::Justification::centredLeft);

    if (logo.isValid())
    {
        const auto logoHeight = (float)headerArea.getHeight() - 6.0f;
        const auto logoWidth = logoHeight * (float)logo.getWidth() / (float)logo.getHeight();
        const auto logoX = (float)getWidth() * 0.5f - logoWidth * 0.5f;

        g.drawImage(logo, juce::Rectangle<float>(logoX, (float)headerArea.getY() + 1.0f, logoWidth, logoHeight),
            juce::RectanglePlacement::centred);
    }

    g.setColour (ui::ink);
    g.fillRect (juce::Rectangle<float> ((float) headerArea.getX(), (float) headerArea.getBottom() - ui::outline,
                                        (float) headerArea.getWidth(), ui::outline));

    // hairlines between sections
    g.setColour (ui::divider);
    g.drawHorizontalLine (inputDividerY, (float) leftColumn.getX(), (float) leftColumn.getRight());
    g.drawHorizontalLine (traceDividerY, (float) leftColumn.getX(), (float) leftColumn.getRight());
    g.drawVerticalLine (rightColumn.getX(), (float) rightColumn.getY(), (float) rightColumn.getBottom());
    g.drawHorizontalLine (spreadDividerY, (float) rightColumn.getX() + 18.0f, (float) rightColumn.getRight());

    // dB scale on the outer sides of the mixer: left of Dry, right of Out, placed with the faders own scale
    // (drawn here rather than in the strips, since the numbers reach past the strips' edges)
    static constexpr float scaleMarks[] = { 6.0f, 3.0f, 0.0f, -3.0f, -6.0f, -10.0f, -20.0f, -30.0f, -60.0f };
    constexpr float tabReach = 19.0f;   // the handle tabs reach 17 px from a bar's centre, plus a small gap
    constexpr float labelWidth = 24.0f;

    g.setColour (ui::subtle);
    g.setFont (ui::font (11.0f));

    for (auto db : scaleMarks)
    {
        const auto text = (db > 0.0f ? "+" : "") + juce::String (juce::roundToInt (db));

        const auto dryY = dryFader.getYForDb (db);
        g.drawText (text, juce::Rectangle<float> (dryFader.getBarCentreX() - tabReach - labelWidth, dryY - 6.0f, labelWidth, 12.0f),
                    juce::Justification::centredRight);

        const auto outY = outFader.getYForDb (db);
        g.drawText (text, juce::Rectangle<float> (outFader.getBarCentreX() + tabReach, outY - 6.0f, labelWidth, 12.0f),
                    juce::Justification::centredLeft);
    }
}

void VocalHarmonizerAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced (22, 14);

    headerArea = area.removeFromTop (46);

    // presets on the right of the header, centred above the line
    auto presetArea = headerArea.withTrimmedBottom(6).removeFromRight(330);
    presetBar.setBounds(presetArea.withSizeKeepingCentre(330, 28));
    presetDialog.setBounds(getLocalBounds());

    // right column: spread on top, faders below
    rightColumn = area.removeFromRight (220);
    auto right = rightColumn.withTrimmedLeft (18);

    spreadDial.setBounds (right.removeFromTop (140).reduced (16, 8));
    spreadDividerY = right.getY();
    right.removeFromTop (10);

    const auto faderWidth = right.getWidth() / 3;
    dryFader.setBounds (right.removeFromLeft (faderWidth));
    wetFader.setBounds (right.removeFromLeft (faderWidth));
    outFader.setBounds (right);

    // left column: input meter, pitch trace, tabs
    leftColumn = area.withTrimmedRight (18);
    auto left = leftColumn;

    inputMeter.setBounds (left.removeFromTop (46));
    inputDividerY = left.getY();
    left.removeFromTop (6);

    pitchTrace.setBounds (left.removeFromTop (196));
    left.removeFromTop (4);

    // full width of left column, "L" lines up with trace labels
    stereoField.setBounds (left.removeFromTop (48));
    traceDividerY = left.getY() + 4;
    left.removeFromTop (4);
    left.removeFromTop (12);

    pageTabs.setBounds (left);

    const auto pageArea = pageTabs.getContentBounds() + pageTabs.getPosition();
    correctionPage.setBounds(pageArea);
    voicePage.setBounds (pageArea);
    synthPage.setBounds (pageArea);
    spreadPage.setBounds (pageArea);
}

void VocalHarmonizerAudioProcessorEditor::timerCallback()
{
    // meters refresh at 20 Hz; each one shows the loudest level since its last refresh
    if (++meterTickCounter >= 3)
    {
        meterTickCounter = 0;

        // input meter: rises instantly, falls about 30 dB per second
        const auto inputDb = audioProcessor.getAndResetInputLevelDb();
        inputLevelShown = inputDb > inputLevelShown ? inputDb : juce::jmax (inputDb, inputLevelShown - 1.5f);
        inputMeter.setLevelDb (inputLevelShown);

        // fader meters: fall about 25 dB per second
        auto fall = [] (float& shown, float peak) { shown = juce::jmax (peak, shown * 0.866f); return shown; };
        dryFader.setMeterLevel (fall (dryLevelShown, audioProcessor.getAndResetDryPeak()));
        wetFader.setMeterLevel (fall (wetLevelShown, audioProcessor.getAndResetWetPeak()));
        outFader.setMeterLevel (fall (outLevelShown, audioProcessor.getAndResetOutPeak()));
    }

    // pitch trace and stereo field at 30 Hz (the trace's history length assumes this rate)
    if (++traceTickCounter < 2)
        return;

    traceTickCounter = 0;

    std::array<float, ui::maxVoices> voiceHz {};
    std::array<float, ui::maxVoices> pans {};
    std::array<float, ui::maxVoices> basePans {};
    std::array<float, ui::maxVoices> ranges {};
    std::array<bool, ui::maxVoices> active {};
    std::array<float, ui::maxVoices> levels{};
    std::array<int, ui::maxVoices> starts{};

    for (size_t i = 0; i < (size_t) ui::maxVoices; ++i)
    {
        voiceHz[i] = audioProcessor.getVoiceOutputHz(i);
        pans[i] = audioProcessor.getVoicePan(i);
        basePans[i] = audioProcessor.getVoiceBasePan(i);
        ranges[i] = audioProcessor.getVoiceMovementRange(i);
        active[i] = voiceHz[i] > 0.0f;
        levels[i] = audioProcessor.getVoiceLevel(i);
        starts[i] = audioProcessor.getVoiceStartCount(i);
    }

    const auto inputHz = audioProcessor.getPitchConfidence() >= 0.4f ? audioProcessor.getDetectedPitchHz() : 0.0f;
    pitchTrace.pushFrame(inputHz, voiceHz, levels, starts);

    stereoField.setVoices(pans, basePans, ranges, active);
    correctionPage.setPitch(audioProcessor.getLeadDisplayNote());
}
