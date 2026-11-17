// AuraRack — NextDSP · composants élémentaires de l'interface (potentiomètre, historique de réduction, niveaux).

#pragma once

#include "NextLookAndFeel.h"

#include <array>
#include <cmath>

namespace aura::ui
{
//==============================================================================
/** Potentiomètre lié à un paramètre : nom au-dessus, valeur (texte du paramètre) en dessous. */
class ParamKnob final : public juce::Component
{
public:
    ParamKnob()
    {
        slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
        slider.setMouseDragSensitivity (180);
        addAndMakeVisible (slider);

        nameLabel.setJustificationType (juce::Justification::centred);
        nameLabel.setFont (NextLookAndFeel::font (10.5f, true));
        nameLabel.setColour (juce::Label::textColourId, colours::textDim);
        nameLabel.setInterceptsMouseClicks (false, false);
        addAndMakeVisible (nameLabel);

        valueLabel.setJustificationType (juce::Justification::centred);
        valueLabel.setFont (NextLookAndFeel::font (11.5f, true));
        valueLabel.setColour (juce::Label::textColourId, colours::text);
        valueLabel.setInterceptsMouseClicks (false, false);
        addAndMakeVisible (valueLabel);
        slider.onValueChange = [this] { valueLabel.setText (slider.getTextFromValue (slider.getValue()), juce::dontSendNotification); };
    }

    /** Lie le potentiomètre au paramètre (une seule fois : la liaison suit ensuite le paramètre). */
    void bind (juce::RangedAudioParameter& parameter)
    {
        attachment = std::make_unique<juce::SliderParameterAttachment> (parameter, slider, nullptr);
        slider.onValueChange();
    }

    void setName (const juce::String& n) { nameLabel.setText (n.toUpperCase(), juce::dontSendNotification); }
    void setAccent (juce::Colour c)      { slider.setColour (juce::Slider::rotarySliderFillColourId, c); repaint(); }

    void resized() override
    {
        auto r = getLocalBounds();
        nameLabel.setBounds (r.removeFromTop (12));
        valueLabel.setBounds (r.removeFromBottom (14));
        slider.setBounds (r);
    }

private:
    juce::Slider slider;
    juce::Label nameLabel, valueLabel;
    std::unique_ptr<juce::SliderParameterAttachment> attachment;
};

//==============================================================================
/** Historique glissant de la réduction de gain (3 s à 60 images/s) : courbe pleine qui descend depuis le haut. */
class GainReductionScope final : public juce::Component
{
public:
    static constexpr int kHistory = 180;

    GainReductionScope() { setInterceptsMouseClicks (false, false); history.fill (0.0f); }

    /** Ajoute un échantillon (dB, >= 0). Redessine seulement si quelque chose a changé ou bouge encore. */
    void push (float reductionDb)
    {
        const bool changed = reductionDb > 0.005f || latest > 0.005f || peakInWindow > 0.005f;
        history[(std::size_t) head] = reductionDb;
        head = (head + 1) % kHistory;
        latest = reductionDb;
        if (--recomputeIn <= 0)
        {
            recomputeIn = 30;
            peakInWindow = *std::max_element (history.begin(), history.end());
        }
        if (changed) repaint();
    }

    void setAccent (juce::Colour c) { accent = c; repaint(); }

    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat();
        g.setColour (colours::background.withAlpha (0.6f));
        g.fillRoundedRectangle (r, 5.0f);

        const float range = std::max (12.0f, std::ceil (peakInWindow / 6.0f) * 6.0f);
        g.setColour (colours::stroke.withAlpha (0.7f));
        for (float db = 6.0f; db < range; db += 6.0f)
        {
            const float y = r.getY() + r.getHeight() * (db / range);
            g.drawHorizontalLine ((int) y, r.getX() + 1.0f, r.getRight() - 1.0f);
        }

        juce::Path p;
        p.startNewSubPath (r.getX(), r.getY());
        for (int i = 0; i < kHistory; ++i)
        {
            const float v = history[(std::size_t) ((head + i) % kHistory)];
            p.lineTo (r.getX() + r.getWidth() * (float) i / (float) (kHistory - 1), r.getY() + r.getHeight() * std::min (1.0f, v / range));
        }
        p.lineTo (r.getRight(), r.getY());
        p.closeSubPath();
        g.setColour (colours::reduction.withAlpha (0.28f));
        g.fillPath (p);
        g.setColour (colours::reduction);
        g.strokePath (p, juce::PathStrokeType (1.2f));

        g.setColour (latest > 0.05f ? colours::text : colours::textDim);
        g.setFont (NextLookAndFeel::font (11.0f, true));
        g.drawText (latest > 0.05f ? "-" + juce::String (latest, 1) + " dB" : "GR", getLocalBounds().reduced (6, 2),
                    juce::Justification::centredRight);
        juce::ignoreUnused (accent);
    }

private:
    std::array<float, kHistory> history {};
    int head = 0, recomputeIn = 0;
    float latest = 0.0f, peakInWindow = 0.0f;
    juce::Colour accent { colours::reduction };
};

//==============================================================================
/** Balistique d'affichage d'un niveau crête : attaque instantanée, retombée en dB/s, maintien de crête. */
struct LevelBallistics
{
    static constexpr float kFloorDb = -72.0f;
    float shownDb = kFloorDb, holdDb = kFloorDb, holdAge = 0.0f;

    /** `peakLinear` : crête depuis la dernière lecture (0 = silence). Renvoie true si l'affichage a changé. */
    bool update (float peakLinear, float dt, float releaseDbPerSec = 26.0f) noexcept
    {
        const float db = peakLinear > 1.0e-6f ? std::max (kFloorDb, 20.0f * std::log10 (peakLinear)) : kFloorDb;
        const float previousShown = shownDb, previousHold = holdDb;
        shownDb = std::max (db, shownDb - releaseDbPerSec * dt);
        if (db >= holdDb) { holdDb = db; holdAge = 0.0f; }
        else if ((holdAge += dt) > 1.5f) holdDb = std::max (kFloorDb, holdDb - 40.0f * dt);
        return std::abs (shownDb - previousShown) > 0.05f || std::abs (holdDb - previousHold) > 0.05f;
    }
};

/** Barre de niveau horizontale -72 … +6 dBFS avec maintien de crête. */
inline void drawLevelBar (juce::Graphics& g, juce::Rectangle<float> r, const LevelBallistics& m)
{
    g.setColour (colours::panelHigh);
    g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
    auto frac = [] (float db) { return juce::jlimit (0.0f, 1.0f, (db - LevelBallistics::kFloorDb) / (6.0f - LevelBallistics::kFloorDb)); };
    const float w = r.getWidth() * frac (m.shownDb);
    if (w > 0.5f)
    {
        g.setColour (m.shownDb > 0.0f ? colours::levelOver : (m.shownDb > -6.0f ? colours::levelHot : colours::levelOk));
        g.fillRoundedRectangle (r.withWidth (w), r.getHeight() * 0.5f);
    }
    if (m.holdDb > LevelBallistics::kFloorDb)
    {
        const float x = r.getX() + r.getWidth() * frac (m.holdDb);
        g.setColour (colours::text);
        g.fillRect (juce::Rectangle<float> (x - 1.0f, r.getY(), 2.0f, r.getHeight()));
    }
}

} // namespace aura::ui
