#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "PluginProcessor.h"

class Tascam424AudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    explicit Tascam424AudioProcessorEditor(Tascam424AudioProcessor&);
    ~Tascam424AudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    Tascam424AudioProcessor& audioProcessor;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Tascam424AudioProcessorEditor)
};
