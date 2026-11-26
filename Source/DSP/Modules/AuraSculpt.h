// AuraRack — NextDSP · AuraSculpt
// Façonneur de transitoires / de dynamique, DXD (352,8 / 384 kHz, double 64 bits). Indépendant du niveau d'entrée.
//
//   Contrôles : Attack (−12 … +12 dB : gain appliqué pendant l'attaque), Sustain (−12 … +12 dB : gain appliqué pendant la
//   résonance / la queue), Speed (0,5 … 4 × : vitesse des détecteurs), Focus (60 Hz … 12 kHz : fréquence que le module
//   surveille et façonne), Focus Amount (0 … 100 % : 0 = tout le spectre, 100 = seulement la bande autour de Focus ;
//   le reste du spectre reste intact), Ceiling (−12 … 0 dBFS ; 0 = Off, strictement transparent : écrêteur doux sinon),
//   Output (−24 … +24 dB). Le Mix et le Bypass sont ceux du slot.
//
//   Principe : deux enveloppes de crête, rapide (attaque 0,05 ms, relâchement 5 ms × Speed⁻¹) et lente (attaque 12 ms,
//   relâchement 90 ms × Speed⁻¹). D = dB(rapide) − dB(lente) : positif pendant une attaque, négatif pendant la queue, nul en
//   régime établi. Gain (dB) = Attack · s(D / 6) + Sustain · s(−D / 6), s = lissage C¹ borné [0, 1] : en régime établi
//   le gain vaut exactement 0 dB quels que soient les réglages. Aucune dépendance au niveau absolu (sauf sous −70 dBFS, où
//   D est forcé à 0 : le bruit de fond n'est pas « façonné »). Le biais de régime établi de D (ondulation du détecteur, fonction de
//   la fréquence) est retiré par une moyenne lente de 0,25 s : seuls les changements plus rapides pilotent le gain, quelle que soit
//   la fréquence (aucun pompage sur un grave tenu). Le gain est lissé (0,25 ms) : pas de clic.
//   Focus : la clé est mélangée (1−a)·x + a·bande(x) ; le gain n'est appliqué qu'à ce mélange : y = x + (g−1)·mélange.
//   Ceiling : plafond à genou doux (identité exacte jusqu'à 3 dB sous le plafond, tanh C¹), ADAA du résidu sous 176,4 kHz ; plafond
//   « doux » : sous surcharge extrême (+12 dB) la crête peut dépasser Ceiling d'environ 1 dB (mesuré).
//   Latence 0.

#pragma once

#include "ColourStage.h"

namespace aura
{
class AuraSculpt final : public AuraModuleDSP
{
public:
    enum Param { attack, sustain, speed, focus, focusAmount, ceiling, output, numParams };

    static const ModuleInfo& info() noexcept
    {
        using S = ParamSpec::Scale;
        static constexpr ParamSpec specs[] = {
            { "Attack",       "dB", -12.0, 12.0, 0.0, S::linear },
            { "Sustain",      "dB", -12.0, 12.0, 0.0, S::linear },
            { "Speed",        "x",  0.5, 4.0, 1.0, S::logarithmic },
            { "Focus",        "Hz", 60.0, 12000.0, 1500.0, S::logarithmic },
            { "Focus Amount", "%",  0.0, 100.0, 0.0, S::linear },
            { "Ceiling",      "dB", -12.0, 0.0, 0.0, S::linear },
            { "Output",       "dB", -24.0, 24.0, 0.0, S::linear },
        };
        static const ModuleInfo i { ModuleType::sculpt, "AuraSculpt", "SCULPT", specs };
        return i;
    }
    const ModuleInfo& getInfo() const noexcept override { return info(); }

    void setParameter (int index, double v) noexcept override
    {
        switch (index)
        {
            case attack:      attackDb = v; break;
            case sustain:     sustainDb = v; break;
            case speed:       speedMul = v; updateTimes(); break;
            case focus:       focusHz = v; band.setup (dsp::TptSvf::Type::bandPass, focusHz, 0.8, 0.0, sampleRate); break;
            case focusAmount: amount = v * 0.01; break;
            case ceiling:     ceilOn = v < -1.0e-6; ceilDb = v; if (ceilOn) knee.prepare (sampleRate, dsp::dbToGain (v), dsp::dbToGain (v - 3.0)); break;
            case output:      outGain.setTarget (dsp::dbToGain (v)); break;
            default: break;
        }
    }

    void reset() noexcept override
    {
        outGain.reset (outGain.getTarget()); band.reset(); knee.reset();
        fastE = slowE = 0.0; dAvg = 0.0; gainDbSm = 0.0; appliedDb = 0.0; statusPeak = 0.0;
    }

    void process (double* L, double* R, int n, const SidechainBlock&) noexcept override
    {
        const bool shaping = attackDb != 0.0 || sustainDb != 0.0;
        for (int i = 0; i < n; ++i)
        {
            double xl = L[i], xr = R[i];
            if (shaping)
            {
                double bl = 0.0, br = 0.0;
                if (amount > 0.0) { bl = band.processSample (xl, 0); br = band.processSample (xr, 1); }
                const double ml = (1.0 - amount) * xl + amount * bl, mr = (1.0 - amount) * xr + amount * br;
                const double a = std::max (std::abs (ml), std::abs (mr));
                fastE += (a > fastE ? aFastAtt : aFastRel) * (a - fastE);
                slowE += (a > slowE ? aSlowAtt : aSlowRel) * (a - slowE);
                double d = 0.0;
                if (slowE > 3.0e-4 && fastE > 1.0e-9) d = dsp::gainToDb (fastE) - dsp::gainToDb (slowE);
                // Le biais de régime établi (ondulation du détecteur, dépend de la fréquence) est retiré par une moyenne lente (0,25 s) :
                // seules les variations plus rapides que 0,25 s (attaques, queues) pilotent le gain.
                dAvg += aAvg * (d - dAvg);
                const double dd = d - dAvg;
                const double gDb = attackDb * shape (dd / 6.0) + sustainDb * shape (-dd / 6.0);
                gainDbSm += gainCoef * (gDb - gainDbSm);
                if (std::abs (gainDbSm) > 1.0e-9)
                {
                    const double dg = dsp::dbToGain (gainDbSm) - 1.0;
                    xl += dg * ml; xr += dg * mr;
                }
                appliedDb = gainDbSm;
            }
            const double go = outGain.next();
            xl *= go; xr *= go;
            if (ceilOn) { xl = knee.process (xl, 0); xr = knee.process (xr, 1); }
            L[i] = xl; R[i] = xr;
            statusPeak = std::max (statusPeak, std::max (std::abs (xl), std::abs (xr)));
        }
    }

    double getGainReductionDb() const noexcept override { return 0.0; }
    double takeStatusPeak() noexcept override { const double p = statusPeak; statusPeak = 0.0; return p; }
    /** Gain instantané appliqué (dB, signé) : lu par la façade. */
    double getMeterAux() const noexcept override { return appliedDb; }

private:
    static double shape (double v) noexcept { v = std::clamp (v, 0.0, 1.0); return v * v * (3.0 - 2.0 * v); }

    void prepareInternal() override
    {
        outGain.prepare (sampleRate, 20.0);
        band.setup (dsp::TptSvf::Type::bandPass, 1500.0, 0.8, 0.0, sampleRate);
        gainCoef = compdetail::alpha (0.00025, sampleRate);
        aAvg = compdetail::alpha (0.25, sampleRate);
        updateTimes();
    }
    void updateTimes() noexcept
    {
        aFastAtt = compdetail::alpha (0.00005, sampleRate);
        aFastRel = compdetail::alpha (0.005 / speedMul, sampleRate);
        aSlowAtt = compdetail::alpha (0.012 / speedMul, sampleRate);
        aSlowRel = compdetail::alpha (0.090 / speedMul, sampleRate);
    }

    double attackDb = 0.0, sustainDb = 0.0, speedMul = 1.0, focusHz = 1500.0, amount = 0.0, ceilDb = 0.0;
    bool ceilOn = false;
    double fastE = 0.0, slowE = 0.0, dAvg = 0.0, aAvg = 0.0;
    double aFastAtt = 0.5, aFastRel = 0.01, aSlowAtt = 0.01, aSlowRel = 0.001, gainCoef = 0.5, gainDbSm = 0.0, appliedDb = 0.0, statusPeak = 0.0;
    dsp::OnePoleSmoother outGain;
    dsp::TptSvf band;
    compdetail::SoftKnee knee;
};

} // namespace aura
