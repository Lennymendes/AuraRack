// AuraRack — NextDSP · internationalisation de l'interface
//
// Un dictionnaire unique : une ligne par clé, une colonne par langue. Ajouter une langue = une entrée dans `Lang`,
// son nom dans kLanguageNames, et une colonne dans kTable (le compilateur refuse une ligne incomplète).
// Les noms de paramètres audio (Threshold, Ratio, Attack…) restent en anglais, comme sur les unités matérielles.
// Lecture sur le thread de messages uniquement.

#pragma once

#include "../DSP/AuraModuleDSP.h"

#include <array>

namespace aura::ui::i18n
{
enum class Lang : int { fr = 0, en, count };
inline constexpr std::array<const char*, (std::size_t) Lang::count> kLanguageNames { "Fran\xc3\xa7" "ais", "English" };

enum class Key : int
{
    settings, language, infoHelp, lufsMeter, lufsReset, momentary, shortTerm, integrated, range, peak,
    emptySlot, chooseModule, addModule, removeModule, bypassModule, enableModule, selectModule, dragToReorder,
    sidechain, latency, chain, inspector,
    helpSettings, helpInfo, helpLufs, helpBypassTile, helpRemoveTile, helpEmptyTile,
    modNone, modEq, modComp, modDuck, modClip, modLimit, modDelay, modVerb, modW9b, modL9a, modE9ds,
    count
};

inline constexpr std::array<std::array<const char*, (std::size_t) Lang::count>, (std::size_t) Key::count> kTable
{{
    /* settings     */ { "R\xc3\xa9glages", "Settings" },
    /* language     */ { "Langue", "Language" },
    /* infoHelp     */ { "Infos / aide contextuelle", "Info / contextual help" },
    /* lufsMeter    */ { "Mesure LUFS (mastering)", "LUFS meter (mastering)" },
    /* lufsReset    */ { "Remettre \xc3\xa0 z\xc3\xa9ro", "Reset" },
    /* momentary    */ { "MOMENTAN\xc3\x89", "MOMENTARY" },
    /* shortTerm    */ { "COURT TERME", "SHORT-TERM" },
    /* integrated   */ { "INT\xc3\x89GR\xc3\x89", "INTEGRATED" },
    /* range        */ { "DYNAMIQUE (LRA)", "RANGE (LRA)" },
    /* peak         */ { "CR\xc3\x8aTE", "PEAK" },
    /* emptySlot    */ { "Slot vide", "Empty slot" },
    /* chooseModule */ { "Choisir un module", "Choose a module" },
    /* addModule    */ { "Ajouter un module", "Add a module" },
    /* removeModule */ { "Retirer le module", "Remove module" },
    /* bypassModule */ { "Mettre en bypass", "Bypass" },
    /* enableModule */ { "Activer", "Enable" },
    /* selectModule */ { "Afficher dans la zone haute", "Show in the upper view" },
    /* dragToReorder*/ { "Glisser pour r\xc3\xa9ordonner", "Drag to reorder" },
    /* sidechain    */ { "SIDECHAIN", "SIDECHAIN" },
    /* latency      */ { "latence", "latency" },
    /* chain        */ { "CHA\xc3\x8eNE", "CHAIN" },
    /* inspector    */ { "MODULE ACTIF", "ACTIVE MODULE" },
    /* helpSettings */ { "R\xc3\xa9glages : langue, aide contextuelle, mesure LUFS", "Settings: language, contextual help, LUFS meter" },
    /* helpInfo     */ { "Affiche les info-bulles et les indicateurs techniques", "Shows tooltips and technical readouts" },
    /* helpLufs     */ { "Sonie ITU-R BS.1770 : momentan\xc3\xa9, court terme, int\xc3\xa9gr\xc3\xa9, LRA, cr\xc3\xaate", "ITU-R BS.1770 loudness: momentary, short-term, integrated, LRA, peak" },
    /* helpBypassTile */ { "Active / met en bypass ce module", "Enables / bypasses this module" },
    /* helpRemoveTile */ { "Retire ce module (slot vide)", "Removes this module (empty slot)" },
    /* helpEmptyTile  */ { "Cliquer pour ajouter un module", "Click to add a module" },
    /* modNone      */ { "Slot vide", "Empty slot" },
    /* modEq        */ { "\xc3\x89galiseur param\xc3\xa9trique multi-bandes, phase minimale", "Multi-band parametric equaliser, minimum phase" },
    /* modComp      */ { "Compresseur : Aura-Opto, Aura-Tube, L-Comp", "Compressor: Aura-Opto, Aura-Tube, L-Comp" },
    /* modDuck      */ { "Ducking sur cl\xc3\xa9 sidechain (kick / 808)", "Sidechain ducking (kick / 808)" },
    /* modClip      */ { "\xc3\x89" "cr\xc3\xaateur sur-\xc3\xa9" "chantillonn\xc3\xa9", "Oversampled clipper" },
    /* modLimit     */ { "Limiteur True Peak \xc3\xa0 anticipation", "Look-ahead true-peak limiter" },
    /* modDelay     */ { "\xc3\x89" "cho st\xc3\xa9r\xc3\xa9o", "Stereo delay" },
    /* modVerb      */ { "R\xc3\xa9verb\xc3\xa9ration", "Reverb" },
    /* modW9b       */ { "Compresseur FET (esprit CL1B), Auto Focus", "FET compressor (CL1B style), Auto Focus" },
    /* modL9a       */ { "Nivelleur opto (esprit LA-2A)", "Optical leveler (LA-2A style)" },
    /* modE9ds      */ { "De-esser : ma\xc3\xaetrise la sibilance sans zozotement", "De-esser: tames sibilance without lisping" },
}};

inline Lang& current() noexcept { static Lang l = Lang::fr; return l; }
inline void setLanguage (int index) noexcept { current() = (Lang) std::clamp (index, 0, (int) Lang::count - 1); }
inline int getLanguageIndex() noexcept { return (int) current(); }

inline juce::String tr (Key k) { return juce::String (juce::CharPointer_UTF8 (kTable[(std::size_t) k][(std::size_t) current()])); }

inline Key descriptionKey (aura::ModuleType t) noexcept
{
    switch (t)
    {
        case aura::ModuleType::eq:    return Key::modEq;
        case aura::ModuleType::comp:  return Key::modComp;
        case aura::ModuleType::duck:  return Key::modDuck;
        case aura::ModuleType::clip:  return Key::modClip;
        case aura::ModuleType::limit: return Key::modLimit;
        case aura::ModuleType::delay: return Key::modDelay;
        case aura::ModuleType::verb:  return Key::modVerb;
        case aura::ModuleType::w9b:   return Key::modW9b;
        case aura::ModuleType::l9a:   return Key::modL9a;
        case aura::ModuleType::e9ds:  return Key::modE9ds;
        case aura::ModuleType::none:  break;
    }
    return Key::modNone;
}
} // namespace aura::ui::i18n
