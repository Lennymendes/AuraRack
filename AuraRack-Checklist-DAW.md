# AuraRack — Checklist de validation en DAW

Objectif : ce qui n'a **pas** pu être vérifié par les bancs automatiques (écoute, hôtes réels, ergonomie). Cocher et noter l'hôte,
l'OS, la fréquence d'échantillonnage et la version (commit) testés. Les bancs (DSP 248, hôte VST3 14, UI 27) passent, mais aucun
n'a tourné dans un vrai DAW.

**Préparation** : installer `AuraRack.vst3`, ouvrir une session 48 kHz (puis refaire les points marqués ★ à 44,1 / 96 / 384 kHz),
latence de lecture ≥ 256 éch., un second plugin de référence (compresseur / de-esser / mètre LUFS de votre choix) pour comparer.

## 1. Voix
- [ ] Charger AuraRack sur une piste voix ; ordre `E9DS → L9A → W9BMK1` ; lecture sans bruit ni clic.
- [ ] **E9DS** : Listen On, caler Frequency / Width sur la sibilance ; Listen Off ; Range 6–10 dB. Pas de zozotement en Split ; comparer Split et Wide.
- [ ] **E9DS Lookahead** Off → 0,5 → 1 → 2 ms : les « t », « ch », « s » brefs passent moins ; **aucun clic** en changeant de cran pendant la lecture.
- [ ] ★ **Latence / PDC** : le DAW affiche la latence du plugin ; elle augmente avec Lookahead (48 éch. à 1 ms / 48 kHz) ; la voix reste calée avec une piste de référence **sans** plugin (bouncer les deux, vérifier l'alignement à l'échantillon) ; Mix 50 % ne fait ni écho ni peigne.
- [ ] **W9BMK1 Auto Focus** (cible 3 / 6 dB) : le VU converge vers le repère en ~10 s ; pas de pompage entre phrases ; Manual : le Threshold retrouvé.
- [ ] **V9C3 Auto Focus** (Focus 3 / 6 dB) : même vérification ; le potard Threshold affiche le seuil effectif (dBu) et s'estompe en Auto ; Peak vs RMS ; Release Auto Fast / Auto Slow.
- [ ] V9C3 Ratio Soft → Limit : la réduction suit le réglage ; Make-Up compense ; SC HPF 140 Hz : un souffle / plosive grave ne fait plus pomper.

## 2. Piano / instrument / DI
- [ ] **V9P2** sur une piste piano (ou DI de guitare / basse) : Input **Inst / DI** et Mic : basculement **sans clic** en lecture.
- [ ] Gain par crans 36 → 42 → 48 dB : **pas de clic**, niveau ± 6 dB par cran ; Fine 0 → 6 dB ; Pad −20 dB.
- [ ] HPF 80 / 120 Hz : le grave est coupé à 12 dB/oct, sans clic en commutant.
- [ ] Saturation : pousser le Gain jusqu'à la zone rouge : le voyant Clip s'allume dès +19 dBu, la distorsion augmente progressivement, **pas d'explosion de niveau** (plafond +6 dBFS).
- [ ] Phase Ø (L, R, L+R) avec une prise stéréo ou deux micros ; Link Dual / Linked ; R Offset ±6 dB (Dual).
- [ ] 48V : l'indicateur bascule, **le son ne change pas** (comportement voulu).
- [ ] V9C3 en Stereo sur le piano : l'image stéréo ne bouge pas ; Dual : canaux indépendants ; M/S.

## 3. Bus / mastering avec LUFS
- [ ] Menu ⚙️ → **Mesure LUFS** : activer ; lecture du Momentané / Court terme / Intégré / LRA / crête.
- [ ] Comparer avec le mètre LUFS de référence sur la **même sortie** (écart attendu ≤ 0,1 LU en intégré après une minute de programme ; la mesure d'AuraRack est prise en sortie du rack, avant le traitement de l'hôte).
- [ ] Remise à zéro de l'intégrée : repart de −∞ ; relancer la lecture.
- [ ] ★ Sinus 997 Hz −23 dBFS stéréo ≈ −23,0 LUFS à 44,1 / 48 / 96 / 384 kHz.
- [ ] Bus : `V9C3 (2–3 dB, Auto Focus) → AuraLimit` : Auto Focus stable sur un mix complet (percussions, basse) ; le seuil ne dérive pas.
- [ ] Mono / source mono : la mesure LUFS compte la voie une fois.

## 4. Interface, glisser-déposer, menus
- [ ] Glisser-déposer des **10 tuiles** (réordonner en lecture) : aperçu animé, un seul changement à l'arrêt, **aucun clic audio gênant** (creux de 5 ms attendu).
- [ ] Clic = sélection ; la sélection suit le slot (pas la position) ; sélection mémorisée après fermeture / réouverture de la fenêtre et rechargement de la session.
- [ ] Pastille LED = bypass ; croix = retrait ; tuile vide « + » et **clic droit** = menus d'ajout ; menus JUCE ouverts dans l'hôte (focus, fermeture à l'Échap).
- [ ] Menu ⚙️ : Français ↔ English appliqué à toute l'interface ; Infos : info-bulles lisibles ; LUFS.
- [ ] Faceplates W9BMK1, L9A, E9DS (6 potards), V9C3, V9P2 : chaque contrôle agit ; molette / glisser fins ; **automation** : enregistrer / relire un paramètre ; double-clic = valeur par défaut.
- [ ] Redimensionnement de la fenêtre, écrans HiDPI (macOS Retina, Windows 125–200 %), plusieurs instances.
- [ ] Rendu des polices (logo W9BMK1) selon l'OS.

## 5. Sessions, hôtes, fréquences
- [ ] Sauvegarder / rouvrir une session : modules, ordre, réglages, langue, sélection, LUFS identiques.
- [ ] Offline bounce vs temps réel : rendu identique (hors dérives de LUFS intégré).
- [ ] ★ 44,1 / 48 / 96 / 384 kHz : mêmes réglages = même réduction / même timbre (aux imprécisions de l'oreille).
- [ ] Hôtes visés : AuraDAW, Reaper, Cubase / Nuendo, Logic (AU), Ableton Live, Pro Tools si pertinent ; routage **Sidechain** propre à chaque hôte.
- [ ] CPU à 384 kHz avec 10 modules actifs : pas de craquements ; arrêt / démarrage de la lecture sans bruit.
- [ ] Changer de module d'un slot en lecture : fondu sans clic ; la latence annoncée se met à jour.

## Notes à relever
Pour chaque échec : hôte + version, OS, fréquence, buffer, module, réglages, lecture / offline, description (clic, dérive, latence, crash), et si reproductible.
