#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <cmath>

Tascam424AudioProcessor::Tascam424AudioProcessor()
    : AudioProcessor(BusesProperties().withInput("Input", juce::AudioChannelSet::stereo(), true)
                                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "Parameters", createParameters())
{
}

Tascam424AudioProcessor::~Tascam424AudioProcessor() = default;

const juce::String Tascam424AudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool Tascam424AudioProcessor::acceptsMidi() const
{
    return false;
}

bool Tascam424AudioProcessor::producesMidi() const
{
    return false;
}

bool Tascam424AudioProcessor::isMidiEffect() const
{
    return false;
}

double Tascam424AudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int Tascam424AudioProcessor::getNumPrograms()
{
    return 1;
}

int Tascam424AudioProcessor::getCurrentProgram()
{
    return 0;
}

void Tascam424AudioProcessor::setCurrentProgram(int index)
{
    juce::ignoreUnused(index);
}

const juce::String Tascam424AudioProcessor::getProgramName(int index)
{
    juce::ignoreUnused(index);
    return {};
}

void Tascam424AudioProcessor::changeProgramName(int index, const juce::String& newName)
{
    juce::ignoreUnused(index, newName);
}

void Tascam424AudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;

    juce::dsp::ProcessSpec spec;
    spec.sampleRate = sampleRate;
    spec.maximumBlockSize = static_cast<juce::uint32>(samplesPerBlock);
    spec.numChannels = 1;

    for (int ch = 0; ch < 2; ++ch)
    {
        preEmphasisFilter[ch].reset();
        deEmphasisFilter[ch].reset();
        eqLowFilter[ch].reset();
        eqHighFilter[ch].reset();

        preEmphasisFilter[ch].prepare(spec);
        deEmphasisFilter[ch].prepare(spec);
        eqLowFilter[ch].prepare(spec);
        eqHighFilter[ch].prepare(spec);
    }

    updateFilterCoefficients();
}

void Tascam424AudioProcessor::releaseResources()
{
}

#ifndef JucePlugin_PreferredChannelConfigurations
bool Tascam424AudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
        && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;

    return true;
}
#endif

void Tascam424AudioProcessor::updateFilterCoefficients()
{
    // C102 pre-emphasis: high shelf boost at 3.4kHz before waveshaper
    // Models feedback loop weakening above 3.4kHz in U101a
    // Boost amount scales with GAIN1 - harder drive = more high freq emphasis
    const float gain1 = apvts.getRawParameterValue("GAIN1")->load();
    const float preEmphasisGaindB = gain1 * 2.0f;

    auto preEmphCoeffs = juce::dsp::IIR::Coefficients<float>::makeHighShelf(
        currentSampleRate, 3400.0f, 0.707f,
        juce::Decibels::decibelsToGain(preEmphasisGaindB));

    // De-emphasis: exact inverse of pre-emphasis to restore balance
    auto deEmphCoeffs = juce::dsp::IIR::Coefficients<float>::makeHighShelf(
        currentSampleRate, 3400.0f, 0.707f,
        juce::Decibels::decibelsToGain(-preEmphasisGaindB));

    // EQ LOW: low shelf at 100Hz
    // Component values from schematic: R108 100k, C106 0.015uF
    const float eqLowGaindB = apvts.getRawParameterValue("EQ_LOW")->load();
    auto eqLowCoeffs = juce::dsp::IIR::Coefficients<float>::makeLowShelf(
        currentSampleRate, 100.0f, 0.707f,
        juce::Decibels::decibelsToGain(eqLowGaindB));

    // EQ HIGH: high shelf at 10kHz
    // Component values from schematic: R107 100k, C105 0.0018uF
    const float eqHighGaindB = apvts.getRawParameterValue("EQ_HIGH")->load();
    auto eqHighCoeffs = juce::dsp::IIR::Coefficients<float>::makeHighShelf(
        currentSampleRate, 10000.0f, 0.707f,
        juce::Decibels::decibelsToGain(eqHighGaindB));

    for (int ch = 0; ch < 2; ++ch)
    {
        preEmphasisFilter[ch].coefficients = preEmphCoeffs;
        deEmphasisFilter[ch].coefficients = deEmphCoeffs;
        eqLowFilter[ch].coefficients = eqLowCoeffs;
        eqHighFilter[ch].coefficients = eqHighCoeffs;
    }
}

float Tascam424AudioProcessor::saturate(float x, float gain1)
{
    // Map gain1 (0-10) to actual voltage gain of U101a
    // Real circuit: Gain = Rf/Rin = 470k / 10k = 47x at max TRIM
    // We scale smoothly from 1x (clean) to 47x (full saturation)
    const float voltageGain = 1.0f + gain1 * 4.6f;

    // Phase inversion - U101a is an inverting amplifier
    const float inverted = -x * voltageGain;

    // Asymmetric tanh - NPN UPC4570 clips positive and negative
    // half cycles differently
    float shaped;
    if (inverted > 0.0f)
        shaped = std::tanh(inverted * 1.0f);
    else
        shaped = std::tanh(inverted * 1.2f);

    // Output compensation - maintain consistent perceived loudness
    const float compensation = 1.0f / (1.0f + gain1 * 0.15f);
    return shaped * compensation;
}

void Tascam424AudioProcessor::loadTransferTable(const std::vector<std::pair<float, float>>& spiceData)
{
    // spiceData is a vector of (inputVoltage, outputVoltage) pairs
    // from an LTspice DC sweep of the 2SD1450 input stage.
    // We normalise the input range to +-1 and map it to table indices.
    if (spiceData.empty())
        return;

    const float vMin = spiceData.front().first;
    const float vMax = spiceData.back().first;
    const float vRange = vMax - vMin;

    // Find output min/max for normalisation
    const float outMin = spiceData.front().second;
    const float outMax = spiceData.back().second;
    const float outRange = outMax - outMin;

    if (vRange == 0.0f || outRange == 0.0f || spiceData.size() < 2)
        return;

    for (int i = 0; i < kTableSize; ++i)
    {
        // Map table index to input voltage
        const float v = vMin + (static_cast<float>(i) / static_cast<float>(kTableSize - 1)) * vRange;

        // Linear interpolation into spice data
        float normV = (v - vMin) / vRange * static_cast<float>(spiceData.size() - 1);
        int idx = static_cast<int>(normV);
        const float frac = normV - static_cast<float>(idx);
        idx = juce::jlimit(0, static_cast<int>(spiceData.size()) - 2, idx);

        const float out = spiceData[static_cast<std::size_t>(idx)].second
                        + frac * (spiceData[static_cast<std::size_t>(idx + 1)].second
                                  - spiceData[static_cast<std::size_t>(idx)].second);

        // Normalise output to +-1 range
        mTransferTable[static_cast<std::size_t>(i)] = (out - outMin) / outRange * 2.0f - 1.0f;
    }

    mTableLoaded = true;
}

float Tascam424AudioProcessor::saturateLUT(float x, float gain1) const
{
    if (!mTableLoaded)
        return saturate(x, gain1); // fall back to tanh if no table loaded

    const float driveGain = std::exp(gain1 * 0.5f);
    const float input = juce::jlimit(-1.0f, 1.0f, x * driveGain);

    // Map +-1 input range to table index
    float normInput = (input + 1.0f) * 0.5f * static_cast<float>(kTableSize - 1);
    int idx = static_cast<int>(normInput);
    const float frac = normInput - static_cast<float>(idx);
    idx = juce::jlimit(0, kTableSize - 2, idx);

    // Linear interpolation between adjacent table entries
    const float out = mTransferTable[static_cast<std::size_t>(idx)]
                    + frac * (mTransferTable[static_cast<std::size_t>(idx + 1)]
                              - mTransferTable[static_cast<std::size_t>(idx)]);

    // Output compensation
    return out / std::tanh(driveGain);
}

void Tascam424AudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    // Read parameters
    const float gain1 = apvts.getRawParameterValue("GAIN1")->load();
    const float gain2 = juce::Decibels::decibelsToGain(
        apvts.getRawParameterValue("GAIN2")->load());

    // Update filter coefficients if parameters changed
    // Note: in production move this to a parameter listener
    // to avoid calling every block
    updateFilterCoefficients();

    const auto totalNumInputChannels = getTotalNumInputChannels();
    const auto totalNumOutputChannels = getTotalNumOutputChannels();

    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear(i, 0, buffer.getNumSamples());

    for (int channel = 0; channel < totalNumInputChannels; ++channel)
    {
        auto* channelData = buffer.getWritePointer(channel);
        const int ch = juce::jlimit(0, 1, channel);

        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
        {
            float x = channelData[sample];

            // STAGE 1: Pre-emphasis (C102 frequency-dependent saturation)
            x = preEmphasisFilter[ch].processSample(x);

            // STAGE 2: UPC4570 saturation (U101a)
            x = mTableLoaded ? saturateLUT(x, gain1) : saturate(x, gain1);

            // STAGE 3: De-emphasis (restore frequency balance)
            x = deEmphasisFilter[ch].processSample(x);

            // STAGE 4: EQ LOW shelf (U102b - 100Hz)
            x = eqLowFilter[ch].processSample(x);

            // STAGE 5: EQ HIGH shelf (U102b - 10kHz)
            x = eqHighFilter[ch].processSample(x);

            // STAGE 6: GAIN2 channel fader
            x *= gain2;

            channelData[sample] = x;
        }
    }
}

juce::AudioProcessorEditor* Tascam424AudioProcessor::createEditor()
{
    return new juce::GenericAudioProcessorEditor(*this);
}

bool Tascam424AudioProcessor::hasEditor() const
{
    return true;
}

void Tascam424AudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    if (auto state = apvts.copyState(); state.isValid())
    {
        std::unique_ptr<juce::XmlElement> xml(state.createXml());
        copyXmlToBinary(*xml, destData);
    }
}

void Tascam424AudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));

    if (xmlState != nullptr)
        if (xmlState->hasTagName(apvts.state.getType()))
            apvts.replaceState(juce::ValueTree::fromXml(*xmlState));
}

juce::AudioProcessorValueTreeState::ParameterLayout Tascam424AudioProcessor::createParameters()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    // GAIN1: controls U101a closed-loop gain
    // 0 = minimum gain (clean), 10 = maximum gain (heavy saturation)
    // Maps to real TRIM pot range of the feedback network
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("GAIN1", 1), "Gain 1",
        juce::NormalisableRange<float>(0.0f, 10.0f, 0.01f),
        3.0f));

    // GAIN2: channel fader output level +/-20dB
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("GAIN2", 1), "Gain 2",
        juce::NormalisableRange<float>(-20.0f, 20.0f, 0.1f),
        0.0f));

    // EQ LOW: 100Hz shelving filter +/-10dB
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("EQ_LOW", 1), "Bass",
        juce::NormalisableRange<float>(-10.0f, 10.0f, 0.1f),
        0.0f));

    // EQ HIGH: 10kHz shelving filter +/-10dB
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("EQ_HIGH", 1), "Treble",
        juce::NormalisableRange<float>(-10.0f, 10.0f, 0.1f),
        0.0f));

    return { params.begin(), params.end() };
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new Tascam424AudioProcessor();
}
