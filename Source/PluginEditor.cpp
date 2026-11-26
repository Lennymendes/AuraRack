// AuraRack — NextDSP · éditeur du plugin.

#include "PluginEditor.h"
#include "UI/I18n.h"

using namespace aura::ui;

//==============================================================================
AuraRackEditor::Content::Content (AuraRackProcessor& p)
    : processor (p), topBar (p), inspector (p), loudness (p), strip (p)
{
    addAndMakeVisible (topBar);
    addAndMakeVisible (inspector);
    addChildComponent (loudness);
    addAndMakeVisible (strip);
    strip.onSelect = [this] (int slot) { selectSlot (slot); };
    applySettings (processor.getUiSettings());
}

void AuraRackEditor::Content::selectSlot (int slot)
{
    auto u = processor.getUiSettings();
    u.selectedSlot = slot;
    processor.setUiSettings (u);
    applySettings (u);
}

void AuraRackEditor::Content::applySettings (const AuraRackProcessor::UiSettings& u)
{
    const bool first = ! settingsApplied;
    const bool languageChanged = first || u.language != shown.language;
    const bool infoChanged = first || u.info != shown.info;
    const bool lufsChanged = first || u.loudness != shown.loudness;
    const bool slotChanged = first || u.selectedSlot != shown.selectedSlot;
    shown = u;
    settingsApplied = true;

    if (languageChanged) { i18n::setLanguage (u.language); repaint(); }
    if (infoChanged)     { strip.setInfoMode (u.info); topBar.setInfoMode (u.info); }
    if (lufsChanged)     { loudness.setVisible (u.loudness); resized(); }
    if (slotChanged)     { inspector.setSelected (u.selectedSlot); strip.setSelected (u.selectedSlot); repaint (inspectorCaption); }
}

void AuraRackEditor::Content::tick (float dt)
{
    const auto u = processor.getUiSettings();
    if (! (u == shown)) applySettings (u);   // restauration d'état, ou changement venu du panneau ⚙️

    topBar.tick (dt);
    inspector.tick (dt);
    strip.tick (dt);
    if (loudness.isVisible()) loudness.tick (dt);
}

void AuraRackEditor::Content::paint (juce::Graphics& g)
{
    g.fillAll (colours::background);
    g.setColour (colours::textDim);
    g.setFont (NextLookAndFeel::font (10.0f, true));
    const auto type = processor.getSlotModule (shown.selectedSlot);
    auto title = i18n::tr (i18n::Key::inspector);
    if (type != aura::ModuleType::none)
        title += juce::String (juce::CharPointer_UTF8 ("   \xc2\xb7   ")) + aura::moduleInfo (type).name;
    g.drawText (title, inspectorCaption, juce::Justification::centredLeft);
    g.drawText (i18n::tr (i18n::Key::chain) + juce::String (juce::CharPointer_UTF8 ("   \xc2\xb7   ")) + i18n::tr (i18n::Key::dragToReorder), stripCaption, juce::Justification::centredLeft);
}

void AuraRackEditor::Content::resized()
{
    topBar.setBounds (0, 0, getWidth(), kTopBarHeight);
    auto area = getLocalBounds().withTrimmedTop (kTopBarHeight).reduced (14, 10);

    stripCaption = area.removeFromBottom (kStripHeight + 18).removeFromTop (18);   // légende, calculée avant de retirer la bande
    area = getLocalBounds().withTrimmedTop (kTopBarHeight).reduced (14, 10);
    strip.setBounds (area.removeFromBottom (kStripHeight));
    area.removeFromBottom (18);
    if (loudness.isVisible())
    {
        area.removeFromBottom (8);
        loudness.setBounds (area.removeFromBottom (LoudnessBar::kHeight));
    }
    area.removeFromBottom (6);
    inspectorCaption = area.removeFromTop (16);
    inspector.setBounds (area);
    repaint();
}

//==============================================================================
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
