#pragma once

#include <array>
#include <atomic>
#include <utility>
#include <vector>

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

class Tascam424AudioProcessor : public juce::AudioProcessor,
                                private juce::AudioProcessorValueTreeState::Listener
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
    void parameterChanged(const juce::String& parameterID, float newValue) override;
    void updateAutoLevelOutput(float gain);

    juce::dsp::IIR::Filter<float> inputCouplingHPF[2];

    // Kept dormant so old pre/de-emphasis code is not accidentally reintroduced.
    juce::dsp::IIR::Filter<float> preEmphasisFilter[2];
    juce::dsp::IIR::Filter<float> deEmphasisFilter[2];

    juce::dsp::IIR::Filter<float> eqLowFilter[2];
    juce::dsp::IIR::Filter<float> eqHighFilter[2];

    double currentSampleRate = 44100.0;

    // U101A transfer curve generated from the LTspice simulation.
    static constexpr int kTableSize = 2048;
    std::array<float, kTableSize> mTransferTable {};
    bool mTableLoaded = false;
    std::atomic<bool> mUpdatingOutputFromAutoLevel { false };

    static float saturate(float x, float gain);
    float saturateLUT(float x, float gain) const;
    void loadTransferTable(const std::vector<std::pair<float, float>>& spiceData);
    bool loadTransferTableFromCsv();
    void updateFilterCoefficients();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Tascam424AudioProcessor)
};
