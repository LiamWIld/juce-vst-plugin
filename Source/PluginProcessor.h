#pragma once

#include <array>
#include <utility>
#include <vector>

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

class Tascam424AudioProcessor : public juce::AudioProcessor
{
public:
    Tascam424AudioProcessor();
    ~Tascam424AudioProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

#ifndef JucePlugin_PreferredChannelConfigurations
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
#endif

    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int index, const juce::String& newName) override;

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;

private:
    juce::AudioProcessorValueTreeState::ParameterLayout createParameters();

    // Pre/de-emphasis filters for C102 frequency-dependent saturation
    // One per channel (stereo)
    juce::dsp::IIR::Filter<float> preEmphasisFilter[2];
    juce::dsp::IIR::Filter<float> deEmphasisFilter[2];

    // EQ biquad filters - one per channel
    juce::dsp::IIR::Filter<float> eqLowFilter[2];
    juce::dsp::IIR::Filter<float> eqHighFilter[2];

    // Current sample rate - needed for filter coefficient calculation
    double currentSampleRate = 44100.0;

    // Lookup table infrastructure (already exists, keep it)
    static constexpr int kTableSize = 4096;
    std::array<float, kTableSize> mTransferTable {};
    bool mTableLoaded = false;

    // Private methods
    static float saturate(float x, float gain1);
    float saturateLUT(float x, float gain1) const;
    void loadTransferTable(const std::vector<std::pair<float, float>>& spiceData);
    void updateFilterCoefficients();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Tascam424AudioProcessor)
};
