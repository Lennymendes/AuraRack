// AuraRack — NextDSP · identité visuelle

#include "NextLookAndFeel.h"

namespace aura::ui
{
using namespace colours;

juce::Font NextLookAndFeel::font (float height, bool bold)
{
    return juce::Font (juce::FontOptions (height, bold ? juce::Font::bold : juce::Font::plain));
}

NextLookAndFeel::NextLookAndFeel()
{
    setColour (juce::ResizableWindow::backgroundColourId, background);
    setColour (juce::Label::textColourId, text);
    setColour (juce::Slider::textBoxTextColourId, text);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::rotarySliderOutlineColourId, stroke);
    setColour (juce::ComboBox::backgroundColourId, panelHigh);
    setColour (juce::ComboBox::textColourId, text);
    setColour (juce::ComboBox::outlineColourId, stroke);
    setColour (juce::ComboBox::arrowColourId, textDim);
    setColour (juce::PopupMenu::backgroundColourId, panelHigh);
    setColour (juce::PopupMenu::textColourId, text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, stroke);
    setColour (juce::PopupMenu::highlightedTextColourId, text);
    setColour (juce::TextEditor::backgroundColourId, panelHigh);
    setColour (juce::TextEditor::textColourId, text);
    setColour (juce::TextEditor::outlineColourId, stroke);
    setColour (juce::TextEditor::focusedOutlineColourId, levelOk);
}

void NextLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float pos,
                                        float startAngle, float endAngle, juce::Slider& slider)
{
    const auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) width, (float) height).reduced (3.0f);
    const float diameter = std::min (bounds.getWidth(), bounds.getHeight());
    const auto centre = bounds.getCentre();
    const float radius = diameter * 0.5f;
    const float thickness = std::max (2.0f, diameter * 0.09f);
    const float arcRadius = radius - thickness * 0.5f;
    const auto accent = slider.findColour (juce::Slider::rotarySliderFillColourId);
    const bool active = slider.isEnabled();

    juce::Path track;
    track.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, startAngle, endAngle, true);
    g.setColour (slider.findColour (juce::Slider::rotarySliderOutlineColourId));
    g.strokePath (track, juce::PathStrokeType (thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Arc de valeur : depuis le zéro visuel (centre pour les plages bipolaires) jusqu'à la valeur.
    const float angle = startAngle + pos * (endAngle - startAngle);
    const bool bipolar = slider.getMinimum() < 0.0 && slider.getMaximum() > 0.0;
    const float from = bipolar ? startAngle + (float) ((0.0 - slider.getMinimum()) / (slider.getMaximum() - slider.getMinimum())) * (endAngle - startAngle)
                               : startAngle;
    juce::Path value;
    value.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, std::min (from, angle), std::max (from, angle), true);
    g.setColour (active ? accent : textDim);
    g.strokePath (value, juce::PathStrokeType (thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Aiguille.
    const float inner = radius * 0.28f, outer = radius * 0.66f;
    const juce::Line<float> needle (centre.x + std::sin (angle) * inner, centre.y - std::cos (angle) * inner,
                                    centre.x + std::sin (angle) * outer, centre.y - std::cos (angle) * outer);
    g.setColour (text);
    g.drawLine (needle, std::max (1.5f, thickness * 0.5f));
}

void NextLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool highlighted, bool)
{
    const auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    const bool on = b.getToggleState();
    const auto accent = b.findColour (juce::ToggleButton::tickColourId);
    g.setColour (on ? accent.withAlpha (0.18f) : panelHigh);
    g.fillRoundedRectangle (r, 5.0f);
    g.setColour (on ? accent : (highlighted ? textDim : stroke));
    g.drawRoundedRectangle (r, 5.0f, 1.0f);
    g.setColour (on ? accent : textDim);
    g.setFont (font (11.0f, true));
    g.drawText (b.getButtonText(), b.getLocalBounds(), juce::Justification::centred);
}

void NextLookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool, int, int, int, int, juce::ComboBox& box)
{
    const auto r = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height).reduced (0.5f);
    g.setColour (findColour (juce::ComboBox::backgroundColourId));
    g.fillRoundedRectangle (r, 5.0f);
    g.setColour (findColour (juce::ComboBox::outlineColourId));
    g.drawRoundedRectangle (r, 5.0f, 1.0f);

    juce::Path arrow;
    const float cx = (float) width - 12.0f, cy = (float) height * 0.5f;
    arrow.addTriangle (cx - 3.5f, cy - 1.5f, cx + 3.5f, cy - 1.5f, cx, cy + 2.5f);
    g.setColour (box.findColour (juce::ComboBox::arrowColourId));
    g.fillPath (arrow);
}

void NextLookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    label.setBounds (8, 1, box.getWidth() - 24, box.getHeight() - 2);
    label.setFont (getComboBoxFont (box));
}

juce::Font NextLookAndFeel::getComboBoxFont (juce::ComboBox&) { return font (12.5f, true); }
juce::Font NextLookAndFeel::getLabelFont (juce::Label& l)     { return l.getFont(); }
juce::Font NextLookAndFeel::getPopupMenuFont()                { return font (13.0f); }

juce::Label* NextLookAndFeel::createSliderTextBox (juce::Slider& slider)
{
    auto* l = new juce::Label();
    l->setJustificationType (juce::Justification::centred);
    l->setFont (font (11.5f, true));
    l->setColour (juce::Label::textColourId, text);
    l->setColour (juce::Label::outlineColourId, juce::Colours::transparentBlack);
    l->setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    l->setColour (juce::Label::outlineWhenEditingColourId, levelOk);
    l->setColour (juce::TextEditor::textColourId, text);
    l->setColour (juce::TextEditor::backgroundColourId, panelHigh);
    l->setBorderSize ({ 0, 0, 0, 0 });
    l->setInterceptsMouseClicks (slider.isEnabled(), slider.isEnabled());
    return l;
}

void NextLookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int width, int height)
{
    g.fillAll (panelHigh);
    g.setColour (stroke);
    g.drawRect (0, 0, width, height, 1);
}

} // namespace aura::ui
