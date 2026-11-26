// AuraRack — NextDSP
// Registre des modules : métadonnées par type et fabrique. Appelé uniquement hors du thread audio.

#pragma once

#include "Modules/AuraClip.h"
#include "Modules/AuraComp.h"
#include "Modules/AuraDelay.h"
#include "Modules/AuraDuck.h"
#include "Modules/AuraE9DS.h"
#include "Modules/AuraEQ.h"
#include "Modules/AuraL9A.h"
#include "Modules/AuraLimit.h"
#include "Modules/AuraVerb.h"
#include "Modules/AuraV9C3.h"
#include "Modules/AuraV9P2.h"
#include "Modules/AuraW9B.h"

#include <memory>

namespace aura
{
inline const ModuleInfo& emptySlotInfo() noexcept
{
    static const ModuleInfo i { ModuleType::none, "Vide", "—", {} };
    return i;
}

inline const ModuleInfo& moduleInfo (ModuleType t) noexcept
{
    switch (t)
    {
        case ModuleType::eq:    return AuraEQ::info();
        case ModuleType::comp:  return AuraComp::info();
        case ModuleType::duck:  return AuraDuck::info();
        case ModuleType::clip:  return AuraClip::info();
        case ModuleType::limit: return AuraLimit::info();
        case ModuleType::delay: return AuraDelay::info();
        case ModuleType::verb:  return AuraVerb::info();
        case ModuleType::w9b:   return AuraW9B::info();
        case ModuleType::l9a:   return AuraL9A::info();
        case ModuleType::e9ds:  return AuraE9DS::info();
        case ModuleType::v9c3:  return AuraV9C3::info();
        case ModuleType::v9p2:  return AuraV9P2::info();
        case ModuleType::none:  break;
    }
    return emptySlotInfo();
}

inline std::unique_ptr<AuraModuleDSP> createModule (ModuleType t)
{
    switch (t)
    {
        case ModuleType::eq:    return std::make_unique<AuraEQ>();
        case ModuleType::comp:  return std::make_unique<AuraComp>();
        case ModuleType::duck:  return std::make_unique<AuraDuck>();
        case ModuleType::clip:  return std::make_unique<AuraClip>();
        case ModuleType::limit: return std::make_unique<AuraLimit>();
        case ModuleType::delay: return std::make_unique<AuraDelay>();
        case ModuleType::verb:  return std::make_unique<AuraVerb>();
        case ModuleType::w9b:   return std::make_unique<AuraW9B>();
        case ModuleType::l9a:   return std::make_unique<AuraL9A>();
        case ModuleType::e9ds:  return std::make_unique<AuraE9DS>();
        case ModuleType::v9c3:  return std::make_unique<AuraV9C3>();
        case ModuleType::v9p2:  return std::make_unique<AuraV9P2>();
        case ModuleType::none:  break;
    }
    return nullptr;
}

/** Latence maximale possible d'un module à cette fréquence (dimensionne les lignes de compensation). */
inline int maxModuleLatency (double sampleRate)
{
    // Seuls AuraClip (sur-échantillonnage), AuraLimit (anticipation) et E9DS (Lookahead réglable) ont une latence ; les autres
    // modules ne sont pas instanciés ici (AuraDelay réserve 2 s de mémoire par canal).
    int worst = 0;
    for (auto t : { ModuleType::clip, ModuleType::limit, ModuleType::e9ds })
    {
        auto m = createModule (t);
        m->prepare (sampleRate, 64);
        worst = std::max (worst, m->getMaxLatencySamples());
    }
    return worst;
}

} // namespace aura
