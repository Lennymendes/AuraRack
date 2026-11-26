# AuraRack 1.0 — NextDSP · rack multi-effets DXD (VST3 / AU)

Rack modulaire de mixage / mastering : **10 slots en série**, réordonnables à chaud, **12 modules** DSP natifs,
traitement intégral en **64 bits** de 44,1 kHz au **DXD 352,8 / 384 kHz**. Marque **NextDSP**, suite **AuraSuite**
(Aura, AuraEngine, AuraDrum, AuraPitch, AuraRack).

C++20 · JUCE 8.0.15 (la même version qu'AuraEngine / AuraDAW) · VST3 (macOS, Windows, Linux) + AU (macOS).

**État** : **version complète (candidate finale, à valider en DAW)** : Étape 5 (auto-gain, A / B, presets d'usine, utilitaire Stéréo / Largeur / Mono grave, latence totale et CPU en barre) en plus de l'Étape 4 (disposition type T-RackS, menu ⚙️ FR / EN, mesure LUFS, module **E9DS**, **V9C3**, **V9P2**) et des Étapes 1, 2 et 3 du cahier des charges : processeur, bus Sidechain, moteur de rack lock-free, 9 modules,
**AuraComp à 3 algorithmes** (Aura-Opto, Aura-Tube, L-Comp), **éditeur NextDSP** (anthracite `#0B0C10`, zone haute « module actif » + bande de chaîne
de 10 tuiles, glisser-déposer, réduction de gain et niveaux en direct), et deux modules à façade dédiée : **W9BMK1** (CL 1B, avec
**Auto Focus**) et **L9A** (LA-2A) ; **E9DS** (de-esser, avec Lookahead), **V9C3** (compresseur VCA à Auto Focus) et **V9P2** (préampli double canal) complètent la liste. Voir aussi `AuraRack-Checklist-DAW.md` (séance de validation en DAW).

## Compiler

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release          # JUCE 8.0.15 téléchargé automatiquement
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure   # banc DSP + hôte VST3
```

- JUCE local : `-DAURA_JUCE_DIR=/chemin/JUCE`.
- Linux : `libasound2-dev libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxext-dev libfreetype-dev libfontconfig1-dev`.
- Noyau DSP seul, sans JUCE : `-DAURA_BUILD_PLUGIN=OFF` (compile `aura_rack_core` et `aurarack_dsp_test`).
- CI : `.github/workflows/build.yml` compile macOS (universel arm64 + x86_64), Windows et Linux, lance les deux bancs
  et publie `AuraRack.vst3` (+ `AuraRack.component` sur macOS) en artefacts.

## Arborescence

| Fichier | Rôle |
|---|---|
| `Source/DSP/AuraDspCore.h` | SVF TPT, lisseurs, rampes, ligne de retard (Lagrange 3e ordre, reset O(1)), sur-échantillonneur polyphase, minimum glissant |
| `Source/DSP/AuraModuleDSP.h` | Classe de base `AuraModuleDSP`, description des paramètres (`ParamSpec`, `ModuleInfo`), bus de clé |
| `Source/DSP/Modules/` | `AuraEQ`, `AuraComp`, `AuraDuck`, `AuraClip`, `AuraLimit`, `AuraDelay`, `AuraVerb`, `AuraW9B`, `AuraL9A`, `AuraE9DS`, `AuraV9C3`, `AuraV9P2` |
| `Source/DSP/AutoGain.h` | Compensation de gain automatique (`LevelMatcher`) : par module et pour le rack |
| `Source/DSP/StereoUtility.h` | Utilitaire Stéréo / Largeur / Mono grave (Mid / Side, Linkwitz-Riley 4e ordre) |
| `Source/Presets.h` | Presets d'usine (Voix, Piano / Acoustique, Bus, Mastering) |
| `Source/DSP/LoudnessMeter.h` | Mesure de sonie BS.1770-4 / EBU R128 (momentané, court terme, intégré, LRA, crête), sans JUCE, sans allocation |
| `Source/DSP/ModuleRegistry.h` | Métadonnées par type, fabrique |
| `Source/Engine/RackEngine.*` | Moteur du rack : 10 slots, ordre atomique, échanges de modules, bypass, Mix compensé, Dry maître |
| `Source/Parameters.*` | Disposition APVTS (168 paramètres), textes qui suivent le module du slot |
| `Source/PluginEditor.*` | Éditeur : une seule horloge `VBlankAttachment`, contenu 1280 × 780 mis à l'échelle |
| `Source/UI/` | `NextLookAndFeel` (vectoriel), `ChainStrip` (bande de chaîne + glisser-déposer), `InspectorView` (zone haute), `SlotComponent`, `TopBar` (+ bouton ⚙️), `SettingsPanel`, `LoudnessBar`, `I18n.h` (dictionnaire FR / EN), `Widgets.h`, `FaceplateKit` (potard, interrupteur, VU, plaque), `Faceplate` (interface + fabrique), `W9bFaceplate`, `L9aFaceplate`, `E9dsFaceplate`, `VertigoFaceplate` (V9C3 + V9P2) |
| `Source/PluginProcessor.*` | Bus principal + Sidechain, traitement float / double, état, latence, CPU |
| `Tests/DspSelfTest.cpp` | Banc DSP, moteur et stress multi-thread, sans JUCE (270 vérifications) |
| `Tests/HostTest.cpp` | Hôte VST3 : charge le binaire compilé comme un DAW (16 vérifications) |
| `Tests/UiTest.cpp` | Éditeur sans fenêtre : glisser-déposer, resynchronisation, rafale de `moveSlot`, sélection / bypass / retrait / ajout, réglages ⚙️ et session restaurée, rendu PNG, liaison de chaque contrôle de façade à un paramètre réel, A / B, presets, session, cohérence des 12 modules (52 vérifications) |

## Architecture audio

**64 bits de bout en bout.** `supportsDoublePrecisionProcessing()` : un hôte 64 bits passe ses tampons `double`
directement ; un hôte 32 bits voit ses échantillons convertis une seule fois en entrée et en sortie. Tout le reste
(filtres, enveloppes, lignes de retard, mélanges) est en `double`.

**Zéro allocation sur le thread audio.** Tout est alloué dans `prepareToPlay()` ou sur le message thread. Le banc
compte les `operator new` pendant 1,5 s de rendu à 352,8 kHz avec les 7 modules, des bypass toutes les 100 ms et deux
réordonnancements : **0**. Le traitement se fait par tranches de 256 échantillons au plus (un bloc plus grand de l'hôte
ne réalloue rien).

**Adaptation DXD.** Les étages non linéaires utilisent l'ADAA du 1er ordre ; le sur-échantillonnage n'est ajouté que
sous 176,4 kHz (×4 sous 88,2 kHz, ×2 jusqu'à 176,4 kHz, ×1 au-delà). Le détecteur True Peak passe de 8× à 2×.

### Threads et échanges lock-free

| Échange | Mécanisme |
|---|---|
| Changer le module d'un slot | Le message thread construit et prépare un `ModuleBox`, le publie par `std::atomic<ModuleBox*>::exchange`. Le thread audio l'adopte au bloc suivant, fond l'ancien et le nouveau en 5 ms, puis renvoie l'ancien par une FIFO SPSC ; le message thread le détruit. |
| Ordre des slots | 10 indices de 4 bits dans un `std::atomic<uint64_t>`. Le thread audio applique le nouvel ordre au creux d'un fondu de 2,5 ms + 2,5 ms. |
| Paramètres | Pointeurs vers les `std::atomic<float>` de l'APVTS, lus une fois par tranche ; un module ne reçoit `setParameter()` que si la valeur a changé. |
| `prepareToPlay` / message thread | `std::mutex` entre ces deux-là uniquement ; le thread audio ne le prend jamais. |

**Réordonnancement sans clic.** Pendant le fondu, le gain s'applique aussi **à l'entrée de chaque slot**. Au moment de
la permutation, chaque module reçoit donc une entrée nulle et continue. Un fondu en sortie seule ne suffit pas : le
changement de source ressortirait 1,5 ms plus tard du limiteur (latence), ou dans les échos d'un Delay ou d'une Reverb.
Le banc l'a montré (pente ×20 avant la correction, ×1,3 après).

### Chaque slot

```
x ──► Dry compensé (retard = latence du module, fondu 5 ms si elle change) ──────────────┐ brut
  └─► Trim In ─► Module (Wet) ─► Mix (Dry aligné × Trim In) ─► Trim Out ─► Bypass ─► y
```

- **Bypass** : crossfade linéaire de 5 ms entre la sortie traitée et l'entrée retardée. Une fois le bypass complet, le
  module n'est plus calculé (CPU) ; à la réactivation il repart d'un état propre (`reset()` en O(1)), masqué par le fondu.
- **Mix 0–100 %** : le Dry est retardé de la latence exacte du module. Avec Limit à Mix 50 % et Master Mix 50 %, une
  impulsion ressort **unique, d'amplitude exacte** (0,25 → 0,25), à la latence annoncée.
- **Trims** −24 / +24 dB, lissés en 20 ms. Un gain à moins de 1e-5 dB de 0 dB vaut exactement 1, un Mix à moins de
  1e-5 des bornes vaut exactement 0 ou 100 % : les arrondis float des hôtes (et le FMA d'Apple Silicon) ne cassent
  pas la transparence bit pour bit.
- La latence d'un slot est conservée en bypass : la latence du plugin ne change qu'au changement de module
  (`setLatencySamples` depuis le message thread).

### Barre globale (paramètres)

Input Gain, Output Gain, Master Mix (Dry aligné sur la latence totale du rack), **Sidechain Source** :
*Bus Sidechain* (bus auxiliaire VST3 / AU, mono ou stéréo) ou *Entrée du rack*. Indicateurs affichés par l'éditeur :
crêtes d'entrée / sortie, réduction de gain par slot, latence active, présence du Sidechain, fréquence, charge CPU.

## Les 12 modules

| Module | Algorithme | Paramètres |
|---|---|---|
| **AuraEQ** | 3 SVF TPT (Zavalishin / Simper) en série : Low Shelf, Peak, High Shelf. g = tan(π·f/fs) : réponse exacte et stable de 20 Hz à 384 kHz. Lissage en log de la fréquence, coefficients recalculés toutes les 16 éch. Une bande à 0 dB est sautée (sortie bit-identique). | Low Freq/Gain, Mid Freq/Gain/Q, High Freq/Gain, Shelf Q, Output |
| **AuraComp** | **3 algorithmes** (paramètre Algorithm), voir la section suivante. Clé interne ou Sidechain. | Algorithm, Key + ceux de l'algorithme |
| **AuraDuck** | Clé Sidechain (ou interne) filtrée HP + LP 24 dB/oct ; maintien de crête ≥ 1,5 période redressée de la plus basse fréquence de la clé (le gain ne suit plus les alternances du kick : pas d'intermodulation sur la 808) ; lissage dB + 2 pôles d'arrondi. **Dyn EQ Sub** : low shelf TPT dont le gain suit l'enveloppe, seules les sub-basses baissent. | Threshold, Depth, Attack, Release, Key HP, Key LP, Mode, Sub Freq, Key |
| **AuraClip** | Genou réglable de hard (0 %) à 6 dB (100 %), C1. ADAA 1er ordre avec antidérivée exacte, repli au point milieu si mal conditionné. Sur-échantillonnage polyphase (127 + 65 coefficients, Kaiser) sous 176,4 kHz seulement. | Drive, Ceiling, Knee, Output, Auto Gain |
| **AuraLimit** | Lookahead 1,5 ms, détection True Peak 8× (2× en DXD, 32 coefficients par phase ; le 4× de BS.1770 sous-estime de ~0,5 dB les crêtes proches de 20 kHz), minimum glissant, relâchement adaptatif (court sur transitoire isolé, long sur réduction soutenue), moyenne glissante de la longueur de l'anticipation : la rampe d'attaque atteint le gain requis à l'arrivée de la crête (démonstration ci-dessous). | Gain, Ceiling (dBTP), Release, Adaptive |
| **AuraDelay** | Ping-pong stéréo continu (0 % = deux delays, 100 % = alternance L / R), Lagrange 3e ordre, temps lissé (glissement type bande), Low Cut / High Cut TPT dans la boucle, retard de groupe du High Cut compensé. | Time, Feedback, Ping-Pong, Low Cut, High Cut, R / L |
| **AuraVerb** | Plate de Dattorro (JAES 1997) mise à l'échelle de fs et de Size ; lectures du réservoir fractionnaires (Size continu) ; allpass modulés ; Decay = vrai RT60 (gain par segment 10^(−3·durée/RT60)) ; Damping en Hz. | Pre-Delay, Size, Decay, Damping, Width, Low Cut |
| **W9BMK1** | Compresseur de caractère « CL 1B » avec façade dédiée et **Auto Focus** : voir la section suivante. | Input, Threshold, Ratio, Attack, Release, Output, Auto Focus, Focus |
| **L9A** | Amplificateur de nivellement opto (esprit LA-2A) avec façade dédiée : voir la section « L9A ». | Peak Reduction, Gain, Mode, Meter |
| **E9DS** | De-esser à bande sibilante dynamique avec façade dédiée : voir la section « E9DS ». | Threshold, Frequency, Width, Range, Release, Mode, Listen, Lookahead |
| **V9C3** | Compresseur VCA quadruple (esprit Vertigo VSC-3) à Auto Focus : voir la section « V9C3 ». | Threshold (dBu), Ratio, Attack, Release, Make-Up, SC HPF, Detect, Mode, Auto Focus, Focus |
| **V9P2** | Préampli double canal (esprit Vertigo VSP-2) : voir la section « V9P2 ». | Gain, Fine, HPF, Pad, Phase, Input, Link, R Offset |

Delay et Verb sortent 100 % wet ; leur Mix de slot passe à 30 % quand on les choisit.

**Garantie de l'anticipation du limiteur.** Avec un retard audio D, un minimum glissant sur D + 1 et une moyenne de
longueur A + 1 (A = anticipation, D = A + retard du détecteur + 1), chaque valeur moyennée couvre l'instant de la crête :
la moyenne est ≤ au gain requis. Le relâchement (instantané vers le bas, exponentiel vers le haut) reste ≤ au minimum,
la propriété est conservée.

## AuraComp : 3 algorithmes pour le DXD

Tout le calcul de dynamique est en `double`, en dB, avec des constantes de temps **en secondes** : coefficient
`1 − exp(−1/(τ·fs))` calculé par `expm1` (aucune annulation, même 7,5 s à 384 kHz), temps dépendants du programme
réévalués à chaque échantillon. Courbes de transfert analytiques et lisses (softplus, genou quadratique), sans table
ni interpolation. Un étage « panneau électroluminescent » (puissance moyenne, τ 0,5 à 2 ms) précède la conversion en dB :
le redressement ne module pas le gain à 2f. États ramenés exactement à zéro sous 1e-15 dB (pas de dénormalisés).
Latence 0.

| Algorithme | Contrôles | Particularités |
|---|---|---|
| **Aura-Opto** (esprit LA-2A) | Gain, Peak Reduction | Peak Reduction règle le niveau envoyé au détecteur (pas un seuil) ; courbe softplus, ratio effectif 3:1. Cellule optique à 2 voies + mémoire : relâchement ≈ 55 ms après un passage de 150 ms, ≈ 670 ms après 12 s de compression ; l'attaque accélère avec la profondeur. |
| **Aura-Tube** (esprit CL 1B) | Threshold, Ratio, Attack, Release, Makeup | Même cellule, temps de base réglés par l'utilisateur (Attack 0,5 à 300 ms, Release 5 ms à 7,5 s), genou 10 dB. Étage de sortie « lampe » : tanh asymétrique à gain petit signal unitaire, ADAA 1er ordre, blocage du continu à 5 Hz. |
| **L-Comp** (« Looser Compressor ») | Crush (un seul bouton) | Crush règle ensemble le seuil (−6 → −46 dB), le ratio (2 → 20:1) et un makeup automatique (85 % de la compensation du plafond). Attaque ≈ 50 µs, relâchement à deux temps (15 / 200 ms), clé filtrée à 60 Hz, détecteur à maintien de crête, plafond doux (C¹, ADAA) : la sortie ne dépasse jamais 0 dBFS. |

Les constantes (temps de cellule, courbes) sont des points de départ établis à partir du comportement publié de ces
appareils, **calibrés par mesure et non par comparaison avec le matériel** : à affiner à l'écoute. Les 10 paramètres
de l'AuraComp existent toujours ; chaque algorithme n'utilise que les siens (l'interface n'affiche que ceux-là), ce qui
évite qu'un changement d'algorithme réinterprète des valeurs.

## W9BMK1 : compresseur CL 1B, Auto Focus et façade

DSP (`Source/DSP/Modules/AuraW9B.h`), tout en `double`, latence 0, partageant `CompCommon.h` avec l'AuraComp
(cellule opto à temps réglables, genou 10 dB, étage « lampe »). Chaîne : Input → clé (HP 60 Hz, panneau EL τ 0,5 à 2 ms)
→ courbe de gain en dB (Threshold, Ratio) → cellule (Attack 0,5 à 300 ms, Release 5 ms à 7,5 s, dépendance au programme
inhérente à la cellule) → étage lampe → Output. Pas d'entrée Key ni de commutateur Auto-release : volontairement, pour
n'exposer que des contrôles utiles. L'étage lampe est évalué directement à 352,8 / 384 kHz (ADAA en dessous de 176,4 kHz) :
aucun affaiblissement des aigus en DXD (20 kHz vs 1 kHz : 0,0001 dB).

### AUTO FOCUS (`Source/DSP/Modules/FocusTracker.h`)

Paramètres **Auto Focus** (Manual / Auto) et **Focus** (1 à 12 dB, continu). En Auto, le Threshold est piloté par un suiveur
lent qui vise une **réduction de gain moyenne** égale à Focus, quel que soit le niveau d'entrée. Deux étages, en dB, constantes
en secondes (même comportement de 44,1 à 384 kHz) :

1. **Anticipation** : niveau moyen du programme (moyenne lente de 3 s du niveau vu par le détecteur) ; le seuil qui donnerait
   exactement la réduction visée à ce niveau est obtenu par **inversion analytique** de la courbe statique (genou compris).
   Le seuil l'approche en lissage τ 0,5 s et à vitesse limitée à 6 dB/s.
2. **Correction lente** : un intégrateur (±12 dB) compare la réduction réellement appliquée (moyenne τ 1,5 s) à la cible et
   rattrape l'écart « réduction au niveau moyen » / « moyenne de la réduction ». Il n'intègre que lorsque le seuil a rejoint
   l'anticipation et s'arrête contre une butée (pas d'emballement).

Protection contre le pompage : porte absolue à −60 dB (le silence gèle tout), porte relative (18 dB sous la moyenne du
programme : une pause ou un creux entre syllabes gèle aussi la boucle ; au-delà de 2,5 s c'est un vrai changement de
programme et la boucle reprend), un clic de 5 ms pèse ≈ 5 ms / 3 s dans la moyenne. À l'activation le seuil part du réglage
manuel (aucun saut) ; à la désactivation il glisse vers lui (20 ms). **Le Threshold manuel est mis de côté pendant l'Auto**
(il est conservé et retrouvé en Manual). Mesuré sur une parole synthétique (syllabes de 180 ms, niveaux sur 10 dB, pauses) :

| Mesure | Résultat |
|---|---|
| Réduction moyenne sur les syllabes, cibles 3 / 6 / 10 dB | 2,8 / 5,7 / 9,5 dB (−0,2 à −0,5 dB) ; −24 ou −12 dBFS : mêmes valeurs à 0,1 dB près |
| Excursion du seuil en régime établi | 0,2 dB sur 0,5 s (échelle syllabe), 0,8 dB sur une phrase de 3,3 s |
| Vitesse maximale du seuil | 6 dB/s |
| Parole +10 dB | seuil +9,3 dB, retour à la cible (5,5 dB pour 6 visés) en < 30 s |
| Clic de 5 ms à +20 dB / pause de 2 s à −45 dB / silence de 5 s (écart à la même parole sans l'événement) | seuil déplacé de 0,35 dB / −0,17 dB / 0,01 dB |
| 44,1 kHz vs 384 kHz | écart de seuil et de réduction < 0,01 dB |

La réduction « moyenne » est celle des parties actives du programme (hors pauses et creux), c'est celle que lit le VU-mètre pendant
qu'on joue ; elle peut s'écarter de la cible de quelques dixièmes de dB selon la matière. Le suiveur est lent par construction :
une dizaine de secondes pour se caler après un changement de programme.

### Façade

`Source/UI/W9bFaceplate.*` (vectorielle, aucune image) : plaque bleue anodisée brossée avec vis et trame de points, sept potards
en aluminium usiné avec anneau de LED ambre (Input, Thresh, Ratio, Focus, Attack, Release, Output), VU rétro-éclairé
(réduction de gain, aiguille masse-ressort), voyant rouge = crête de sortie (de −6 à 0 dBFS), commutateur MANUAL / AUTO, un
seul interrupteur ACTIVE / BYPASS, logo script (police script installée, sinon serif italique). Deux mises en page : large
(façade de rack) et compacte (carte de slot). **En AUTO** : le potard Thresh n'est plus réglable, affiche le seuil effectif
calculé par le moteur et se nomme « Thresh auto » ; Focus s'allume (estompé en Manual) et un repère bleu sur le VU marque la
cible : l'aiguille converge vers lui. Chaque contrôle est un vrai paramètre (p1..p8 du slot et Bypass du slot), le test UI le
vérifie (un contrôle = un paramètre distinct). Le Mix du slot reste disponible ; In / Out / Bypass génériques sont masqués.

## L9A : amplificateur de nivellement opto (esprit LA-2A)

DSP (`Source/DSP/Modules/AuraL9A.h`), `double`, latence 0, constantes en secondes : même réponse de 44,1 à 384 kHz.
Paramètres : **Peak Reduction** (niveau envoyé au détecteur, pas un seuil), **Gain** (make-up, −12 à +30 dB), **Mode**
(Comp / Limit), **Meter** (GR / Output). Chaîne : clé liée stéréo (HP 30 Hz, panneau EL) → courbe softplus → cellule opto
à 2 voies + mémoire (relâchement dépendant du programme) → Gain → étage lampe.

| Mode | Courbe | Cellule |
|---|---|---|
| Comp | ratio effectif ≈ 3:1, genou doux de 10 dB | attaque 10 ms, voie rapide 60 ms |
| Limit | ratio effectif ≈ 10:1, genou de 4 dB | attaque 6 ms, voie rapide 45 ms |

Le passage Comp ↔ Limit est lissé (20 ms) et la cellule garde son état : pas de clic (variation de la réduction < 0,01 dB par
échantillon). Le module publie (`getMeterAux`) son **niveau de sortie à balistique VU** (deux pôles de 45 ms, 99 % en ≈ 300 ms,
RMS en dBFS) : 0 VU = −18 dBFS RMS. `Meter` est un vrai paramètre (sauvegardé, automatisable) qui choisit ce que la façade
affiche ; il ne change pas le son. Mesures : relâchement à 50 % en 57 ms après un passage de 150 ms, 730 ms après 12 s de
compression ; invariance en fréquence 0,04 dB ; 20 kHz vs 1 kHz : 0,001 dB ; contenu non harmonique à 384 kHz : −151 dB.

Façade (`Source/UI/L9aFaceplate.*`) : panneau argent brossé, VU au centre (échelle VU ou réduction de gain), GAIN à gauche,
PEAK REDUCTION à droite, trois interrupteurs : COMP / LIMIT, METER GR / OUT, ACTIVE / BYPASS. Constantes calibrées par
mesure, pas par comparaison avec le matériel.

## E9DS : de-esser DXD

> **Origine.** Le cahier des charges décrit le E9DS comme « inspiré du E9DS du lecteur AURA ». Aucune spécification ni
> aucun code de ce module ne figure dans ce dépôt ni dans les documents du projet : **E9DS est une conception AuraRack à
> part entière, pas un portage.** Si l'algorithme du lecteur doit être repris à l'identique (ou servir de référence de
> mesure), il suffit de fournir sa source ou sa description : les paramètres et la façade sont déjà prêts à l'accueillir.

Objectif : maîtriser la sibilance et la dureté haut-médium (voix, mais aussi résonances de piano, cymbales, bus) **sans
zozotement et sans pompage**, en préservant l'air et le timbre.

Chaîne (`Source/DSP/Modules/AuraE9DS.h`) : entrée → bande sibilante (passe-bande TPT, Frequency 2–14 kHz, Width Q 0,5–4, liée
en stéréo) → enveloppe crête (attaque 0,1 ms, relâchement 4 ms) → courbe statique (6:1, genou 6 dB, saturation douce à Range)
→ lissage dans le domaine dB (attaque 0,25 ms, Release 10–300 ms) → gain.

- **Split** (défaut) : seule la bande est atténuée, `y = x − (1 − g)·bande`. Le passe-bande est de phase nulle à f0 : c'est une
  cloche dynamique, le corps et l'air hors bande restent intacts — c'est ce qui évite le zozotement d'un de-esser large.
- **Wide** : gain sur tout le signal (pour un bus agressif sur tout le haut du spectre).
- **Listen** : la sortie est la bande de détection, pour caler Frequency / Width à l'oreille.
- **Range** : réduction maximale (0–18 dB), jamais dépassée (saturation douce) : pas de trou dans le signal.
- Constantes en secondes, calcul en double, latence 0 : même réponse à 44,1 et 384 kHz. Mix et Bypass : ceux du slot.
- Façade (`E9dsFaceplate`) : affichage de la **réponse vivante** de la cloche dynamique (ce que le module fait réellement au
  spectre) avec courbe fantôme à pleine réduction, barre de niveau de la bande face au seuil, valeur de GR ; 6 potards (dont Lookahead), MODE,
  LISTEN, ACTIVE / BYPASS (un contrôle = un paramètre, vérifié par le test UI).

**Lookahead** (Off · 0,5 · 1 · 2 ms). Le détecteur voit le signal **non retardé** pendant que l'audio (et la bande) sont lus
`délai` échantillons en arrière : la réduction est installée quand le « s » ou le « t » arrive. La latence est annoncée à
l'hôte (`getLatencySamples`, **48 éch. à 1 ms / 48 kHz, 384 à 384 kHz**) et le moteur la relit à chaque tranche : Dry / Mix du
slot et latence totale suivent (compensation par le DAW). Changer de cran fait un fondu de 1,5 ms + 1,5 ms (sans clic).
**Off = comportement précédent, bit-identique**. Sous le seuil, la sortie est l'entrée retardée **bit pour bit** (testé).
Mesuré : un « t » de 4 ms à 6,5 kHz a une crête de sortie plus basse avec 2 ms d'anticipation ; réduction en régime établi
identique à 48 et 384 kHz ; moteur : Mix 50 % + Lookahead = une impulsion unique d'amplitude exacte à la latence annoncée.
La structure (paramètre choisi, latence, détecteur indépendant du trajet audio) est prête à être alignée sur le E9DS du lecteur
AURA dès que sa description est fournie.

**Avec W9BMK1 et L9A.** Ordre usuel sur une voix : `E9DS → L9A (nivellement) → W9BMK1 (couleur / contrôle)` — le de-esser
d'abord, pour que le compresseur ne « pousse » pas les sibilantes ; ou `L9A → E9DS` si l'on veut dessiber le signal déjà
nivelé (une compression opto remonte les « s »). Tous trois exposent leur réduction de gain à la bande de chaîne, et se
réordonnent par glisser-déposer sans coupure.

Mesures (banc DSP) : réduction statique = théorie à 0,04 dB près (48 et 384 kHz) ; Range jamais dépassé (7,97 dB pour 8) ;
44,1 vs 384 kHz : 0,002 dB d'écart ; sous le seuil : sortie bit-identique ; Split réduit 6,5 kHz de 7,6 dB et laisse 1 kHz à
−0,04 dB ; attaque : 85 % de la réduction en 5 ms ; relâchement (60 ms) sous 10 % en 146 ms.

## V9C3 : compresseur VCA (esprit Vertigo VSC-3)

> Conception AuraRack inspirée du comportement publié de ce type d'appareil : **aucune revendication de fidélité au matériel**,
> constantes calibrées par mesure. DSP : `Source/DSP/Modules/AuraV9C3.h`, `double`, latence 0, constantes en secondes
> (même réponse de 44,1 à 384 kHz). Étalonnage : **0 dBu = −18 dBFS** (+18 dBu = 0 dBFS, EBU R68) ; Threshold −22 … +23 dBu.

| Contrôle | Réglage |
|---|---|
| Mode | **Stereo** (une clé liée, un gain pour L et R : image intacte) · **Mono** (clé sommée) · **Dual** (deux détecteurs indépendants) · **M/S** (compression du Mid et du Side par leur propre clé) |
| Detect | **Peak** (attaque 20 µs, relâchement 8 ms) ou **RMS** (fenêtre 10 ms, référencée crête) |
| Ratio | **Soft** (« TipToe » : ≈ 1:1 au seuil, tendant vers 8:1 en profondeur), 2:1, 4:1, 8:1, 10:1, **Limit** 40:1 ; genou 4 dB |
| Attack | 10 crans : 0,1 · 0,2 · 0,4 · 0,7 · 1,5 · 3 · 6 · 12 · 25 · 40 ms |
| Release | 10 crans : 50 … 1200 ms, + **Auto Fast** (0,04 / 0,5 s) et **Auto Slow** (0,15 / 2,5 s), dépendants du programme |
| SC HPF | Off · 70 Hz · 140 Hz sur la clé (un grave fort ne fait plus pomper) |
| Make-Up | 0 … 24 dB |
| Auto Focus / Focus | seuil piloté par le `FocusTracker` (celui du W9BMK1) vers **1 … 12 dB de réduction moyenne**, sans pompage ; le potard Threshold affiche le seuil effectif (dBu) |

**Couleur (modèle « Quad Discrete VCA 1979 », allégé).** Plus aucun étage saturant dans le chemin du signal (ni « transformateur »
des graves, ni coloration permanente, rien dans le bas-médium, aucune perte d'aigus). Au repos (pas de réduction, Make-Up 0) la sortie
est **identique à l'entrée bit pour bit**, y compris à 20 kHz. La distorsion du VCA est un terme additif (H3 et un peu de H2) ∝ (GR/20)² :
mesuré H3 ≈ −93 dBc à 3 dB de GR, −76 dBc à 8 dB, −63 dBc à 16 dB ; le Make-Up n'ajoute aucune coloration (gain exact). Le terme est calculé
sur le signal passe-bas (min(20 kHz, 0,45·fs/3)) : aucun repliement (mesuré < −110 dBc, 18 kHz comprimé à 44,1 kHz).
**Soft (TipToe)** : réduction = 0,875·(o − 6·atan(o/6)) (pente nulle au seuil, croît vers 8:1, sans coude) ; Auto Fast 30 ms / 350 ms,
Auto Slow 120 ms / 1,8 s.

**Affichage** (`VertigoFaceplate`) : bargraphe de réduction à zone fine (0–6 dB sur la moitié gauche) puis jusqu'à 24 dB,
seuil effectif en dBu, repère de cible en Auto Focus.

Mesures (banc DSP) : courbes statiques = théorie à ±0,3 dB (Peak et RMS, 2/4/8/10/40:1 et Soft) ; Stereo : un seul gain ;
Dual : le canal faible reste intact ; M/S : Mid −4 dB et plus, Side ±0,3 dB ; Release 120 / 380 / 1000 ms à ±20 % ; Auto Fast
> 3× plus long après un passage soutenu qu'après un bref ; SC HPF 140 Hz : un 40 Hz à −6 dBFS ne comprime plus ; Auto Focus
3 / 6 dB à ±1 dB (parole synthétique, deux niveaux), Soft / RMS / M-S à ±1,5 dB, seuil quasi immobile ; 44,1 vs 384 kHz : < 0,1 dB.

## V9P2 : préampli micro double canal (esprit Vertigo VSP-2)

> Conception AuraRack, sans revendication de fidélité au matériel. DSP : `Source/DSP/Modules/AuraV9P2.h`, `double`, latence 0.
> Étalonnage identique (0 dBu = −18 dBFS) ; plafond +24 dBu (= +6 dBFS), voyant Clip rouge dès **+19 dBu**.

- **Gain** 12 … 72 dB par pas de 6 dB + **Fine** 0 … +6 dB ; **le gain net est relatif à 36 dB (= unité)** : un rack n'a pas de
  micro derrière, le module garde donc le niveau d'un signal de ligne au centre de la plage (Gain net = Gain + Fine − 36 − Pad).
- **HPF** Off / 80 Hz / 120 Hz (12 dB/oct) ; **Pad** −20 dB ; **Phase Ø** Off / L / R / L+R ; **Input** Mic / **Inst-DI** ;
  **Link** Dual (deux canaux, **R Offset** ±6 dB) ou Linked. **Le commutateur 48V a été supprimé** (il n'avait aucun effet audio) ;
  tous les contrôles de la façade agissent sur le son.
- **Mic** : transformateur type **JT-16 1:2** très linéaire (+6 dB passifs, phase quasi linéaire, faible capacité, forte tenue en niveau)
  qui prend 6 dB du gain : l'étage **Twin OPA** fournit le reste « à fond » → harmoniques 2 et 3 plus riches, son ouvert et tranchant.
  Le transformateur n'ajoute qu'un pôle HF à 69 kHz (−1 dB à 35 kHz, actif dès 192 kHz) et une non-linéarité de noyau minuscule sous 60 Hz.
  **Inst/DI** : directement sur l'étage, avec plus de contre-réaction (≈ 0,3 × les harmoniques du Mic) et sans limite HF : plus neutre.
  Mesuré à +18 dBu : H3 −60 dBc (Mic) / −71 dBc (DI), H2 −62 / −73 dBc ; à 35 kHz −0,89 / 0,00 dB.
- **Bande passante** 10 Hz … 35 kHz à ±1 dB (mesuré à 384 kHz, Mic et DI : ≤ 0,89 dB). **Sortie** type THAT 1646 : linéaire jusqu'à
  +21 dBu, genou tanh C¹ jusqu'à +24 dBu (ADAA sous 176,4 kHz) ; H3 < −72 dBc à +18 dBu en DI. Harmoniques de l'étage calculés sur le
  signal passe-bas : pas de repliement (< −85 dBc à 44,1 kHz avec 15 kHz dans le genou).
- Chaîne : gains lissés (20 ms) → entrée (Mic / DI) → étage Twin OPA → HPF (fondu 10 ms) → polarité (rampe 5 ms) → sortie propre.
- Mesures : gains par cran, Fine et Pad exacts à 0,05 dB ; HPF −12 dB une octave sous la coupure à 44,1 et 384 kHz ; polarité ;
  Dual / Linked ; **aucun clic** (cran de gain, polarité, Mic → DI, HPF, Pad) ; plafond ≈ +6 dBFS, voyant vert / rouge ; gain identique à 44,1 et 384 kHz.
- Façade : barre de niveau d'entrée en dBu (le module publie la crête **avant** l'étage de saturation), voyants Signal (vert) / Clip
  (rouge), gain net affiché ; R Offset estompé en mode Linked.

## Étape 5 : gain staging, A / B, presets, utilitaire stéréo, suivi latence / CPU

### Auto-gain (`Source/DSP/AutoGain.h`)

Compensation de gain **par module** (bouton `AGC` dans l'en-tête du module, paramètre `sN_agc`) et **pour le rack entier**
(bouton `AUTO GAIN` de la barre, paramètre `auto_gain`). Le niveau **RMS** de sortie est ramené à celui de l'entrée : mesure
sur les deux voies (constante 0,4 s), correction lissée sur 1 s, **limitée à ±12 dB**, appliquée par rampe sur chaque tranche
(aucun saut). Il suit donc les changements de ratio, de saturation, de Make-Up ou d'activation, **lentement** (≈ 5 s pour se
caler à 0,3 dB). Garde-fous : **porte à −60 dBFS** (sur un silence ou un creux la correction est gelée : pas de remontée du
bruit, pas de pompage), le **gain acquis est conservé en bypass**, le retrait de l'option ramène à 0 dB par rampe ;
**option désactivée = aucune multiplication, rien n'est mesuré** (sortie bit-identique, coût nul). Un changement de type de
module remet la correction à 0 dB. L'en-tête affiche la correction acquise (« AGC −3,2 »). **C'est une égalisation de niveau
RMS, pas de sonie (LUFS)** ; pour la sonie, voir le mètre LUFS. Mesuré : +9 dB de module ramenés à ±0,3 dB en 10 s, réglage
changé de +6 à −6 dB recalé, plafond ±12 dB, silence de 5 s sans dérive (< 0,01 dB), bypass sans perte du gain, rack à
+12 dB cumulés compensé.

### A / B, presets, snapshots

- **A / B** (boutons de la barre) : deux **configurations complètes du rack** (modules, réglages, auto-gain, utilitaire, ordre
  des slots). Un clic bascule ; B part d'une copie de A ; `A>B` / `B>A` copie la configuration courante vers l'autre
  emplacement. Les retouches de l'emplacement quitté sont conservées. Les deux emplacements sont **enregistrés dans la
  session** (nœud `AB` de l'état ; état version 4) ; une session d'une version antérieure s'ouvre avec A actif et B vide.
  Le changement installe d'abord les modules, puis les réglages (fondus de module du moteur : pas de clic).
- **Presets d'usine** (menu `PRESET`) : **Voix** (préampli HPF 80 Hz, E9DS Lookahead 1 ms, V9C3 Auto Focus 4 dB, EQ),
  **Piano / Acoustique** (préampli, EQ doux, L9A, salle courte), **Bus** (EQ de nettoyage, V9C3 de cohésion RMS Auto Slow,
  écrêteur léger) et **Mastering** (EQ large, V9C3 Auto Focus 1,5 dB, E9DS, limiteur True Peak −1 dBTP, mono grave 100 Hz),
  plus « Rack vide ». Les valeurs sont des **points de départ établis par mesure, pas à l'écoute** : à affiner. La barre
  affiche le preset chargé, avec `*` s'il a été retouché. Le preset est enregistré avec la session ; l'écart se recalcule à la
  réouverture. Charger un preset écrase le rack courant (utiliser A / B pour garder l'ancien).

### Utilitaire Stéréo / Largeur / Mono grave (`Source/DSP/StereoUtility.h`)

Fin de rack (avant le gain de sortie), en Mid / Side : **Width** 0–200 % (Side), **Bass Mono** (Off / 60 / 100 / 150 / 250 Hz :
le **Side** passe par un passe-haut Linkwitz-Riley 4e ordre, le Mid n'est pas touché), **Mono** (Side = 0), tous lissés
(20 ms), automatisables. **Width 100 %, Bass Mono Off, Mono Off : sortie bit-identique.** Mesuré : −6 dB à la coupure, plus de
35 dB deux octaves dessous, Side intact au-dessus, même résultat à 44,1 et 384 kHz, Mid grave inchangé et toujours mono,
sans clic. Un indicateur de **corrélation de phase L / R** (−1 … +1, lissé 0,3 s) est affiché dans la barre.

### Barre supérieure

Deux rangées. Rangée 1 : titre, Input / Output / Master Mix, Sidechain, niveaux, fréquence (DXD), **CPU**, ⚙️. Rangée 2 :
presets, A / B, **Auto Gain** du rack, utilitaire stéréo (Width, Bass Mono, Mono, corrélation) et **latence totale**
(échantillons + ms, avec la PDC de tous les modules : Lookahead du E9DS, limiteur…). La latence affichée est celle du moteur ;
elle est transmise à l'hôte par le minuteur du processeur (≈ 30 ms). Le CPU est le temps de calcul / durée du bloc, lissé.

## Ajouter un module au rack

1. **DSP** : une classe `AuraXxx : AuraModuleDSP` avec `info()` (`ParamSpec`), `setParameter`, `process`, `reset`, et au besoin
   `getGainReductionDb`, `takeStatusPeak` (crête, voyant) ou `getMeterAux` (valeur d'affichage propre au module) ; un nouvel
   identifiant **à la fin** de `ModuleType` (l'ordre est persisté), la ligne dans `ModuleRegistry.h`.
2. **Interface** : sans façade, la carte générique affiche les paramètres ; avec façade, une classe `XxxFaceplate : Faceplate`
   construite avec les briques de `FaceplateKit.h` (`PlateKnob`, `PlateSwitch`, `VuDial`, thème), une ligne dans
   `Faceplate::exists()` / `Faceplate::create()`, et une couleur dans `NextLookAndFeel::accentFor`.
3. **Tests** : un test DSP (statique, fréquences d'échantillonnage, DXD) et la vérification « un contrôle = un paramètre ».

## L'éditeur NextDSP

- **Vectoriel, sans image** : `Path` et `Graphics` JUCE, palette anthracite, un accent par type de module. Le contenu a une
  taille fixe (1280 × 780) mise à l'échelle en bloc : la fenêtre est redimensionnable à ratio constant.
- **Une seule horloge** : `VBlankAttachment` pour toute l'interface. Elle lit des atomiques, ne repeint que les rectangles
  dont la valeur affichée change (l'historique de réduction de gain ne repeint pas tant qu'il n'y a pas de réduction),
  et s'arrête avec la fenêtre.
- **Niveaux** : le thread audio maintient les crêtes (un seul écrivain) ; l'interface lit avec `exchange(0)`, donc aucune
  crête n'est perdue entre deux images, même à 384 kHz (blocs de ~1,3 ms pour 16 ms par image). Balistique d'affichage
  côté interface.
- **Glisser-déposer sans course** : les cartes sont liées à l'identité du slot (paramètres `sN_*`), pas à sa position.
  La vue lit l'ordre en **un mot atomique de 64 bits** et le compare à celui qu'elle affiche (resynchronisation animée si
  l'ordre change ailleurs : restauration d'état, annulation). Pendant le glissement rien n'est écrit ; au relâchement un
  seul `moveSlot`, par **compare-and-swap** côté moteur. Le thread audio n'est jamais bloqué.
- **Disposition type T-RackS** (`PluginEditor`, `InspectorView`, `ChainStrip`) :
  - **zone haute « module actif »** : le module sélectionné en grand — façade pour W9BMK1 / L9A / E9DS, grands potentiomètres
    homogènes pour les autres (AuraEQ, AuraComp, AuraDuck, AuraClip, AuraLimit, AuraDelay, AuraVerb) ;
  - **bande basse « chaîne »** : 10 tuiles dans l'ordre du signal. Clic = sélection ; **glisser = réordonner en temps réel**
    (aperçu animé, un seul `moveSlot` au relâchement) ; pastille LED = bypass ; croix = retirer ; tuile vide « + » (ou clic droit)
    = menu d'ajout rapide. Réduction de gain par tuile ; en mode Infos, valeur en dB.
  - **État d'interface** : `AuraRackProcessor::UiSettings` (langue, aide, LUFS, module sélectionné), enregistré dans l'état de
    la session (propriétés `uiLanguage`, `uiInfo`, `uiLoudness`, `uiSlot` ; état version 3, les sessions antérieures s'ouvrent
    avec les valeurs par défaut). Ce ne sont **pas** des paramètres automatisables. L'éditeur relit ces réglages à chaque
    image : restauration d'état et panneau ⚙️ se propagent seuls. La **sélection suit l'identité du slot**, pas sa position :
    réordonner ne change pas le module affiché en haut.
- **Menu ⚙️** (`SettingsPanel`, bulle ouverte depuis la barre) : **Langue** (Français / English), **Infos / aide contextuelle**
  (info-bulles sur les tuiles et la barre, descriptions de module, valeur de GR sur les tuiles), **Mesure LUFS (mastering)** +
  remise à zéro. Les noms de paramètres audio (Threshold, Ratio…) restent en anglais, comme sur le matériel.
- **i18n** (`UI/I18n.h`) : un dictionnaire (une ligne par clé, une colonne par langue). Ajouter une langue = une entrée dans
  `Lang`, son nom dans `kLanguageNames`, une colonne dans `kTable` (une ligne incomplète ne compile pas).
- **Module LUFS** (`LoudnessBar` + `DSP/LoudnessMeter.h`) : momentané (400 ms), court terme (3 s), intégré (portes −70 LUFS
  puis −10 LU), LRA (portes −70 / −20 LU, percentiles 10–95 %), crête **échantillon** (pas un true-peak). Pondération K
  généralisée à toute fréquence d'échantillonnage. Mesuré **en sortie du rack**, désactivé par défaut : aucun calcul audio
  ni place prise. Un canal source mono compte une fois. Vérifié : sinus 997 Hz −23 dBFS stéréo = −23,0 LUFS à 48 kHz
  (−23,00), 44,1 / 96 / 384 kHz à ±0,03 LU ; LRA −20 puis −30 LUFS = 10,0 LU ; portes relatives ; mono ; moteur.
- Sélecteur de module par slot (`setSlotModule`), Bypass, Mix, trims In / Out, indicateur DXD, latence, voyant Sidechain,
  CPU.

## Bancs d'essai

`aurarack_dsp_test` (sans JUCE, 270 vérifications), `aurarack_host_test` (hôte VST3, 16) et `aurarack_ui_test` (éditeur, 52) : tout passe
(Linux GCC 13, et CI macOS / Windows / Linux). Le banc DSP passe aussi sous **ThreadSanitizer** (stress : le message thread change
modules, ordre, paramètres et bypass en rafale pendant que le thread audio tourne à 384 kHz : aucune course) et sous
**AddressSanitizer + UBSan** (aucune erreur, aucune fuite).

| Mesure | Résultat |
|---|---|
| Peak 1 kHz +12 dB à 44,1 / 352,8 / 384 kHz | 12,0000 dB |
| Low shelf 20 Hz +6 dB à 384 kHz (mesuré à 2 Hz) | 5,9994 dB |
| EQ à 0 dB, rack vide ; EQ 0 dB + Limit sous le plafond | sortie bit-identique (retardée de la latence pour le Limit) |
| Aura-Tube, sinus −6 dBFS, seuil −18, 4:1 | réduction = théorie à 0,04 dB près (ondulation résiduelle du détecteur) |
| Aura-Opto / Aura-Tube, échelon d'amplitude, 44,1 / 96 / 352,8 kHz contre 384 kHz | écart max 0,05 dB (Opto) · 0,13 dB (Tube) : la dynamique ne dépend pas de la fréquence |
| Contenu non harmonique, sinus 18 kHz comprimé à 384 kHz | Opto −151 dB · Tube −148 dB · L-Comp −95 dB (5 kHz à 44,1 kHz : −122 / −120 / −70 dB) |
| L-Comp 100 % : 24 dB d'écart en entrée | 1,2 dB en sortie ; crête ≤ 0 dBFS ; 97 % de la réduction en 0,5 ms |
| Duck Dyn EQ, kick → 808 à 352,8 kHz | sub 45 Hz −11,7 dB, charleston 6 kHz −0,008 dB, ondulation 0,02 dB |
| Clip, 7 kHz, drive 18 dB : repliement / clip naïf | 44,1 k : −70 / −12 dB · 96 k : −53 / −16 dB · 352,8 k (ADAA seul) : −45 / −36 dB |
| Limit, +12 dB sur bruit + transitoires, plafond −1 dBTP | −0,987 dBTP (44,1 k) · −0,988 dBTP (384 k) ; pire cas sur 30 bruits : −0,972 (44,1 k), −0,995 (96 k), −0,979 (384 k) ; true peak mesuré par sinc ×16 |
| Delay ping-pong 10 ms, feedback 50 % | échos à 10 / 20 / 30 ms ±1 éch., amplitudes 1 / 0,5 / 0,25 |
| Verb, Decay 2 s (T20, octave 1 kHz) | 2,09 s à 48 kHz et à 384 kHz |
| Latence annoncée à l'hôte, Limit à 352,8 kHz | 546 éch., impulsion 64 bits intacte à cet endroit |
| Duck par le bus Sidechain du VST3 | sub −11,7 dB pendant le kick, 0,0 dB sans |
| État sauvegardé → nouvelle instance | rendu bit-identique |

**Charge CPU** (rack complet, 7 modules, stéréo, un cœur Xeon 2,3 GHz) : 5,2 % à 44,1 kHz · 9,1 % à 96 kHz ·
15,2 % à 352,8 kHz · 16,6 % à 384 kHz. Un AuraComp seul à 384 kHz : Aura-Opto 3,1 % · Aura-Tube 6,6 % · L-Comp 2,6 %.

## Paramètres et compatibilité des sessions

Identifiants figés : `in_gain`, `out_gain`, `master_mix`, `sc_source`, puis pour chaque slot `sN_type` (non
automatisable), `sN_bypass`, `sN_mix`, `sN_in`, `sN_out`, `sN_p1` … `sN_p10`. Les 10 paramètres génériques sont
normalisés ; l'hôte affiche leur valeur dans l'unité du module du slot (« 10.8 dB », « 8.00 kHz »…). Choisir un module
remet ses paramètres à leurs valeurs par défaut ; restaurer une session les conserve. L'ordre des slots est enregistré
dans l'état (`order="0,1,…"`).

## Limites connues

- **AuraClip sur-échantillonné** : le filtre de redescente déborde les angles d'un hard clip (Gibbs, jusqu'à ~1,2 dB à
  96 kHz, drive extrême). Un écrêtage après redescente rendrait le repliement retiré ; **placez AuraLimit après** pour
  un plafond True Peak garanti. En DXD (ADAA seul), le plafond est exact.
- **ADAA** : retard d'un demi-échantillon de la fréquence de traitement (1,4 µs en DXD), non compensé dans le Mix
  (0,18 rad à 20 kHz).
- **Réordonnancement** : creux de 5 ms (descente + remontée) pendant la permutation.
- **Plateformes** : développé et testé sous Linux (VST3). macOS (AU, VST3 universel) et Windows sont couverts par la
  CI ; une séance dans un vrai DAW (AuraDAW, Logic, Cubase, Ableton, Reaper) reste à faire, surtout le routage
  Sidechain propre à chaque hôte.
- **Interface** : rendue et testée sans fenêtre (rendu PNG, glisser-déposer simulé) ; l'aspect et la fluidité dans de vrais
  DAW (macOS, Windows, HiDPI) restent à valider. Pas encore d'affichage de courbes (EQ, courbe de transfert du comp).
- **Aura-Tube / L-Comp** : l'ADAA (sous 176,4 kHz seulement ; évaluation directe en DXD) ajoute un demi-échantillon de retard sur ces deux chemins.
  Changer d'algorithme remet à zéro l'enveloppe (léger saut de gain).
- **W9BMK1 / L9A** : constantes calibrées par mesure, non comparées au matériel (aucune revendication de « dépasser l'original ») ; la police du logo dépend de l'OS ; façades non vues dans un vrai DAW.
- **E9DS** : conception propre à AuraRack (la spécification du E9DS du lecteur AURA n'est pas disponible) ; détection sur la bande seule (pas de détection relative au niveau large-bande), anticipation optionnelle (Lookahead, jusqu'à 2 ms, qui ajoute sa latence) ; avec Lookahead Off (latence 0, attaque 0,25 ms) un « t » ou « ch » très bref peut passer avant la réduction. Constantes validées par mesure sur signaux synthétiques, **pas à l'écoute sur de vraies voix** : à valider.
- **V9C3** : **Threshold, Ratio, Attack, Release, Make-Up et Focus sont communs aux deux voies** en Dual et M/S (pas de réglages séparés par voie) ; **pas de compensation d'égale sonie** au changement de Ratio ; la distorsion du VCA est une approximation calibrée par mesure, pas une comparaison avec le matériel ; Soft utilise une approximation (ratio 4, sans genou) dans l'Auto Focus (validé à ±1,5 dB).
- **V9P2** : le Gain est **relatif à 36 dB** (pas de gain micro réel) ; plus de 48V ; pas d'impédance d'entrée ni de modèle de micro ; JT-16 / Twin OPA / THAT 1646 sont des modèles calibrés par mesure, **non comparés au matériel** ; l'ordre des paramètres a changé (sessions V9P2 enregistrées avant cette version : Link / R Offset à re-régler).
- **Validation DAW** : V9C3, V9P2, le Lookahead E9DS et les façades n'ont **pas** été essayés dans un vrai DAW : suivre `AuraRack-Checklist-DAW.md`.
- **Auto-gain** : égalisation de niveau RMS (pas de sonie), lente par construction (≈ 5 s), limitée à ±12 dB, gelée sous −60 dBFS ; sur du matériel très dynamique elle peut gommer l'effet voulu d'un compresseur de caractère (à utiliser pour comparer, pas systématiquement).
- **Presets** : points de départ établis par mesure, **non écoutés** ; ils écrasent le rack (A / B pour comparer). Pas de presets utilisateur nommés (A / B et la session font office de snapshots).
- **Utilitaire stéréo** : un seul réglage pour tout le rack, en fin de chaîne ; la corrélation est une mesure large bande (pas de vectorscope).
- **Latence** : la valeur affichée est celle du moteur ; l'hôte la reçoit par le minuteur (≈ 30 ms de retard). Changer de Lookahead en lecture change la latence : la plupart des DAW recalent à l'arrêt / reprise.
- **LUFS** : crête échantillon seulement (pas de true-peak) ; l'intégrée est lue par histogramme à pas de 0,1 LU (écart ≤ 0,03 LU mesuré) ; la mesure suit la sortie du rack, avant le traitement de l'hôte.
- **Interface T-RackS** : vérifiée par rendu PNG et événements souris simulés ; les menus contextuels (ajout / retrait) et la bulle ⚙️ s'ouvrent via JUCE et n'ont pas été exercés dans un vrai DAW ; les cartes génériques (AuraEQ…) sont lisibles mais sans courbes.
- **Auto Focus** : lent par construction (≈ 10 s pour se caler après un changement de programme) ; le Threshold manuel est ignoré pendant l'Auto ; testé sur une parole synthétique et un sinus, pas sur du matériel musical varié : à valider à l'écoute (percussions, mix complet).
- **Aura-Tube, Attack court** : à 0,5 ms l'enveloppe suit les graves (< 500 Hz) : comportement réel d'un compresseur
  rapide, pas un défaut de calcul. Monter Attack à quelques ms le supprime.
- **L-Comp** : sans anticipation, l'attaque de ~50 µs laisse passer le tout début d'un pic ; c'est le plafond doux
  (C¹, ADAA) qui l'écrête, donc ce pic est légèrement saturé plutôt que comprimé. La sortie ne dépasse jamais 0 dBFS.
