# AuraRack 0.3 — NextDSP · rack multi-effets DXD (VST3 / AU)

Rack modulaire de mixage / mastering : **10 slots en série**, réordonnables à chaud, **9 modules** DSP natifs,
traitement intégral en **64 bits** de 44,1 kHz au **DXD 352,8 / 384 kHz**. Marque **NextDSP**, suite **AuraSuite**
(Aura, AuraEngine, AuraDrum, AuraPitch, AuraRack).

C++20 · JUCE 8.0.15 (la même version qu'AuraEngine / AuraDAW) · VST3 (macOS, Windows, Linux) + AU (macOS).

**État** : Étapes 1, 2 et 3 du cahier des charges : processeur, bus Sidechain, moteur de rack lock-free, 9 modules,
**AuraComp à 3 algorithmes** (Aura-Opto, Aura-Tube, L-Comp), **éditeur NextDSP** (anthracite `#0B0C10`, rack en grille
de 10 cartes, glisser-déposer, réduction de gain et niveaux en direct), et deux modules à façade dédiée : **W9BMK1** (CL 1B, avec
**Auto Focus**) et **L9A** (LA-2A).

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
| `Source/DSP/Modules/` | `AuraEQ`, `AuraComp`, `AuraDuck`, `AuraClip`, `AuraLimit`, `AuraDelay`, `AuraVerb`, `AuraW9B` |
| `Source/DSP/ModuleRegistry.h` | Métadonnées par type, fabrique |
| `Source/Engine/RackEngine.*` | Moteur du rack : 10 slots, ordre atomique, échanges de modules, bypass, Mix compensé, Dry maître |
| `Source/Parameters.*` | Disposition APVTS (154 paramètres), textes qui suivent le module du slot |
| `Source/PluginEditor.*` | Éditeur : une seule horloge `VBlankAttachment`, contenu 1280 × 780 mis à l'échelle |
| `Source/UI/` | `NextLookAndFeel` (vectoriel), `RackView` (grille + glisser-déposer), `SlotComponent`, `TopBar`, `Widgets.h`, `FaceplateKit` (potard, interrupteur, VU, plaque), `Faceplate` (interface + fabrique), `W9bFaceplate`, `L9aFaceplate` |
| `Source/PluginProcessor.*` | Bus principal + Sidechain, traitement float / double, état, latence, CPU |
| `Tests/DspSelfTest.cpp` | Banc DSP, moteur et stress multi-thread, sans JUCE (136 vérifications) |
| `Tests/HostTest.cpp` | Hôte VST3 : charge le binaire compilé comme un DAW (14 vérifications) |
| `Tests/UiTest.cpp` | Éditeur sans fenêtre : glisser-déposer, resynchronisation, rafale de `moveSlot`, rendu PNG, liaison de chaque contrôle de façade à un paramètre réel (11 vérifications) |

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

## Les 9 modules

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
- Sélecteur de module par slot (`setSlotModule`), Bypass, Mix, trims In / Out, indicateur DXD, latence, voyant Sidechain,
  CPU.

## Bancs d'essai

`aurarack_dsp_test` (sans JUCE, 136 vérifications), `aurarack_host_test` (hôte VST3, 14) et `aurarack_ui_test` (éditeur, 7) : tout passe
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
- **W9BMK1 / L9A** : constantes calibrées par mesure, non comparées au matériel (aucune revendication de « dépasser l'original ») ; la police du logo dépend de l'OS ; façades non vues dans un vrai DAW.
- **Auto Focus** : lent par construction (≈ 10 s pour se caler après un changement de programme) ; le Threshold manuel est ignoré pendant l'Auto ; testé sur une parole synthétique et un sinus, pas sur du matériel musical varié : à valider à l'écoute (percussions, mix complet).
  Changer d'algorithme remet à zéro l'enveloppe (léger saut de gain).
- **Aura-Tube, Attack court** : à 0,5 ms l'enveloppe suit les graves (< 500 Hz) : comportement réel d'un compresseur
  rapide, pas un défaut de calcul. Monter Attack à quelques ms le supprime.
- **L-Comp** : sans anticipation, l'attaque de ~50 µs laisse passer le tout début d'un pic ; c'est le plafond doux
  (C¹, ADAA) qui l'écrête, donc ce pic est légèrement saturé plutôt que comprimé. La sortie ne dépasse jamais 0 dBFS.
