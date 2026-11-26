// AuraRack — NextDSP
// Moteur du rack (voir RackEngine.h pour le modèle de threads).

#include "RackEngine.h"

namespace aura
{
//==============================================================================
// CompensatedDry
//==============================================================================
void CompensatedDry::prepare (double sampleRate, int newMaxLatency)
{
    maxLatency = std::max (0, newMaxLatency);
    for (auto& l : lines) l.prepare ((std::size_t) maxLatency + 4);
    fade.prepare (sampleRate, RackEngine::kSwapFadeMs);
    reset();
}

void CompensatedDry::reset() noexcept
{
    for (auto& l : lines) l.reset();
    fade.reset (1.0);
    from = target;
}

void CompensatedDry::setLatency (int samples) noexcept
{
    samples = std::clamp (samples, 0, maxLatency);
    if (samples == target) return;
    // Un nouveau changement pendant un fondu repart de la prise dominante.
    if (fade.isRamping() && fade.getCurrent() >= 0.5)
        from = target;
    else if (! fade.isRamping())
        from = target;
    target = samples;
    fade.reset (0.0);
    fade.setTarget (1.0);
}

void CompensatedDry::process (const double* inL, const double* inR, double* outL, double* outR, int n) noexcept
{
    const auto t = (std::size_t) target;
    for (int i = 0; i < n; ++i)
    {
        lines[0].push (inL[i]);
        lines[1].push (inR[i]);
        if (fade.isRamping())
        {
            const double w = fade.next();
            const auto f = (std::size_t) from;
            outL[i] = (1.0 - w) * lines[0].read (f) + w * lines[0].read (t);
            outR[i] = (1.0 - w) * lines[1].read (f) + w * lines[1].read (t);
            if (! fade.isRamping()) from = target;
        }
        else
        {
            outL[i] = lines[0].read (t);
            outR[i] = lines[1].read (t);
        }
    }
}

//==============================================================================
// RackEngine
//==============================================================================
RackEngine::RackEngine()
{
    for (auto& s : slots)
        s.current = new ModuleBox();
}

RackEngine::~RackEngine()
{
    for (auto& s : slots)
    {
        delete s.incoming.exchange (nullptr);
        delete s.current;
        delete s.previous;
        delete s.retirePending;
    }
    collectGarbage();
}

void RackEngine::bindSlot (int slot, const SlotParameterSources& sources) noexcept
{
    slots[(std::size_t) slot].sources = sources;
    slots[(std::size_t) slot].paramsDirty = true;
}

void RackEngine::bindGlobal (const GlobalParameterSources& sources) noexcept { globals = sources; }

//==============================================================================
void RackEngine::prepare (double newSampleRate, int /*maxBlockSize : traité par tranches de kChunk*/)
{
    std::lock_guard<std::mutex> lock (structMutex);

    sampleRate = newSampleRate;
    maxModuleLatencySamples = maxModuleLatency (sampleRate);

    for (auto* buf : { &rackInL, &rackInR, &dryL, &dryR, &wetL, &wetR, &prevL, &prevR, &rawL, &rawR, &gainIn, &reorderBuf })
        buf->assign ((std::size_t) kChunk, 0.0);

    // Les modules rendus par le thread audio peuvent être détruits ici : l'audio est arrêté.
    while (garbageRead.load() != garbageWrite.load())
    {
        delete garbage[garbageRead.load() % kGarbageCapacity];
        garbageRead.fetch_add (1);
    }

    int totalLatency = 0;
    for (int i = 0; i < kNumSlots; ++i)
    {
        auto& s = slots[(std::size_t) i];

        if (auto* in = s.incoming.exchange (nullptr))
        {
            delete s.current;
            s.current = in;
        }
        delete s.previous;      s.previous = nullptr;
        delete s.retirePending; s.retirePending = nullptr;

        if (s.current->module != nullptr)
        {
            s.current->module->prepare (sampleRate, kChunk);
            s.current->latency = s.current->module->getLatencySamples();
        }
        else
        {
            s.current->latency = 0;
        }
        slotsMeta[(std::size_t) i].latency.store (s.current->latency);
        totalLatency += s.current->latency;

        s.active.prepare (sampleRate, kBypassFadeMs);
        s.swap.prepare (sampleRate, kSwapFadeMs);
        s.mix.prepare (sampleRate, 20.0);
        s.inTrim.prepare (sampleRate, 20.0);
        s.outTrim.prepare (sampleRate, 20.0);

        const bool bypassed = readOr (s.sources.bypass, 0.0f) >= 0.5f;
        s.active.reset (bypassed ? 0.0 : 1.0);
        s.swap.reset (1.0);
        s.mix.reset (proportionFromPercent (readOr (s.sources.mix, 100.0f)));
        s.inTrim.reset (gainFromDb (readOr (s.sources.inTrim, 0.0f)));
        s.outTrim.reset (gainFromDb (readOr (s.sources.outTrim, 0.0f)));
        s.paramsDirty = true;
        s.wasBypassed = false;

        s.dry.prepare (sampleRate, maxModuleLatencySamples);
        s.dry.setLatency (s.current->latency);
        s.dry.reset();
    }

    inputGain.prepare (sampleRate, 20.0);
    outputGain.prepare (sampleRate, 20.0);
    masterMix.prepare (sampleRate, 20.0);
    inputGain.reset (gainFromDb (readOr (globals.inputGain, 0.0f)));
    outputGain.reset (gainFromDb (readOr (globals.outputGain, 0.0f)));
    masterMix.reset (proportionFromPercent (readOr (globals.masterMix, 100.0f)));

    masterDry.prepare (sampleRate, kNumSlots * maxModuleLatencySamples);
    masterDry.setLatency (totalLatency);
    masterDry.reset();
    meters.activeLatency.store (totalLatency);

    activeOrderPacked = requestedOrder.load();
    activeOrder = RackRouting::unpack (activeOrderPacked);
    reorderGain.prepare (sampleRate, kReorderFadeMs);
    reorderGain.reset (1.0);

    prepared = true;
}

void RackEngine::releaseResources()
{
    std::lock_guard<std::mutex> lock (structMutex);
    prepared = false;
}

void RackEngine::reset() noexcept
{
    if (! prepared) return;
    for (auto& s : slots)
    {
        if (s.current->module != nullptr) s.current->module->reset();
        if (s.previous != nullptr) { retire (s, s.previous); s.previous = nullptr; }
        s.swap.reset (1.0);
        s.active.reset (s.active.getTarget());
        s.mix.reset (s.mix.getTarget());
        s.inTrim.reset (s.inTrim.getTarget());
        s.outTrim.reset (s.outTrim.getTarget());
        s.dry.reset();
    }
    inputGain.reset (inputGain.getTarget());
    outputGain.reset (outputGain.getTarget());
    masterMix.reset (masterMix.getTarget());
    masterDry.reset();
}

//==============================================================================
void RackEngine::setModuleType (int slot, ModuleType type)
{
    if (slot < 0 || slot >= kNumSlots) return;

    collectGarbage();

    std::lock_guard<std::mutex> lock (structMutex);
    auto& meta = slotsMeta[(std::size_t) slot];
    if (meta.type.load() == (int) type) return;

    auto box = std::make_unique<ModuleBox>();
    box->type = type;
    box->module = createModule (type);
    if (box->module != nullptr && prepared)
    {
        box->module->prepare (sampleRate, kChunk);   // allocation ici, hors du thread audio
        box->latency = box->module->getLatencySamples();
    }

    meta.type.store ((int) type);
    meta.latency.store (box->latency);

    // Un module publié mais pas encore adopté n'a jamais été vu par le thread audio : on peut le détruire.
    delete slots[(std::size_t) slot].incoming.exchange (box.release(), std::memory_order_acq_rel);
}

void RackEngine::setOrder (const SlotOrder& order) noexcept
{
    if (RackRouting::isValid (order))
        requestedOrder.store (RackRouting::pack (order), std::memory_order_release);
}

void RackEngine::collectGarbage()
{
    std::lock_guard<std::mutex> lock (structMutex);
    auto r = garbageRead.load (std::memory_order_relaxed);
    const auto w = garbageWrite.load (std::memory_order_acquire);
    while (r != w)
    {
        delete garbage[r % kGarbageCapacity];
        ++r;
    }
    garbageRead.store (r, std::memory_order_release);
}

int RackEngine::getRequestedLatency() const noexcept
{
    int total = 0;
    for (const auto& m : slotsMeta) total += m.latency.load();
    return total;
}

bool RackEngine::pushGarbage (ModuleBox* box) noexcept
{
    const auto w = garbageWrite.load (std::memory_order_relaxed);
    if (w - garbageRead.load (std::memory_order_acquire) >= kGarbageCapacity) return false;
    garbage[w % kGarbageCapacity] = box;
    garbageWrite.store (w + 1, std::memory_order_release);
    return true;
}

void RackEngine::retire (Slot& slot, ModuleBox* box) noexcept
{
    // Chaque échange de module passe par setModuleType(), qui vide la FIFO d'abord : elle contient au plus
    // kNumSlots entrées et ne peut pas déborder. retirePending n'est qu'une garde (réessayée à chaque tranche).
    if (! pushGarbage (box))
        slot.retirePending = box;
}

//==============================================================================
// Thread audio
//==============================================================================
void RackEngine::process (double* left, double* right, int numSamples,
                          const double* scL, const double* scR, bool sidechainConnected) noexcept
{
    if (! prepared || numSamples <= 0) return;

    int offset = 0;
    while (offset < numSamples)
    {
        // Réordonnancement : descente 2,5 ms, permutation au creux, remontée 2,5 ms.
        const auto requested = requestedOrder.load (std::memory_order_acquire);
        if (reorderGain.getTarget() == 0.0 && ! reorderGain.isRamping())
        {
            activeOrderPacked = requested;
            activeOrder = RackRouting::unpack (requested);
            reorderGain.setTarget (1.0);
        }
        else if (reorderGain.getTarget() == 1.0 && ! reorderGain.isRamping() && requested != activeOrderPacked)
        {
            reorderGain.setTarget (0.0);
        }

        int len = std::min (kChunk, numSamples - offset);
        if (reorderGain.getTarget() == 0.0 && reorderGain.isRamping())
            len = std::min (len, reorderGain.getRemaining());   // la tranche s'arrête pile au creux

        processChunk (left + offset, right + offset, len,
                      scL != nullptr ? scL + offset : nullptr,
                      scR != nullptr ? scR + offset : nullptr,
                      sidechainConnected);
        offset += len;
    }
}

void RackEngine::processChunk (double* L, double* R, int n, const double* scL, const double* scR, bool scOn) noexcept
{
    // 1. Adoption des modules publiés, puis latence totale du rack.
    int totalLatency = 0;
    for (int i = 0; i < kNumSlots; ++i)
    {
        auto& s = slots[(std::size_t) i];
        if (s.retirePending != nullptr && pushGarbage (s.retirePending)) s.retirePending = nullptr;
        adoptIncoming (s, i);
        totalLatency += s.current->latency;
    }
    masterDry.setLatency (totalLatency);
    meters.activeLatency.store (totalLatency, std::memory_order_relaxed);

    // 2. Gain d'entrée, copie de l'entrée du rack (Dry maître, clé « entrée du rack »).
    inputGain.setTarget (gainFromDb (readOr (globals.inputGain, 0.0f)));
    outputGain.setTarget (gainFromDb (readOr (globals.outputGain, 0.0f)));
    masterMix.setTarget (proportionFromPercent (readOr (globals.masterMix, 100.0f)));

    double peakInL = 0.0, peakInR = 0.0;
    for (int i = 0; i < n; ++i)
    {
        const double g = inputGain.next();
        L[i] *= g;
        R[i] *= g;
        rackInL[(std::size_t) i] = L[i];
        rackInR[(std::size_t) i] = R[i];
        peakInL = std::max (peakInL, std::abs (L[i]));
        peakInR = std::max (peakInR, std::abs (R[i]));
    }
    masterDry.process (rackInL.data(), rackInR.data(), dryL.data(), dryR.data(), n);

    // 3. Clé sidechain.
    SidechainBlock key;
    const auto source = readOr (globals.sidechainSource, 0.0f) >= 0.5f ? SidechainSource::rackInput
                                                                        : SidechainSource::externalBus;
    if (source == SidechainSource::externalBus && scOn && scL != nullptr && scR != nullptr)
        key = { scL, scR, true };
    else if (source == SidechainSource::rackInput)
        key = { rackInL.data(), rackInR.data(), true };
    meters.sidechainActive.store (source == SidechainSource::externalBus && key.available, std::memory_order_relaxed);

    // 4. Les 10 slots, dans l'ordre actif.
    //    Pendant un réordonnancement, le gain de fondu s'applique aussi à l'entrée de chaque slot : au moment de
    //    la permutation, chaque module reçoit une entrée nulle, donc continue. Sans cela, le changement de source
    //    ressortirait plus tard d'un module à latence (limiteur) ou dans les échos d'un Delay / d'une Reverb.
    const bool reordering = reorderGain.isRamping() || reorderGain.getCurrent() != 1.0;
    double* rg = reorderBuf.data();
    for (int i = 0; i < n; ++i) rg[i] = reorderGain.next();

    for (int pos = 0; pos < kNumSlots; ++pos)
    {
        if (reordering)
            for (int i = 0; i < n; ++i) { L[i] *= rg[i]; R[i] *= rg[i]; }
        const int index = activeOrder[(std::size_t) pos];
        processSlot (slots[(std::size_t) index], index, L, R, n, key);
    }

    // 5. Fondu de réordonnancement en sortie, Mix maître (Dry aligné sur la latence du rack), gain de sortie.
    double peakOutL = 0.0, peakOutR = 0.0;
    for (int i = 0; i < n; ++i)
    {
        const double mm = masterMix.next();
        const double go = outputGain.next();
        const double dl = dryL[(std::size_t) i], dr = dryR[(std::size_t) i];
        L[i] = (dl * (1.0 - mm) + L[i] * rg[i] * mm) * go;
        R[i] = (dr * (1.0 - mm) + R[i] * rg[i] * mm) * go;
        peakOutL = std::max (peakOutL, std::abs (L[i]));
        peakOutR = std::max (peakOutR, std::abs (R[i]));
    }

    auto holdMax = [] (std::atomic<float>& a, double v)
    {
        if ((float) v > a.load (std::memory_order_relaxed)) a.store ((float) v, std::memory_order_relaxed);
    };
    holdMax (meters.inputPeak[0], peakInL);
    holdMax (meters.inputPeak[1], peakInR);
    holdMax (meters.outputPeak[0], peakOutL);
    holdMax (meters.outputPeak[1], peakOutR);
}

void RackEngine::adoptIncoming (Slot& s, int /*slotIndex*/) noexcept
{
    ModuleBox* in = s.incoming.exchange (nullptr, std::memory_order_acq_rel);
    if (in == nullptr) return;

    if (s.previous != nullptr) retire (s, s.previous);   // un fondu encore en cours est abrégé
    s.previous = s.current;
    s.current = in;
    s.swap.reset (0.0);
    s.swap.setTarget (1.0);
    s.paramsDirty = true;
    s.dry.setLatency (in->latency);
}

void RackEngine::applyModuleParameters (Slot& s) noexcept
{
    auto* m = s.current->module.get();
    if (m == nullptr) return;

    const auto specs = m->getInfo().params;
    for (std::size_t i = 0; i < specs.size(); ++i)
    {
        const float v = readOr (s.sources.params[i], (float) specs[i].defaultNormalised());
        if (s.paramsDirty || v != s.lastNorm[i])
        {
            s.lastNorm[i] = v;
            m->setParameter ((int) i, specs[i].toPlain (v));
        }
    }

    if (s.paramsDirty)
    {
        m->reset();   // O(1) : les lissages partent des valeurs réelles, pas des valeurs par défaut
        s.paramsDirty = false;
    }
}

void RackEngine::processSlot (Slot& s, int slotIndex, double* L, double* R, int n, const SidechainBlock& key) noexcept
{
    s.active.setTarget (readOr (s.sources.bypass, 0.0f) >= 0.5f ? 0.0 : 1.0);
    s.mix.setTarget (proportionFromPercent (readOr (s.sources.mix, 100.0f)));
    s.inTrim.setTarget (gainFromDb (readOr (s.sources.inTrim, 0.0f)));
    s.outTrim.setTarget (gainFromDb (readOr (s.sources.outTrim, 0.0f)));
    applyModuleParameters (s);

    // Entrée brute retardée de la latence du module : chemin Dry du Mix et chemin Bypass, toujours alignés.
    double* rl = rawL.data();
    double* rr = rawR.data();
    s.dry.process (L, R, rl, rr, n);

    auto& gr = meters.gainReductionDb[(std::size_t) slotIndex];
    AuraModuleDSP* cur = s.current->module.get();
    AuraModuleDSP* prev = s.previous != nullptr ? s.previous->module.get() : nullptr;

    // Bypass complet : on rend l'entrée retardée, le module ne consomme plus de CPU.
    const bool fullyBypassed = ! s.active.isRamping() && s.active.getCurrent() == 0.0;
    if (fullyBypassed)
    {
        if (s.previous != nullptr) { retire (s, s.previous); s.previous = nullptr; }
        std::copy (rl, rl + n, L);
        std::copy (rr, rr + n, R);
        s.wasBypassed = true;
        gr.store (0.0f, std::memory_order_relaxed);
        return;
    }
    if (s.wasBypassed)
    {
        if (cur != nullptr) cur->reset();   // repart d'un état propre, masqué par le fondu d'entrée
        s.wasBypassed = false;
    }

    // Slot vide, sans fondu en cours : identité exacte (le Dry a déjà enregistré l'historique).
    if (cur == nullptr && s.previous == nullptr && s.dry.getLatency() == 0 && ! s.dry.isFading())
    {
        gr.store (0.0f, std::memory_order_relaxed);
        return;
    }

    // Trim d'entrée puis traitement Wet (nouveau module, et l'ancien pendant un échange).
    double* wl = wetL.data();
    double* wr = wetR.data();
    double* pl = prevL.data();
    double* pr = prevR.data();
    for (int i = 0; i < n; ++i)
    {
        const double g = s.inTrim.next();
        gainIn[(std::size_t) i] = g;
        wl[i] = L[i] * g;
        wr[i] = R[i] * g;
    }
    if (s.previous != nullptr)
    {
        std::copy (wl, wl + n, pl);
        std::copy (wr, wr + n, pr);
        if (prev != nullptr) prev->process (pl, pr, n, key);
    }
    if (cur != nullptr) cur->process (wl, wr, n, key);

    // Mix (Dry aligné), trim de sortie, fondu de bypass linéaire 5 ms, fondu d'échange 5 ms.
    for (int i = 0; i < n; ++i)
    {
        const double gIn = gainIn[(std::size_t) i];
        const double m = s.mix.next();
        const double gOut = s.outTrim.next();
        const double b = s.active.next();

        double wetSampleL = wl[i], wetSampleR = wr[i];
        if (s.previous != nullptr)
        {
            const double w = s.swap.next();
            wetSampleL = (1.0 - w) * pl[i] + w * wetSampleL;
            wetSampleR = (1.0 - w) * pr[i] + w * wetSampleR;
        }

        // Forme dry·(1-m) + wet·m : exacte à 0 % et à 100 % (dry + m·(wet - dry) ne l'est pas en flottant).
        const double dL = rl[i] * gIn, dR = rr[i] * gIn;
        const double procL = (dL * (1.0 - m) + wetSampleL * m) * gOut;
        const double procR = (dR * (1.0 - m) + wetSampleR * m) * gOut;
        L[i] = b * procL + (1.0 - b) * rl[i];
        R[i] = b * procR + (1.0 - b) * rr[i];
    }

    if (s.previous != nullptr && ! s.swap.isRamping())
    {
        retire (s, s.previous);
        s.previous = nullptr;
    }

    gr.store (cur != nullptr ? (float) cur->getGainReductionDb() : 0.0f, std::memory_order_relaxed);
    if (cur != nullptr)
    {
        auto& status = meters.slotStatus[(std::size_t) slotIndex];
        const auto peak = (float) cur->takeStatusPeak();
        if (peak > status.load (std::memory_order_relaxed)) status.store (peak, std::memory_order_relaxed);   // un seul écrivain
        meters.slotAux[(std::size_t) slotIndex].store ((float) cur->getMeterAux(), std::memory_order_relaxed);
    }
}

} // namespace aura
