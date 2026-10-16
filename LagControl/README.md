# LagControl

Application Windows avec interface graphique qui **ajoute du lag réseau à une
seule application** de votre choix. Toutes les autres (navigateur, Discord,
Steam…) gardent une connexion normale.

> À utiliser pour tester le comportement d'un programme ou d'un jeu en
> mauvaise connexion, sur vos propres serveurs / parties privées. Utiliser un
> « lag switch » sur des serveurs publics enfreint les conditions d'utilisation
> de la plupart des jeux en ligne et peut entraîner un bannissement.

## Fonctions

| | |
| --- | --- |
| **Choix de l'application** | Liste des applications ouvertes avec recherche, filtre « connectées à Internet », bouton *Parcourir .exe*, ou nom tapé à la main. La cible est suivie par son nom : si vous relancez le jeu, il reste ciblé. |
| **Choix du lag** | Latence de 0 à 10 000 ms (curseur + champ + préréglages), variation aléatoire (±), perte de paquets en %, sens du trafic (entrant, sortant ou les deux). Modifiable en direct pendant que le lag est actif. |
| **Touche activer / désactiver** | Raccourci global (marche même quand le jeu est au premier plan), **F8** par défaut. Cliquez sur la touche affichée pour la changer. Mode *Basculer* (un appui = on/off) ou *Maintenir* (lag tant que la touche est enfoncée). |
| **Arrêt d'urgence** | Gros bouton rouge dans l'app **et** raccourci global (**Ctrl+F12** par défaut) : coupe tout immédiatement, relâche les paquets en attente et rend le réseau normal. |
| **Sécurités** | Désactivation automatique après X secondes (optionnel), bips sonores de confirmation, fenêtre « toujours au premier plan ». Fermer l'app (ou un plantage) rétablit aussi le réseau : Windows ferme le pilote tout seul. |
| **Statistiques** | Paquets retardés, perdus, en attente, connexions suivies. |

Les réglages sont sauvegardés dans `%APPDATA%\LagControl\config.json`
(journal : `lagcontrol.log` dans le même dossier).

## Installation

### Option 1 — l'exécutable (le plus simple)

Dans l'onglet **Actions** du dépôt GitHub, ouvrez le dernier run du workflow
**LagControl** et téléchargez l'artefact **LagControl-windows** : c'est un
`LagControl.exe` prêt à lancer. Vous pouvez aussi le fabriquer vous-même avec
`build_exe.bat` (résultat dans `dist\`).

### Option 2 — avec Python

1. Installez [Python 3.10+](https://www.python.org/downloads/) en cochant
   **« Add python.exe to PATH »**.
2. Double-cliquez sur **`lancer.bat`** : il installe les dépendances au premier
   lancement puis démarre l'application.

Dans les deux cas Windows demande les **droits administrateur** (fenêtre UAC) :
c'est obligatoire pour intercepter le trafic réseau et pour les raccourcis
globaux.

## Utilisation

1. **Application cible** : cliquez sur l'application dans la liste.
2. **Réglages du lag** : choisissez la latence (l'estimation du ping ajouté
   s'affiche en haut à droite de la carte).
3. Appuyez sur **F8** (ou sur le bouton vert) pour activer, à nouveau pour
   désactiver.
4. En cas de problème : **⛔ ARRÊT D'URGENCE** ou **Ctrl+F12**.

## Comment ça marche

LagControl utilise le pilote [WinDivert](https://reqrypt.org/windivert.html)
(via [pydivert](https://github.com/ffalcinelli/pydivert)) pour intercepter les
paquets TCP/UDP. En parallèle il relève, plusieurs fois par seconde, les ports
réseau ouverts par l'application ciblée (`psutil`). Un paquet dont le port local
appartient à la cible est mis en file d'attente puis réinjecté après le délai
choisi ; tous les autres paquets sont réinjectés immédiatement. L'ordre des
paquets est conservé même avec la variation activée.

À savoir :

- une connexion toute neuve peut passer sans lag pendant ~0,3 s, le temps
  d'être repérée ;
- avec « Les deux », le ping augmente d'environ **2 × la latence** (aller + retour) ;
- certains anti-triches bloquent les pilotes de capture réseau.

## Structure

| Fichier | Rôle |
| --- | --- |
| `main.py` | Point d'entrée, élévation administrateur, journal |
| `lagcontrol/gui.py` | Interface (customtkinter) |
| `lagcontrol/engine.py` | Capture WinDivert, file de retard, arrêt d'urgence |
| `lagcontrol/processes.py` | Liste des applications et suivi de leurs ports |
| `lagcontrol/hotkeys.py` | Raccourcis clavier globaux |
| `lagcontrol/config.py` | Réglages persistants |
| `tests/` | Tests du moteur (sans Windows) : `python -m unittest discover tests` |
