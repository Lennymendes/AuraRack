// AuraRack — NextDSP · suivi « Auto Focus » du seuil d'un compresseur
//
// But : l'utilisateur choisit une réduction de gain MOYENNE visée (1 à 12 dB) ; le suiveur déplace le Threshold,
// lentement, pour que le programme joué comprime à peu près de cette valeur, quel que soit son niveau d'entrée.
//
// Deux étages (tout en dB) :
//
//   1. ANTICIPATION (rapide, lissée à τ 0,5 s) : seuil qui donnerait exactement la réduction visée AU niveau moyen du
//      programme (`program`, moyenne lente τ 3 s en dB), par inversion analytique de la courbe statique du compresseur,
//      genou compris. Le seuil l'approche à vitesse limitée (6 dB/s).
//   2. CORRECTION LENTE (quelques secondes) : un intégrateur ±12 dB compare la réduction RÉELLEMENT appliquée (moyenne τ 1,5 s,
//      relâchement dépendant du programme compris) à la cible et rattrape l'écart « réduction au niveau moyen » contre
//      « moyenne de la réduction » (le programme fluctue). Gain normalisé par la pente 1 − 1/ratio : même dynamique à tout
//      ratio. Il n'intègre QUE lorsque le seuil a rejoint l'anticipation (sinon il s'emballerait pendant la course du
//      seuil) et s'arrête contre une butée (anti-emballement).
//
// Protections contre le pompage (la moyenne ne doit jamais suivre une syllabe, un clic ou une pause) :
//   - porte absolue (−60 dB) : le silence ne déplace rien (tout est gelé) ;
//   - porte relative : un niveau 18 dB sous la moyenne lente du programme (pause entre deux phrases, creux) gèle aussi
//     la boucle ; si ce niveau bas dure plus de 2,5 s, c'est un vrai changement de programme et la boucle reprend ;
//   - un clic de 5 ms ne pèse que ≈ 5 ms / 3 s dans la moyenne : < 0,3 dB de déplacement du seuil.
//
// À l'activation le seuil part de la valeur manuelle (aucun saut). Constantes en secondes : même comportement de 44,1 à
// 384 kHz.

#pragma once

#include "../AuraDspCore.h"

namespace aura::compdetail
{
class FocusTracker
{
public:
    static constexpr double kGateDb = -60.0, kRelativeGateDb = 18.0, kRelativeTimeoutS = 2.5;
    static constexpr double kProgramTauS = 3.0, kSeedTauS = 0.08, kSeedTimeS = 0.4, kReductionTauS = 1.5;
    static constexpr double kProgramOffsetDb = 2.0;   // la moyenne en dB d'un signal fluctuant est ≈ 2 dB sous le niveau qui pilote le compresseur (crête de parole)
    static constexpr double kTrimRatePerS = 0.7, kTrimLimitDb = 12.0, kSettledDb = 0.5, kMaxSlewDbPerS = 6.0, kThresholdTauS = 0.5;
    static constexpr double kMinThresholdDb = -60.0, kMaxThresholdDb = 0.0;

    void prepare (double sampleRate) noexcept
    {
        sr = sampleRate;
        aThreshold = alpha (kThresholdTauS);
        aProgram = alpha (kProgramTauS); aSeed = alpha (kSeedTauS); aReduction = alpha (kReductionTauS);
    }

    /** (Ré)initialise à l'activation : le seuil démarre à `thresholdDb` (le réglage manuel). */
    void reset (double thresholdDb) noexcept
    {
        threshold = thresholdDb;
        trim = 0.0; program = 0.0; reduction = 0.0; belowTime = 0.0; seedLeft = kSeedTimeS; hasProgram = false;
    }

    void setTargetReduction (double db) noexcept { target = db; }
    double getThreshold() const noexcept { return threshold; }
    double getProgramLevel() const noexcept { return program; }
    double getAverageReduction() const noexcept { return reduction; }

    /** Un appel par échantillon. `xDb` : niveau vu par le détecteur ; `reductionDb` : réduction appliquée. */
    void process (double xDb, double reductionDb, double ratio, double kneeDb) noexcept
    {
        bool open = xDb > kGateDb;
        if (open && hasProgram && seedLeft <= 0.0 && xDb < program - kRelativeGateDb)
        {
            belowTime += 1.0 / sr;
            if (belowTime < kRelativeTimeoutS) open = false;   // pause : la boucle garde sa mémoire
        }
        else if (open)
            belowTime = 0.0;

        if (! open) return;

        // Niveau du programme (en dB) : amorcé vite pendant les 0,4 premières secondes de signal, puis lent.
        if (! hasProgram) { program = xDb; hasProgram = true; }
        if (seedLeft > 0.0)
        {
            program += aSeed * (xDb - program);
            seedLeft -= 1.0 / sr;
        }
        else
            program += aProgram * (xDb - program);

        reduction += aReduction * (reductionDb - reduction);
        if (seedLeft > 0.0) return;

        const double ff = thresholdFor (program + kProgramOffsetDb, target, ratio, kneeDb);
        const double slope = std::max (0.3, 1.0 - 1.0 / std::max (1.0, ratio));
        const double error = reduction - target;                       // > 0 : trop de réduction -> remonter le seuil
        const double raw = ff + trim;
        const double goal = std::clamp (raw, kMinThresholdDb, kMaxThresholdDb);
        const bool atStop = (raw >= kMaxThresholdDb && error > 0.0) || (raw <= kMinThresholdDb && error < 0.0);
        if (std::abs (goal - threshold) < kSettledDb && ! atStop)
            trim = std::clamp (trim + kTrimRatePerS / slope * error / sr, -kTrimLimitDb, kTrimLimitDb);

        threshold += std::clamp (aThreshold * (goal - threshold), -kMaxSlewDbPerS / sr, kMaxSlewDbPerS / sr);   // lissage final : l'ondulation des syllabes ne passe pas
    }

    /** Seuil qui donne `reductionDb` au niveau `levelDb` (inversion de compdetail::kneeReduction). */
    static double thresholdFor (double levelDb, double reductionDb, double ratio, double kneeDb) noexcept
    {
        const double s = std::max (0.05, 1.0 - 1.0 / std::max (1.0, ratio));
        const double over = (kneeDb > 0.0 && reductionDb <= 0.5 * s * kneeDb)
                                ? std::sqrt (2.0 * kneeDb * reductionDb / s) - 0.5 * kneeDb
                                : reductionDb / s;
        return levelDb - over;
    }

private:
    double alpha (double tau) const noexcept { return -std::expm1 (-1.0 / (tau * sr)); }

    double sr = 48000.0, aThreshold = 0.0, aProgram = 0.0, aSeed = 0.0, aReduction = 0.0;
    double target = 3.0, threshold = -18.0, trim = 0.0, program = 0.0, reduction = 0.0, belowTime = 0.0, seedLeft = kSeedTimeS;
    bool hasProgram = false;
};
} // namespace aura::compdetail
