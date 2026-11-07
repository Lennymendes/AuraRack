# AuraRack 0.2 — NextDSP · rack multi-effets DXD (VST3 / AU)

Rack modulaire de mixage / mastering : **10 slots en série**, réordonnables à chaud, **7 modules** DSP natifs,
traitement intégral en **64 bits** de 44,1 kHz au **DXD 352,8 / 384 kHz**. Marque **NextDSP**, suite **AuraSuite**
(Aura, AuraEngine, AuraDrum, AuraPitch, AuraRack).

C++20 · JUCE 8.0.15 (la même version qu'AuraEngine / AuraDAW) · VST3 (macOS, Windows, Linux) + AU (macOS).

**État** : Étapes 1 et 2 du cahier des charges (processeur, bus Sidechain, moteur de rack lock-free, 7 modules).
L'interface est provisoire (`GenericAudioProcessorEditor` : tous les paramètres en liste) ; l'Étape 3 apporte
le `PluginEditor` NextDSP (anthracite `#0B0C10`, rack, glisser-déposer).

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
| `Source/DSP/Modules/` | `AuraEQ`, `AuraComp`, `AuraDuck`, `AuraClip`, `AuraLimit`, `AuraDelay`, `AuraVerb` |
| `Source/DSP/ModuleRegistry.h` | Métadonnées par type, fabrique |
| `Source/Engine/RackEngine.*` | Moteur du rack : 10 slots, ordre atomique, échanges de modules, bypass, Mix compensé, Dry maître |
| `Source/Parameters.*` | Disposition APVTS (145 paramètres), textes qui suivent le module du slot |
| `Source/PluginProcessor.*` | Bus principal + Sidechain, traitement float / double, état, latence, CPU |
| `Tests/DspSelfTest.cpp` | Banc DSP, moteur et stress multi-thread, sans JUCE (55 vérifications) |
| `Tests/HostTest.cpp` | Hôte VST3 : charge le binaire compilé comme un DAW (14 vérifications) |

## Architecture audio

**64 bits de bout en bout.** `supportsDoublePrecisionProcessing()` : un hôte 64 bits passe ses tampons `double`
directement ; un hôte 32 bits voit ses échantillons convertis une seule fois en entrée et en sortie. Tout le reste
(filtres, enveloppes, lignes de retard, mélanges) est en `double`.

**Zéro allocation sur le thread audio.** Tout est alloué dans `prepareToPlay()` ou sur le message thread. Le banc
compte les `operator new` pendant 1,5 s de rendu à 352,8 kHz avec les 7 modules, des bypass toutes les 100 ms et deux
réordonnancements : **0**. Le traitement se fait par tranches de 256 échantillons au plus (un bloc plus grand de l'hôte
ne réalloue rien).

**Adaptation DXD.** Les étages non linéaires utilisent l'ADAA du 1er ordre ; le sur-échantillonnage n'est ajouté que
sous 176,4 kHz (×4 sous 88,2 kHz, ×2 jusqu'à 176,4 kHz, ×1 au-delà). Le détecteur True Peak passe de 4× à 2×.

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
- **Trims** −24 / +24 dB, lissés en 20 ms.
- La latence d'un slot est conservée en bypass : la latence du plugin ne change qu'au changement de module
  (`setLatencySamples` depuis le message thread).

### Barre globale (paramètres)

Input Gain, Output Gain, Master Mix (Dry aligné sur la latence totale du rack), **Sidechain Source** :
*Bus Sidechain* (bus auxiliaire VST3 / AU, mono ou stéréo) ou *Entrée du rack*. Indicateurs exposés au futur éditeur :
crêtes d'entrée / sortie, réduction de gain par slot, latence active, présence du Sidechain, fréquence, charge CPU.

## Les 7 modules

| Module | Algorithme | Paramètres |
|---|---|---|
| **AuraEQ** | 3 SVF TPT (Zavalishin / Simper) en série : Low Shelf, Peak, High Shelf. g = tan(π·f/fs) : réponse exacte et stable de 20 Hz à 384 kHz. Lissage en log de la fréquence, coefficients recalculés toutes les 16 éch. Une bande à 0 dB est sautée (sortie bit-identique). | Low Freq/Gain, Mid Freq/Gain/Q, High Freq/Gain, Shelf Q, Output |
| **AuraComp** | Détection Peak ou RMS (10 ms) liée en stéréo, courbe à genou doux (Giannoulis / Massberg / Reiss), lissage en dB. Mode Opto : relâchement dépendant de la réduction. Clé interne ou Sidechain. | Threshold, Ratio, Attack, Release, Knee, Makeup, Detector, Style, Key |
| **AuraDuck** | Clé Sidechain (ou interne) filtrée HP + LP 24 dB/oct ; maintien de crête ≥ 1,5 période redressée de la plus basse fréquence de la clé (le gain ne suit plus les alternances du kick : pas d'intermodulation sur la 808) ; lissage dB + 2 pôles d'arrondi. **Dyn EQ Sub** : low shelf TPT dont le gain suit l'enveloppe, seules les sub-basses baissent. | Threshold, Depth, Attack, Release, Key HP, Key LP, Mode, Sub Freq, Key |
| **AuraClip** | Genou réglable de hard (0 %) à 6 dB (100 %), C1. ADAA 1er ordre avec antidérivée exacte, repli au point milieu si mal conditionné. Sur-échantillonnage polyphase (127 + 65 coefficients, Kaiser) sous 176,4 kHz seulement. | Drive, Ceiling, Knee, Output, Auto Gain |
| **AuraLimit** | Lookahead 1,5 ms, détection True Peak (32 coefficients par phase), minimum glissant, relâchement adaptatif (court sur transitoire isolé, long sur réduction soutenue), moyenne glissante de la longueur de l'anticipation : la rampe d'attaque atteint le gain requis à l'arrivée de la crête (démonstration ci-dessous). | Gain, Ceiling (dBTP), Release, Adaptive |
| **AuraDelay** | Ping-pong stéréo continu (0 % = deux delays, 100 % = alternance L / R), Lagrange 3e ordre, temps lissé (glissement type bande), Low Cut / High Cut TPT dans la boucle, retard de groupe du High Cut compensé. | Time, Feedback, Ping-Pong, Low Cut, High Cut, R / L |
| **AuraVerb** | Plate de Dattorro (JAES 1997) mise à l'échelle de fs et de Size ; lectures du réservoir fractionnaires (Size continu) ; allpass modulés ; Decay = vrai RT60 (gain par segment 10^(−3·durée/RT60)) ; Damping en Hz. | Pre-Delay, Size, Decay, Damping, Width, Low Cut |

Delay et Verb sortent 100 % wet ; leur Mix de slot passe à 30 % quand on les choisit.

**Garantie de l'anticipation du limiteur.** Avec un retard audio D, un minimum glissant sur D + 1 et une moyenne de
longueur A + 1 (A = anticipation, D = A + retard du détecteur + 1), chaque valeur moyennée couvre l'instant de la crête :
la moyenne est ≤ au gain requis. Le relâchement (instantané vers le bas, exponentiel vers le haut) reste ≤ au minimum,
la propriété est conservée.

## Bancs d'essai

`aurarack_dsp_test` (sans JUCE, 55 vérifications) et `aurarack_host_test` (hôte VST3, 14 vérifications) : tout passe
(Linux, GCC 13, JUCE 8.0.15). Le banc DSP passe aussi sous **ThreadSanitizer** (stress : le message thread change
modules, ordre, paramètres et bypass en rafale pendant que le thread audio tourne à 384 kHz : aucune course) et sous
**AddressSanitizer + UBSan** (aucune erreur, aucune fuite).

| Mesure | Résultat |
|---|---|
| Peak 1 kHz +12 dB à 44,1 / 352,8 / 384 kHz | 12,0000 dB |
| Low shelf 20 Hz +6 dB à 384 kHz (mesuré à 2 Hz) | 5,9994 dB |
| EQ à 0 dB, rack vide | sortie bit-identique |
| Comp, DC −6 dB, seuil −18, 4:1 | 8,9846 dB (théorie 8,9846) |
| Duck Dyn EQ, kick → 808 à 352,8 kHz | sub 45 Hz −11,7 dB, charleston 6 kHz −0,008 dB, ondulation 0,02 dB |
| Clip, 7 kHz, drive 18 dB : repliement / clip naïf | 44,1 k : −70 / −12 dB · 96 k : −53 / −16 dB · 352,8 k (ADAA seul) : −45 / −36 dB |
| Limit, +12 dB sur bruit + transitoires, plafond −1 dBTP | −0,973 dBTP (44,1 k) · −0,990 dBTP (384 k), true peak mesuré par sinc ×16 |
| Delay ping-pong 10 ms, feedback 50 % | échos à 10 / 20 / 30 ms ±1 éch., amplitudes 1 / 0,5 / 0,25 |
| Verb, Decay 2 s (T20, octave 1 kHz) | 2,09 s à 48 kHz et à 384 kHz |
| Latence annoncée à l'hôte, Limit à 352,8 kHz | 546 éch., impulsion 64 bits intacte à cet endroit |
| Duck par le bus Sidechain du VST3 | sub −11,7 dB pendant le kick, 0,0 dB sans |
| État sauvegardé → nouvelle instance | rendu bit-identique |

**Charge CPU** (rack complet, 7 modules, stéréo, un cœur Xeon 2,3 GHz) : 4,5 % à 44,1 kHz · 8,8 % à 96 kHz ·
16,4 % à 352,8 kHz · 17,1 % à 384 kHz.

## Paramètres et compatibilité des sessions

Identifiants figés : `in_gain`, `out_gain`, `master_mix`, `sc_source`, puis pour chaque slot `sN_type` (non
automatisable), `sN_bypass`, `sN_mix`, `sN_in`, `sN_out`, `sN_p1` … `sN_p9`. Les 9 paramètres génériques sont
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
  CI ; une séance dans un vrai DAW (Pro Tools via AuraDAW, Logic, Cubase, Ableton) reste à faire, surtout le routage
  Sidechain propre à chaque hôte.
- **Interface** : provisoire jusqu'à l'Étape 3.
