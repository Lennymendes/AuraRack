// AuraRack — NextDSP · panneau ⚙️ Réglages (ouvert en bulle depuis la barre du haut)
//
// Langue (menu), aide contextuelle (interrupteur), mesure LUFS / mode mastering (interrupteur) + remise à zéro.
// Le panneau ne garde aucun état : il lit et écrit AuraRackProcessor::UiSettings, que l'éditeur applique à toute l'interface.

#pragma once

#include "../PluginProcessor.h"
#include "I18n.h"
#include "NextLookAndFeel.h"

namespace aura::ui
{
class SettingsPanel final : public juce::Component
{
public:
    static constexpr int kWidth = 320, kHeight = 214;

    explicit SettingsPanel (AuraRackProcessor& p) : processor (p)
    {
        for (int i = 0; i < (int) i18n::Lang::count; ++i)
            languageBox.addItem (juce::String (juce::CharPointer_UTF8 (i18n::kLanguageNames[(std::size_t) i])), i + 1);
        const auto u = processor.getUiSettings();
        languageBox.setSelectedId (u.language + 1, juce::dontSendNotification);
        infoToggle.setToggleState (u.info, juce::dontSendNotification);
        lufsToggle.setToggleState (u.loudness, juce::dontSendNotification);

        languageBox.onChange = [this] { apply ([this] (auto& s) { s.language = languageBox.getSelectedId() - 1; }); };
        infoToggle.onClick = [this] { apply ([this] (auto& s) { s.info = infoToggle.getToggleState(); }); };
        lufsToggle.onClick = [this] { apply ([this] (auto& s) { s.loudness = lufsToggle.getToggleState(); }); };
        resetButton.onClick = [this] { processor.resetLoudness(); };

        for (auto* c : std::initializer_list<juce::Component*> { &languageBox, &infoToggle, &lufsToggle, &resetButton })
            addAndMakeVisible (*c);
        refreshTexts();
        setLookAndFeel (&lnf);
        setSize (kWidth, kHeight);
    }

    ~SettingsPanel() override { setLookAndFeel (nullptr); }

    void resized() override
    {
        auto r = getLocalBounds().reduced (18, 14);
        r.removeFromTop (22);                                   // titre
        r.removeFromTop (8);
        languageArea = r.removeFromTop (30);
        languageBox.setBounds (languageArea.withTrimmedLeft (110));
        r.removeFromTop (12);
        infoToggle.setBounds (r.removeFromTop (28));
        r.removeFromTop (8);
        lufsToggle.setBounds (r.removeFromTop (28));
        r.removeFromTop (8);
        resetButton.setBounds (r.removeFromTop (28).withTrimmedLeft (110));
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (colours::panel);
        g.setColour (colours::text);
        g.setFont (NextLookAndFeel::font (14.0f, true));
        g.drawText (juce::String (juce::CharPointer_UTF8 ("\xe2\x9a\x99  ")) + i18n::tr (i18n::Key::settings), 18, 12, getWidth() - 36, 22, juce::Justification::centredLeft);
        g.setColour (colours::textDim);
        g.setFont (NextLookAndFeel::font (11.5f, true));
        g.drawText (i18n::tr (i18n::Key::language), languageArea.withWidth (100), juce::Justification::centredLeft);
    }

private:
    template <typename Fn>
    void apply (Fn&& fn)
    {
        auto u = processor.getUiSettings();
        fn (u);
        processor.setUiSettings (u);
        if (u.language != i18n::getLanguageIndex()) { i18n::setLanguage (u.language); refreshTexts(); repaint(); }
    }

    void refreshTexts()
    {
        infoToggle.setButtonText (i18n::tr (i18n::Key::infoHelp));
        lufsToggle.setButtonText (i18n::tr (i18n::Key::lufsMeter));
        resetButton.setButtonText (i18n::tr (i18n::Key::lufsReset) + " LUFS");
    }

    NextLookAndFeel lnf;   // la bulle vit dans sa propre fenêtre : elle porte son propre style
    AuraRackProcessor& processor;
    juce::ComboBox languageBox;
    juce::ToggleButton infoToggle, lufsToggle;
    juce::TextButton resetButton;
    juce::Rectangle<int> languageArea;
};

} // namespace aura::ui
