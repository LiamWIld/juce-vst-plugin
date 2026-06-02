#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "BinaryData.h"

#include <algorithm>
#include <cmath>

Tascam424AudioProcessor::Tascam424AudioProcessor()
    : AudioProcessor(BusesProperties().withInput("Input", juce::AudioChannelSet::stereo(), true)
                                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "Parameters", createParameters())
{
    loadTransferTableFromCsv();
    apvts.addParameterListener("GAIN1", this);
    apvts.addParameterListener("AUTO_LEVEL", this);
}

Tascam424AudioProcessor::~Tascam424AudioProcessor()
{
    apvts.removeParameterListener("GAIN1", this);
    apvts.removeParameterListener("AUTO_LEVEL", this);
}

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
        inputCouplingHPF[ch].reset();
        preEmphasisFilter[ch].reset();
        deEmphasisFilter[ch].reset();
        eqLowFilter[ch].reset();
        eqHighFilter[ch].reset();

        inputCouplingHPF[ch].prepare(spec);
        preEmphasisFilter[ch].prepare(spec);
        deEmphasisFilter[ch].prepare(spec);
        eqLowFilter[ch].prepare(spec);
        eqHighFilter[ch].prepare(spec);
    }

    if (!mTableLoaded)
        loadTransferTableFromCsv();

    updateFilterCoefficients();
}

void Tascam424AudioProcessor::releaseResources()
{
}

void Tascam424AudioProcessor::parameterChanged(const juce::String& parameterID, float newValue)
{
    if (mUpdatingOutputFromAutoLevel)
        return;

    if (parameterID == "GAIN1")
    {
        if (apvts.getRawParameterValue("AUTO_LEVEL")->load() >= 0.5f)
            updateAutoLevelOutput(newValue);
    }
    else if (parameterID == "AUTO_LEVEL" && newValue >= 0.5f)
    {
        updateAutoLevelOutput(apvts.getRawParameterValue("GAIN1")->load());
    }
}

void Tascam424AudioProcessor::updateAutoLevelOutput(float gain)
{
    auto* outputParam = apvts.getParameter("GAIN2");
    if (outputParam == nullptr)
        return;

    const float output = juce::jlimit(0.0f, 10.0f, 10.0f - (gain * 0.625f));
    const float normalisedOutput = outputParam->convertTo0to1(output);

    mUpdatingOutputFromAutoLevel = true;
    outputParam->setValueNotifyingHost(normalisedOutput);
    mUpdatingOutputFromAutoLevel = false;
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
    // AC-coupled input behavior before the preamp transfer stage.
    auto inputCouplingCoeffs = juce::dsp::IIR::Coefficients<float>::makeHighPass(
        currentSampleRate, 20.0f, 0.707f);

    for (int ch = 0; ch < 2; ++ch)
        inputCouplingHPF[ch].coefficients = inputCouplingCoeffs;
}

float Tascam424AudioProcessor::saturate(float x, float gain)
{
    // Fallback curve used only when the embedded LTspice LUT is unavailable.
    const float voltageGain = std::pow(47.0f, gain / 10.0f);

    const float inverted = -x * voltageGain;

    float shaped;
    if (inverted > 0.0f)
        shaped = std::tanh(inverted * 1.0f);
    else
        shaped = std::tanh(inverted * 1.2f);

    const float compensation = 1.0f / (1.0f + gain * 0.15f);
    return shaped * compensation;
}

void Tascam424AudioProcessor::loadTransferTable(const std::vector<std::pair<float, float>>& spiceData)
{
    if (spiceData.size() < 2)
        return;

    const float inMin = spiceData.front().first;
    const float inMax = spiceData.back().first;
    const float inRange = inMax - inMin;

    if (inRange <= 0.0f)
        return;

    std::size_t sourceIndex = 0;

    for (int i = 0; i < kTableSize; ++i)
    {
        const float input = -1.0f + 2.0f * static_cast<float>(i) / static_cast<float>(kTableSize - 1);

        while (sourceIndex + 2 < spiceData.size()
               && spiceData[sourceIndex + 1].first < input)
        {
            ++sourceIndex;
        }

        const auto& a = spiceData[sourceIndex];
        const auto& b = spiceData[std::min(sourceIndex + 1, spiceData.size() - 1)];
        const float range = b.first - a.first;
        const float frac = range > 0.0f ? juce::jlimit(0.0f, 1.0f, (input - a.first) / range) : 0.0f;
        const float output = a.second + frac * (b.second - a.second);

        mTransferTable[static_cast<std::size_t>(i)] = juce::jlimit(-1.0f, 1.0f, output);
    }

    mTableLoaded = true;
}

bool Tascam424AudioProcessor::loadTransferTableFromCsv()
{
    if (BinaryData::u101a_transfer_lut_csv == nullptr || BinaryData::u101a_transfer_lut_csvSize <= 0)
    {
        DBG("fourtwofour: embedded LUT missing");
        return false;
    }

    juce::StringArray lines;
    lines.addLines(juce::String::fromUTF8(BinaryData::u101a_transfer_lut_csv,
                                          BinaryData::u101a_transfer_lut_csvSize));

    std::vector<std::pair<float, float>> points;
    points.reserve(static_cast<std::size_t>(lines.size()));

    for (const auto& rawLine : lines)
    {
        const auto line = rawLine.trim();

        if (line.isEmpty() || line.startsWithIgnoreCase("input_norm"))
            continue;

        const auto comma = line.indexOfChar(',');
        if (comma < 0)
            continue;

        const float input = line.substring(0, comma).trim().getFloatValue();
        const float output = line.substring(comma + 1).trim().getFloatValue();

        points.emplace_back(juce::jlimit(-1.0f, 1.0f, input),
                            juce::jlimit(-1.0f, 1.0f, output));
    }

    if (points.size() < 2)
    {
        DBG("fourtwofour: embedded LUT parse failed (" << points.size() << " points)");
        return false;
    }

    loadTransferTable(points);

    if (!mTableLoaded)
        DBG("fourtwofour: embedded LUT load failed (" << points.size() << " points)");

    return mTableLoaded;
}

float Tascam424AudioProcessor::saturateLUT(float x, float gain) const
{
    if (!mTableLoaded)
        return saturate(x, gain);

    // Gain sets drive into the U101A transfer curve generated from LTspice.
    const float driveGain = std::pow(47.0f, gain / 10.0f);
    const float input = juce::jlimit(-1.0f, 1.0f, x * driveGain);

    float normInput = (input + 1.0f) * 0.5f * static_cast<float>(kTableSize - 1);
    int idx = static_cast<int>(normInput);
    const float frac = normInput - static_cast<float>(idx);
    idx = juce::jlimit(0, kTableSize - 2, idx);

    const float out = mTransferTable[static_cast<std::size_t>(idx)]
                    + frac * (mTransferTable[static_cast<std::size_t>(idx + 1)]
                              - mTransferTable[static_cast<std::size_t>(idx)]);

    const float compensation = 1.0f / (1.0f + gain * 0.15f);
    return out * compensation;
}

void Tascam424AudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const float gain = apvts.getRawParameterValue("GAIN1")->load();
    const float output = apvts.getRawParameterValue("GAIN2")->load() / 10.0f;

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

            // AC-coupled input behavior before the preamp transfer stage.
            x = inputCouplingHPF[ch].processSample(x);

            // U101A transfer curve generated from the LTspice simulation.
            x = mTableLoaded ? saturateLUT(x, gain) : saturate(x, gain);

            // Final output trim.
            x *= output;

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

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("GAIN1", 1), "Gain 1",
        juce::NormalisableRange<float>(0.0f, 10.0f, 0.01f),
        3.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("GAIN2", 1), "Output",
        juce::NormalisableRange<float>(0.0f, 10.0f, 0.01f),
        10.0f));

    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID("AUTO_LEVEL", 1), "Auto Level",
        false));

    return { params.begin(), params.end() };
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new Tascam424AudioProcessor();
}
