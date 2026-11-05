#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "PitchDetector.h"

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

    // dry and wet buffer
    dryBuffer.setSize(1, samplesPerBlock, false, true, true);
    wetBuffer.setSize(1, samplesPerBlock, false, true, true);

    // wet mix
    wetMixSmoother.reset(sampleRate, 0.02); // 20 ms ramp
    wetMixSmoother.setCurrentAndTargetValue(getWetMix());

    // pitch detector
    pitchDetector.prepare (sampleRate);
    detectedPitchHz.store (0.0f, std::memory_order_relaxed);
    pitchConfidence.store (0.0f, std::memory_order_relaxed);

    latchedPitch = 0.0f;
    candidateNote = -1;
    candidateSamples = 0;
    latchedNote.store(-1, std::memory_order_relaxed);

    // pitch shifter
    pitchShifter.prepare(sampleRate);
    pitchShifter.setIntervalSemitones(7.0f); // TEMPORARY SET INTERVAL , REMOVE LATER
    const auto shifterLatency = pitchShifter.getLatencySamples();
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
    pitchShifter.reset();
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

    //
    for (const auto metadata : midiMessages)
    {
        const auto message = metadata.getMessage();
        const auto note = message.getNoteNumber();

        if (message.isNoteOn())
        {
            heldMidiNoteCounts[note].fetch_add(1, std::memory_order_relaxed);
        }
        else if (message.isNoteOff())
        {
            auto& count = heldMidiNoteCounts[note];
            const auto currentCount = count.load(std::memory_order_relaxed);

            if (currentCount > 0)
                count.store(currentCount - 1, std::memory_order_relaxed);
        }
    }

    // set detector gate
    const auto thresholdDb = pitchGateThresholdDb.load(std::memory_order_relaxed);
    const auto thresholdRms = std::pow(10.0f, thresholdDb * 0.05f);
    pitchDetector.setMinimumFrameRms(thresholdRms);
    

    dryBuffer.copyFrom(0, 0, buffer, 0, 0, numSamples); // full copy buffer to dryBuffer on channel 0

    const auto* dry = dryBuffer.getReadPointer(0);
    auto* wet = wetBuffer.getWritePointer(0);

    const auto targetWetMix = getWetMix();
    wetMixSmoother.setTargetValue(targetWetMix);
    
    for (int sample = 0; sample < numSamples; ++sample) {

        pitchDetector.processSample(dry[sample]);
        const auto detectorPitch = pitchDetector.getPitch();
        const auto detectorConfidence = pitchDetector.getConfidence();
        updateLatchedNote(detectorPitch, detectorConfidence, 1); // latch detected pitch

        dryBufferDelayed[dryDelayWritePosition] = dry[sample];

        const auto dryDelayReadPosition = (dryDelayWritePosition + 1) % dryBufferDelayed.size();
        const auto delayedDrySample = dryBufferDelayed[dryDelayReadPosition];
        dryDelayWritePosition = dryDelayReadPosition;


        // if latching set to 0ms, use straight from pitch detection
        const auto pitchForShifter = latchTimeMs.load(std::memory_order_relaxed) <= 0.0f ? detectorPitch : latchedPitch;

        wet[sample] = pitchShifter.processSample(dry[sample], pitchForShifter, detectorConfidence);

        const auto mix = wetMixSmoother.getNextValue();

        const auto outputSample = delayedDrySample * (1.0f - mix) + wet[sample] * mix;

        // send buffer back out with a mix of dry & wet
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
            buffer.setSample(channel, sample, outputSample); 
        
    }

    detectedPitchHz.store (pitchDetector.getPitch(), std::memory_order_relaxed);
    pitchConfidence.store (pitchDetector.getConfidence(), std::memory_order_relaxed);

}

void VocalHarmonizerAudioProcessor::updateLatchedNote(float pitch, float confidence, int samplesElapsed) noexcept
{
    constexpr float minimumConfidence = 0.5f;
    const auto requiredSamples = juce::roundToInt(latchTimeMs.load(std::memory_order_relaxed) * 0.001 * currentSampleRate);

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
