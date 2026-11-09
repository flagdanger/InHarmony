#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "PitchDetector.h"

#include <algorithm>

//==============================================================================
VocalHarmonizerAudioProcessor::VocalHarmonizerAudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
     : AudioProcessor (BusesProperties()
                     #if ! JucePlugin_IsMidiEffect
                      #if ! JucePlugin_IsSynth
                       .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                      #endif
                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                     #endif
                       ),
#else
    :
#endif
apvts(*this, nullptr, "Parameters", createParameterLayout())
{
    gateParam = apvts.getRawParameterValue("gate");
    formantParam = apvts.getRawParameterValue("formant");
    breathParam = apvts.getRawParameterValue("breath");
    lowCutParam = apvts.getRawParameterValue("lowCut");
    highCutParam = apvts.getRawParameterValue("highCut");
    jitterParam = apvts.getRawParameterValue("jitter");
    jitterParam = apvts.getRawParameterValue("jitter");
    glideParam = apvts.getRawParameterValue("glide");
    scoopParam = apvts.getRawParameterValue("scoop");
    glideModeParam = apvts.getRawParameterValue("glideMode");
    attackParam = apvts.getRawParameterValue("attack");
    decayParam = apvts.getRawParameterValue("decay");
    sustainParam = apvts.getRawParameterValue("sustain");
    releaseParam = apvts.getRawParameterValue("release");
    attackCurveParam = apvts.getRawParameterValue("attackCurve");
    decayCurveParam = apvts.getRawParameterValue("decayCurve");
    releaseCurveParam = apvts.getRawParameterValue("releaseCurve");
    spreadParam = apvts.getRawParameterValue("spread");
    movementParam = apvts.getRawParameterValue("movement");
    movementRateParam = apvts.getRawParameterValue("movementRate");
    chorusParam = apvts.getRawParameterValue("chorus");
    chorusRateParam = apvts.getRawParameterValue("chorusRate");
    chorusRandomParam = apvts.getRawParameterValue("chorusRandom");
    dryParam = apvts.getRawParameterValue("dry");
    wetParam = apvts.getRawParameterValue("wet");
    outParam = apvts.getRawParameterValue("out");
    dryCeilingParam = apvts.getRawParameterValue("dryCeiling");
    wetCeilingParam = apvts.getRawParameterValue("wetCeiling");
    outCeilingParam = apvts.getRawParameterValue("outCeiling");
}

juce::AudioProcessorValueTreeState::ParameterLayout VocalHarmonizerAudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    auto percent = [] { return juce::NormalisableRange<float>(0.0f, 100.0f, 1.0f); };

    auto gainRange = juce::NormalisableRange<float>(-60.0f, 6.0f, 0.1f);
    gainRange.setSkewForCentre(-12.0f); // more fader travel near 0 dB

    // limits
    // same scale as the faders so the red handle lines up with the meter, but values snap to 0 dB:
    const auto ceilingSkew = std::log(0.5f) / std::log((-12.0f + 60.0f) / 66.0f);   // gainRange's skew (center -12 dB)

    juce::NormalisableRange<float> ceilingRange(-60.0f, 6.0f,
        [ceilingSkew](float start, float end, float proportion) { return start + (end - start) * std::pow(proportion, 1.0f / ceilingSkew); },
        [ceilingSkew](float start, float end, float value) { return std::pow((value - start) / (end - start), ceilingSkew); },
        [](float, float, float value) { return juce::jmin(std::round(value * 10.0f) / 10.0f, 0.0f); });   // 0.1 dB steps, max 0


    // tone for the harmonies, log ranges, continuous
    auto logRange = [](float low, float high)
        {
            return juce::NormalisableRange<float>(low, high,
                [](float start, float end, float proportion) { return start * std::pow(end / start, proportion); },
                [](float start, float end, float value) { return std::log(value / start) / std::log(end / start); });
        };

    // "120" below 1 kHz, "12.0 k" above, the dial adds " Hz"
    auto hzToText = [](float value, int) 
        { 
            return value >= 1000.0f ? juce::String(value / 1000.0f, 1) + " k" : juce::String(juce::roundToInt(value)); 
        };
    auto textToHz = [](const juce::String& text) 
        { 
            const auto number = text.getFloatValue();
            return text.containsIgnoreCase("k") ? number * 1000.0f : number; 
        };

    // note envelope (ms, skewed so short times get most of the travel, continuous so nothing snaps)
    auto envelopeRange = [](float maxMs, float centreMs)
        {
            auto range = juce::NormalisableRange<float>(0.0f, maxMs, 0.0f);
            range.setSkewForCentre(centreMs);
            return range;
        };

    // envelope curves: -100% = fast start, 0 = straight, +100% = slow start
    auto curveText = juce::AudioParameterFloatAttributes().withStringFromValueFunction(
        [](float value, int) { return juce::String(juce::roundToInt(value)); });

    // whole ms up to 500, then switch to seconds with 2 decimal places
    auto msOrSeconds = juce::AudioParameterFloatAttributes()
        .withStringFromValueFunction([](float valueMs, int)
            {
                return valueMs < 500.0f ? juce::String(juce::roundToInt(valueMs)) + " ms"
                    : juce::String(valueMs / 1000.0f, 2) + " s";
            })
        .withValueFromStringFunction([](const juce::String& text)
            {
                // typed values ("250 ms", "1.5 s") or plain numbers taken in as ms
                const auto number = text.getFloatValue();
                const auto isSeconds = text.containsIgnoreCase("s") && !text.containsIgnoreCase("ms");
                return isSeconds ? number * 1000.0f : number;
            });


    // continuous, no snapping
    auto glideRange = juce::NormalisableRange<float>(0.0f, 1000.0f, 0.0f);
    glideRange.setSkewForCentre(200.0f); // fine control for short glides

    // how fast voices drift around the stereo field 
    // logarithmic amd continuous, 0.29 hz is roughly 3.5s per move
    juce::NormalisableRange<float> moveRateRange(0.1f, 2.0f,
        [](float start, float end, float proportion) { return start * std::pow(end / start, proportion); },
        [](float start, float end, float value) { return std::log(value / start) / std::log(end / start); });

    // chorus rate
    // logarithmic, so every doubling of speed gets the same dial travel. continuous so it never snaps
    juce::NormalisableRange<float> rateRange(0.1f, 5.0f,
        [](float start, float end, float proportion) { return start * std::pow(end / start, proportion); },  // dial -> Hz
        [](float start, float end, float value) { return std::log(value / start) / std::log(end / start); }); // Hz -> dial
    //rateRange.setSkewForCentre(1.0f);

    // IDs are permanent
    // continuous so the handle slides smoothly; shown as whole dB
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{ "gate", 1 }, "Pitch Gate", juce::NormalisableRange<float>(-90.0f, -1.0f, 0.0f), -60.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction(
            [] (float value, int) { return juce::String(juce::roundToInt(value)); })));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{ "formant", 1 }, "Formant", percent(), 35.0f));    
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{ "breath", 1 }, "Breath", percent(), 30.0f));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{ "lowCut", 1 }, "Low Cut", logRange(20.0f, 500.0f), 120.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction(hzToText).withValueFromStringFunction(textToHz)));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{ "highCut", 1 }, "High Cut", logRange(2000.0f, 20000.0f), 20000.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction(hzToText).withValueFromStringFunction(textToHz)));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{ "jitter", 1 }, "Jitter", percent(), 0.0f));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{ "glide", 1 }, "Glide", glideRange, 42.0f, msOrSeconds)); // only show whole values
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{ "scoop", 1 }, "Scoop", juce::NormalisableRange<float>(0.0f, 12.0f, 1.0f), 4.0f));
    layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{ "glideMode", 1 }, "Glide Mode", juce::StringArray{ "From Voice", "Duet", "From Last" }, 0));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{ "attack", 1 }, "Attack", envelopeRange(2000.0f, 200.0f), 10.0f, msOrSeconds));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{ "decay", 1 }, "Decay", envelopeRange(2000.0f, 300.0f), 200.0f, msOrSeconds));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{ "sustain", 1 }, "Sustain", percent(), 100.0f));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{ "release", 1 }, "Release", envelopeRange(3000.0f, 300.0f), 100.0f, msOrSeconds));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{ "attackCurve", 1 }, "Attack Curve", juce::NormalisableRange<float>(-100.0f, 100.0f), 0.0f, curveText));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{ "decayCurve", 1 }, "Decay Curve", juce::NormalisableRange<float>(-100.0f, 100.0f), 0.0f, curveText));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{ "releaseCurve", 1 }, "Release Curve", juce::NormalisableRange<float>(-100.0f, 100.0f), 0.0f, curveText));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{ "spread", 1 }, "Spread", percent(), 0.0f));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{ "movement", 1 }, "Movement", percent(), 15.0f));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{ "movementRate", 1 }, "Movement Rate", moveRateRange, 0.29f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction(
            [](float value, int) { return juce::String(value, 2); })));     // show two decimals
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{ "chorus", 1 }, "Chorus", percent(), 80.0f));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{ "chorusRate", 1 }, "Chorus Rate", rateRange, 0.6f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction(
            [](float value, int) { return juce::String(value, 2); })));     // show two decimals
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{ "chorusRandom", 1 }, "Chorus Random", percent(), 0.0f));
    // gains read "+3.0 dB" above zero, "-6.0 dB" below (typing "-6" or "+3 dB" both work)
    auto gainText = juce::AudioParameterFloatAttributes().withStringFromValueFunction([] (float value, int)
    {
        auto rounded = std::round(value * 10.0f) / 10.0f;
        if (std::abs(rounded) < 0.05f)
            rounded = 0.0f; // avoid "-0.0"

        return (rounded > 0.0f ? "+" : "") + juce::String(rounded, 1) + " dB";
    });

    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{ "dry", 1 }, "Dry", gainRange, 0.0f, gainText));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{ "wet", 1 }, "Wet", gainRange, 0.0f, gainText));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{ "out", 1 }, "Output", gainRange, 0.0f, gainText));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{ "dryCeiling", 1 }, "Dry Ceiling", ceilingRange, 0.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction(
            [](float value, int) { return juce::String(value, 1) + " dB"; })));     // one decimal, with unit
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{ "wetCeiling", 1 }, "Wet Ceiling", ceilingRange, 0.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction(
            [](float value, int) { return juce::String(value, 1) + " dB"; })));     // one decimal, with unit
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{ "outCeiling", 1 }, "Out Ceiling", ceilingRange, 0.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction(
            [](float value, int) { return juce::String(value, 1) + " dB"; })));     // one decimal, with unit

    return layout;
}

VocalHarmonizerAudioProcessor::~VocalHarmonizerAudioProcessor()
{
}

//==============================================================================
const juce::String VocalHarmonizerAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool VocalHarmonizerAudioProcessor::acceptsMidi() const
{
   #if JucePlugin_WantsMidiInput
    return true;
   #else
    return false;
   #endif
}

bool VocalHarmonizerAudioProcessor::producesMidi() const
{
   #if JucePlugin_ProducesMidiOutput
    return true;
   #else
    return false;
   #endif
}

bool VocalHarmonizerAudioProcessor::isMidiEffect() const
{
   #if JucePlugin_IsMidiEffect
    return true;
   #else
    return false;
   #endif
}

double VocalHarmonizerAudioProcessor::getTailLengthSeconds() const
{
    return 0.1;
}

int VocalHarmonizerAudioProcessor::getNumPrograms()
{
    return 1;   // NB: some hosts don't cope very well if you tell them there are 0 programs,
                // so this should be at least 1, even if you're not really implementing programs.
}

int VocalHarmonizerAudioProcessor::getCurrentProgram()
{
    return 0;
}

void VocalHarmonizerAudioProcessor::setCurrentProgram (int index)
{
}

const juce::String VocalHarmonizerAudioProcessor::getProgramName (int index)
{
    return {};
}

void VocalHarmonizerAudioProcessor::changeProgramName (int index, const juce::String& newName)
{
}

//==============================================================================
void VocalHarmonizerAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{

    juce::ignoreUnused (samplesPerBlock);
    currentSampleRate = sampleRate; // need an accessible variable

    // dry buffers
    dryBuffer.setSize(2, samplesPerBlock, false, true, true);
    monoBuffer.setSize(1, samplesPerBlock, false, true, true);

    // spread control
    spreadSmoother.reset(sampleRate, 0.05); // 50 ms
    movementSmoother.reset(sampleRate, 0.05);
    chorusDepthSmoother.reset(sampleRate, 0.05);
    chorusRandomSmoother.reset(sampleRate, 0.05);

    spreadSmoother.setCurrentAndTargetValue(getSpread());
    movementSmoother.setCurrentAndTargetValue(getMovement());
    chorusDepthSmoother.setCurrentAndTargetValue(getChorusDepth());
    chorusRandomSmoother.setCurrentAndTargetValue(getChorusRandom());

    updatePanGains(getSpread(), getMovement());
    
    
    for (std::size_t i = 0; i < maximumVoices; ++i) {
        // jitter pitch drift, called once every 32 samples
        pitchWanderers[i].prepare(sampleRate / panUpdateInterval, 7000u + static_cast<std::uint32_t>(i) * 131u, 0.4, 1.5);
        timingWanderers[i].prepare(sampleRate / panUpdateInterval, 9000u + static_cast<std::uint32_t>(i) * 173u, 1.0, 3.0);
        
        // slow random pan drift, called once every 32 samples
        panWanderers[i].prepare(sampleRate / panUpdateInterval, 5000u + static_cast<std::uint32_t>(i) * 97u, 2.0, 5.0);
    }

    updateMovementRate();
    panUpdateCounter = 0;

    // 5 ms follow so timing steps between updates are smooth
    timingSmoothingCoefficient = static_cast<float>(1.0 - std::exp(-1.0 / (0.005 * sampleRate)));

    // ~250 ms glide when more than 6 voices are used, stepped once every 32
    layoutSmoothing = static_cast<float>(1.0 - std::exp(-static_cast<double>(panUpdateInterval) / (0.25 * sampleRate)));

    // parameters for glide modes
    chordWindowSamples = static_cast<int>(0.030 * sampleRate);   // 30 ms window to play other notes to make up one "chord"
    lastPlayedNote = -1;
    glideSourceNote = -1;
    samplesSinceNoteOn = 1 << 30;
    monoNoteStackSize = 0;
    activeGlideMode = getGlideMode();

    // 1.5s attack for pan drift, stepped once every 32 samples
    panAttackStep = static_cast<float>(panUpdateInterval / (1.5 * sampleRate));

    chorus.prepare(sampleRate);
    chorus.setRate(getChorusRate());

    // volume control
    dryGainSmoother.reset(sampleRate, 0.02);
    wetGainSmoother.reset(sampleRate, 0.02);
    outputGainSmoother.reset(sampleRate, 0.02);

    dryGainSmoother.setCurrentAndTargetValue(dbToGain(getDryGainDb()));
    wetGainSmoother.setCurrentAndTargetValue(dbToGain(getWetGainDb()));
    outputGainSmoother.setCurrentAndTargetValue(dbToGain(getOutputGainDb()));

    // pitch detector
    pitchDetector.prepare (sampleRate);
    detectedPitchHz.store (0.0f, std::memory_order_relaxed);
    pitchConfidence.store (0.0f, std::memory_order_relaxed);

    latchedPitch = 0.0f;
    candidateNote = -1;
    candidateSamples = 0;
    latchedNote.store(-1, std::memory_order_relaxed);

    // pitch shifters
    lastIntervalSourceNote = -1;

    for (auto& voice: Voices) {
        voice.shifter.prepare(sampleRate);
        voice.shifter.setFormantTracking(getFormantTracking());
        voice.shifter.setPitchSettlingTimeMs(getGlidingTimeMs());
        voice.shifter.setIntervalSemitones(0.0f);

        voice.timingDelay.prepare(sampleRate, maximumTimingMs);
        voice.timingDelayTarget = 0.0f;
        voice.timingDelaySmoothed = 0.0f;
        voice.shifter.setPitchDriftCents(0.0f);
        voice.panMovementAttack = 0.0f;

        voice.midiNote = -1;
        voice.noteHeld = false;

        // asdr envelope
        voice.envelope.setSampleRate(sampleRate);
        voice.envelope.setParameters(getEnvelopeParameters());
        voice.envelope.reset();
    }
    
    const auto shifterLatency = Voices[0].shifter.getLatencySamples();
    for (auto& line : dryDelayLines)
        line.assign(shifterLatency + 1, 0.0f);
    
    dryDelayWritePosition = 0;
    setLatencySamples(static_cast<int>(shifterLatency));

    // breath
    breathExtract.prepare(sampleRate, shifterLatency);
    breathLevel = 0.0f;
    breathLevelCoefficient = static_cast<float>(1.0 - std::exp(-1.0 / (0.020 * sampleRate))); // 20 ms

    // harmony tone filters 12dB/octave butterworth
    juce::dsp::ProcessSpec spec{ sampleRate, static_cast<juce::uint32>(samplesPerBlock), 2 };
    lowCutFilter.prepare(spec);
    highCutFilter.prepare(spec);
    lowCutFilter.setType(juce::dsp::StateVariableTPTFilterType::highpass);
    highCutFilter.setType(juce::dsp::StateVariableTPTFilterType::lowpass);
    lowCutFilter.setResonance(1.0f / juce::MathConstants<float>::sqrt2);
    highCutFilter.setResonance(1.0f / juce::MathConstants<float>::sqrt2);

    lowCutSmoother.reset(sampleRate, 0.05);
    highCutSmoother.reset(sampleRate, 0.05);
    lowCutSmoother.setCurrentAndTargetValue(getLowCutHz());
    highCutSmoother.setCurrentAndTargetValue(juce::jmin(getHighCutHz(), 0.45f * static_cast<float>(sampleRate)));
    lowCutFilter.setCutoffFrequency(lowCutSmoother.getCurrentValue());
    highCutFilter.setCutoffFrequency(highCutSmoother.getCurrentValue());

    // limiters, 80ms default release
    dryLimiter.setCeilingDb(getDryCeilingDb());
    wetLimiter.setCeilingDb(getWetCeilingDb());
    outLimiter.setCeilingDb(getOutCeilingDb());

    dryLimiter.prepare(sampleRate);
    wetLimiter.prepare(sampleRate);
    outLimiter.prepare(sampleRate);


    // clears midi piano
    for (auto& count : heldMidiNoteCounts) 
        count.store(0, std::memory_order_relaxed);

}

void VocalHarmonizerAudioProcessor::releaseResources()
{
    pitchDetector.reset();
    breathExtract.reset();

    for (auto& voice : Voices) {
        voice.shifter.reset();
        voice.timingDelay.clear();
    }

    chorus.reset();
    lowCutFilter.reset();
    highCutFilter.reset();
    dryLimiter.reset();
    wetLimiter.reset();
    outLimiter.reset();
}

#ifndef JucePlugin_PreferredChannelConfigurations
bool VocalHarmonizerAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
  #if JucePlugin_IsMidiEffect
    juce::ignoreUnused (layouts);
    return true;
  #else
    // This is the place where you check if the layout is supported.
    // In this template code we only support mono or stereo.
    // Some plugin hosts, such as certain GarageBand versions, will only
    // load plugins that support stereo bus layouts.
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
     && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    // This checks if the input layout matches the output layout
   #if ! JucePlugin_IsSynth
    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;
   #endif

    return true;
  #endif
}
#endif

void VocalHarmonizerAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;

    auto totalNumInputChannels  = getTotalNumInputChannels(); //still most likely getting a stereo input
    auto totalNumOutputChannels = getTotalNumOutputChannels();
    const int numSamples = buffer.getNumSamples();

    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear (i, 0, numSamples);

    // switching glide mode releases everything, so no voice is left in the old mode
    const auto glideMode = getGlideMode();
    if (glideMode != activeGlideMode) {
        releaseAllVoices();
        monoNoteStackSize = 0;
        activeGlideMode = glideMode;
    }

    // handle midi
    for (const auto metadata : midiMessages)
        handleMidiMessage(metadata.getMessage());

    // in case get sent a bigger block
    if (numSamples > dryBuffer.getNumSamples()) {
        dryBuffer.setSize(2, numSamples, false, false, true);
        monoBuffer.setSize(1, numSamples, false, false, true);
    }

    // copy L and R (mono track: R is a copy of L)
    const auto numInputs = juce::jmin(totalNumInputChannels, buffer.getNumChannels());
    dryBuffer.copyFrom(0, 0, buffer, 0, 0, numSamples);
    dryBuffer.copyFrom(1, 0, buffer, numInputs >= 2 ? 1 : 0, 0, numSamples);

    // mono sum for detection, shifters and breath
    monoBuffer.copyFrom(0, 0, dryBuffer.getReadPointer(0), numSamples, 0.5f);
    monoBuffer.addFrom(0, 0, dryBuffer, 1, 0, numSamples, 0.5f);

    const auto* mono = monoBuffer.getReadPointer(0);
    const auto* dryLeft = dryBuffer.getReadPointer(0);
    const auto* dryRight = dryBuffer.getReadPointer(1);


    // set detector gate
    const auto thresholdDb = getPitchGateThresholdDb();
    const auto thresholdRms = std::pow(10.0f, thresholdDb * 0.05f);
    pitchDetector.setMinimumFrameRms(thresholdRms);

    // set filters
    lowCutSmoother.setTargetValue(getLowCutHz());
    highCutSmoother.setTargetValue(juce::jmin(getHighCutHz(), 0.45f * static_cast<float>(getSampleRate())));

    // set envelope per voice
    const auto envelopeParameters = getEnvelopeParameters();
    for (auto& voice : Voices)
        voice.envelope.setParameters(envelopeParameters);

    // set spread
    spreadSmoother.setTargetValue(getSpread());
    movementSmoother.setTargetValue(getMovement());
    chorusDepthSmoother.setTargetValue(getChorusDepth());
    chorusRandomSmoother.setTargetValue(getChorusRandom());

    chorus.setRate(getChorusRate()); // speed change doesn't jump delay, so no smoothing
    updateMovementRate();

    // set volumes
    dryGainSmoother.setTargetValue(dbToGain(getDryGainDb()));
    wetGainSmoother.setTargetValue(dbToGain(getWetGainDb()));
    outputGainSmoother.setTargetValue(dbToGain(getOutputGainDb()));

    // set limiters
    dryLimiter.setCeilingDb(getDryCeilingDb());
    wetLimiter.setCeilingDb(getWetCeilingDb());
    outLimiter.setCeilingDb(getOutCeilingDb());

    // set glide and formant
    for (auto& voice : Voices) {
        voice.shifter.setPitchSettlingTimeMs(getGlidingTimeMs());
        voice.shifter.setFormantTracking(getFormantTracking());
    }
    
    // metering for the editor
    float inputSquareSum = 0.0f;
    float dryBlockPeak = 0.0f, wetBlockPeak = 0.0f, outBlockPeak = 0.0f;

    for (int sample = 0; sample < numSamples; ++sample) {

        pitchDetector.processSample(mono[sample]);
        const auto detectorPitch = pitchDetector.getPitch();
        const auto detectorConfidence = pitchDetector.getConfidence();
        updateLatchedNote(detectorPitch, detectorConfidence, 1); // latch detected pitch

        const auto currentLatchedNote = latchedNote.load(std::memory_order_relaxed);

        if (currentLatchedNote >= 0 && currentLatchedNote != lastIntervalSourceNote) {
            updateVoiceIntervals(currentLatchedNote);
            lastIntervalSourceNote = currentLatchedNote;
        }


        dryDelayLines[0][dryDelayWritePosition] = dryLeft[sample];
        dryDelayLines[1][dryDelayWritePosition] = dryRight[sample];

        const auto dryDelayReadPosition = (dryDelayWritePosition + 1) % dryDelayLines[0].size();
        const auto delayedDryLeft = dryDelayLines[0][dryDelayReadPosition];
        const auto delayedDryRight = dryDelayLines[1][dryDelayReadPosition];
        dryDelayWritePosition = dryDelayReadPosition;


        // always use 50ms latching time
        const auto pitchForShifter = (detectorPitch > 0.0f && latchedPitch > 0.0f) ? latchedPitch : detectorPitch;

        // calc breath
        const auto breathSample = breathExtract.processSample(mono[sample], detectorPitch, detectorConfidence);

        // calc jitter
        const auto jitterAmount = getJitter();

        // calc spread params
        const auto currentSpread = spreadSmoother.getNextValue();
        const auto currentMovement = movementSmoother.getNextValue();
        const auto currentChorusDepth = chorusDepthSmoother.getNextValue();
        const auto currentChorusRandom = chorusRandomSmoother.getNextValue();

        // update pan movement at control rate (every 32 samples)
        if (++panUpdateCounter >= panUpdateInterval) {
            panUpdateCounter = 0;
            updatePanGains(currentSpread, currentMovement);
            updateJitter(jitterAmount);
        }

        // sum up the voices
        float wetLeft = 0.0f;
        float wetRight = 0.0f;

        int activeVoices = 0;

        for (auto& voice : Voices) {
            if (voice.midiNote < 0)
                continue;

            ++activeVoices;

            const auto voiceConfidence = detectorConfidence; // keep following singer, envelope handles fade
            const auto shiftedSample = voice.shifter.processSample(mono[sample], pitchForShifter, voiceConfidence);

            // run sample through jitter delay
            voice.timingDelaySmoothed += timingSmoothingCoefficient * (voice.timingDelayTarget - voice.timingDelaySmoothed);
            const auto voiceSample = voice.timingDelay.process(shiftedSample, voice.timingDelaySmoothed) * voice.envelope.getNextSample();

            // add to L/R wet output
            wetLeft += voiceSample * voice.panLeft;
            wetRight += voiceSample * voice.panRight;

            // free the voice once its release has finished
            if (!voice.noteHeld && !voice.envelope.isActive())
                voice.midiNote = -1;
        }

        const auto dryGain = dryGainSmoother.getNextValue();
        const auto wetGain = wetGainSmoother.getNextValue();
        const auto outGain = outputGainSmoother.getNextValue();

        constexpr float voiceGain = 0.55f; // fixed voice headroom

        // spread scales chorus with pan and drift
        const auto chorusAmount = currentSpread * currentChorusDepth;

        // sqrt active voices to keep breath level normal
        const auto breathTarget = activeVoices > 0 ? std::sqrt(static_cast<float>(activeVoices)) : 0.0f;
        breathLevel += breathLevelCoefficient * (breathTarget - breathLevel);

        const auto breathPart = breathSample * getBreathAmount() * breathLevel * voiceGain;

        // process filter
        // move cutoffs only while dial is changing
        if (lowCutSmoother.isSmoothing())
            lowCutFilter.setCutoffFrequency(lowCutSmoother.getNextValue());
        if (highCutSmoother.isSmoothing())
            highCutFilter.setCutoffFrequency(highCutSmoother.getNextValue());

        // harmony tone: low cut then high cut on the summed voices + breath, before the chorus
        auto toneLeft = wetLeft * voiceGain + breathPart;
        auto toneRight = wetRight * voiceGain + breathPart;
        toneLeft = highCutFilter.processSample(0, lowCutFilter.processSample(0, toneLeft));
        toneRight = highCutFilter.processSample(1, lowCutFilter.processSample(1, toneRight));
        
        
        // process chorus
        const auto chorusRandomness = currentChorusRandom;
        float chorusLeft = 0.0f;
        float chorusRight = 0.0f;
        chorus.process(toneLeft, toneRight, chorusAmount, chorusRandomness, chorusLeft, chorusRight); // filtered harmonies into the chorus
        
        // limit dry level
        auto dryOutLeft = delayedDryLeft * dryGain;
        auto dryOutRight = delayedDryRight * dryGain;
        dryLimiter.process(dryOutLeft, dryOutRight);

        // limit wet level
        auto wetOutLeft = chorusLeft * wetGain;
        auto wetOutRight = chorusRight * wetGain;
        wetLimiter.process(wetOutLeft, wetOutRight);


        // final output
        auto left = (dryOutLeft + wetOutLeft) * outGain;
        auto right = (dryOutRight + wetOutRight) * outGain;
        outLimiter.process(left, right);

        inputSquareSum += mono[sample] * mono[sample];
        dryBlockPeak = std::max({ dryBlockPeak, std::abs(dryOutLeft), std::abs(dryOutRight) });
        wetBlockPeak = std::max({ wetBlockPeak, std::abs(wetOutLeft), std::abs(wetOutRight) });
        outBlockPeak = std::max({ outBlockPeak, std::abs(left), std::abs(right) });


        if (buffer.getNumChannels() >= 2) {
            buffer.setSample(0, sample, left);
            buffer.setSample(1, sample, right);

        } else {
            buffer.setSample(0, sample, 0.5f * (left + right)); // mono out, fold down
        }
        
    }

    detectedPitchHz.store (pitchDetector.getPitch(), std::memory_order_relaxed);
    pitchConfidence.store (pitchDetector.getConfidence(), std::memory_order_relaxed);

    // editor metering
    const auto inputRms = std::sqrt(inputSquareSum / static_cast<float>(juce::jmax(1, numSamples)));
    storeMax(inputLevelDb, juce::Decibels::gainToDecibels(inputRms, -100.0f));
    storeMax(dryPeak, dryBlockPeak);
    storeMax(wetPeak, wetBlockPeak);
    storeMax(outPeak, outBlockPeak);

    for (std::size_t i = 0; i < maximumVoices; ++i) {
        const auto& voice = Voices[i];
        voiceDisplayHz[i].store(voice.midiNote >= 0 ? voice.shifter.getCurrentOutputHz() : 0.0f, std::memory_order_relaxed);
        voiceDisplayPan[i].store(voice.currentPan, std::memory_order_relaxed);
        voiceDisplayBasePan[i].store(voice.currentBasePan, std::memory_order_relaxed);
        voiceDisplayRange[i].store(voice.currentMovementRange, std::memory_order_relaxed);
    }

    samplesSinceNoteOn = std::min(samplesSinceNoteOn + numSamples, 1 << 30);
}

void VocalHarmonizerAudioProcessor::handleMidiMessage(const juce::MidiMessage& message) noexcept
{
    const auto note = message.getNoteNumber();

    if (message.isNoteOn()) {
        const auto previousCount = heldMidiNoteCounts[note].fetch_add(1, std::memory_order_relaxed);

        // if repeat note-on, do nothing
        if (previousCount > 0)
            return;

        // notes in chord all glide from the note before the chord
        if (samplesSinceNoteOn > chordWindowSamples)
            glideSourceNote = lastPlayedNote;

        samplesSinceNoteOn = 0;
        lastPlayedNote = note;

        if (activeGlideMode == GlideMode::duet) {
            monoNoteOn(note);
            return;
        }

        // set new note to available voice
        Voice* freeVoice = nullptr;
        for (auto& voice : Voices) {
            if (voice.midiNote < 0) {
                freeVoice = &voice;
                break;
            }
        }

        // if all voices used, ignore new notes
        if (freeVoice == nullptr)
            return;

        setVoiceNote(*freeVoice, note);
        startVoice(*freeVoice);

        // where the new note slides in from, scoop caps the distance in both poly modes
        const auto scoop = getScoopSemitones();

        if (activeGlideMode == GlideMode::fromLast && glideSourceNote >= 0)
            freeVoice->shifter.startGlideFromOffset(std::clamp(static_cast<float>(glideSourceNote - note), -scoop, scoop));
        else
            freeVoice->shifter.startGlideFromInput(scoop);

    }
    else if (message.isNoteOff()) {
        auto& count = heldMidiNoteCounts[note];
        const auto currentCount = count.load(std::memory_order_relaxed);

        if (currentCount <= 0)
            return;

        const auto remainingCount = currentCount - 1;
        count.store(remainingCount, std::memory_order_relaxed);

        // keep the voice active for duplicate note-ons of the same pitch
        if (remainingCount > 0)
            return;

        if (activeGlideMode == GlideMode::duet) {
            monoNoteOff(note);
            return;
        }

        for (auto& voice : Voices) {
            if (voice.midiNote == note && voice.noteHeld) {
                voice.noteHeld = false;
                voice.envelope.noteOff();
                break;
            }
        }

    }
}

// point a voice at a MIDI note (interval from the sung note; the glide smooths the change)
void VocalHarmonizerAudioProcessor::setVoiceNote(Voice& voice, int note) noexcept
{
    voice.midiNote = note;

    const auto sourceNote = latchedNote.load(std::memory_order_relaxed);
    voice.shifter.setIntervalSemitones(sourceNote >= 0 ? static_cast<float>(note - sourceNote) : 0.0f);
}

// a voice starting: envelope, pan drift, clean timing delay
void VocalHarmonizerAudioProcessor::startVoice(Voice& voice) noexcept
{
    voice.noteHeld = true;
    voice.envelope.noteOn(); // attacks from the current level, so a restart during a release doesn't click
    voice.panMovementAttack = 0.0f;

    voice.timingDelay.clear();
    voice.timingDelaySmoothed = voice.timingDelayTarget;
}

void VocalHarmonizerAudioProcessor::releaseAllVoices() noexcept
{
    for (auto& voice : Voices) {
        if (voice.midiNote >= 0 && voice.noteHeld) {
            voice.noteHeld = false;
            voice.envelope.noteOff();
        }
    }
}

// duet: slide to the new note without restarting the envelope
void VocalHarmonizerAudioProcessor::monoNoteOn(int note) noexcept
{
    removeFromMonoStack(note);
    if (monoNoteStackSize < static_cast<int>(monoNoteStack.size()))
        monoNoteStack[static_cast<std::size_t>(monoNoteStackSize++)] = note;

    auto& voice = Voices[0];
    const auto wasSounding = voice.midiNote >= 0;
    const auto wasHeld = wasSounding && voice.noteHeld;

    setVoiceNote(voice, note);

    // legato: still held, so just let the glide carry the pitch from the old note to the new one
    if (wasHeld)
        return;

    startVoice(voice);

    // restarting during a release: the voice is still on the old note, so it slides from there
    if (wasSounding)
        return;

    // starting from silence: slide all the way from the last note played, or start on pitch if there isn't one
    voice.shifter.startGlideFromOffset(glideSourceNote >= 0 ? static_cast<float>(glideSourceNote - note) : 0.0f);
}

// duet: releasing the current key slides back to the (newest) old key still held
// or releases if none are left
void VocalHarmonizerAudioProcessor::monoNoteOff(int note) noexcept
{
    removeFromMonoStack(note);

    auto& voice = Voices[0];
    if (voice.midiNote != note || !voice.noteHeld)
        return; // an older key that wasn't sounding

    if (monoNoteStackSize > 0) {
        setVoiceNote(voice, monoNoteStack[static_cast<std::size_t>(monoNoteStackSize - 1)]);
    }
    else {
        voice.noteHeld = false;
        voice.envelope.noteOff();
    }
}

void VocalHarmonizerAudioProcessor::removeFromMonoStack(int note) noexcept
{
    int write = 0;
    for (int read = 0; read < monoNoteStackSize; ++read)
        if (monoNoteStack[static_cast<std::size_t>(read)] != note)
            monoNoteStack[static_cast<std::size_t>(write++)] = monoNoteStack[static_cast<std::size_t>(read)];

    monoNoteStackSize = write;
}

void VocalHarmonizerAudioProcessor::updateVoiceIntervals(int latchedSourceNote) noexcept
{
    if (latchedSourceNote < 0)
        return;

    for (auto& voice : Voices) {

        // every voice stays on its midi note, including ones fading during release
        if (voice.midiNote >= 0) {
            const auto interval = static_cast<float>(voice.midiNote - latchedSourceNote);

            voice.shifter.setIntervalSemitones(interval);
        }
    }
}

void VocalHarmonizerAudioProcessor::updateLatchedNote(float pitch, float confidence, int samplesElapsed) noexcept
{
    constexpr float minimumConfidence = 0.35f; // theshold
    const auto requiredSamples = juce::roundToInt(latchTimeMs * 0.001 * currentSampleRate);

    if (!std::isfinite(pitch) || pitch <= 0.0 || confidence < minimumConfidence) {

        candidateNote = -1;
        candidateSamples = 0;
        return; // keep last latched note during weak input
    }

    const auto midiNote = juce::jlimit(0, 127, juce::roundToInt(69.0 + 12.0 * std::log2(pitch / 440.0)));

    if (midiNote == latchedNote.load(std::memory_order_relaxed))
    {
        candidateNote = -1;
        candidateSamples = 0;
        return;
    }

    if (midiNote != candidateNote)
    {
        candidateNote = midiNote;
        candidateSamples = 0;
    }

    candidateSamples += samplesElapsed;

    if (candidateSamples >= requiredSamples)
    {
        const auto newLatchedNote = candidateNote;

        latchedPitch =440.0f * std::pow( 2.0f, (static_cast<float>(newLatchedNote) - 69.0f) / 12.0f);

        latchedNote.store(newLatchedNote, std::memory_order_relaxed);

        candidateNote = -1;
        candidateSamples = 0;
    }
}


void VocalHarmonizerAudioProcessor::updatePanGains(float spreadAmount, float movementDepth) noexcept
{
    constexpr auto quarterPi = juce::MathConstants<float>::pi * 0.25f;
    constexpr auto sqrt2 = juce::MathConstants<float>::sqrt2;

    // how many levels the layout needs, based on the highest slot still sounding
    int highestSlot = -1;
    for (std::size_t i = 0; i < maximumVoices; ++i)
        if (Voices[i].midiNote >= 0)
            highestSlot = static_cast<int>(i);

    const auto levels = std::max(minimumLayoutLevels, highestSlot / 2 + 1);

    // spread scales drift with pan and chorus, 100% movement gives full L-R range
    const auto movement = spreadAmount * movementDepth;

    for (std::size_t i = 0; i < maximumVoices; ++i)
    {

        auto& voice = Voices[i];
        

        // squared so pan drift is more eased in
        const auto attack = voice.panMovementAttack * voice.panMovementAttack;
        
        // slot 0 = R level 1, slot 1 = L level 1, slot 2 = R level 2, ...
        const auto side = (i % 2 == 0) ? 1.0f : -1.0f;
        const auto level = static_cast<float>(i / 2 + 1);
        const auto targetPan = side * outerPan * level / static_cast<float>(levels);
        
        // a voice that just started appears at its spot
        // existing ones glide when the layout changes
        if (voice.panMovementAttack <= 0.0f)
            voice.layoutPan = targetPan;
        else
            voice.layoutPan += layoutSmoothing * (targetPan - voice.layoutPan);


        // as the drift range grows, its center moves to the middle so it never runs off an edge;
        // at 100% movement it is the full width
        const auto slotPan = voice.layoutPan * spreadAmount;
        const auto drift = movement * attack;
        const auto driftCenter = slotPan * (1.0f - drift);

        const auto pan = std::clamp(driftCenter + drift * panWanderers[i].next(), -1.0f, 1.0f);

        voice.currentPan = pan;
        voice.currentBasePan = driftCenter;
        voice.currentMovementRange = drift;

        const auto angle = (pan + 1.0f) * quarterPi;            // 0 - pi/2
        

        voice.panMovementAttack = std::min(1.0f, voice.panMovementAttack + panAttackStep);
        
        // constant power panning
        voice.panLeft = sqrt2 * std::cos(angle);
        voice.panRight = sqrt2 * std::sin(angle);
    }
}

void VocalHarmonizerAudioProcessor::updateMovementRate() noexcept
{
    const auto speed = getMovementRate() / baseMovementRateHz;

    for (auto& wanderer : panWanderers)
        wanderer.setSpeed(speed);
}

void VocalHarmonizerAudioProcessor::updateJitter(float jitterAmount) noexcept
{
    const auto samplesPerMs = static_cast<float>(currentSampleRate * 0.001);

    for (std::size_t i = 0; i < maximumVoices; ++i)
    {
        auto& voice = Voices[i];

        // always advance so voices are already moving when jitter goes up
        const auto drift = pitchWanderers[i].next();   // -1 - 1
        const auto timing = timingWanderers[i].next(); // -1 - 1

        voice.shifter.setPitchDriftCents(jitterAmount * maximumDriftCents * drift);

        // 0 - maximumTimingMs late, never early (cant be ahead of input)
        voice.timingDelayTarget = jitterAmount * maximumTimingMs * (0.5f + 0.5f * timing) * samplesPerMs;
    }
}

//==============================================================================
bool VocalHarmonizerAudioProcessor::hasEditor() const
{
    return true; // (change this to false if you choose to not supply an editor)
}

juce::AudioProcessorEditor* VocalHarmonizerAudioProcessor::createEditor()
{
    return new VocalHarmonizerAudioProcessorEditor (*this);
}

//==============================================================================
void VocalHarmonizerAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void VocalHarmonizerAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml(getXmlFromBinary(data, sizeInBytes));

    if (xml != nullptr && xml->hasTagName(apvts.state.getType()))
        apvts.replaceState(juce::ValueTree::fromXml(*xml));
}

//==============================================================================
// This creates new instances of the plugin..
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new VocalHarmonizerAudioProcessor();
}
