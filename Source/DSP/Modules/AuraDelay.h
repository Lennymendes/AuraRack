// AuraRack — NextDSP · AuraDelay
// Delay stéréo Ping-Pong. Lignes fractionnaires (Lagrange 3e ordre, 4 points), temps lissé (glissement
// type bande, sans clic), filtres Low-Cut / High-Cut TPT dans la boucle de réinjection.
// Ping-Pong 0 % : deux delays indépendants L / R ; 100 % : l'entrée mono attaque L, L réinjecte R, R réinjecte L.
// Sortie 100 % wet : le dosage se fait avec le Mix du slot.

#pragma once

#include "../AuraModuleDSP.h"

namespace aura
{
class AuraDelay final : public AuraModuleDSP
{
public:
    enum Param { time, feedback, pingPong, lowCut, highCut, ratioR, numParams };

    static constexpr double kMaxTimeMs = 2000.0;

    static const ModuleInfo& info() noexcept
    {
        using S = ParamSpec::Scale;
        static constexpr ParamSpec specs[] = {
            { "Time",      "ms", 1.0,    kMaxTimeMs, 375.0,  S::logarithmic },
            { "Feedback",  "%",  0.0,    95.0,       40.0,   S::linear },
            { "Ping-Pong", "%",  0.0,    100.0,      100.0,  S::linear },
            { "Low Cut",   "Hz", 20.0,   2000.0,     120.0,  S::logarithmic },
            { "High Cut",  "Hz", 1000.0, 20000.0,    8000.0, S::logarithmic },
            { "R / L",     "x",  0.5,    2.0,        1.0,    S::logarithmic },
        };
        static const ModuleInfo i { ModuleType::delay, "AuraDelay", "DELAY", specs, 30.0 };
        return i;
    }

    const ModuleInfo& getInfo() const noexcept override { return info(); }

    void setParameter (int index, double v) noexcept override
    {
        switch (index)
        {
            case time:     timeMs = v; break;
            case feedback: fbSmooth.setTarget (std::clamp (v, 0.0, 95.0) / 100.0); return;
            case pingPong: ppSmooth.setTarget (std::clamp (v, 0.0, 100.0) / 100.0); return;
            case lowCut:   lowCutSmooth.setTarget (std::log (v)); return;
            case highCut:  highCutSmooth.setTarget (std::log (v)); return;
            case ratioR:   ratio = v; break;
            default: return;
        }
        const double maxSamples = kMaxTimeMs * 0.001 * sampleRate;
        timeSmooth[0].setTarget (std::clamp (timeMs * 0.001 * sampleRate, 2.0, maxSamples));
        timeSmooth[1].setTarget (std::clamp (timeMs * ratio * 0.001 * sampleRate, 2.0, maxSamples));
    }

    void reset() noexcept override
    {
        for (auto& l : lines) l.reset();
        for (auto& f : hp) f.reset();
        for (auto& f : lp) f.reset();
        for (auto& s : timeSmooth) s.reset (s.getTarget());
        fbSmooth.reset (fbSmooth.getTarget());
        ppSmooth.reset (ppSmooth.getTarget());
        lowCutSmooth.reset (lowCutSmooth.getTarget());
        highCutSmooth.reset (highCutSmooth.getTarget());
        updateFilters();
    }

    void process (double* L, double* R, int n, const SidechainBlock&) noexcept override
    {
        for (int i = 0; i < n; ++i)
        {
            if ((lowCutSmooth.isSmoothing() || highCutSmooth.isSmoothing()))
            {
                lowCutSmooth.next();
                highCutSmooth.next();
                if ((++filterCounter & 31) == 0) updateFilters();
            }
            const double dL = timeSmooth[0].next();
            const double dR = timeSmooth[1].next();
            const double fb = fbSmooth.next();
            const double pp = ppSmooth.next();

            // Sorties des lignes (retard dL / dR), filtrées : c'est ce qui sort et ce qui est réinjecté.
            // Le passe-bas de boucle retarde chaque répétition de son retard de groupe (√2/ωc, 11 µs à 20 kHz) :
            // on le retire du temps de lecture, les échos tombent exactement sur la grille à chaque passage.
            double yL = lines[0].readFractional (std::max (1.0, dL - 1.0 - loopGroupDelay));
            double yR = lines[1].readFractional (std::max (1.0, dR - 1.0 - loopGroupDelay));
            yL = lp[0].processSample (hp[0].processSample (yL, 0), 0);
            yR = lp[1].processSample (hp[1].processSample (yR, 0), 0);

            const double inL = L[i], inR = R[i];
            const double mono = 0.5 * (inL + inR);

            // Mélange continu stéréo <-> ping-pong.
            const double wL = (1.0 - pp) * (inL + fb * yL) + pp * (mono + fb * yR);
            const double wR = (1.0 - pp) * (inR + fb * yR) + pp * (fb * yL);
            lines[0].push (wL);
            lines[1].push (wR);

            L[i] = yL;
            R[i] = yR;
        }
    }

private:
    void updateFilters() noexcept
    {
        using T = dsp::TptSvf::Type;
        // Retard de groupe basse fréquence du passe-bas Butterworth numérique (fréquence pré-déformée).
        const double fc = std::exp (highCutSmooth.getCurrent());
        const double wc = 2.0 * sampleRate * std::tan (dsp::kPi * std::min (fc, 0.45 * sampleRate) / sampleRate);
        loopGroupDelay = 1.4142135623730951 / wc * sampleRate;
        for (int c = 0; c < 2; ++c)
        {
            hp[(std::size_t) c].setup (T::highPass, std::exp (lowCutSmooth.getCurrent()), 0.7071067811865476, 0.0, sampleRate);
            lp[(std::size_t) c].setup (T::lowPass,  std::exp (highCutSmooth.getCurrent()), 0.7071067811865476, 0.0, sampleRate);
        }
    }

    void prepareInternal() override
    {
        for (auto& l : lines) l.prepare ((std::size_t) std::ceil (kMaxTimeMs * 0.001 * sampleRate) + 8);
        for (auto& s : timeSmooth) s.prepare (sampleRate, 80.0);
        fbSmooth.prepare (sampleRate, 20.0);
        ppSmooth.prepare (sampleRate, 20.0);
        lowCutSmooth.prepare (sampleRate, 30.0);
        highCutSmooth.prepare (sampleRate, 30.0);
    }

    std::array<dsp::DelayLine, 2> lines;
    std::array<dsp::TptSvf, 2> hp, lp;   // un filtre par ligne (canal interne 0)
    std::array<dsp::OnePoleSmoother, 2> timeSmooth;
    dsp::OnePoleSmoother fbSmooth, ppSmooth, lowCutSmooth, highCutSmooth;
    double timeMs = 375.0, ratio = 1.0, loopGroupDelay = 0.0;
    unsigned filterCounter = 0;
};

} // namespace aura
