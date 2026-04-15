#include "PluginEditor.h"

Tascam424AudioProcessorEditor::Tascam424AudioProcessorEditor(Tascam424AudioProcessor& p)
    : AudioProcessorEditor(&p), audioProcessor(p)
{
    setSize(400, 300);
}

Tascam424AudioProcessorEditor::~Tascam424AudioProcessorEditor() = default;

void Tascam424AudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));
}

void Tascam424AudioProcessorEditor::resized()
{
}
