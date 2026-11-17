// AuraRack — NextDSP
// Test de l'éditeur sans fenêtre : glisser-déposer des slots, resynchronisation sur l'ordre du moteur,
// puis rendu de l'interface en PNG (argument facultatif : dossier de sortie).

#include "../Source/PluginEditor.h"

#include <cstdio>

namespace
{
int failures = 0, checks = 0;

void check (bool ok, const char* what)
{
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
    juce::AudioBuffer<double> buf (3, 512);   // 2 canaux + Sidechain
    juce::MidiBuffer midi;
    for (int b = 0; b < blocks; ++b)
    {
        for (int i = 0; i < 512; ++i)
        {
            const double t = (double) (b * 512 + i) / fs;
            const double kick = (std::fmod (t, 0.5) < 0.1) ? 0.9 * std::sin (2.0 * juce::MathConstants<double>::pi * 60.0 * t) : 0.0;
            const double music = 0.5 * std::sin (2.0 * juce::MathConstants<double>::pi * 220.0 * t) + 0.3 * std::sin (2.0 * juce::MathConstants<double>::pi * 3000.0 * t);
            buf.setSample (0, i, music + kick); buf.setSample (1, i, music - kick * 0.5); buf.setSample (2, i, kick);
        }
        p.processBlock (buf, midi);
    }
}
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    std::puts ("AuraRack — test de l'éditeur");

    const double fs = 384000.0;
    AuraRackProcessor processor;
    processor.setProcessingPrecision (juce::AudioProcessor::doublePrecision);
    processor.setPlayConfigDetails (3, 2, fs, 512);
    processor.prepareToPlay (fs, 512);

    auto setParam = [&] (const juce::String& id, float plain)
    {
        auto* p = processor.getValueTreeState().getParameter (id);
        p->setValueNotifyingHost (p->convertTo0to1 (plain));
    };
    auto setNormalised = [&] (const juce::String& id, float n) { processor.getValueTreeState().getParameter (id)->setValueNotifyingHost (n); };

    using aura::ModuleType;
    const ModuleType layout[] = { ModuleType::eq, ModuleType::comp, ModuleType::comp, ModuleType::comp, ModuleType::duck,
                                  ModuleType::clip, ModuleType::limit, ModuleType::delay, ModuleType::verb, ModuleType::none };
    for (int s = 0; s < 10; ++s)
        processor.setSlotModule (s, layout[s]);
    // Slot 2 : Aura-Tube ; slot 3 : L-Comp (choix d'algorithme = paramètre 1, normalisé 0 / 0,5 / 1).
    setNormalised (aura::params::generic (2, aura::AuraComp::algorithm), 0.5f);
    setNormalised (aura::params::generic (3, aura::AuraComp::algorithm), 1.0f);
    setNormalised (aura::params::generic (1, aura::AuraComp::peakReduction), 0.75f);
    setNormalised (aura::params::generic (2, aura::AuraComp::threshold), 0.65f);
    setParam (aura::params::mix (7), 30.0f);
    renderAudio (processor, fs, 200);

    AuraRackEditor editor (processor);
    editor.setBounds (0, 0, AuraRackEditor::kDesignWidth, AuraRackEditor::kDesignHeight);
    for (int i = 0; i < 20; ++i) { renderAudio (processor, fs, 20); editor.tickForTesting (1.0f / 60.0f); }

    // --- Glisser-déposer : le slot 0 (position 0) est déposé sur la cellule de la position 3.
    auto* rack = dynamic_cast<aura::ui::RackView*> (editor.getChildComponent (0)->getChildComponent (1));
    check (rack != nullptr, "RackView trouvé dans l'éditeur");
    if (rack == nullptr) return 1;

    auto& slot0 = rack->getSlotComponent (0);
    const auto origin = juce::Point<float> (40.0f, 14.0f);   // dans l'en-tête, hors liste déroulante et bouton
    slot0.mouseDown (mouseEvent (slot0, origin, origin, false));
    const auto cell3Centre = rack->getSlotComponent (3).getBounds().getCentre().toFloat();
    const auto delta = cell3Centre - slot0.getBounds().getCentre().toFloat();
    const auto rackStart = slot0.getPosition().toFloat() + origin;   // souris, en coordonnées du rack
    for (int step = 1; step <= 10; ++step)
    {
        const auto mouseInRack = rackStart + delta * ((float) step / 10.0f);
        slot0.mouseDrag (mouseEvent (slot0, mouseInRack - slot0.getPosition().toFloat(), origin, true));   // position locale au slot, comme JUCE
        editor.tickForTesting (1.0f / 60.0f);
    }
    check (processor.getSlotOrder() == aura::RackRouting::identity(), "Pendant le glissement : l'ordre du moteur n'est pas modifié");
    slot0.mouseUp (mouseEvent (slot0, rackStart + delta - slot0.getPosition().toFloat(), origin, true));

    const auto expected = aura::RackRouting::moved (aura::RackRouting::identity(), 0, 3);
    check (processor.getSlotOrder() == expected, "Au relâchement : moveSlot(0 -> 3) appliqué au moteur");
    check (rack->getDisplayedPosition (0) == 3 && rack->getDisplayedPosition (1) == 0, "La vue affiche le nouvel ordre");
    for (int i = 0; i < 90; ++i) editor.tickForTesting (1.0f / 60.0f);
    check (rack->getSlotComponent (0).getBounds().getCentre().toFloat().getDistanceFrom (cell3Centre) < 2.0f, "Animation terminée : le slot 0 occupe la cellule 3");

    // --- Ordre modifié ailleurs (restauration d'état, annulation) : la vue se resynchronise sans verrou.
    processor.setSlotOrder ({ 9, 8, 7, 6, 5, 4, 3, 2, 1, 0 });
    for (int i = 0; i < 120; ++i) editor.tickForTesting (1.0f / 60.0f);
    check (rack->getDisplayedPosition (9) == 0 && rack->getDisplayedPosition (0) == 9, "Ordre changé hors interface : la vue suit");

    // --- moveSlot concurrent : 4 fils s'agitent pendant que l'interface tourne, l'ordre reste une permutation valide.
    {
        std::atomic<bool> stop { false };
        std::vector<std::thread> threads;
        for (int t = 0; t < 3; ++t)
            threads.emplace_back ([&, t] { unsigned x = 17u + (unsigned) t; while (! stop) { x = x * 1664525u + 1013904223u; processor.moveSlot ((int) (x >> 8) % 10, (int) (x >> 16) % 10); } });
        bool valid = true;
        for (int i = 0; i < 200; ++i)
        {
            valid = valid && aura::RackRouting::isValid (processor.getSlotOrder());
            editor.tickForTesting (1.0f / 60.0f);
        }
        stop = true;
        for (auto& th : threads) th.join();
        check (valid && aura::RackRouting::isValid (processor.getSlotOrder()), "moveSlot concurrent (CAS) : l'ordre reste une permutation valide");
    }

    // --- Rendu PNG.
    processor.setSlotOrder (aura::RackRouting::identity());
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
        std::printf ("  rendu : %s (%d x %d)\n", dir.getChildFile ("auraRack-editor.png").getFullPathName().toRawUTF8(), image.getWidth(), image.getHeight());
    }

    std::printf ("\n%d / %d vérifications réussies\n", checks - failures, checks);
    return failures == 0 ? 0 : 1;
}
