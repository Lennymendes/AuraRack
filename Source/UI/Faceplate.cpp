// AuraRack — NextDSP · fabrique des façades

#include "Faceplate.h"
#include "E9dsFaceplate.h"
#include "L9aFaceplate.h"
#include "VertigoFaceplate.h"
#include "W9bFaceplate.h"

namespace aura::ui
{
bool Faceplate::exists (aura::ModuleType t) noexcept { return t == aura::ModuleType::w9b || t == aura::ModuleType::l9a || t == aura::ModuleType::e9ds
                                                         || t == aura::ModuleType::v9c3 || t == aura::ModuleType::v9p2 || t == aura::ModuleType::f976
                                                         || t == aura::ModuleType::d9x || t == aura::ModuleType::sculpt || t == aura::ModuleType::tone; }

std::unique_ptr<Faceplate> Faceplate::create (aura::ModuleType t, juce::AudioProcessorValueTreeState& apvts, int slot)
{
    if (t == aura::ModuleType::w9b) return std::make_unique<W9bFaceplate> (apvts, slot);
    if (t == aura::ModuleType::l9a) return std::make_unique<L9aFaceplate> (apvts, slot);
    if (t == aura::ModuleType::e9ds) return std::make_unique<E9dsFaceplate> (apvts, slot);
    if (exists (t) && t != aura::ModuleType::w9b && t != aura::ModuleType::l9a && t != aura::ModuleType::e9ds) return std::make_unique<VertigoFaceplate> (t, apvts, slot);
    return nullptr;
}

} // namespace aura::ui
