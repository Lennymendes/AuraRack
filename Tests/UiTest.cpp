// AuraRack — NextDSP
// Test de l'éditeur sans fenêtre : glisser-déposer des slots, resynchronisation sur l'ordre du moteur,
// puis rendu de l'interface en PNG (argument facultatif : dossier de sortie).

#include "../Source/PluginEditor.h"
#include "../Source/UI/E9dsFaceplate.h"
#include "../Source/UI/VertigoFaceplate.h"
#include "../Source/UI/L9aFaceplate.h"
#include "../Source/UI/W9bFaceplate.h"

#include <cstdio>
#include <cstring>
#include <set>

namespace
{
int failures = 0, checks = 0;

void check (bool ok, const char* what, int value = 0)
{
    juce::ignoreUnused (value);
    ++checks;
    if (! ok) ++failures;
    std::printf ("  [%s] %s\n", ok ? " OK " : "FAIL", what);
}

juce::MouseEvent mouseEvent (juce::Component& on, juce::Point<float> pos, juce::Point<float> downPos, bool dragged)
{
    return { juce::Desktop::getInstance().getMainMouseSource(), pos, juce::ModifierKeys::leftButtonModifier, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
             &on, &on, juce::Time::getCurrentTime(), downPos, juce::Time::getCurrentTime(), 1, dragged };
}

void renderAudio (AuraRackProcessor& p, double fs, int blocks)
{
    juce::AudioBuffer<double> buf (4, 512);   // 2 canaux + bus Sidechain stéréo
    juce::MidiBuffer midi;
    for (int b = 0; b < blocks; ++b)
    {
        for (int i = 0; i < 512; ++i)
        {
            const double t = (double) (b * 512 + i) / fs;
            const double kick = (std::fmod (t, 0.5) < 0.1) ? 0.9 * std::sin (2.0 * juce::MathConstants<double>::pi * 60.0 * t) : 0.0;
            const double music = 0.5 * std::sin (2.0 * juce::MathConstants<double>::pi * 220.0 * t) + 0.3 * std::sin (2.0 * juce::MathConstants<double>::pi * 3000.0 * t);
            buf.setSample (0, i, music + kick); buf.setSample (1, i, music - kick * 0.5); buf.setSample (2, i, kick); buf.setSample (3, i, kick);
        }
        p.processBlock (buf, midi);
    }
}
}

int main (int argc, char** argv)
{
    std::setvbuf (stdout, nullptr, _IONBF, 0);   // une panne doit laisser la trace de la dernière étape
    juce::ScopedJuceInitialiser_GUI juceInit;
    std::puts ("AuraRack — test de l'éditeur");

    const double fs = 384000.0;
    AuraRackProcessor processor;
    processor.setProcessingPrecision (juce::AudioProcessor::doublePrecision);
    processor.setPlayConfigDetails (4, 2, fs, 512);
    processor.prepareToPlay (fs, 512);

    auto setParam = [&] (const juce::String& id, float plain)
    {
        auto* p = processor.getValueTreeState().getParameter (id);
        p->setValueNotifyingHost (p->convertTo0to1 (plain));
    };
    auto setNormalised = [&] (const juce::String& id, float n) { processor.getValueTreeState().getParameter (id)->setValueNotifyingHost (n); };

    using aura::ModuleType;
    const ModuleType layout[] = { ModuleType::eq, ModuleType::comp, ModuleType::comp, ModuleType::comp, ModuleType::duck,
                                  ModuleType::e9ds, ModuleType::limit, ModuleType::delay, ModuleType::l9a, ModuleType::w9b };
    for (int s = 0; s < 10; ++s)
        processor.setSlotModule (s, layout[s]);
    // Slot 2 : Aura-Tube ; slot 3 : L-Comp (choix d'algorithme = paramètre 1, normalisé 0 / 0,5 / 1).
    setNormalised (aura::params::generic (2, aura::AuraComp::algorithm), 0.5f);
    setNormalised (aura::params::generic (3, aura::AuraComp::algorithm), 1.0f);
    setNormalised (aura::params::generic (1, aura::AuraComp::peakReduction), 0.75f);
    setNormalised (aura::params::generic (2, aura::AuraComp::threshold), 0.65f);
    setParam (aura::params::mix (7), 30.0f);
    using W = aura::AuraW9B;
    setNormalised (aura::params::generic (9, W::input), 0.55f);
    setNormalised (aura::params::generic (9, W::threshold), 0.55f);
    setNormalised (aura::params::generic (9, W::ratio), 0.60f);
    setNormalised (aura::params::generic (9, W::attack), 0.45f);
    setNormalised (aura::params::generic (9, W::release), 0.55f);
    setNormalised (aura::params::generic (9, W::output), 0.62f);
    setNormalised (aura::params::generic (9, W::autoFocus), 1.0f);
    setNormalised (aura::params::generic (9, W::focus), 5.0f / 11.0f);   // Focus 6 dB
    using LL = aura::AuraL9A;
    setNormalised (aura::params::generic (8, LL::peakReduction), 0.62f);
    setNormalised (aura::params::generic (8, LL::gain), 0.35f);
    setNormalised (aura::params::generic (8, LL::mode), 1.0f);
    renderAudio (processor, fs, 200);

    AuraRackEditor editor (processor);
    editor.setBounds (0, 0, AuraRackEditor::kDesignWidth, AuraRackEditor::kDesignHeight);
    for (int i = 0; i < 20; ++i) { renderAudio (processor, fs, 20); editor.tickForTesting (1.0f / 60.0f); }

    // --- Glisser-déposer dans la bande : le slot 0 (position 0) est déposé sur la position 3.
    auto& content = editor.getContent();
    auto& strip = content.strip;
    auto tileCentre = [&] (int s) { return strip.getTileBounds (s).getCentre(); };

    const auto start = tileCentre (0), end = tileCentre (3);
    strip.mouseDown (mouseEvent (strip, start, start, false));
    for (int step = 1; step <= 10; ++step)
    {
        strip.mouseDrag (mouseEvent (strip, start + (end - start) * ((float) step / 10.0f), start, true));
        editor.tickForTesting (1.0f / 60.0f);
    }
    check (processor.getSlotOrder() == aura::RackRouting::identity(), "Pendant le glissement : l'ordre du moteur n'est pas modifié");
    strip.mouseUp (mouseEvent (strip, end, start, true));

    const auto expected = aura::RackRouting::moved (aura::RackRouting::identity(), 0, 3);
    check (processor.getSlotOrder() == expected, "Au relâchement : moveSlot(0 -> 3) appliqué au moteur");
    check (strip.getDisplayedPosition (0) == 3 && strip.getDisplayedPosition (1) == 0, "La bande affiche le nouvel ordre");
    for (int i = 0; i < 90; ++i) editor.tickForTesting (1.0f / 60.0f);
    check (tileCentre (0).getDistanceFrom (end) < 2.0f, "Animation terminée : la tuile du slot 0 occupe la position 3");
    check (content.inspector.getSelected() == 0, "Réordonner ne change pas le module affiché en haut (sélection = identité du slot)");

    // --- Ordre modifié ailleurs (restauration d'état, annulation) : la bande se resynchronise sans verrou.
    processor.setSlotOrder ({ 9, 8, 7, 6, 5, 4, 3, 2, 1, 0 });
    for (int i = 0; i < 120; ++i) editor.tickForTesting (1.0f / 60.0f);
    check (strip.getDisplayedPosition (9) == 0 && strip.getDisplayedPosition (0) == 9, "Ordre changé hors interface : la bande suit");

    // --- Rafale de déplacements depuis le thread de messages, interface qui tourne : la vue suit toujours l'ordre du moteur.
    {
        unsigned x = 17u;
        bool followed = true;
        for (int i = 0; i < 200; ++i)
        {
            x = x * 1664525u + 1013904223u;
            processor.moveSlot ((int) (x >> 8) % 10, (int) (x >> 16) % 10);
            editor.tickForTesting (1.0f / 60.0f);
        }
        for (int i = 0; i < 120; ++i) editor.tickForTesting (1.0f / 60.0f);
        const auto order = processor.getSlotOrder();
        for (int pos = 0; pos < 10; ++pos)
            followed = followed && strip.getDisplayedPosition (order[(std::size_t) pos]) == pos;
        check (aura::RackRouting::isValid (order) && followed, "200 déplacements enchaînés : permutation valide, vue synchronisée");
    }

    processor.setSlotOrder (aura::RackRouting::identity());
    for (int i = 0; i < 120; ++i) editor.tickForTesting (1.0f / 60.0f);

    // --- Sélection, bypass, retrait, ajout rapide.
    {
        auto click = [&] (juce::Point<float> p) { strip.mouseDown (mouseEvent (strip, p, p, false)); strip.mouseUp (mouseEvent (strip, p, p, false)); };
        click (tileCentre (5));
        check (content.inspector.getSelected() == 5 && processor.getUiSettings().selectedSlot == 5, "Clic sur une tuile : le module s'affiche en grand et la sélection est mémorisée");
        check (content.inspector.getCard (5).isVisible() && ! content.inspector.getCard (4).isVisible(), "Zone haute : une seule carte visible");

        auto* bypass = processor.getValueTreeState().getParameter (aura::params::bypass (6));
        check (bypass->getValue() < 0.5f, "Slot 7 actif au départ");
        click (strip.getBypassRect (6).getCentre());
        check (bypass->getValue() > 0.5f, "Pastille LED : bypass du module");
        click (strip.getBypassRect (6).getCentre());
        check (bypass->getValue() < 0.5f, "Pastille LED : réactivation");

        click (strip.getRemoveRect (4).getCentre());
        check (processor.getSlotModule (4) == ModuleType::none, "Croix : le module est retiré (slot vide)");
        strip.showAddMenuForTesting (4, (int) ModuleType::e9ds);
        for (int i = 0; i < 5; ++i) editor.tickForTesting (1.0f / 60.0f);
        check (processor.getSlotModule (4) == ModuleType::e9ds && content.inspector.getSelected() == 4, "Ajout rapide : E9DS inséré et affiché en grand");
        check (processor.getValueTreeState().getParameter (aura::params::generic (4, aura::AuraE9DS::threshold))->getValue()
                   == (float) aura::AuraE9DS::info().params[0].defaultNormalised(), "E9DS ajouté : paramètres aux valeurs par défaut du module");
    }

    // --- Réglages ⚙️ : langue, aide, LUFS ; persistance dans l'état de la session.
    {
        auto u = processor.getUiSettings();
        u.language = 1; u.info = true; u.loudness = true;
        processor.setUiSettings (u);
        for (int i = 0; i < 5; ++i) { renderAudio (processor, fs, 4); editor.tickForTesting (1.0f / 60.0f); }
        check (aura::ui::i18n::getLanguageIndex() == 1 && aura::ui::i18n::tr (aura::ui::i18n::Key::chain) == "CHAIN", "Langue : English appliquée à l'interface");
        check (content.loudness.isVisible() && processor.isLoudnessEnabled(), "LUFS activé : module visible et mesure active côté moteur");

        juce::MemoryBlock state;
        processor.getStateInformation (state);
        AuraRackProcessor restored;
        restored.setProcessingPrecision (juce::AudioProcessor::doublePrecision);
        restored.setPlayConfigDetails (4, 2, fs, 512);
        restored.prepareToPlay (fs, 512);
        restored.setStateInformation (state.getData(), (int) state.getSize());
        check (restored.getUiSettings() == processor.getUiSettings(), "Session restaurée : langue, aide, LUFS et sélection identiques");
        check (restored.getUiSettings().loudness && restored.isLoudnessEnabled(), "Session restaurée : la mesure LUFS est réactivée");
        check (restored.getSlotModule (4) == ModuleType::e9ds, "Session restaurée : le module E9DS est restauré");
    }

    // --- Rendu PNG.
    for (int i = 0; i < 120; ++i) { renderAudio (processor, fs, 8); editor.tickForTesting (1.0f / 60.0f); }
    if (argc > 1)
    {
        const juce::File dir { juce::String (juce::CharPointer_UTF8 (argv[1])) };
        dir.createDirectory();
        auto image = editor.createComponentSnapshot (editor.getLocalBounds(), true, 1.0f);
        juce::PNGImageFormat png;
        juce::FileOutputStream out (dir.getChildFile ("auraRack-editor.png"));
        out.setPosition (0); out.truncate();
        png.writeImageToStream (image, out);
        // Façades seules : grande et compacte, W9BMK1 (Auto Focus) et L9A (vue GR et vue Output).
        auto snapshot = [&] (juce::Component& c, int w, int h, const char* file)
        {
            c.setBounds (0, 0, w, h);
            auto img = c.createComponentSnapshot (c.getLocalBounds(), true, 1.0f);
            juce::FileOutputStream o (dir.getChildFile (file));
            o.setPosition (0); o.truncate();
            png.writeImageToStream (img, o);
        };
        {
            const aura::ui::FaceplateFeed feed { 7.5f, 0.85f, -31.0f };
            aura::ui::W9bFaceplate wide (processor.getValueTreeState(), 9), compact (processor.getValueTreeState(), 9);
            wide.setBounds (0, 0, 1000, 385); compact.setBounds (0, 0, 240, 262);
            for (int i = 0; i < 120; ++i) { wide.tick (1.0f / 60.0f, feed); compact.tick (1.0f / 60.0f, feed); }
            snapshot (wide, 1000, 385, "w9bmk1-wide.png");
            snapshot (compact, 240, 262, "w9bmk1-compact.png");
        }
        {
            aura::ui::L9aFaceplate wide (processor.getValueTreeState(), 8), compact (processor.getValueTreeState(), 8);
            wide.setBounds (0, 0, 1000, 385); compact.setBounds (0, 0, 240, 262);
            for (int i = 0; i < 120; ++i) { wide.tick (1.0f / 60.0f, { 6.0f, 0.0f, -20.0f }); compact.tick (1.0f / 60.0f, { 6.0f, 0.0f, -20.0f }); }
            snapshot (wide, 1000, 385, "l9a-wide-gr.png");
            snapshot (compact, 240, 262, "l9a-compact-gr.png");
            setNormalised (aura::params::generic (8, LL::meter), 1.0f);
            for (int i = 0; i < 120; ++i) wide.tick (1.0f / 60.0f, { 6.0f, 0.0f, -20.0f });
            snapshot (wide, 1000, 385, "l9a-wide-out.png");
            setNormalised (aura::params::generic (8, LL::meter), 0.0f);
        }
        // Disposition complète : E9DS actif + LUFS en anglais, puis W9BMK1 en français sans LUFS.
        auto fullSnapshot = [&] (const char* file)
        {
            for (int i = 0; i < 30; ++i) { renderAudio (processor, fs, 6); editor.tickForTesting (1.0f / 60.0f); }
            auto img = editor.createComponentSnapshot (editor.getLocalBounds(), true, 1.0f);
            juce::FileOutputStream o (dir.getChildFile (file));
            o.setPosition (0); o.truncate();
            png.writeImageToStream (img, o);
        };
        fullSnapshot ("auraRack-e9ds-lufs-en.png");
        {
            aura::ui::E9dsFaceplate wide (processor.getValueTreeState(), 4), compact (processor.getValueTreeState(), 4);
            wide.setBounds (0, 0, 1000, 385); compact.setBounds (0, 0, 240, 262);
            for (int i = 0; i < 120; ++i) { wide.tick (1.0f / 60.0f, { 6.0f, 0.0f, -22.0f }); compact.tick (1.0f / 60.0f, { 6.0f, 0.0f, -22.0f }); }
            snapshot (wide, 1000, 385, "e9ds-wide.png");
            snapshot (compact, 240, 262, "e9ds-compact.png");
        }
        {
            const auto keep5 = processor.getSlotModule (5), keep6 = processor.getSlotModule (6);
            processor.setSlotModule (5, aura::ModuleType::v9c3); processor.setSlotModule (6, aura::ModuleType::v9p2);
            aura::ui::VertigoFaceplate v3 (aura::ModuleType::v9c3, processor.getValueTreeState(), 5), v2 (aura::ModuleType::v9p2, processor.getValueTreeState(), 6);
            v3.setBounds (0, 0, 1000, 385); v2.setBounds (0, 0, 1000, 385);
            for (int i = 0; i < 120; ++i) { v3.tick (1.0f / 60.0f, { 9.0f, 0.0f, -4.0f }); v2.tick (1.0f / 60.0f, { 0.0f, 0.0f, 21.0f }); }
            snapshot (v3, 1000, 385, "v9c3-wide.png");
            snapshot (v2, 1000, 385, "v9p2-wide.png");
            processor.setSlotModule (5, keep5); processor.setSlotModule (6, keep6);
        }
        {
            auto u = processor.getUiSettings();
            u.language = 0; u.loudness = false; u.selectedSlot = 9;
            processor.setUiSettings (u);
            fullSnapshot ("auraRack-w9b-fr.png");
            u.selectedSlot = 1;   // module générique : grands potentiomètres homogènes
            processor.setUiSettings (u);
            fullSnapshot ("auraRack-comp-generic.png");
        }
        std::printf ("  rendu : %s (%d x %d)\n", dir.getChildFile ("auraRack-editor.png").getFullPathName().toRawUTF8(), image.getWidth(), image.getHeight());
    }

    // --- Façades : chaque contrôle visible correspond à UN paramètre réel, distinct (aucun bouton décoratif ni doublon).
    {
        auto bindingCheck = [&] (juce::Component& face, int slot, int expectedKnobs, int expectedSwitches, const char* name)
        {
            auto& apvts = processor.getValueTreeState();
            auto snapshot = [&] {
                std::vector<float> v;
                for (int i = 0; i < aura::kMaxModuleParams; ++i) v.push_back (apvts.getParameter (aura::params::generic (slot, i))->getValue());
                v.push_back (apvts.getParameter (aura::params::bypass (slot))->getValue());
                return v;
            };
            std::set<int> claimed;
            int knobs = 0, switches = 0, others = 0, orphan = 0;
            for (auto* child : face.getChildren())
            {
                const auto before = snapshot();
                if (auto* sl = dynamic_cast<juce::Slider*> (child))
                {
                    ++knobs;
                    sl->setValue (sl->getValue() > 0.5 * (sl->getMinimum() + sl->getMaximum()) ? sl->getMinimum() + 0.2 * (sl->getMaximum() - sl->getMinimum())
                                                                                              : sl->getMinimum() + 0.8 * (sl->getMaximum() - sl->getMinimum()),
                                  juce::sendNotificationSync);
                }
                else if (auto* bt = dynamic_cast<juce::ToggleButton*> (child))
                {
                    ++switches;
                    bt->setToggleState (! bt->getToggleState(), juce::sendNotificationSync);
                }
                else { ++others; continue; }
                const auto after = snapshot();
                int changed = 0, index = -1;
                for (std::size_t i = 0; i < before.size(); ++i) if (std::abs (before[i] - after[i]) > 1.0e-6f) { ++changed; index = (int) i; }
                if (changed != 1 || ! claimed.insert (index).second) ++orphan;
            }
            char what[160];
            std::snprintf (what, sizeof (what), "%s : %d potards + %d interrupteurs, chacun lié à un paramètre distinct (aucun mort, aucun doublon)", name, knobs, switches);
            check (knobs == expectedKnobs && switches == expectedSwitches && others == 0 && orphan == 0, what);
        };
        aura::ui::W9bFaceplate w9 (processor.getValueTreeState(), 9);
        aura::ui::L9aFaceplate l9 (processor.getValueTreeState(), 8);
        w9.setBounds (0, 0, 1000, 385); l9.setBounds (0, 0, 1000, 385);
        bindingCheck (w9, 9, 7, 2, "W9BMK1");
        bindingCheck (l9, 8, 2, 3, "L9A");
        aura::ui::E9dsFaceplate e9 (processor.getValueTreeState(), 4);
        e9.setBounds (0, 0, 1000, 385);
        bindingCheck (e9, 4, 6, 3, "E9DS (avec Lookahead)");
        processor.setSlotModule (5, aura::ModuleType::v9c3); processor.setSlotModule (6, aura::ModuleType::v9p2);
        aura::ui::VertigoFaceplate v3 (aura::ModuleType::v9c3, processor.getValueTreeState(), 5), v2 (aura::ModuleType::v9p2, processor.getValueTreeState(), 6);
        v3.setBounds (0, 0, 1000, 385); v2.setBounds (0, 0, 1000, 385);
        bindingCheck (v3, 5, 8, 3, "V9C3");
        bindingCheck (v2, 6, 5, 4, "V9P2");

        // Auto Focus : en AUTO le potard Thresh n'est plus réglable (le moteur le pilote) ; en MANUAL il l'est de nouveau.
        auto& apvts = processor.getValueTreeState();
        apvts.getParameter (aura::params::generic (9, aura::AuraW9B::autoFocus))->setValueNotifyingHost (1.0f);
        w9.tick (1.0f / 60.0f, { 0.0f, 0.0f, -31.0f });
        juce::Slider* thresh = nullptr;
        int index = 0;
        for (auto* child : w9.getChildren()) if (auto* sl = dynamic_cast<juce::Slider*> (child)) { if (index++ == 1) thresh = sl; }
        check (thresh != nullptr && ! thresh->isEnabled(), "W9BMK1 en AUTO : le potard Thresh est piloté par le moteur (non réglable)");
        apvts.getParameter (aura::params::generic (9, aura::AuraW9B::autoFocus))->setValueNotifyingHost (0.0f);
        w9.tick (1.0f / 60.0f, { 0.0f, 0.0f, -31.0f });
        check (thresh != nullptr && thresh->isEnabled(), "W9BMK1 en MANUAL : le potard Thresh est de nouveau réglable");
    }


    // =================================================================================================
    // Finalisation : A / B, presets, auto-gain, utilitaire stéréo, latence, cohérence de TOUS les modules.
    // =================================================================================================
    {
        std::puts ("\n-- A / B, presets, session");
        const double rate = 48000.0;
        auto makeProc = [&] (std::unique_ptr<AuraRackProcessor>& p)
        {
            p = std::make_unique<AuraRackProcessor>();
            p->setProcessingPrecision (juce::AudioProcessor::doublePrecision);
            p->setPlayConfigDetails (4, 2, rate, 512);
            p->prepareToPlay (rate, 512);
        };
        auto valueOf = [] (AuraRackProcessor& p, const juce::String& id) { return p.getValueTreeState().getParameter (id)->getValue(); };
        auto setPlainOn = [] (AuraRackProcessor& p, const juce::String& id, float plain)
        {
            auto* prm = p.getValueTreeState().getParameter (id);
            prm->setValueNotifyingHost (prm->convertTo0to1 (plain));
        };

        std::unique_ptr<AuraRackProcessor> q;
        makeProc (q);

        // Presets d'usine : modules, ordre, auto-gain, rendu sain, non modifié juste après le chargement.
        const auto& factory = aura::presets::factory();
        check (AuraRackProcessor::getNumFactoryPresets() == 4, "4 presets d'usine : Voix, Piano / Acoustique, Bus, Mastering");
        for (int i = 0; i < AuraRackProcessor::getNumFactoryPresets(); ++i)
        {
            q->loadFactoryPreset (i);
            bool typesOk = true, agcOk = true;
            for (int s = 0; s < aura::kNumSlots; ++s)
            {
                const auto want = s < (int) factory[(std::size_t) i].slots.size() ? factory[(std::size_t) i].slots[(std::size_t) s].type : aura::ModuleType::none;
                typesOk = typesOk && q->getSlotModule (s) == want;
                const bool wantAgc = s < (int) factory[(std::size_t) i].slots.size() && factory[(std::size_t) i].slots[(std::size_t) s].autoGain;
                agcOk = agcOk && (valueOf (*q, aura::params::autoGainSlot (s)) > 0.5f) == wantAgc;
            }
            for (int b = 0; b < 6; ++b) renderAudio (*q, rate, 24);   // 3 s de signal musical
            juce::AudioBuffer<double> buf (4, 512); buf.clear(); juce::MidiBuffer midi; q->processBlock (buf, midi);
            bool finite = true;
            for (int ch = 0; ch < 2; ++ch) for (int n = 0; n < 512; ++n) finite = finite && std::isfinite (buf.getSample (ch, n)) && std::abs (buf.getSample (ch, n)) < 8.0;
            char what[160];
            std::snprintf (what, sizeof what, "Preset « %s » : modules, ordre et auto-gain conformes, rendu fini, non modifié", factory[(std::size_t) i].nameFr);
            check (typesOk && agcOk && finite && q->getSlotOrder() == aura::RackRouting::identity() && q->getPresetIndex() == i && ! q->isPresetModified(), what);
        }
        q->loadFactoryPreset (0);
        for (int b = 0; b < 4; ++b) renderAudio (*q, rate, 12);
        check (q->getMeters().activeLatency.load() == 48,
               "Latence totale : preset Voix (E9DS Lookahead 1 ms à 48 kHz) = 48 échantillons (valeur affichée en barre ; le minuteur la transmet à l'hôte)", q->getMeters().activeLatency.load());
        q->loadFactoryPreset (3);
        setPlainOn (*q, aura::params::generic (1, aura::AuraV9C3::focus), 5.0f);
        check (q->isPresetModified(), "Preset retouché : détecté comme modifié");

        // A / B.
        q->loadFactoryPreset (0);                       // Voix
        const auto v9c3Slot = 2;
        const float ratioA = valueOf (*q, aura::params::generic (v9c3Slot, aura::AuraV9C3::ratio));
        check (! q->isAbStored (1) && q->getAbActive() == 0, "A actif au départ ; B vide");
        q->selectAb (1);
        check (q->getAbActive() == 1 && q->isAbStored (1) && std::abs (valueOf (*q, aura::params::generic (v9c3Slot, aura::AuraV9C3::ratio)) - ratioA) < 1.0e-6f,
              "Bascule vers B : B part d'une copie de A");
        setPlainOn (*q, aura::params::generic (v9c3Slot, aura::AuraV9C3::ratio), 4.0f);          // Limit en B
        q->setSlotModule (3, aura::ModuleType::limit);                                           // EQ -> Limit en B
        q->setSlotModule (6, aura::ModuleType::delay);
        const float ratioB = valueOf (*q, aura::params::generic (v9c3Slot, aura::AuraV9C3::ratio));
        q->selectAb (0);
        check (std::abs (valueOf (*q, aura::params::generic (v9c3Slot, aura::AuraV9C3::ratio)) - ratioA) < 1.0e-6f
                   && q->getSlotModule (3) == aura::ModuleType::eq && q->getSlotModule (6) == aura::ModuleType::none,
               "Retour sur A : réglages ET modules de A restaurés");
        q->selectAb (1);
        check (std::abs (valueOf (*q, aura::params::generic (v9c3Slot, aura::AuraV9C3::ratio)) - ratioB) < 1.0e-6f
                   && q->getSlotModule (3) == aura::ModuleType::limit && q->getSlotModule (6) == aura::ModuleType::delay,
               "Retour sur B : les retouches de B sont conservées");
        renderAudio (*q, rate, 24);
        {   // L'ordre fait partie de l'instantané.
            q->selectAb (0);
            q->moveSlot (0, 3);
            const auto orderA = q->getSlotOrder();
            q->selectAb (1);
            const bool bUntouched = q->getSlotOrder() == aura::RackRouting::identity();
            q->selectAb (0);
            check (bUntouched && q->getSlotOrder() == orderA, "L'ordre des slots est mémorisé séparément en A et en B");
        }
        q->copyToOtherAb();
        q->selectAb (1);
        check (q->isAbStored (1) && q->getSlotModule (3) == aura::ModuleType::eq && q->getSlotOrder() == q->getSlotOrder()
                   && q->getSlotModule (6) == aura::ModuleType::none, "Copie vers l'autre emplacement : B devient identique à A");
        q->setSlotModule (3, aura::ModuleType::limit);   // B se distingue de nouveau
        q->setSlotModule (6, aura::ModuleType::delay);

        // Session : preset, A / B, globaux, auto-gain par slot.
        setPlainOn (*q, aura::params::width, 140.0f);
        setPlainOn (*q, aura::params::bassMono, 3.0f);
        setPlainOn (*q, aura::params::mono, 1.0f);
        setPlainOn (*q, aura::params::autoGain, 1.0f);
        setPlainOn (*q, aura::params::autoGainSlot (6), 1.0f);
        juce::MemoryBlock blob;
        q->getStateInformation (blob);
        std::unique_ptr<AuraRackProcessor> r;
        makeProc (r);
        r->setStateInformation (blob.getData(), (int) blob.getSize());
        check (r->getPresetIndex() == q->getPresetIndex() && r->getAbActive() == 1 && r->isAbStored (0) && r->isAbStored (1),
               "Session restaurée : preset, A / B actif et les deux emplacements");
        check (std::abs (valueOf (*r, aura::params::width) - valueOf (*q, aura::params::width)) < 1.0e-6f && valueOf (*r, aura::params::mono) > 0.5f
                   && valueOf (*r, aura::params::autoGain) > 0.5f && valueOf (*r, aura::params::autoGainSlot (6)) > 0.5f
                   && std::abs (valueOf (*r, aura::params::bassMono) - valueOf (*q, aura::params::bassMono)) < 1.0e-6f,
               "Session restaurée : Width, Bass Mono, Mono, auto-gain rack et slot");
        check (r->getSlotModule (3) == aura::ModuleType::limit && r->getSlotModule (6) == aura::ModuleType::delay, "Session restaurée : modules du B actif");
        r->selectAb (0);
        check (r->getSlotModule (3) == aura::ModuleType::eq && r->getSlotModule (6) == aura::ModuleType::none && valueOf (*r, aura::params::mono) < 0.5f,
              "Session restaurée : l'emplacement A, y compris le global Mono, revient intact");
        r->clearRack();
        bool allEmpty = true; for (int s = 0; s < aura::kNumSlots; ++s) allEmpty = allEmpty && r->getSlotModule (s) == aura::ModuleType::none;
        check (allEmpty && r->getPresetIndex() == -1, "Rack vide : tous les slots vides, aucun preset");

        // Sessions d'avant (sans A / B ni nouveaux paramètres) : les valeurs par défaut s'appliquent.
        {
            juce::MemoryBlock legacy;
            std::unique_ptr<AuraRackProcessor> old;
            makeProc (old);
            old->getStateInformation (legacy);
            auto xml = juce::AudioProcessor::getXmlFromBinary (legacy.getData(), (int) legacy.getSize());
            bool stripped = false;
            if (xml != nullptr)
            {
                for (auto* child = xml->getFirstChildElement(); child != nullptr;)
                {
                    auto* next = child->getNextElement();
                    const auto id = child->getStringAttribute ("id");
                    if (id == aura::params::autoGain || id == aura::params::width || id == aura::params::bassMono || id == aura::params::mono || id.endsWith ("_agc") || child->hasTagName ("AB"))
                    { xml->removeChildElement (child, true); stripped = true; }
                    child = next;
                }
                juce::MemoryBlock rebuilt;
                juce::AudioProcessor::copyXmlToBinary (*xml, rebuilt);
                std::unique_ptr<AuraRackProcessor> fresh;
                makeProc (fresh);
                fresh->setStateInformation (rebuilt.getData(), (int) rebuilt.getSize());
                check (stripped && std::abs (valueOf (*fresh, aura::params::width) - 100.0f / 200.0f) < 1.0e-6f && fresh->getAbActive() == 0 && ! fresh->isAbStored (1),
                       "Session d'une version antérieure : nouveaux paramètres par défaut (Width 100 %), A actif, B vide");
            }
        }

        // Barre du haut : boutons A / B, menu des presets, texte du preset.
        std::puts ("\n-- barre du haut");
        auto& bar = content.topBar;
        const int abBefore = processor.getAbActive();
        bar.getAbButton (1).onClick();
        for (int i = 0; i < 3; ++i) editor.tickForTesting (1.0f / 60.0f);
        const bool toB = processor.getAbActive() == 1;
        bar.getAbButton (0).onClick();
        for (int i = 0; i < 3; ++i) editor.tickForTesting (1.0f / 60.0f);
        check (abBefore == 0 && toB && processor.getAbActive() == 0, "Boutons A / B : un clic bascule la configuration complète");
        int menuItems = 0;
        { auto menu = bar.buildPresetMenu(); juce::PopupMenu::MenuItemIterator it (menu); while (it.next()) if (! it.getItem().isSeparator) ++menuItems; }
        check (menuItems == 5, "Menu des presets : 4 presets + « Rack vide »", menuItems);
        check (bar.getHeight() == aura::ui::TopBar::kHeight && bar.getPresetButton().isVisible() && bar.getCopyButton().isVisible(), "Barre du haut : deuxième rangée (presets, A / B, utilitaire, latence) visible");

        // Cohérence : tous les modules dans la disposition, les menus, le dictionnaire et la persistance.
        std::puts ("\n-- cohérence des 12 modules");
        bool dictOk = true;
        for (std::size_t k = 0; k < (std::size_t) aura::ui::i18n::Key::count; ++k)
            for (std::size_t l = 0; l < (std::size_t) aura::ui::i18n::Lang::count; ++l)
                dictOk = dictOk && std::strlen (aura::ui::i18n::kTable[k][l]) > 0;
        check (dictOk, "Dictionnaire FR / EN : aucune entrée vide");
        bool described = true, accented = true, named = true;
        for (int t = 1; t < aura::kNumModuleTypes; ++t)
        {
            const auto type = (ModuleType) t;
            described = described && aura::ui::i18n::descriptionKey (type) != aura::ui::i18n::Key::modNone;
            accented = accented && aura::ui::colours::accentFor (type) != aura::ui::colours::stroke;
            named = named && std::strlen (aura::moduleInfo (type).name) > 0 && ! aura::moduleInfo (type).params.empty();
        }
        check (described && accented && named, "Chaque module : nom, paramètres, description FR / EN et couleur d'accent");

        // Les 12 modules posés (en deux fois, 10 slots), affichés en grand : façade ou cartes génériques, sans erreur, état restauré.
        std::unique_ptr<AuraRackProcessor> all;
        makeProc (all);
        bool shownOk = true, restoredOk = true, renderOk = true;
        for (int pass = 0; pass < 2; ++pass)
        {
            for (int s = 0; s < aura::kNumSlots; ++s)
                all->setSlotModule (s, (ModuleType) (1 + (pass * aura::kNumSlots + s) % (aura::kNumModuleTypes - 1)));
            for (int b = 0; b < 4; ++b) renderAudio (*all, rate, 12);
            juce::AudioBuffer<double> chk (4, 512); chk.clear(); juce::MidiBuffer midi2; all->processBlock (chk, midi2);
            for (int ch = 0; ch < 2; ++ch) for (int n = 0; n < 512; ++n) renderOk = renderOk && std::isfinite (chk.getSample (ch, n));

            AuraRackEditor ed (*all);
            ed.setBounds (0, 0, 1280, 780);
            for (int s = 0; s < aura::kNumSlots; ++s)
            {
                ed.getContent().selectSlot (s);
                for (int i = 0; i < 3; ++i) ed.tickForTesting (1.0f / 60.0f);
                shownOk = shownOk && ed.getContent().inspector.getSelected() == s && all->getSlotModule (s) != ModuleType::none;
            }
            juce::MemoryBlock b2; all->getStateInformation (b2);
            std::unique_ptr<AuraRackProcessor> back; makeProc (back);
            back->setStateInformation (b2.getData(), (int) b2.getSize());
            for (int s = 0; s < aura::kNumSlots; ++s) restoredOk = restoredOk && back->getSlotModule (s) == all->getSlotModule (s);
        }
        check (shownOk && restoredOk && renderOk, "Les 12 modules : posés, affichés dans la zone haute, rendu fini, session restaurée à l'identique");
    }

    std::printf ("\n%d / %d vérifications réussies\n", checks - failures, checks);
    return failures == 0 ? 0 : 1;
}
