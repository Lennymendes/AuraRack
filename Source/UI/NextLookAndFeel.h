// AuraRack — NextDSP · identité visuelle
// Sombre, minimaliste, entièrement vectoriel (aucune image, aucun skeuomorphisme).

#pragma once

#include "../DSP/AuraModuleDSP.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace aura::ui
{
namespace colours
{
    inline const juce::Colour background { 0xff0b0c10 };   // anthracite
    inline const juce::Colour panel      { 0xff13151b };
    inline const juce::Colour panelHigh  { 0xff1a1d25 };
    inline const juce::Colour stroke     { 0xff262a35 };
    inline const juce::Colour text       { 0xffe6e8ee };
    inline const juce::Colour textDim    { 0xff7b8294 };
    inline const juce::Colour reduction  { 0xffff8a5b };   // réduction de gain
    inline const juce::Colour levelOk    { 0xff3dd6c6 };
    inline const juce::Colour levelHot   { 0xffffd166 };
    inline const juce::Colour levelOver  { 0xffff5d73 };

    /** Couleur d'accent d'un type de module. */
    inline juce::Colour accentFor (ModuleType t)
    {
        switch (t)
        {
            case ModuleType::eq:    return juce::Colour (0xff6aa6ff);
            case ModuleType::comp:  return juce::Colour (0xff3dd6c6);
            case ModuleType::duck:  return juce::Colour (0xffff8a5b);
            case ModuleType::clip:  return juce::Colour (0xffff5d73);
            case ModuleType::limit: return juce::Colour (0xffffd166);
            case ModuleType::delay: return juce::Colour (0xffb28dff);
            case ModuleType::verb:  return juce::Colour (0xff7be0a1);
            case ModuleType::w9b:   return juce::Colour (0xff4a86ff);
            case ModuleType::none:  break;
        }
        return stroke;
    }
}

class NextLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    NextLookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height, float sliderPos,
                           float rotaryStartAngle, float rotaryEndAngle, juce::Slider&) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;
    void drawComboBox (juce::Graphics&, int width, int height, bool isButtonDown, int bx, int by, int bw, int bh, juce::ComboBox&) override;
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    juce::Font getLabelFont (juce::Label&) override;
    juce::Font getPopupMenuFont() override;
    void drawPopupMenuBackground (juce::Graphics&, int width, int height) override;
    juce::Label* createSliderTextBox (juce::Slider&) override;

    static juce::Font font (float height, bool bold = false);
};

} // namespace aura::ui
