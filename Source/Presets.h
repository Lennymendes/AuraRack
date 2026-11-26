// AuraRack — NextDSP · presets d'usine (départs propres)
//
// Chaque preset décrit les slots utilisés dans l'ordre du signal (slot 1, 2, …) avec des valeurs PHYSIQUES (dB, Hz, indices
// de choix) ; les autres slots sont vides et les paramètres non cités gardent la valeur par défaut du module. Les valeurs sont
// des points de départ musicaux établis par mesure sur signaux de test, pas par écoute : à affiner à l'oreille.
//
// Le chargement passe par le même chemin que les instantanés A / B (AuraRackProcessor::applySnapshot) : modules, ordre et
// réglages sont installés ensemble, sans clic (fondus de module du moteur).

#pragma once

#include "DSP/ModuleRegistry.h"

#include <initializer_list>
#include <vector>

namespace aura::presets
{
struct Setting { int param; double plain; };

struct SlotDef
{
    ModuleType type = ModuleType::none;
    std::vector<Setting> settings;
    double mixPercent = -1.0;   // < 0 : défaut du module
    bool autoGain = false;
};

struct Global
{
    double inGain = 0.0, outGain = 0.0;
    bool autoGain = false;
    double widthPercent = 100.0;
    int bassMono = 0;           // 0 Off, 1 60 Hz, 2 100 Hz, 3 150 Hz, 4 250 Hz
    bool mono = false;
};

struct Preset
{
    const char* nameFr;
    const char* nameEn;
    const char* descFr;
    const char* descEn;
    std::vector<SlotDef> slots;   // slot 1 … n
    Global global;
};

inline const std::vector<Preset>& factory()
{
    using M = ModuleType;
    static const std::vector<Preset> all = {
        { "Voix", "Vocal",
          "Pr\xc3\xa9" "ampli \xc2\xb7 de-esser anticip\xc3\xa9 \xc2\xb7 compresseur Auto Focus \xc2\xb7 EQ",
          "Preamp \xc2\xb7 look-ahead de-esser \xc2\xb7 Auto Focus compressor \xc2\xb7 EQ",
          { { M::v9p2, { { AuraV9P2::hpf, 1 } } },                                                       // HPF 80 Hz
            { M::e9ds, { { AuraE9DS::threshold, -32.0 }, { AuraE9DS::frequency, 6800.0 }, { AuraE9DS::range, 8.0 }, { AuraE9DS::lookahead, 2 } } },   // 1 ms
            { M::v9c3, { { AuraV9C3::ratio, 2 }, { AuraV9C3::attack, 5 }, { AuraV9C3::release, 10 }, { AuraV9C3::autoFocus, 1 }, { AuraV9C3::focus, 4.0 }, { AuraV9C3::makeUp, 2.0 } }, -1.0, true },
            { M::eq,   { { AuraEQ::pkFreq, 300.0 }, { AuraEQ::pkGain, -2.0 }, { AuraEQ::pkQ, 1.2 }, { AuraEQ::hsFreq, 10000.0 }, { AuraEQ::hsGain, 2.0 } } } },
          {} },
        { "Piano / Acoustique", "Piano / Acoustic",
          "Pr\xc3\xa9" "ampli \xc2\xb7 EQ doux \xc2\xb7 nivellement opto \xc2\xb7 salle courte",
          "Preamp \xc2\xb7 gentle EQ \xc2\xb7 opto leveller \xc2\xb7 short room",
          { { M::v9p2, {} },
            { M::eq,   { { AuraEQ::lsFreq, 120.0 }, { AuraEQ::lsGain, -1.5 }, { AuraEQ::hsFreq, 12000.0 }, { AuraEQ::hsGain, 1.5 } } },
            { M::l9a,  { { AuraL9A::peakReduction, 30.0 }, { AuraL9A::gain, 2.0 }, { AuraL9A::mode, 0 } }, -1.0, true },
            { M::verb, { { AuraVerb::decay, 1.4 }, { AuraVerb::preDelay, 15.0 }, { AuraVerb::damping, 6000.0 } }, 12.0 } },
          {} },
        { "Bus", "Bus",
          "EQ de nettoyage \xc2\xb7 compresseur de coh\xc3\xa9sion \xc2\xb7 \xc3\xa9" "cr\xc3\xaa" "tage l\xc3\xa9ger",
          "Clean-up EQ \xc2\xb7 glue compressor \xc2\xb7 light clipper",
          { { M::eq,   { { AuraEQ::lsFreq, 35.0 }, { AuraEQ::lsGain, -3.0 } } },
            { M::v9c3, { { AuraV9C3::ratio, 1 }, { AuraV9C3::attack, 7 }, { AuraV9C3::release, 11 }, { AuraV9C3::scHpf, 1 }, { AuraV9C3::detect, 1 }, { AuraV9C3::autoFocus, 1 }, { AuraV9C3::focus, 2.5 } }, -1.0, true },
            { M::clip, { { AuraClip::drive, 1.5 }, { AuraClip::ceiling, -0.3 }, { AuraClip::knee, 40.0 } } } },
          {} },
        { "Mastering", "Mastering",
          "EQ large \xc2\xb7 compresseur st\xc3\xa9r\xc3\xa9o doux \xc2\xb7 de-esser \xc2\xb7 limiteur True Peak \xc2\xb7 mono grave 100 Hz",
          "Broad EQ \xc2\xb7 gentle stereo compressor \xc2\xb7 de-esser \xc2\xb7 True Peak limiter \xc2\xb7 bass mono 100 Hz",
          { { M::eq,   { { AuraEQ::lsFreq, 80.0 }, { AuraEQ::lsGain, 0.5 }, { AuraEQ::hsFreq, 12000.0 }, { AuraEQ::hsGain, 1.0 } } },
            { M::v9c3, { { AuraV9C3::ratio, 1 }, { AuraV9C3::attack, 8 }, { AuraV9C3::release, 11 }, { AuraV9C3::scHpf, 2 }, { AuraV9C3::detect, 1 }, { AuraV9C3::autoFocus, 1 }, { AuraV9C3::focus, 1.5 } }, -1.0, true },
            { M::e9ds, { { AuraE9DS::threshold, -28.0 }, { AuraE9DS::frequency, 8000.0 }, { AuraE9DS::width, 1.0 }, { AuraE9DS::range, 4.0 }, { AuraE9DS::lookahead, 2 } } },
            { M::limit, { { AuraLimit::inputGain, 2.0 }, { AuraLimit::ceiling, -1.0 }, { AuraLimit::release, 80.0 } } } },
          { 0.0, 0.0, false, 100.0, 2, false } }
    };
    return all;
}

} // namespace aura::presets
