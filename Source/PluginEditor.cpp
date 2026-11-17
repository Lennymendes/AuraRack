// AuraRack — NextDSP · éditeur du plugin.

#include "PluginEditor.h"

AuraRackEditor::AuraRackEditor (AuraRackProcessor& p)
    : AudioProcessorEditor (&p), processor (p), content (p),
      vblank (this, [this] (double t) { onVBlank (t); })
{
    setLookAndFeel (&lookAndFeel);
    addAndMakeVisible (content);

    setResizable (true, true);
    setResizeLimits (kDesignWidth / 2, kDesignHeight / 2, kDesignWidth * 2, kDesignHeight * 2);
    if (auto* c = getConstrainer())
        c->setFixedAspectRatio ((double) kDesignWidth / (double) kDesignHeight);
    setSize (kDesignWidth, kDesignHeight);
}

AuraRackEditor::~AuraRackEditor()
{
    setLookAndFeel (nullptr);
}

void AuraRackEditor::paint (juce::Graphics& g)
{
    g.fillAll (aura::ui::colours::background);
}

void AuraRackEditor::resized()
{
    content.setTransform (juce::AffineTransform::scale ((float) getWidth() / (float) kDesignWidth));
    content.setBounds (0, 0, kDesignWidth, kDesignHeight);
}

void AuraRackEditor::onVBlank (double timestampSeconds)
{
    const float dt = lastTimestamp > 0.0 ? (float) juce::jlimit (0.0, 0.1, timestampSeconds - lastTimestamp) : 1.0f / 60.0f;
    lastTimestamp = timestampSeconds;
    content.tick (dt);
}
