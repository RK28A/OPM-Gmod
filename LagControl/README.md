# LagControl

Application Windows en **C++** (interface Dear ImGui) qui **ajoute du lag réseau
à une seule application** de votre choix. Toutes les autres (navigateur,
Discord, Steam…) gardent une connexion normale.

> À utiliser pour tester le comportement d'un programme ou d'un jeu en mauvaise
> connexion, sur vos propres serveurs / parties privées. Utiliser un « lag
> switch » sur des serveurs publics enfreint les conditions d'utilisation de la
> plupart des jeux en ligne et peut entraîner un bannissement.

## Compiler le .exe — un double-clic

Double-cliquez sur **`build.bat`**. Il :

1. trouve Visual Studio ou les *Build Tools* (et propose de les installer
   automatiquement avec `winget` s'ils manquent) ;
2. télécharge WinDivert 2.2.2 une seule fois (empreinte SHA-256 vérifiée) ;
3. compile, puis ouvre le dossier **`dist\`** :

```
dist\LagControl.exe
dist\WinDivert.dll
dist\WinDivert64.sys
```

Gardez ces trois fichiers ensemble et lancez `LagControl.exe` (Windows demande
les droits administrateur : obligatoire pour intercepter le réseau).

Sans rien installer : l'onglet **Actions** du dépôt GitHub compile aussi le
programme à chaque push — téléchargez l'artefact **LagControl-windows-x64** du
dernier run du workflow **LagControl**.

## Fonctions

| | |
| --- | --- |
| **Choix de l'application** | Liste des applications ouvertes avec recherche, filtre « connectées », bouton *Parcourir .exe*, ou nom tapé dans la recherche (« Cibler quand même »). La cible est suivie par son nom : si vous relancez le jeu, il reste ciblé. |
| **Choix du lag** | Latence de 0 à 10 000 ms (curseur + champ + préréglages), variation aléatoire (±), perte de paquets en %, sens du trafic (entrant, sortant, les deux). Modifiable en direct. |
| **Touche activer / désactiver** | Raccourci global (marche même quand le jeu est au premier plan), **F8** par défaut. Cliquez sur la touche affichée pour la changer. Mode *Basculer* (un appui = on/off) ou *Maintenir* (lag tant que la touche est enfoncée). |
| **Arrêt d'urgence** | Gros bouton rouge dans l'app **et** raccourci global (**Ctrl+F12** par défaut) : coupe tout immédiatement, relâche les paquets en attente et rend le réseau normal. |
| **Sécurités** | Désactivation automatique après X secondes, bips de confirmation, fenêtre « toujours au premier plan ». Fermer l'app (ou un plantage) rétablit aussi le réseau : Windows ferme le pilote tout seul. |
| **Statistiques** | Paquets retardés, perdus, en attente, connexions suivies. |

Les réglages sont enregistrés dans `%APPDATA%\LagControl\config.ini`.

## Comment ça marche

- Le pilote [WinDivert](https://reqrypt.org/windivert.html) intercepte les
  paquets TCP/UDP de la machine.
- En parallèle, LagControl relève plusieurs fois par seconde les ports réseau
  ouverts par l'application ciblée (`GetExtendedTcpTable` / `GetExtendedUdpTable`).
- Un paquet dont le port local appartient à la cible part dans une file
  d'attente et ressort après le délai choisi ; tous les autres sont réinjectés
  immédiatement. L'ordre des paquets est conservé même avec la variation.

À savoir :

- une connexion toute neuve peut passer sans lag pendant ~0,3 s, le temps
  d'être repérée ;
- avec « Les deux », le ping augmente d'environ **2 × la latence** (aller + retour) ;
- certains anti-triches bloquent les pilotes de capture réseau.

## Structure

| Fichier | Rôle |
| --- | --- |
| `build.bat` | Compilation en un clic (MSVC x64) |
| `src/main.cpp` | Fenêtre Win32 + Direct3D 11 + boucle ImGui |
| `src/app.cpp` | Interface et logique (activation, raccourcis, urgence) |
| `src/ui.cpp` | Thème et widgets dessinés à la main |
| `src/engine.cpp` | Capture WinDivert, file de retard, arrêt |
| `src/processes.cpp` | Liste des applications et suivi de leurs ports |
| `src/hotkeys.cpp` | Noms des touches, enregistrement d'un raccourci |
| `src/settings.cpp` | Réglages persistants |
| `src/lag_core.h` | Logique portable (file, horloge, ports), testée par `tests/core_tests.cpp` |
| `third_party/imgui` | [Dear ImGui](https://github.com/ocornut/imgui) 1.91.9 (MIT) |

Tests de la partie portable, sur n'importe quel compilateur :

```
cd tests
g++ -std=c++17 -Wall -Wextra -I../src core_tests.cpp -o core_tests && ./core_tests
```
