# MusicDAWM — Mode d'emploi

*Version 0.2.0 (préversion) pour Windows 64 bits. Novembre 2026.*

MusicDAWM est un logiciel de composition et d'arrangement basé sur [Ardour](https://ardour.org) 9.8. On enregistre sur un enregistreur multipiste (Zoom R8, Korg SR-1…), puis on apporte les prises dans MusicDAWM pour les caler sur une grille, ajouter batterie et basse, mixer et finaliser la chanson.

Ce mode d'emploi décrit **ce que MusicDAWM ajoute à Ardour**, rangé par fonctionnalité. Tout ce qui fonctionne comme dans Ardour (édition, automation, plug-ins, export avancé…) est décrit dans le [manuel d'Ardour](https://manual.ardour.org) (en anglais).

**Conventions.** Le programme est en anglais : les noms de menus et de boutons sont écrits tels qu'ils apparaissent, en **gras** (par exemple **Window → Drum Studio**). Les touches sont écrites ainsi : <kbd>Espace</kbd>, <kbd>Ctrl</kbd>+<kbd>Z</kbd>. Annuler une action se fait toujours par **Edit → Undo** (<kbd>Ctrl</kbd>+<kbd>Z</kbd>).

---

## Sommaire

**Partie I — Prise en main**
1. [Installer et lancer MusicDAWM](#1-installer-et-lancer-musicdawm)
2. [Régler l'audio](#2-régler-laudio)
3. [L'écran de démarrage](#3-lécran-de-démarrage)
4. [L'interface en bref](#4-linterface-en-bref)
5. [Le parcours type d'une chanson](#5-le-parcours-type-dune-chanson)

**Partie II — Apporter une chanson**
6. [Depuis l'enregistreur : une chanson à partir de fichiers WAV](#6-depuis-lenregistreur--une-chanson-à-partir-de-fichiers-wav)
7. [Depuis un autre logiciel : DAWproject, Waveform, Cubase](#7-depuis-un-autre-logiciel--dawproject-waveform-cubase)

**Partie III — Le tempo et la grille**
8. [Pourquoi une carte de tempo](#8-pourquoi-une-carte-de-tempo)
9. [Détecter les temps et les mesures](#9-détecter-les-temps-et-les-mesures)
10. [Taper le tempo](#10-taper-le-tempo)
11. [Corriger la grille](#11-corriger-la-grille)

**Partie IV — Arranger : batterie et basse**
12. [Drum Studio : la batterie](#12-drum-studio--la-batterie)
13. [Bass Studio : la basse](#13-bass-studio--la-basse)
14. [Batterie et basse ensemble](#14-batterie-et-basse-ensemble)
15. [Les sons de batterie et de basse](#15-les-sons-de-batterie-et-de-basse)

**Partie V — Retoucher**
16. [Melodyne et les plug-ins ARA (en attente)](#16-melodyne-et-les-plug-ins-ara-en-attente)

**Partie VI — Mixer**
17. [La fenêtre Final Mix](#17-la-fenêtre-final-mix)
18. [Le mix de départ](#18-le-mix-de-départ)
19. [Juger le mix : Avant, Après, Référence](#19-juger-le-mix--avant-après-référence)
20. [Le master : volume perçu, cible, limiteur](#20-le-master--volume-perçu-cible-limiteur)
21. [Freeze et plug-ins fournis](#21-freeze-et-plug-ins-fournis)

**Partie VII — Finaliser**
22. [Finalise Mix : le fichier prêt à publier](#22-finalise-mix--le-fichier-prêt-à-publier)

**Partie VIII — Référence**
23. [Dépannage](#23-dépannage)
24. [Raccourcis clavier](#24-raccourcis-clavier)
25. [Où sont les fichiers](#25-où-sont-les-fichiers)
26. [Différences avec Ardour 9.8](#26-différences-avec-ardour-98)
27. [Glossaire](#27-glossaire)
28. [Outils en ligne de commande](#28-outils-en-ligne-de-commande)
29. [Licences et crédits](#29-licences-et-crédits)

---

# Partie I — Prise en main

## 1. Installer et lancer MusicDAWM

MusicDAWM est un **dossier portable** : il n'y a pas d'installation, et rien n'est écrit en dehors du dossier, sauf vos réglages (§25).

### Télécharger

- **Version publiée :** page **Releases** du dépôt GitHub, fichier `MusicDAWM-<version>-win64.zip` (environ 250 Mo).
- **Version intermédiaire :** **Actions → Windows build** → le dernier passage vert → **Artifacts** → `MusicDAWM-<version>-win64`. GitHub l'emballe dans un second zip : décompressez-le une fois. Il faut être connecté à GitHub.

### Installer

1. **Avant de décompresser :** clic droit sur le zip → **Propriétés** → cochez **Débloquer** → **OK**. Sans cela, Windows affiche « Windows a protégé votre ordinateur » pour chaque programme du dossier. (En PowerShell : `Unblock-File .\MusicDAWM-0.2.0-win64.zip`.)
2. Décompressez où vous voulez, par exemple dans `C:\`. Vous obtenez un dossier `MusicDAWM-<version>-win64`. **Gardez sa structure telle quelle :** le programme trouve ses fichiers à partir du dossier `bin`.
3. Lancez **`bin\MusicDAWM.exe`**. Un raccourci sur le bureau : clic droit sur le fichier → **Envoyer vers → Bureau**.

**Smart App Control (Windows 11).** S'il est **activé**, il bloque les programmes non signés sans proposer de « Exécuter quand même », et MusicDAWM n'est pas signé : il ne démarrera pas. Vérifiez dans **Sécurité Windows → Contrôle des applications et du navigateur → Smart App Control**. Le désactiver ne peut pas être annulé sans réinitialiser Windows : décidez-le en connaissance de cause. En mode **Désactivé** ou **Évaluation**, l'étape « Débloquer » suffit.

### Mettre à jour

Décompressez la nouvelle version à côté de l'ancienne. Vos réglages et vos chansons ne sont pas dans le dossier du programme (§25) : rien n'est perdu. Supprimez l'ancien dossier quand la nouvelle version fonctionne.

**Help → About** affiche la version de MusicDAWM et celle d'Ardour sur laquelle elle est basée.

## 2. Régler l'audio

Au premier lancement, Ardour pose quelques questions (taille de l'affichage, préférences), puis vient l'**écran de démarrage** (§3). Quand vous ouvrez ou créez une chanson, la fenêtre **Audio/MIDI Setup** apparaît :

| Réglage | Choix recommandé |
|---|---|
| **Audio System** | **PortAudio (default)** |
| **Driver** | **Windows WASAPI**. Si votre carte son n'apparaît pas, essayez **MME** (DirectSound et WDM-KS existent aussi). |
| **Output Device** | vos haut-parleurs ou votre casque |
| **Input Device** | n'importe lequel, ou aucun : MusicDAWM n'enregistre pas |
| **Sample Rate** | la fréquence de vos enregistrements : **44,1 kHz** ou **48 kHz** pour un Zoom R8 |
| **Buffer Size** | 1024 échantillons convient : vous ne jouez pas en direct à travers le logiciel, la latence ne compte pas |

Cliquez ensuite sur **Start**.

> **Pas d'ASIO.** Cette version n'a pas de pilote ASIO. Pour de la lecture, WASAPI est de toute façon le meilleur choix.

## 3. L'écran de démarrage

Au lancement (et avec **Session → New** ou **Session → Open**), MusicDAWM affiche la page **START** :

| Bouton | Ce qu'il fait | Détails |
|---|---|---|
| **New song from WAV files…** | Crée une chanson à partir des prises d'un enregistreur | §6 |
| **Open a song…** | Ouvre une chanson (un fichier `.ardour`) | |
| **Recent songs** | Les chansons ouvertes récemment | |
| **Bring a song from another program: DAWproject… / Waveform… / Cubase…** | Crée une chanson vide nommée « Imported » et la date, puis ouvre l'import | §7 |
| **New empty song** | La page de création d'Ardour, avec toutes ses options (nom, dossier, modèle, fréquence) | |

Les onglets **RECENT**, **OPEN** et **NEW** à gauche sont les pages d'origine d'Ardour.

Pour renommer une chanson : **Session → Rename**.

## 4. L'interface en bref

MusicDAWM a l'interface d'Ardour, avec quelques ajouts :

| Où | Quoi |
|---|---|
| **Éditeur** (la vue principale) | les pistes et leurs régions sur une ligne de temps, les règles (mesures, tempo, marqueurs) |
| **Barre latérale de l'éditeur** (à droite ; **View → Show Editor List**, <kbd>Maj</kbd>+<kbd>L</kbd>) | ses pages, dont **Drum Studio** et **Bass Studio** ; la liste déroulante en haut change de page |
| **Window → Final Mix…** | la table de mixage simplifiée (§17) |
| **Window → Drum Studio / Bass Studio** | ouvre directement la page correspondante de la barre latérale |
| **Edit → Tempo → Tap Tempo Map…** | la grille : détection et tap (§9–11) |
| **Session → Import DAWproject… / Import Waveform Project… / Import Cubase Project…** | les imports (§7) |
| **Mix** (en haut à droite) | la table de mixage complète d'Ardour |

### La présentation « auteur-compositeur »

MusicDAWM n'enregistre pas : vous enregistrez sur le R8 et vous apportez les prises. Les commandes qui ne servent qu'à l'enregistrement sont donc **masquées** :
- le bouton **Record** de la barre de transport, **Punch In/Out** et le mode **Rec:** ;
- le bouton de page **Rec** (en haut à droite) ;
- le bouton d'armement de chaque piste et les boutons **In / Disk** des tranches de mixage.

Pour les retrouver : cochez **View → Show Recording Controls** (décochez pour les masquer). Le choix est gardé au prochain lancement. Les raccourcis clavier d'enregistrement fonctionnent toujours.

## 5. Le parcours type d'une chanson

1. **Apporter les prises** : **New song from WAV files…** sur le dossier `AUDIO` du projet R8 (§6).
2. **Poser la grille** : **Edit → Tempo → Tap Tempo Map…** → **Detect beats and bars** sur la piste qui a le rythme le plus net ; corriger si besoin (§9–11).
3. **Ajouter la batterie** : Drum Studio — écouter les styles, créer la piste, jouer les parties en direct pendant que la chanson tourne, aplatir en MIDI (§12).
4. **Ajouter la basse** : Bass Studio — taper les accords, écouter, créer la ligne (§13).
5. **Mixer** : **Window → Final Mix…** — rôles, **Starting mix**, comparer **Before / After / Ref**, ajuster (§17–20).
6. **Finaliser** : **Finalise Mix…** — un WAV (et un MP3) au bon volume, prêt à publier (§22).

---

# Partie II — Apporter une chanson

## 6. Depuis l'enregistreur : une chanson à partir de fichiers WAV

### Pas à pas

1. Copiez le dossier du projet depuis la carte SD de l'enregistreur vers le PC (par exemple `PROJ003`, avec son dossier `AUDIO`).
2. Sur l'écran de démarrage, cliquez **New song from WAV files…**.
3. Entrez dans le dossier `AUDIO` et sélectionnez toutes les prises (<kbd>Ctrl</kbd>+<kbd>A</kbd>), ou sélectionnez simplement le dossier. Cliquez **Make the song**.
4. La fenêtre **Audio/MIDI Setup** s'ouvre si l'audio n'est pas encore démarré (§2) : choisissez la fréquence de l'enregistreur.
5. La chanson est créée dans le dossier indiqué sur la page **NEW** (en général `Documents`), et nommée d'après le projet (`PROJ003` ; un numéro est ajouté si ce nom existe déjà).

### Ce que vous obtenez

- **Une piste par fichier**, nommée d'après le fichier : les numéros et les `_` du début sont retirés, `01_Guitar_L.WAV` devient « Guitar L ». Un fichier mono donne une piste mono, un fichier stéréo une piste stéréo.
- **Chaque fichier commence au début de la chanson**, comme le R8 et le Korg SR-1 les écrivent : un solo enregistré au milieu du morceau contient du silence avant le solo, il tombe donc à sa place.
- Un **récapitulatif** indique le nombre de pistes créées et les fichiers convertis ou illisibles.

### Bon à savoir

- Les fichiers sont **copiés** dans le dossier de la chanson ; ceux de la carte ou de votre copie ne sont pas modifiés.
- Un fichier à une autre fréquence que la chanson (44,1 kHz dans une chanson à 48 kHz) est converti avec le meilleur convertisseur. Choisissez la fréquence de l'enregistreur dans **Audio/MIDI Setup** pour éviter toute conversion.
- **Enregistreurs qui horodatent les prises** (fichiers Broadcast WAV qui ne contiennent que la prise et son heure d'enregistrement) : cochez **Place the files by their recording time stamp** dans la fenêtre de choix des fichiers. La prise la plus ancienne commence la chanson, les autres gardent leur écart avec elle ; les fichiers sans horodatage commencent à 0.
- Fonctionne avec n'importe quels fichiers WAV, pas seulement ceux du R8.

## 7. Depuis un autre logiciel : DAWproject, Waveform, Cubase

Créez d'abord une chanson vide (ou utilisez **Bring a song from another program** sur l'écran de démarrage, qui le fait pour vous).

### DAWproject (Cubase 14, Studio One 6.5, Bitwig 5…)

1. Dans l'autre logiciel, exportez le morceau en fichier **.dawproject** (Cubase : *Fichier → Exporter → DAWproject*).
2. **Session → Import DAWproject…**, choisissez le fichier, **Import**.
3. Un récapitulatif liste ce qui a été repris et ce qui ne l'a pas été.

| Repris | Pas (encore) repris — listé dans le récapitulatif |
|---|---|
| Tempo et mesure, avec leurs changements (une rampe de tempo devient un saut à son début) | Plug-ins et leurs réglages (y compris EQ et compresseurs intégrés de l'autre logiciel) : à remettre à la main, d'après la liste |
| Marqueurs | Audio étiré dans le temps (time-stretch) : joué à sa vitesse d'origine ; le récapitulatif dit lequel et de combien |
| Pistes audio et MIDI, noms et couleurs ; dossiers aplatis ; groupes et effets → bus, avec routages et envois | Expressions de notes, formes de fondus enchaînés, panoramique des envois, VCA, scènes, vidéo |
| Volume, panoramique, mute, solo ; automation du volume, du pan, du mute et des envois | |
| Clips audio : position, partie du fichier, fondus, boucles (régions répétées), clips désactivés (régions muettes), noms | |
| Clips MIDI avec leurs notes (temps, durée, vélocité, canal) | |

L'import s'ajoute à la chanson ouverte et ne s'annule pas en une étape : si le résultat ne convient pas, fermez sans enregistrer.

### Waveform (version 11 et antérieures)

1. **Session → Import Waveform Project…**
2. Choisissez le **fichier d'édition** (`<nom> Edit 1.tracktionedit`, dans le dossier du projet Waveform). Laissez le dossier tel que Waveform l'a laissé : MusicDAWM retrouve l'audio grâce au fichier de projet (`<nom>.tracktion`) voisin, ou par nom de fichier dans ce dossier.
3. Récapitulatif comme pour DAWproject.

Repris : pistes (nommées d'après leurs fichiers audio, puisque Waveform ne garde pas les noms par défaut ; fichiers mono → pistes mono), positions des clips, découpes, gain de clip, fondus, faders, pan, mute, solo, niveau master, tempo et mesure, marqueurs, clips MIDI. Listés sans être repris : plug-ins, envois auxiliaires, pan de clip, time-stretch, boucles.

Waveform 14 n'a plus de fichier `.tracktion` mais exporte lui-même en DAWproject : utilisez **Import DAWproject** pour lui.

**Session → Convert Waveform Project to DAWproject…** écrit le même morceau en fichier `.dawproject`, audio inclus, pour l'ouvrir dans un autre logiciel ou l'archiver.

### Cubase

**Session → Import Cubase Project…** explique l'étape à faire dans Cubase : ses fichiers `.cpr` ne peuvent pas être lus (format non publié). Exportez le projet depuis **Cubase 14.0.20 ou plus récent** en DAWproject (*Fichier → Exporter → DAWproject*), puis choisissez ce fichier. Les versions plus anciennes ne savent pas exporter en DAWproject : exportez les pistes en fichiers audio et importez-les avec *Import Audio/MIDI files*.

---

# Partie III — Le tempo et la grille

## 8. Pourquoi une carte de tempo

Une chanson enregistrée **sans clic** a un tempo qui bouge un peu. Tant que la grille de mesures du logiciel ne suit pas le morceau, la batterie, la basse et tout ce qui est calé sur la grille se décale.

La **carte de tempo** fait suivre la grille à l'interprétation. **Edit → Tempo → Tap Tempo Map…** propose deux façons de la faire, que l'on peut combiner :
- **Détecter** : MusicDAWM écoute une ou plusieurs pistes et trouve lui-même les temps et les mesures (§9) ;
- **Taper** : vous tapez la touche <kbd>T</kbd> en rythme pendant la lecture (§10).

Ce qui change et ce qui ne change pas :
- **L'audio reste exactement où il a été enregistré.** Seule la grille bouge.
- Le MIDI, Drum Studio et Bass Studio suivent les mesures et les temps : ils se déplacent avec la nouvelle grille.
- **Edit → Undo** ramène la carte précédente en une étape.
- La grille peut ensuite être retouchée avec les outils de tempo d'Ardour (déplacer les marqueurs de tempo).

## 9. Détecter les temps et les mesures

En haut de la fenêtre **Tap Tempo Map** :

1. **Listen to:** cochez les pistes audio qui ont un rythme net : la batterie seule, ou plusieurs pistes ensemble (batterie + basse + guitare). La première fois, les pistes dont le nom contient *drum*, *batterie*, *perc*, *kick* ou *beat* sont cochées ; s'il n'y en a pas, toutes le sont.
2. Choisissez la **mesure** plus bas (**Time signature**, par exemple 4/4).
3. Cliquez **Detect beats and bars**.

MusicDAWM écoute les pistes cochées sur toute la chanson, puis :
- trouve le **tempo** et **chaque temps**, en suivant le groupe s'il accélère ou ralentit, même nettement (de 80 à 120, ou un refrain plus rapide) ;
- **devine le premier temps de chaque mesure** : celui où il y a le plus de basses (grosse caisse, notes de basse) et où les accords changent ;
- choisit le bon tempo entre un tempo et son double : sur une ballade où le charleston joue en croches, il garde le tempo lent ;
- **fait la carte de tempo** (une étape d'annulation).

La fenêtre indique ensuite le tempo, le nombre de temps trouvés, la netteté de la pulsation et les **mesures sans temps net** (une coupure, un passage calme) à vérifier.

À savoir :
- Il faut au moins 8 secondes de musique avec une pulsation. Un jeu très libre (rubato) se tape mieux.
- Seules les pistes audio peuvent être écoutées ; une piste Drum Studio est déjà sur la grille.
- La détection vient du détecteur de tempo de RunBPM, adapté aux chansons entières et aux mesures.

## 10. Taper le tempo

1. Choisissez ce que vous tapez : **every beat** (chaque temps) ou **every bar start** (chaque début de mesure), et la **mesure** (4/4, 3/4, 6/8…).
2. Lancez la lecture (<kbd>Espace</kbd>) un peu avant le premier temps et **tapez <kbd>T</kbd>** (ou cliquez **Tap**) en rythme. **Le premier tap commence une mesure** : commencez sur un premier temps (un décompte ou un silence avant ne gêne pas). Inutile de taper toute la chanson.
3. Arrêtez la lecture. Chaque lecture est une **prise** : pour corriger un passage, rejouez-le et tapez seulement ce passage ; les nouveaux taps remplacent les anciens à cet endroit.
4. Cliquez **Apply to the tempo map**. La fenêtre indique combien de taps ont servi, combien de doubles taps ont été ignorés et combien de taps manqués ont été complétés.

Options :
- **Smooth my timing** (activé par défaut) lisse les petites irrégularités de vos taps tout en suivant les vrais changements de tempo.
- **Snap taps to the hits of:** choisissez une piste aux attaques nettes (batterie, guitare grattée) : chaque tap se cale sur l'attaque la plus proche.

**Outside the taps** — avant votre premier et après votre dernier tap :
- **Extend the grid to the whole song** (par défaut) :
  - avec une piste choisie dans **Snap taps to the hits of**, MusicDAWM **suit le rythme de cette piste** au-delà de vos taps : 8 mesures tapées suffisent pour toute la chanson ;
  - là où il ne trouve pas d'attaques nettes (coupure, couplet calme), il continue sur la tendance du tempo et se recale ensuite ; la fenêtre liste ces mesures (« No clear hits in bars 41–44 ») ;
  - sans piste choisie, la grille continue au tempo de vos 16 derniers (et, avant, premiers) taps.
- **Keep the tempo map outside the tapped passage** : seul le passage tapé change ; la carte avant et après reste à sa place dans le temps, la numérotation des mesures continue. Le tempo juste avant et juste après est légèrement ajusté pour que le passage commence sur une mesure. La mesure (time signature) est conservée.

Les taps sont pris sur ce que vous entendez (la latence de lecture est compensée).

## 11. Corriger la grille

| Problème | Solution |
|---|---|
| Les barres de mesure sont décalées d'un temps (le « 1 » n'est pas au bon endroit) | **Bar 1 one beat earlier** ou **Bar 1 one beat later**, jusqu'à ce que le « 1 » tombe sur le premier temps. Chaque clic refait la carte. |
| Le tempo trouvé est le double ou la moitié de celui que vous sentez | La fenêtre indique l'autre tempo possible. Tapez 4 à 8 temps à la bonne vitesse, puis **Detect** à nouveau : vos taps choisissent la vitesse, et **votre premier tap marque un début de mesure**. |
| Un passage est faux (la coupure, une intro sans batterie) | Tapez seulement ce passage, choisissez **Keep the tempo map outside the tapped passage**, puis **Apply**. |
| Une mesure en 6/8 | La détection trouve la pulsation du morceau : vérifiez les barres et corrigez avec les boutons, ou tapez. |
| Retouche fine | Outils de tempo d'Ardour : déplacez les marqueurs de tempo dans la règle **Tempo**. |

---

# Partie IV — Arranger : batterie et basse

## 12. Drum Studio : la batterie

Drum Studio donne un batteur à la chanson : on choisit un style, puis on joue les parties en direct pendant que la chanson tourne, comme on les annoncerait à un batteur. Tout ce qui est joué est noté sur la ligne de temps, se rejoue à l'identique, et peut devenir du MIDI ordinaire, modifiable note par note.

**Ouvrir :** **Window → Drum Studio**. C'est une page de la barre latérale de l'éditeur : la ligne de temps reste visible.

### Les réglages

| Réglage | Choix |
|---|---|
| **Style** | Rock, Pop, Ballad, Blues shuffle, Funk, Country, Reggae, Bossa nova, Jazz swing, Half-time, 6/8, Punk |
| **Kit** | 4 kits acoustiques AVL, 8 kits General MIDI, ou **No instrument** pour votre propre plug-in de batterie (§15) |
| **Feel** | **Normal**, **Half-time** (rythmes deux fois plus lents par rapport au tempo de la chanson : un rythme d'une mesure en occupe deux) ou **Double-time** (deux fois plus rapides) |
| **Switch at** | quand une partie demandée commence : **Next beat**, **Next bar** (par défaut) ou **Next 2 bars** (les phrases de deux mesures commencent sur les mesures impaires) |

Chaque style contient **28 rythmes** : 7 parties (**Intro, Verse** couplet, **Pre-chorus** pré-refrain, **Chorus** refrain, **Bridge** pont, **Fill** roulement, **Ending** fin) × 4 complexités (1 = simple, 4 = chargé). La grille du panneau a une ligne par partie et une colonne par complexité.

### 1. Écouter avant de choisir

**Avant que la piste batterie existe**, cliquez sur une case : le rythme joue en boucle avec le kit et le feel choisis, au tempo de la chanson (le transport reste arrêté). Cliquez d'autres cases, changez de style, de kit ou de feel pendant l'écoute ; **Stop** arrête l'écoute.

### 2. Créer la piste

**Create Drum Track** crée une piste MIDI *Drums* avec le kit et les 28 rythmes du style. Si le style est en 6/8 et que la chanson n'a qu'une mesure, la chanson passe en 6/8 (annulable). Le tempo conseillé du style est affiché ; le tempo de la chanson n'est pas modifié.

Pour changer de style ou de feel ensuite : choisissez-en un autre et cliquez **Load Style**.

### 3. Jouer en direct

- **Cliquez une case** pour démarrer ou changer de partie. Transport arrêté, un clic lance la lecture à la tête de lecture avec ce rythme.
- Pendant la lecture, le changement se fait au prochain point **Switch at**. La case en attente est éclairée, celle qui joue est rouge.
- **Keys** : activez-le pour jouer au clavier — <kbd>I</kbd> intro, <kbd>V</kbd> couplet, <kbd>P</kbd> pré-refrain, <kbd>C</kbd> refrain, <kbd>B</kbd> pont, <kbd>F</kbd> roulement, <kbd>E</kbd> fin ; <kbd>1</kbd>–<kbd>4</kbd> changent la complexité de la partie en cours. Tant que **Keys** est activé, ces touches pilotent la batterie au lieu de leurs raccourcis habituels ; désactivez-le (ou changez de page) pour les retrouver.
- Un **roulement** (Fill) joue une fois (une mesure ; deux en half-time), puis la partie précédente revient d'elle-même. La **fin** (Ending) joue une fois.
- **Stop** coupe la batterie immédiatement ; le transport continue.

### 4. Enregistrer, rejouer, ajuster

1. Activez **Record**, placez la tête de lecture au début et jouez la chanson en changeant de partie. Arrêtez le transport : chaque changement est devenu un **marqueur de cue** dans la règle *Location Markers*, sur le temps ou la mesure où il a eu lieu. **Record** se désactive à l'arrêt.
2. Rejouez depuis le début : la batterie refait vos changements.
3. Ajustez sans rejouer : **faites glisser** un marqueur pour déplacer un changement, ou supprimez-le. Pour refaire un passage, réactivez **Record** et rejouez par-dessus : seuls les marqueurs du passage joué sont remplacés.

**Play Cues** indique si la piste batterie suit les marqueurs.

### 5. Aplatir en MIDI

**Flatten** transforme les changements enregistrés en **régions MIDI** sur la piste batterie, nommées par exemple *Rock Verse 2*. Chaque région a sa propre copie des notes : supprimer une grosse caisse dans un couplet ne touche pas les autres. **Flatten** désactive aussi **Play Cues**, pour que les marqueurs ne rejouent pas la batterie une seconde fois. **Edit → Undo** retire les régions ; réactiver **Play Cues** fait rejouer les marqueurs.

Après l'aplatissement, la batterie se modifie comme n'importe quel MIDI : double-clic sur une région pour éditer les notes. Les exports l'incluent.

### Bon à savoir

- Les pistes batterie démarrent à −6 dB (les kits AVL sont forts sur les cymbales accentuées). La piste écoute **In** et **Disk** ensemble : elle joue les régions, les cues et un pad MIDI branché.
- Les changements sont déclenchés par la boucle d'affichage du programme : si le programme se fige plus d'un temps juste avant un changement (ouverture d'une fenêtre de plug-in), ce changement peut arriver un temps trop tard ; faites glisser son marqueur.
- Une chanson avec une piste batterie reste une session Ardour ordinaire, avec quelques réglages en plus qu'Ardour ignore.

## 13. Bass Studio : la basse

Bass Studio écrit une ligne de basse à partir de la grille d'accords que vous tapez. La ligne est une région MIDI ordinaire, modifiable note par note.

**Ouvrir :** **Window → Bass Studio** (page de la barre latérale, à côté de Drum Studio).

### 1. Taper les accords

Dans **Chords**, **un accord par mesure** : `C G Am F`. Les barres `|` et les retours à la ligne sont ignorés : écrivez comme sur papier.

| Écriture | Signification |
|---|---|
| `C G Am F` | quatre mesures, un accord chacune |
| `C,G` | deux accords dans la mesure (une demi-mesure chacun) |
| `%` | répète la mesure précédente |
| `N.C.` ou `x` | une mesure sans basse |
| `C/E` | accord de do, mi à la basse |
| `[Couplet]` | un commentaire (ignoré) |
| `| C | G | Am | F |` | même chose que `C G Am F` |

Noms d'accords reconnus : majeur `C`, mineur `Am` / `Amin` / `A-`, septième `D7`, septième majeure `Bbmaj7` / `BbM7`, mineur septième `Em7`, demi-diminué `F#m7b5` / `F#ø`, diminué `Bdim` / `Bdim7` / `B°`, augmenté `Gaug` / `G+`, suspendus `Esus4` / `Esus2` / `Esus`, et les extensions `6`, `9`, `11`, `13`, `add9`, `b9`, `#11`… Un accord inconnu est signalé avec le numéro de sa mesure.

### 2. Choisir le style et le son

| Réglage | Choix |
|---|---|
| **Start bar** | la mesure du premier accord (1 = début de la chanson ; choisissez la mesure après un décompte ou une intro) |
| **Style** | voir le tableau ci-dessous |
| **Complexity** | 1 (simple) à 4 (chargé) |
| **Feel** | **Normal**, **Half-time**, **Double-time**, comme Drum Studio |
| **Sound** | Acoustic, Finger (par défaut), Pick, Fretless, Slap 1 et 2, Synth 1 et 2, Acid, Techno Bass |
| **Follow the drum parts** | chaque mesure prend la complexité de la partie de batterie jouée à cet endroit (§14) |

| Style | Caractère |
|---|---|
| **Root notes** | fondamentales longues, puis fondamentale-quinte |
| **Rock / pop eighths** | croches régulières sur la fondamentale, avec quinte et octave aux complexités hautes |
| **Country / two-feel** | fondamentale-quinte, arpèges en complexité 3–4 |
| **Disco octaves** | fondamentale et octave en alternance |
| **Jazz walking** | ligne marchée : fondamentale, tierce, quinte, note d'approche chromatique vers l'accord suivant |
| **Reggae one-drop** | basse aérée, accent sur le temps fort |
| **Funk sixteenths** | doubles croches syncopées, octaves |
| **Ballad** | notes tenues, arpège léger |
| **Blues shuffle** | croches ternaires, motif boogie (1-3-5-6-♭7) |
| **Bossa nova** | fondamentale pointée, quinte |
| **Follow the kick drum** | une note sur chaque coup de grosse caisse de la piste Drum Studio (§14) |

Les notes se placent dans le registre de la basse (mi grave à ré♯ une octave au-dessus) et suivent chaque changement d'accord, même au milieu d'une mesure. Les **notes d'approche** (complexités hautes) amènent l'accord suivant par un demi-ton.

### 3. Écouter

**Listen** joue les 4 premières mesures en boucle (le transport reste arrêté). Changez style, complexité, feel ou son pendant l'écoute ; cliquez **Listen** à nouveau pour arrêter.

### 4. Créer la ligne

**Make Bass Line** place la ligne sur une piste **Bass** (créée la première fois), en une région MIDI *Bass line* qui commence à **Start bar**.
- Refaire **Make Bass Line** remplace cette région ; vos propres régions sur la piste basse restent.
- **Edit → Undo** ramène la ligne précédente.
- La grille d'accords et les réglages sont enregistrés avec la chanson.

### 5. Modifier

Double-clic sur la région pour déplacer, supprimer ou ajouter des notes, comme pour tout MIDI. Le son peut aussi être changé ensuite dans le Fluid Synth de la piste.

À savoir : la mesure (4/4, 3/4…) est celle de **Start bar**, pour toute la grille.

## 14. Batterie et basse ensemble

Bass Studio sait lire la piste Drum Studio :

- **Follow the drum parts** (coché par défaut) : chaque mesure de basse prend la complexité de la partie de batterie jouée à cet endroit, d'après les marqueurs de cue enregistrés (par exemple complexité 1 au couplet, 4 au refrain). Pendant un roulement, la basse garde la complexité précédente ; à la fin (Ending), elle joue simple.
- **Follow the kick drum** (style) : la basse joue une note sur chaque coup de grosse caisse. Les grosses caisses sont lues dans les régions de la piste batterie (performance aplatie) ou, à défaut, dans les rythmes que jouent ses marqueurs de cue. Aux complexités hautes, certaines notes passent à l'octave ou à la quinte, et la dernière amène l'accord suivant.

**Ordre conseillé :** posez la grille (§9), enregistrez la performance de batterie (§12), puis tapez les accords et faites la basse.

Le **Feel** se règle séparément pour la batterie et la basse : une basse en double tempo sur une batterie normale est possible.

## 15. Les sons de batterie et de basse

### Kits fournis

| Kit | Type | Lecteur |
|---|---|---|
| Black Pearl, Red Zeppelin, Blonde Bop, Buskman's Holiday | acoustiques échantillonnés (AVL) | plug-in x42-avldrums |
| Standard, Room, Power, Electronic, 808/909, Dance, Jazz, Brush | General MIDI (banque GeneralUser GS) | ACE Fluid Synth (inclus dans Ardour) |

Les **10 basses** de Bass Studio viennent de la même banque General MIDI.

Les rythmes de Drum Studio sont des notes de batterie **General MIDI** (grosse caisse 36, caisse claire 38, charleston 42/44/46, toms, cymbales) : n'importe quel instrument de batterie General MIDI les joue. Choisissez **Kit: No instrument** pour utiliser votre propre plug-in (MT Power DrumKit, par exemple), ajouté ensuite sur la piste.

### Kits DrumGizmo (téléchargement séparé)

Les kits DrumGizmo sont des batteries acoustiques très réalistes, enregistrées avec plusieurs micros (*DRSKit*, *CrocellKit*, *MuldjordKit*…). Ils sont lourds (de quelques centaines de Mo à quelques Go), donc absents du zip :

1. **Le plug-in DrumGizmo :** téléchargez-le sur **drumgizmo.org** (*Downloads*, version VST Windows 64 bits) et placez-le dans un dossier que MusicDAWM explore pour les VST (**Edit → Preferences → Plugins → VST** : ajoutez le dossier, puis **Scan for Plugins**).
2. **Un kit :** sur **drumgizmo.org → Kits**, téléchargez-en un et décompressez-le dans un endroit durable (par exemple `Documents\DrumGizmo\DRSKit`). Chaque dossier de kit contient un fichier de *kit* (`.xml`) et une ou plusieurs *tables MIDI* (`Midimap*.xml`).
3. **Dans Drum Studio :** **Kit: No instrument**, choisissez le style, **Create Drum Track**. Puis ajoutez DrumGizmo sur la piste batterie (dans la table de mixage, double-clic dans la zone vide des processeurs de la piste → DrumGizmo).
4. **Dans la fenêtre de DrumGizmo :** chargez le fichier de kit et la table MIDI General MIDI du kit (souvent *Midimap_full.xml*).
5. DrumGizmo garde le kit avec la chanson. Le premier chargement d'un gros kit prend du temps et de la mémoire.

À savoir :
- Les kits DrumGizmo sont sous licence **CC-BY 4.0** : une chanson publiée avec des crédits doit aussi créditer le kit (la page du kit indique comment). Les kits AVL et General MIDI fournis ne demandent aucun crédit.
- Si certains éléments sont muets ou faux, la table MIDI du kit n'utilise pas les numéros General MIDI pour eux : choisissez une autre table ou modifiez-la.
- MusicDAWM n'a pas encore été essayé avec DrumGizmo.

---

# Partie V — Retoucher

## 16. Melodyne et les plug-ins ARA (en attente)

> **En attente (octobre 2026).** Essayé avec Melodyne essential : le clip s'ouvre dans Melodyne et les notes supprimées sont muettes, mais **les changements de hauteur ne s'entendent pas** et Melodyne **plante souvent**. En attendant, utilisez Melodyne comme un plug-in normal sur la piste (mode Transfer).

ARA permet à un plug-in comme Melodyne de travailler directement sur un clip : il voit tout le clip, sans étape de « Transfer ».

**Ouvrir un clip dans Melodyne :**
1. Double-clic sur la région pour ouvrir son **éditeur de région**.
2. Dans la zone **Region Effects**, clic droit → **New Plugin** → Melodyne (VST3).
3. Double-clic sur Melodyne dans la zone pour ouvrir sa fenêtre : le clip est déjà analysé.

**Ce qui suit le clip :** le déplacer, le raccourcir ou le renommer garde les retouches ; le couper aussi (les deux morceaux les gardent) ; les copies partagent les retouches. Le gain de clip, son enveloppe et les fondus s'appliquent après Melodyne ; les changements de tempo atteignent Melodyne.

**Enregistrement :** les retouches sont enregistrées dans le dossier de la chanson, sous `plugins\ara-….dat`. Copiez le dossier entier pour les garder.

À savoir : ARA ne fonctionne qu'avec Melodyne en **Region Effect** ; Melodyne essential ne traite que le monophonique ; seul le format VST3 est géré.

---

# Partie VI — Mixer

## 17. La fenêtre Final Mix

**Window → Final Mix…** présente la chanson comme une table de mixage simple : une colonne par piste, regroupées par **rôle** — Drums, Bass, Guitars, Keys, Vocals, Backing vocals, Other. Le rôle est deviné d'après le nom de la piste (« Kick », « Bass DI », « Lead Vox »…) et se change dans le panneau FX de la piste.

### Une colonne, de haut en bas

| Élément | Rôle |
|---|---|
| **Nom** et **↺** | réinitialise la piste : pas d'effets, fader à 0 dB, pan au centre |
| **Pan** | gauche – centre – droite ; double-clic pour revenir au centre |
| **Fader** | le fader vertical et le vumètre de la table classique ; la case au-dessus affiche le niveau en dB et accepte une valeur tapée ; la case voisine affiche la crête (clic pour la remettre à zéro) |
| **FX** | ouvre le panneau d'effets de la piste (ci-dessous) |
| **M** / **S** | mute, solo |
| **⊖** | retire la piste de ce mix : sa colonne disparaît et la piste est muette (ni entendue, ni exportée) ; **+ Tracks** en bas la ramène, avec son état de mute d'avant ; annulable ; enregistré avec la chanson |

### Le panneau FX d'une piste

| Réglage | Effet |
|---|---|
| **High-pass** | retire le grave inutile (grondement, boue) ; la fréquence de départ dépend du rôle (100 Hz pour une voix, 35 Hz pour une basse) |
| **Brightness** | un léger relèvement (ou une baisse) des aigus au-dessus de 6 kHz environ |
| **Punch** | compression qui régularise le niveau en gardant les attaques |
| **Space** | quantité envoyée à la réverbération commune |
| **Role** | déplace la colonne dans un autre groupe |
| **Vocal preset** (voix uniquement) | voir ci-dessous |

### Les presets de voix

Pour une piste de rôle **Vocals** ou **Backing vocals**, le panneau FX propose un menu **Vocal preset** de 10 sons de voix tirés de mixages professionnels. Le projet de recherche DiffVox (Sony et Queen Mary University of London) a mesuré les effets de centaines de voix mixées ; MusicDAWM en a retenu 10 typiques, refaits avec ses propres effets (EQ, compresseur, quantité de réverbération ; pas de delay). Leurs noms décrivent le son : *Bright, present, smooth, lots of reverb*, *Warm, smooth, lots of reverb*, *Very punchy, some reverb*, *Natural, dry*…

- Choisir un preset règle l'EQ, la compression, la quantité de **Space** et le high-pass de la piste. **None** revient aux outils simples ; annulable.
- Les réglages du panneau partent ensuite du preset : **Brightness** ajoute aux aigus du preset (ou en retire) ; **Punch** à 50 % est la compression du preset (0 % aucune, 100 % le double) ; **Space** part de la réverbération du preset.
- La compression s'adapte au niveau de l'enregistrement : un preset sonne pareil sur une prise faible ou forte.
- La réverbération est la réverbération commune : un preset règle la quantité, pas la pièce.

### Le haut de la fenêtre

- **FX ON** coupe et rétablit tous ces effets d'un coup, pour comparer (les réglages sont gardés).
- **Reset settings** réinitialise toutes les pistes.
- **Open classic mixer** ouvre la table complète d'Ardour.
- ⏮ et ▶/■ : retour au début, lecture ou arrêt (<kbd>Espace</kbd> fonctionne aussi).

Tout ce que fait la fenêtre est fait de plug-ins et d'envois ordinaires, nommés *Final Mix EQ*, *Final Mix Punch*, le bus *Final Mix Space*, *Final Mix Match EQ* et *Final Mix Limiter* : la table classique les montre et ils s'y retouchent.

## 18. Le mix de départ

Choisissez le **Genre** (Pop, Rock, Acoustic / Folk, Electronic, Hip-hop, Metal, Jazz) et cliquez **Starting mix**. MusicDAWM mesure le volume de chaque enregistrement et règle :

- les **faders**, pour que chaque rôle soit au niveau typique du genre par rapport à la voix principale (par exemple en Rock, la batterie 2 dB et la basse 4 dB sous la voix ; deux guitares se partagent le niveau des guitares) ;
- le **pan** : batterie, basse et voix principale au centre ; les pistes dont le nom finit par « L » / « R » (ou contient « left » / « right ») de ce côté ; plusieurs guitares, claviers ou chœurs répartis à gauche et à droite ; une guitare seule légèrement décalée ;
- les **effets** : un high-pass sur chaque piste à la fréquence de son rôle, et des quantités de brillance, de punch et d'espace typiques du genre et du rôle.

C'est un **point de départ** à écouter et à ajuster. Réglez d'abord les rôles (panneau FX) et retirez les pistes témoins (⊖) avant de cliquer.

**Randomise FX** (en bas) essaie d'autres quantités d'effets autour de celles du genre, en gardant faders et pans. **Starting mix**, **Randomise FX** et **Reset settings** s'annulent chacun en une étape.

Les pistes sont mesurées telles qu'enregistrées (avec le gain de clip), avant leurs propres plug-ins. Les pistes MIDI (Drum Studio, Bass Studio) gardent leur fader et reçoivent seulement pan et effets.

## 19. Juger le mix : Avant, Après, Référence

Les boutons **Before / After / Ref** (en haut à droite) permettent de comparer **au même volume perçu**, car plus fort semble toujours « meilleur » :

| Bouton | Ce que vous entendez |
|---|---|
| **After** | le mix |
| **Before** | les pistes telles qu'enregistrées (faders à 0 dB, pan au centre, sans effets Final Mix), ramenées au volume du mix. Le mix est gardé et revient avec **After** ; pendant **Before**, les réglages du mix sont grisés. Fermer la fenêtre ou rouvrir la chanson revient toujours à **After**. |
| **Ref** | la chanson de référence seule, au volume du mix |

### La chanson de référence

**Reference…** choisit une chanson finie que vous aimez, dans le style visé (WAV, FLAC, AIFF ou Ogg ; MP3 et M4A si FFmpeg est installé, §23). Elle est importée en piste **Reference** qui va directement aux haut-parleurs, sans passer par le bus master : l'EQ et le limiteur du mix ne la colorent pas. Elle est muette sauf en **Ref** et n'est jamais exportée. Une nouvelle référence remplace l'ancienne.

### Match to reference

**Match to reference** (par règles, rien n'est envoyé nulle part) analyse le mix (un export rapide et silencieux) et la référence — volume perçu, crêtes, équilibre de six bandes de fréquences (sub-basse, basse, bas-médium, médium, présence, air) et largeur stéréo — puis :
- règle la **cible de volume** sur celle de la référence (Custom) ;
- place une **EQ master** douce (*Final Mix Match EQ*, avant le limiteur) qui rapproche le mix un peu plus qu'à mi-chemin de l'équilibre de la référence, 4 dB au plus par bande ;
- dit ce qu'il a fait et ce qu'il reste à essayer à la main : panoramique plus large ou plus étroit, plus ou moins de Punch.

Écoutez avec **After** et **Ref**, ajustez, refaites le match si vous changez beaucoup. **Clear match** coupe l'EQ de match et rétablit la cible précédente. **FX ON** coupe aussi l'EQ de match.

## 20. Le master : volume perçu, cible, limiteur

La colonne **Master**, à droite :

- **Le volume perçu** de la chanson pendant la lecture, en **LUFS** (l'unité des plateformes de streaming) : le grand chiffre couvre toute la lecture depuis son début, **Now** le dernier instant, **Peak** la plus haute crête réelle (true peak). Chaque nouvelle lecture recommence la mesure (**Restart** le fait à la main).
- **Target** : **Streaming (−14 LUFS)**, **Loud (−9 LUFS)** ou **Custom**. La colonne indique l'écart avec la cible.
- **Limiter ceiling** : le changer place un limiteur en dernier sur le bus master (la première fois), qui maintient les crêtes réelles sous ce niveau. −1 dBTP est sûr pour le streaming et le MP3.
- Le fader et le vumètre master, comme dans la table classique.

## 21. Freeze et plug-ins fournis

### Freeze (geler une piste)

Melodyne, les simulateurs d'ampli ou les grosses réverbérations peuvent faire peiner l'ordinateur. Dans la table de mixage (**Mix**), cliquez **Freeze** en bas de la tranche de la piste : la piste est calculée une fois avec ses plug-ins, puis joue cette copie, presque sans consommer de processeur. Le bouton affiche **Frozen**. Recliquez pour **dégeler** : l'enregistrement et les plug-ins d'origine reviennent, intacts.

- Une piste gelée ne peut pas être modifiée (plug-ins, régions).
- Le gel calcule toute la chanson (du début au marqueur de fin).
- Le bouton n'existe que sur les pistes audio ; il peut être masqué dans **Edit → Preferences → Appearance → Mixer**.

### Plug-ins fournis

En plus des plug-ins ACE d'Ardour (compresseur, delay, EQ, expandeur, réverbération, lecteur Fluid Synth, Reasonable Synth) :

| Plug-in | Usage |
|---|---|
| **x42-dpl Digital Peak Limiter** (mono et stéréo) | maintient les crêtes sous un plafond (par exemple −1 dB) pour monter le volume sans saturer ; à placer en dernier sur le master |
| **x42-fil4 Parametric Equalizer** (mono et stéréo) | filtre passe-haut, plateaux et quatre bandes |
| **x42-avldrums** | les kits de batterie AVL de Drum Studio |

Ils apparaissent avec les commandes génériques de MusicDAWM (curseurs), sans leurs fenêtres graphiques propres.

---

# Partie VII — Finaliser

## 22. Finalise Mix : le fichier prêt à publier

**Finalise Mix…** (en bas à droite de la fenêtre Final Mix) exporte la chanson, du début au marqueur de fin :
- en **WAV 24 bits**, et en **MP3** si FFmpeg est installé (§23) ;
- **exactement au volume de la cible** (**Target**), avec un limiteur de crêtes réelles au **Limiter ceiling** : le fichier est prêt à être mis en ligne.

La fenêtre choisit le dossier et le nom (par défaut le dossier *export* de la chanson). À la fin, elle affiche le volume et la crête mesurés, et peut ouvrir le dossier. Un résultat qui manque la cible de plus de 1,5 LU ou le plafond de plus de 0,5 dB est signalé comme un échec, jamais comme une chanson terminée.

Pour d'autres formats ou réglages, l'export d'Ardour reste disponible (**Session → Export**).

---

# Partie VIII — Référence

## 23. Dépannage

| Problème | Que faire |
|---|---|
| « Windows a protégé votre ordinateur » | **Informations complémentaires → Exécuter quand même**, ou débloquez le zip et décompressez à nouveau (§1) |
| Le programme ne démarre pas du tout, sans message | Vérifiez Smart App Control (§1) |
| Pas de son | Dans **Audio/MIDI Setup**, essayez le pilote **MME** au lieu de WASAPI, et vérifiez que la fréquence correspond au réglage de la carte son dans les paramètres Son de Windows |
| Textes ou boutons illisibles ou vides | Lancez depuis une invite de commandes avec `set PANGOCAIRO_BACKEND=win32` puis `bin\MusicDAWM.exe`, et signalez laquelle des deux affichages est correcte |
| Pas d'option MP3 (Finalise Mix, export, référence) | Le MP3 demande le programme gratuit **FFmpeg**, non fourni. Téléchargez une version Windows (par exemple « release essentials » sur gyan.dev) et copiez `ffmpeg.exe` et `ffprobe.exe` (de son dossier `bin`) directement dans `C:\Program Files\ffmpeg\`, puis relancez MusicDAWM. Le WAV fonctionne toujours. |
| Un plug-in fait planter le programme au démarrage | **Safe Mode** : cochez « Safe Mode: Disable all Plugins » dans la fenêtre de session, ouvrez la chanson, puis retirez le plug-in |
| Une fenêtre « Support Ardour Development » après un export | C'est l'appel aux dons d'Ardour (MusicDAWM est construit sur son travail). « Never ever ask me about this again » pour ne plus la voir. Rien n'est envoyé. |
| Drum Studio : pas de kit General MIDI dans la liste | La banque GeneralUser GS ou le lecteur ACE Fluid Synth manque : vérifiez que le dossier du programme est complet (`share\…\soundfonts\GeneralUser-GS.sf2`) |
| Bass Studio : « The General MIDI soundfont or ACE Fluid Synth is missing » | Même chose que ci-dessus |
| La grille détectée est décalée ou au double tempo | §11 |

**Signaler un problème :** **Help → Report a Bug** ouvre la page des problèmes de MusicDAWM. Décrivez ce que vous avez fait, ce que vous attendiez et ce qui s'est passé. Ne signalez pas les problèmes de MusicDAWM au projet Ardour : ils peuvent venir de nos modifications.

## 24. Raccourcis clavier

| Touche | Où | Action |
|---|---|---|
| <kbd>Espace</kbd> | partout | lecture / arrêt |
| <kbd>Ctrl</kbd>+<kbd>Z</kbd> | partout | annuler (**Edit → Undo**) |
| <kbd>Maj</kbd>+<kbd>L</kbd> | éditeur | afficher / masquer la barre latérale (Drum Studio, Bass Studio…) |
| <kbd>T</kbd> | fenêtre Tap Tempo Map | taper un temps |
| <kbd>I</kbd> <kbd>V</kbd> <kbd>P</kbd> <kbd>C</kbd> <kbd>B</kbd> <kbd>F</kbd> <kbd>E</kbd> | Drum Studio, **Keys** activé | intro, couplet, pré-refrain, refrain, pont, roulement, fin |
| <kbd>1</kbd> – <kbd>4</kbd> | Drum Studio, **Keys** activé | complexité de la partie en cours |

Les autres raccourcis sont ceux d'Ardour (**Window → Keyboard Shortcuts**).

## 25. Où sont les fichiers

| Quoi | Où |
|---|---|
| **Le programme** | le dossier décompressé (`MusicDAWM-<version>-win64`) ; rien d'autre n'est installé |
| **Vos réglages** | `%LOCALAPPDATA%\MusicDAWM9` (par exemple `C:\Users\<vous>\AppData\Local\MusicDAWM9`), séparé du dossier `Ardour9` d'Ardour. Pour repartir des réglages par défaut : fermez le programme, renommez ou supprimez ce dossier. |
| **Vos chansons** | là où vous les créez (par défaut `Documents`) ; une chanson est un dossier contenant un fichier `.ardour` |
| **Fichiers temporaires de Drum Studio et Bass Studio** | le dossier de cache de MusicDAWM (rythmes convertis en half-time / double-time, lignes de basse générées) ; peut être vidé sans risque |
| **Retouches Melodyne** | dans le dossier de la chanson, `plugins\ara-….dat` |

Les chansons sont des sessions **Ardour 9** ordinaires : elles s'ouvrent dans Ardour 9.8, et les sessions Ardour 9.8 s'ouvrent dans MusicDAWM (sans les pages Drum Studio et Bass Studio dans Ardour).

## 26. Différences avec Ardour 9.8

| Domaine | MusicDAWM | Ardour 9.8 pour Windows |
|---|---|---|
| Nom, icône, réglages | MusicDAWM, icône et écran d'accueil propres, dossier `MusicDAWM9` | Ardour, `Ardour9` |
| Réseau | **Ne contacte jamais Internet de lui-même** (la vérification des nouveautés est retirée) ; les fonctions que vous lancez vous-même (recherche Freesound) vont en ligne | vérifie les nouveautés sur ardour.org |
| Rapports de plantage | aucun | possibles |
| Help → Report a Bug | page des problèmes de MusicDAWM | suivi d'Ardour |
| Distribution | dossier portable (zip) | installeur |
| Pilotes audio | WASAPI, MME, DirectSound, WDM-KS ; **pas d'ASIO** | idem, plus ASIO |
| Plug-ins | VST3, VST2, LV2 ; CLAP prévu | VST3, VST2, LV2 |
| Fourni en plus | kits AVL, banque General MIDI GeneralUser GS, limiteur x42-dpl, EQ x42-fil4 | d'autres plug-ins tiers |
| Ajouts | écran de démarrage, nouvelle chanson depuis WAV, présentation auteur-compositeur, Tap Tempo Map avec détection, Drum Studio, Bass Studio, Final Mix, imports DAWproject / Waveform / Cubase, ARA | — |
| Corrections | vumètres et vues d'ensemble justes sur les processeurs AVX-512 ; plantages corrigés (marqueurs de cue, suppression d'un plug-in pendant son chargement) | non corrigés en 9.8 |

## 27. Glossaire

| Terme | Signification |
|---|---|
| **Carte de tempo** (tempo map) | la suite des tempos et des mesures de la chanson ; elle place la grille de mesures |
| **Cue / marqueur de cue** | un marqueur sur la ligne de temps qui déclenche une case (un rythme) de Drum Studio |
| **Complexité** | de 1 (simple) à 4 (chargé), pour les rythmes et les lignes de basse |
| **Feel** | rythmes et lignes joués normalement, deux fois plus lents (half-time) ou deux fois plus vite (double-time) par rapport au tempo de la chanson |
| **Aplatir** (Flatten) | transformer une performance de Drum Studio en régions MIDI modifiables |
| **General MIDI** | une norme qui fixe les numéros d'instruments et de notes de batterie (grosse caisse 36, caisse claire 38…) |
| **Banque de sons / soundfont** | un fichier `.sf2` qui contient des instruments échantillonnés ; MusicDAWM fournit GeneralUser GS |
| **Région** | un morceau d'audio ou de MIDI sur une piste |
| **LUFS** | unité du volume perçu, utilisée par les plateformes de streaming (−14 LUFS est courant) |
| **True peak / dBTP** | la crête réelle du signal, y compris entre les échantillons ; à garder sous −1 dBTP |
| **Broadcast WAV / horodatage** | un WAV qui contient l'heure d'enregistrement de la prise |
| **ARA** | une liaison entre logiciel et plug-in qui permet à Melodyne de travailler directement sur un clip |
| **Freeze** | calculer une piste avec ses plug-ins pour l'alléger |
| **DAWproject** | format d'échange ouvert entre logiciels de musique |

## 28. Outils en ligne de commande

Le dossier `bin` contient aussi les outils en ligne de commande d'Ardour sous des noms MusicDAWM (`musicdawm9-new_session.exe`, `musicdawm9-new_empty_session.exe`, `musicdawm9-export.exe`, `musicdawm9-lua.exe`), qui fonctionnent comme dans Ardour, ainsi que les programmes de test de MusicDAWM (`musicdawm9-*_test.exe`, `musicdawm9-mother_smoke.exe`), destinés à la chaîne de construction et non à vos chansons. `musicdawm9-drum_studio_demo.exe <dossier> <style>` écrit une courte chanson de démonstration dans un style (par exemple `rock` ou `six_eight`).

## 29. Licences et crédits

- **MusicDAWM** est une version modifiée d'**Ardour**. Le code source est sous « GPL version 2 ou ultérieure ». Comme la version Windows inclut le SDK ARA (Apache-2.0), **le programme Windows est distribué sous GNU GPL version 3**.
- **Kits AVL** (x42-avldrums) : CC-BY-SA ; la musique faite avec ne demande pas de crédit.
- **GeneralUser GS** (S. Christian Collins) : utilisation libre, y compris dans un logiciel. L'auteur précise qu'il ne peut pas garantir l'origine de chaque échantillon.
- **x42-dpl, x42-fil4, x42-avldrums** (Robin Gareus) : GPL.
- **Presets de voix** : d'après les données du projet DiffVox (licence MIT).
- **Détection du tempo** : d'après RunBPM.

Les textes des licences sont dans le dossier `licences` du zip ; la liste complète des composants est dans `THIRD_PARTY.md`.
