// AuraRack — NextDSP
// Identifiants et disposition des paramètres (APVTS).
//
// Globaux : in_gain, out_gain, master_mix, sc_source.
// Par slot n (1 … 10) : sN_type (non automatisable), sN_bypass, sN_mix, sN_in, sN_out, sN_p1 … sN_p9.
// Les 9 paramètres génériques sont normalisés [0, 1] ; leur sens et leur affichage suivent le module du slot
// (ModuleInfo::params). Les identifiants sont figés : ils sont enregistrés dans les sessions des DAW.

#pragma once

#include "Engine/RackEngine.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <functional>

namespace aura::params
{
inline const juce::String inGain    { "in_gain" };
inline const juce::String outGain   { "out_gain" };
inline const juce::String masterMix { "master_mix" };
inline const juce::String scSource  { "sc_source" };

inline juce::String slotPrefix (int slot)              { return "s" + juce::String (slot + 1) + "_"; }
inline juce::String type (int slot)                    { return slotPrefix (slot) + "type"; }
inline juce::String bypass (int slot)                  { return slotPrefix (slot) + "bypass"; }
inline juce::String mix (int slot)                     { return slotPrefix (slot) + "mix"; }
inline juce::String inTrim (int slot)                  { return slotPrefix (slot) + "in"; }
inline juce::String outTrim (int slot)                 { return slotPrefix (slot) + "out"; }
inline juce::String generic (int slot, int index)      { return slotPrefix (slot) + "p" + juce::String (index + 1); }

/** Renvoie le type de module courant d'un slot (pour l'affichage des paramètres génériques). */
using SlotTypeProvider = std::function<ModuleType (int slot)>;

juce::AudioProcessorValueTreeState::ParameterLayout createLayout (SlotTypeProvider slotType);

} // namespace aura::params
