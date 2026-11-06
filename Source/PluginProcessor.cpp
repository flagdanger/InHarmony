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
                       )
#endif
{
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

    // dry buffer
    dryBuffer.setSize(1, samplesPerBlock, false, true, true);

    // spread control
    spreadSmoother.reset(sampleRate, 0.05); // 50 ms
    spreadSmoother.setCurrentAndTargetValue(getSpread());
    updatePanGains(getSpread());
    
    // slow random pan drift, called once every 32 samples
    for (std::size_t i = 0; i < maximumVoices; ++i)
        panWanderers[i].prepare(sampleRate / panUpdateInterval, 5000u + static_cast<std::uint32_t>(i) * 97u, 2.0, 5.0);

    panUpdateCounter = 0;

    // 1.5s attack for pan drift, stepped once every 32 samples
    panAttackStep = static_cast<float>(panUpdateInterval / (1.5 * sampleRate));

    chorus.prepare(sampleRate);

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
    voiceTailSamples = static_cast<std::size_t>(std::round(0.1 * sampleRate));
    lastIntervalSourceNote = -1;

    for (auto& voice: Voices) {
        voice.shifter.prepare(sampleRate);
        voice.shifter.setFormantTracking(getFormantTracking());
        voice.shifter.setPitchSettlingTimeMs(getGlidingTimeMs());
        voice.shifter.setIntervalSemitones(0.0f);

        voice.midiNote = -1;
        voice.noteHeld = false;
        voice.tailSamplesRemaining = 0; // acount for release time
    }
    
    const auto shifterLatency = Voices[0].shifter.getLatencySamples();
    dryBufferDelayed.assign(shifterLatency + 1, 0.0f);
    dryDelayWritePosition = 0;
    setLatencySamples(static_cast<int>(shifterLatency));

    // clears midi piano
    for (auto& count : heldMidiNoteCounts) 
        count.store(0, std::memory_order_relaxed);

}

void VocalHarmonizerAudioProcessor::releaseResources()
{
    pitchDetector.reset();
    for (auto& voice : Voices) {
        voice.shifter.reset();
        voice.panMovementAttack = 0.0f;
    }

    chorus.reset();
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

    // handle midi
    for (const auto metadata : midiMessages)
        handleMidiMessage(metadata.getMessage());

    // set detector gate
    const auto thresholdDb = pitchGateThresholdDb.load(std::memory_order_relaxed);
    const auto thresholdRms = std::pow(10.0f, thresholdDb * 0.05f);
    pitchDetector.setMinimumFrameRms(thresholdRms);


    dryBuffer.copyFrom(0, 0, buffer, 0, 0, numSamples); // full copy buffer to dryBuffer on channel 0

    const auto* dry = dryBuffer.getReadPointer(0);

    // set spread
    spreadSmoother.setTargetValue(getSpread());

    // set volumes
    dryGainSmoother.setTargetValue(dbToGain(getDryGainDb()));
    wetGainSmoother.setTargetValue(dbToGain(getWetGainDb()));
    outputGainSmoother.setTargetValue(dbToGain(getOutputGainDb()));

    // set glide and formant
    for (auto& voice : Voices) {
        voice.shifter.setPitchSettlingTimeMs(getGlidingTimeMs());
        voice.shifter.setFormantTracking(getFormantTracking());
    }
    
    for (int sample = 0; sample < numSamples; ++sample) {

        pitchDetector.processSample(dry[sample]);
        const auto detectorPitch = pitchDetector.getPitch();
        const auto detectorConfidence = pitchDetector.getConfidence();
        updateLatchedNote(detectorPitch, detectorConfidence, 1); // latch detected pitch

        const auto currentLatchedNote = latchedNote.load(std::memory_order_relaxed);

        if (currentLatchedNote >= 0 && currentLatchedNote != lastIntervalSourceNote) {
            updateVoiceIntervals(currentLatchedNote);
            lastIntervalSourceNote = currentLatchedNote;
        }


        dryBufferDelayed[dryDelayWritePosition] = dry[sample];

        const auto dryDelayReadPosition = (dryDelayWritePosition + 1) % dryBufferDelayed.size();
        const auto delayedDrySample = dryBufferDelayed[dryDelayReadPosition];
        dryDelayWritePosition = dryDelayReadPosition;


        // always use 50ms latching time
        const auto pitchForShifter = (detectorPitch > 0.0f && latchedPitch > 0.0f) ? latchedPitch : detectorPitch;

        // calc pan gain only while spread is changing
        const auto currentSpread = spreadSmoother.getNextValue();

        // update pan movement at control rate (every 32 samples)
        if (++panUpdateCounter >= panUpdateInterval) {
            panUpdateCounter = 0;
            updatePanGains(currentSpread);
        }

        // sum up the voices
        float wetLeft = 0.0f;
        float wetRight = 0.0f;

        for (auto& voice : Voices) {
            if (voice.midiNote < 0)
                continue;

            const auto voiceConfidence = voice.noteHeld ? detectorConfidence : 0.0f;
            const auto voiceSample =  voice.shifter.processSample(dry[sample], pitchForShifter, voiceConfidence);

            wetLeft += voiceSample * voice.panLeft;
            wetRight += voiceSample * voice.panRight;

            // let voice exist until tail is over
            if (!voice.noteHeld && voice.tailSamplesRemaining > 0) {
                --voice.tailSamplesRemaining;

                if (voice.tailSamplesRemaining == 0)
                    voice.midiNote = -1;
            }
        }

        const auto dryGain = dryGainSmoother.getNextValue();
        const auto wetGain = wetGainSmoother.getNextValue();
        const auto outGain = outputGainSmoother.getNextValue();

        constexpr float voiceGain = 0.6f; // fixed voice headroom

        // chorus fades in from 33% spread, gets wetter past 80%
        float chorusAmount = 0.0f;
        if (currentSpread > 0.33f) {
            if (currentSpread < 0.8f) { // 0 -> 0.45
                chorusAmount = 0.45f * (currentSpread - 0.33f) / (0.8f - 0.33f);
            }
            else { // // 0.45 -> 0.8
                chorusAmount = 0.45f + 0.35f * (currentSpread - 0.8f) / (1.0f - 0.8f);
            }
        }

        // LFO only until 80% spread, then mix in random chorus
        const auto chorusRandomness = std::clamp((currentSpread - 0.8f) / 0.2f, 0.0f, 1.0f);
        float chorusLeft = 0.0f;
        float chorusRight = 0.0f;
        chorus.process(wetLeft * voiceGain, wetRight * voiceGain, chorusAmount, chorusRandomness, chorusLeft, chorusRight);


        const auto dryPart = delayedDrySample * dryGain; // dry stays centred
        const auto left = (dryPart + chorusLeft * wetGain) * outGain;
        const auto right = (dryPart + chorusRight * wetGain) * outGain;


        if (buffer.getNumChannels() >= 2) {
            buffer.setSample(0, sample, left);
            buffer.setSample(1, sample, right);

        } else {
            buffer.setSample(0, sample, 0.5f * (left + right)); // mono out, fold down
        }
        
    }

    detectedPitchHz.store (pitchDetector.getPitch(), std::memory_order_relaxed);
    pitchConfidence.store (pitchDetector.getConfidence(), std::memory_order_relaxed);

}

void VocalHarmonizerAudioProcessor::handleMidiMessage(const juce::MidiMessage& message) noexcept
{
    const auto note = message.getNoteNumber();

    if (message.isNoteOn()) {
        const auto previousCount = heldMidiNoteCounts[note].fetch_add(1, std::memory_order_relaxed);

        // if repeat note-on, do nothing
        if (previousCount > 0)
            return;

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

        freeVoice->midiNote = note;
        freeVoice->noteHeld = true;
        freeVoice->tailSamplesRemaining = 0;
        freeVoice->panMovementAttack = 0.0f;

        const auto sourceNote = latchedNote.load(std::memory_order_relaxed);

        const auto interval = sourceNote >= 0 ? static_cast<float>(note - sourceNote) : 0.0f;

        freeVoice->shifter.setIntervalSemitones(interval);
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

        for (auto& voice : Voices) {
            if (voice.midiNote == note && voice.noteHeld) {
                voice.noteHeld = false;
                voice.tailSamplesRemaining = voiceTailSamples;
                break;
            }
        }

    }
}

void VocalHarmonizerAudioProcessor::updateVoiceIntervals(int latchedSourceNote) noexcept
{
    if (latchedSourceNote < 0)
        return;

    for (auto& voice : Voices) {

        // freeze interval during note-off release time
        if (voice.midiNote >= 0 && voice.noteHeld) {
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


void VocalHarmonizerAudioProcessor::updatePanGains(float spreadAmount) noexcept
{
    constexpr auto quarterPi = juce::MathConstants<float>::pi * 0.25f;
    constexpr auto sqrt2 = juce::MathConstants<float>::sqrt2;

    // drift fades in above 50% spread, up to +-0.15 around the original position
    const auto movement = std::clamp((spreadAmount - 0.5f) * 2.0f, 0.0f, 1.0f) * 0.15f;

    for (std::size_t i = 0; i < maximumVoices; ++i)
    {

        auto& voice = Voices[i];
        voice.panMovementAttack = std::min(1.0f, voice.panMovementAttack + panAttackStep);

        // squared so pan drift is more eased in
        const auto attack = voice.panMovementAttack * voice.panMovementAttack;
        
        const auto pan = std::clamp(voicePanPositions[i] * spreadAmount + movement * attack * panWanderers[i].next(), -1.0f, 1.0f);
        const auto angle = (pan + 1.0f) * quarterPi;            // 0 - pi/2

        // constant power panning
        voice.panLeft = sqrt2 * std::cos(angle);
        voice.panRight = sqrt2 * std::sin(angle);
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
    // You should use this method to store your parameters in the memory block.
    // You could do that either as raw data, or use the XML or ValueTree classes
    // as intermediaries to make it easy to save and load complex data.
}

void VocalHarmonizerAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    // You should use this method to restore your parameters from this memory block,
    // whose contents will have been created by the getStateInformation() call.
}

//==============================================================================
// This creates new instances of the plugin..
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new VocalHarmonizerAudioProcessor();
}
