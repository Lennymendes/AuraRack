// AuraRack — NextDSP · façades de module : interface commune et fabrique
//
// Une façade remplace les potentiomètres génériques d'un slot par une face avant dédiée. Elle est liée aux paramètres
// du slot par des attachements (aucun état propre côté audio) et reçoit une fois par image les indicateurs du moteur.

#pragma once

#include "../DSP/AuraModuleDSP.h"
#include "FaceplateKit.h"

namespace aura::ui
{
/** Indicateurs du moteur pour un slot (lus par la carte, une fois par image). */
struct FaceplateFeed
{
    float reductionDb = 0.0f;   // réduction de gain (dB, >= 0)
    float statusPeak = 0.0f;    // crête de sortie depuis la dernière image (linéaire) ; 0 = rien
    float aux = 0.0f;           // valeur propre au module : seuil effectif (W9BMK1, dB), niveau VU (L9A, dBFS)
};

class Faceplate : public juce::Component
{
public:
    /** Une fois par image (thread de messages). */
    virtual void tick (float dtSeconds, const FaceplateFeed&) = 0;

    /** Le module de ce type a-t-il une façade dédiée ? */
    static bool exists (aura::ModuleType) noexcept;
    static std::unique_ptr<Faceplate> create (aura::ModuleType, juce::AudioProcessorValueTreeState&, int slotIndex);
};

} // namespace aura::ui
