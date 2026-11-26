// AuraRack — NextDSP · AuraTone
// Sculpteur de timbre propre en DXD (352,8 / 384 kHz, double 64 bits) : Body, Air, Tilt et doseur d'harmoniques.
//
//   Contrôles : Body (−12 … +12 dB : bas-médium, cloche large à 180 Hz, Q 0,5), Air (−12 … +12 dB : étagère haute à
//   11 kHz, Q 0,6 : « soyeux », sans dureté), Tilt (−6 … +6 dB : bascule autour de 650 Hz — + éclaircit : graves −T/2,
//   aigus +T/2 par étagères appariées), Even (0 … 100 % : harmonique 2, « chaleur »), Odd (0 … 100 % : harmonique 3,
//   « densité »), Output (−24 … +24 dB). Le Mix et le Bypass sont ceux du slot.
//
//   Filtres : SVF TPT (aucune déformation de fréquence près de Nyquist ; stables à 20 Hz comme à 384 kHz). Inactifs
//   (réglage 0 dB) ils sont court-circuités : tout à zéro = sortie bit pour bit identique à l'entrée.
//   Doseur : termes additifs (HarmonicStage), sans repliement à aucune fréquence d'échantillonnage ; à 100 %,
//   amplitude −12 dBFS : H2 ≈ −28 dBc (Even) ou H3 ≈ −34 dBc (Odd) ; l'effet croît avec le niveau (H2 ∝ niveau, H3 ∝ niveau²).
//   Latence 0.

#pragma once

#include "ColourStage.h"

namespace aura
{
class AuraTone final : public AuraModuleDSP
{
public:
    enum Param { body, air, tilt, even, odd, output, numParams };

    static constexpr double kRef = 0.25, kC2 = 0.08, kC3 = 0.08;

    static const ModuleInfo& info() noexcept
    {
        using S = ParamSpec::Scale;
        static constexpr ParamSpec specs[] = {
            { "Body",   "dB", -12.0, 12.0, 0.0, S::linear },
            { "Air",    "dB", -12.0, 12.0, 0.0, S::linear },
            { "Tilt",   "dB", -6.0, 6.0, 0.0, S::linear },
            { "Even",   "%",  0.0, 100.0, 0.0, S::linear },
            { "Odd",    "%",  0.0, 100.0, 0.0, S::linear },
            { "Output", "dB", -24.0, 24.0, 0.0, S::linear },
        };
        static const ModuleInfo i { ModuleType::tone, "AuraTone", "TONE", specs };
        return i;
    }
    const ModuleInfo& getInfo() const noexcept override { return info(); }

    void setParameter (int index, double v) noexcept override
    {
        switch (index)
        {
            case body:   bodySm.setTarget (v); break;
            case air:    airSm.setTarget (v); break;
            case tilt:   tiltSm.setTarget (v); break;
            case even:   evenAmt = v * 0.01; break;
            case odd:    oddAmt = v * 0.01; break;
            case output: outGain.setTarget (dsp::dbToGain (v)); break;
            default: break;
        }
    }

    void reset() noexcept override
    {
        for (auto* s : { &bodySm, &airSm, &tiltSm }) s->reset (s->getTarget());
        outGain.reset (outGain.getTarget());
        bodyF.reset(); airF.reset(); tiltLo.reset(); tiltHi.reset(); harm.reset();
        counter = 0; applyFilters();
        statusPeak = 0.0;
    }

    void process (double* L, double* R, int n, const SidechainBlock&) noexcept override
    {
        for (int i = 0; i < n; ++i)
        {
            const double b = bodySm.next(), a = airSm.next(), t = tiltSm.next();
            if ((counter++ & 15) == 0) { curBody = b; curAir = a; curTilt = t; applyFilters(); }
            double xl = L[i], xr = R[i];
            if (bodyOn)  { xl = bodyF.processSample (xl, 0); xr = bodyF.processSample (xr, 1); }
            if (tiltOn)  { xl = tiltHi.processSample (tiltLo.processSample (xl, 0), 0); xr = tiltHi.processSample (tiltLo.processSample (xr, 1), 1); }
            if (airOn)   { xl = airF.processSample (xl, 0); xr = airF.processSample (xr, 1); }
            xl = harm.process (xl, 0, kRef, kC2 * evenAmt, kC3 * oddAmt);
            xr = harm.process (xr, 1, kRef, kC2 * evenAmt, kC3 * oddAmt);
            const double go = outGain.next();
            xl *= go; xr *= go;
            L[i] = xl; R[i] = xr;
            statusPeak = std::max (statusPeak, std::max (std::abs (xl), std::abs (xr)));
        }
    }

    double takeStatusPeak() noexcept override { const double p = statusPeak; statusPeak = 0.0; return p; }

private:
    void prepareInternal() override
    {
        for (auto* s : { &bodySm, &airSm, &tiltSm }) s->prepare (sampleRate, 20.0);
        outGain.prepare (sampleRate, 20.0);
        harm.prepare (sampleRate);
        applyFilters();
    }

    void applyFilters() noexcept
    {
        constexpr double eps = 1.0e-4;
        bodyOn = std::abs (curBody) > eps; airOn = std::abs (curAir) > eps; tiltOn = std::abs (curTilt) > eps;
        if (bodyOn) bodyF.setup (dsp::TptSvf::Type::bell, 180.0, 0.5, curBody, sampleRate);
        if (airOn)  airF.setup (dsp::TptSvf::Type::highShelf, 11000.0, 0.6, curAir, sampleRate);
        if (tiltOn)
        {
            tiltLo.setup (dsp::TptSvf::Type::lowShelf,  650.0, 0.5, -0.5 * curTilt, sampleRate);
            tiltHi.setup (dsp::TptSvf::Type::highShelf, 650.0, 0.5,  0.5 * curTilt, sampleRate);
        }
    }

    double evenAmt = 0.0, oddAmt = 0.0, curBody = 0.0, curAir = 0.0, curTilt = 0.0, statusPeak = 0.0;
    int counter = 0;
    bool bodyOn = false, airOn = false, tiltOn = false;
    dsp::OnePoleSmoother bodySm, airSm, tiltSm, outGain;
    dsp::TptSvf bodyF, airF, tiltLo, tiltHi;
    compdetail::HarmonicStage harm;
};

} // namespace aura
