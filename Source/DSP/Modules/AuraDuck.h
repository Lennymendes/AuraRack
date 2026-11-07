// AuraRack — NextDSP · AuraDuck
// Ducker spécialisé Kick / 808.
//
// Clé : bus Sidechain VST3 (le kick) ou, à défaut, détecteur interne sur l'entrée du module.
// La clé passe par un passe-haut et un passe-bas TPT 24 dB/oct (deux SVF Butterworth en cascade)
// pour n'écouter que le corps du kick.
//
// Enveloppe « ultra-rapide sans distorsion basse fréquence » :
//   1. redressement + maintien de crête (hold) d'au moins 1,5 période redressée de la plus basse fréquence
//      de la clé : l'enveloppe ne suit plus les alternances de la fondamentale, donc le gain n'est pas
//      modulé à 2·f (c'est cette ondulation qui crée l'intermodulation sur une 808) ;
//   2. lissage attaque / relâchement de la réduction en dB ;
//   3. deux pôles supplémentaires sur le gain (constante = attaque / 2) : arrondit les angles
//      de la courbe sans ralentir l'attaque perçue.
//
// Modes : Full Band (gain large bande) ou Dynamic EQ Sub (low shelf TPT dont le gain suit l'enveloppe :
// seules les sub-basses de la 808 baissent quand le kick tape ; à 0 dB le filtre est exactement transparent).

#pragma once

#include "../AuraModuleDSP.h"

namespace aura
{
class AuraDuck final : public AuraModuleDSP
{
public:
    enum Param { threshold, depth, attack, release, keyHighPass, keyLowPass, mode, subFreq, keySource, numParams };

    static const ModuleInfo& info() noexcept
    {
        using S = ParamSpec::Scale;
        static constexpr const char* modes[] = { "Full Band", "Dyn EQ Sub" };
        static constexpr const char* keys[]  = { "Sidechain", "Interne" };
        static constexpr ParamSpec specs[] = {
            { "Threshold", "dB", -60.0, 0.0,    -24.0, S::linear },
            { "Depth",     "dB", 0.0,   48.0,   12.0,  S::linear },
            { "Attack",    "ms", 0.05,  20.0,   0.5,   S::logarithmic },
            { "Release",   "ms", 5.0,   1000.0, 80.0,  S::logarithmic },
            { "Key HP",    "Hz", 20.0,  500.0,  30.0,  S::logarithmic },
            { "Key LP",    "Hz", 60.0,  4000.0, 150.0, S::logarithmic },
            { "Mode",      "",   0.0,   1.0,    1.0,   S::choice, modes, 2 },
            { "Sub Freq",  "Hz", 30.0,  300.0,  120.0, S::logarithmic },
            { "Key",       "",   0.0,   1.0,    0.0,   S::choice, keys, 2 },
        };
        static const ModuleInfo i { ModuleType::duck, "AuraDuck", "DUCK", specs };
        return i;
    }

    const ModuleInfo& getInfo() const noexcept override { return info(); }

    void setParameter (int index, double v) noexcept override
    {
        switch (index)
        {
            case threshold:   thresholdDb = v; break;
            case depth:       depthDb = std::max (0.0, v); break;
            case attack:      attackMs = v; updateTimes(); break;
            case release:     releaseCoef = dsp::timeCoef (v, sampleRate); break;
            case keyHighPass: keyHp = v; updateKeyFilters(); updateTimes(); break;
            case keyLowPass:  keyLp = v; updateKeyFilters(); break;
            case mode:        dynEq = v >= 0.5; break;
            case subFreq:     subTan = std::tan (dsp::kPi * std::min (v, 0.45 * sampleRate) / sampleRate); break;
            case keySource:   preferSidechain = v < 0.5; break;
            default: break;
        }
    }

    void reset() noexcept override
    {
        for (auto& f : keyFilters) f.reset();
        shelf.reset();
        held = 0.0; holdCounter = 0;
        grDb = 0.0; gain1 = gain2 = 1.0;
    }

    void process (double* L, double* R, int n, const SidechainBlock& key) noexcept override
    {
        const bool external = preferSidechain && key.available;
        const double* kL = external ? key.left  : L;
        const double* kR = external ? key.right : R;
        constexpr double kRangeDb = 6.0;   // la réduction atteint Depth 6 dB au-dessus du seuil

        for (int i = 0; i < n; ++i)
        {
            // 1. Clé filtrée HP 24 dB + LP 24 dB, redressée, liée en stéréo.
            double a = kL[i], b = kR[i];
            a = keyFilters[0].processSample (keyFilters[1].processSample (a, 0), 0);
            b = keyFilters[0].processSample (keyFilters[1].processSample (b, 1), 1);
            a = keyFilters[2].processSample (keyFilters[3].processSample (a, 0), 0);
            b = keyFilters[2].processSample (keyFilters[3].processSample (b, 1), 1);
            const double rect = std::max (std::abs (a), std::abs (b));

            // Maintien de crête.
            if (rect >= held)          { held = rect; holdCounter = holdSamples; }
            else if (holdCounter > 0)  { --holdCounter; }
            else                       { held = rect; }

            // 2. Réduction cible et lissage en dB.
            const double over = dsp::gainToDb (held) - thresholdDb;
            const double target = depthDb * std::clamp (over / kRangeDb, 0.0, 1.0);
            const double c = target > grDb ? attackCoef : releaseCoef;
            grDb = target + c * (grDb - target);

            // 3. Deux pôles d'arrondi sur le gain.
            const double gTarget = dsp::dbToGain (-grDb);
            gain1 = gTarget + roundCoef * (gain1 - gTarget);
            gain2 = gain1 + roundCoef * (gain2 - gain1);

            if (dynEq)
            {
                // Low shelf Butterworth (Q 0,707) au gain -GR : A = 10^(-GR/40) = sqrt(gain).
                shelf.setLowShelfFast (subTan, 1.4142135623730951, std::sqrt (gain2));
                L[i] = shelf.processSample (L[i], 0);
                R[i] = shelf.processSample (R[i], 1);
            }
            else
            {
                L[i] *= gain2;
                R[i] *= gain2;
            }
        }
    }

    double getGainReductionDb() const noexcept override { return -dsp::gainToDb (gain2); }

private:
    void prepareInternal() override
    {
        updateKeyFilters();
        updateTimes();
    }

    void updateKeyFilters() noexcept
    {
        using T = dsp::TptSvf::Type;
        // Butterworth 4e ordre = deux sections de Q 0,5412 et 1,3066.
        keyFilters[0].setup (T::highPass, keyHp, 0.5411961001461970, 0.0, sampleRate);
        keyFilters[1].setup (T::highPass, keyHp, 1.3065629648763766, 0.0, sampleRate);
        keyFilters[2].setup (T::lowPass,  keyLp, 0.5411961001461970, 0.0, sampleRate);
        keyFilters[3].setup (T::lowPass,  keyLp, 1.3065629648763766, 0.0, sampleRate);
    }

    void updateTimes() noexcept
    {
        attackCoef = dsp::timeCoef (attackMs, sampleRate);
        roundCoef  = dsp::timeCoef (0.5 * attackMs, sampleRate);
        // 1,5 période du signal redressé (2·f) à la plus basse fréquence de la clé, entre 2 et 40 ms.
        const double holdMs = std::clamp (750.0 / keyHp, 2.0, 40.0);
        holdSamples = (int) std::lround (holdMs * 0.001 * sampleRate);
    }

    double thresholdDb = -24.0, depthDb = 12.0, attackMs = 0.5, keyHp = 30.0, keyLp = 150.0;
    double attackCoef = 0.0, releaseCoef = 0.0, roundCoef = 0.0, subTan = 0.0;
    bool dynEq = true, preferSidechain = true;

    std::array<dsp::TptSvf, 4> keyFilters;
    dsp::TptSvf shelf;
    double held = 0.0;
    int holdCounter = 0, holdSamples = 0;
    double grDb = 0.0, gain1 = 1.0, gain2 = 1.0;
};

} // namespace aura
