# OPM-Gmod

> **⚠️ Projet à but strictement éducatif.**
> Ce dépôt existe pour l'étude du *reverse engineering*, des internals du moteur
> **Source**, du *hooking* et de la création d'interfaces **ImGui** en C++.
> Il n'est **pas** destiné à tricher sur des serveurs en ligne : cela enfreint
> les conditions d'utilisation de Garry's Mod et de Steam. À n'utiliser que sur
> votre propre installation, à vos risques.

## À propos

**OPM-Gmod** est un fork durci de
[GMod-SDK](https://github.com/Gaztoof/GMod-SDK), un module interne pour
[Garry's Mod](https://store.steampowered.com/app/4000/Garrys_Mod/) construit à
partir d'interfaces obtenues en *reversant* le jeu.

L'objectif ici est avant tout pédagogique : servir de support pour comprendre
comment

- retrouver et appeler les interfaces du moteur Source (tier0/tier1, engine,
  vgui, mathlib…) ;
- poser des *hooks* sur les fonctions clés du client (`CreateMove`,
  `FrameStageNotify`, `Paint`, `RenderView`…) ;
- construire un *overlay* avec ImGui sur un backend DirectX 9 ;
- écrire du C++ défensif (validation des entrées, sécurité mémoire, RAII)
  au-dessus d'une base issue du reverse engineering.

```
cd tests && make test   # 67 cases, no Windows or DirectX needed
make asan               # the same suite under ASan + UBSan
```

The logic worth testing lives in `GMod-SDK/core/` — deliberately free of
Windows, DirectX and SDK dependencies so it can be compiled, sanitised and run
anywhere.  Moving more of the module in there is how coverage grows.

CI builds Debug and Release for Win32 and x64, runs the tests on gcc and clang
with warnings as errors, and runs clang-tidy. See `.github/workflows/ci.yml`.

Par rapport à la base d'origine, ce fork ajoute une passe de durcissement
(sécurité mémoire, chaînes de format, validation d'état, propriété des
ressources), une suite de tests hôte et de l'intégration continue. Le détail
des changements est consigné dans **[REVIEW.md](REVIEW.md)**.

## Contenu

- SDK d'interfaces Source *reversées* (x86 et x64).
- *Hooks* du client et de l'overlay de rendu.
- Interface ImGui in-game (s'ouvre avec la touche **INSER / INSERT**).
- Système de notifications *toast* portable et testé
  (`GMod-SDK/ImGui/imgui-notify/`).
- Helpers mathématiques d'angles portables et testés (`GMod-SDK/mathlib/`).
- Tests hôte (gcc / clang) et CI (MSBuild Win32/x64, ASan/UBSan, clang-tidy).

## Aperçu

![](https://i.imgur.com/TecyXLF.png)
![](https://i.imgur.com/So6vWVn.png)
![](https://i.imgur.com/85YRzrO.png)

## Compilation

Ouvrez `GMod-SDK.sln` dans Visual Studio et compilez en **Release x86** ou
**Release x64** (Debug fonctionne aussi). La build a besoin du DirectX SDK de
juin 2010 (D3DX9) ; la méthode utilisée en CI (paquet NuGet
`Microsoft.DXSDK.D3DX`) est décrite dans `.github/workflows/ci.yml`.

Pour charger le module : injectez la `.dll` compilée dans le processus de
Garry's Mod avec l'injecteur de votre choix, puis appuyez sur **INSER** pour
ouvrir le menu.

### Tests portables (ni Windows ni DirectX requis)

Les parties indépendantes de la plateforme (notifications, maths d'angles) se
testent sur n'importe quelle toolchain :

```
cd tests && make test   # 24 cas, sans Windows ni DirectX
make asan               # la même suite sous ASan + UBSan
```

## Structure

| Dossier | Rôle |
| --- | --- |
| `GMod-SDK/tier0`, `tier1`, `mathlib`, `engine`, `vgui`, `client` | Interfaces et types du moteur Source (reversés) |
| `GMod-SDK/hooks` | Hooks du client et du rendu |
| `GMod-SDK/hacks` | Fonctionnalités et menu |
| `GMod-SDK/ImGui` | ImGui, backend DX9/Win32 et notifications |
| `tests` | Tests hôte des composants portables |

## Licence

Distribué sous licence **MIT** — voir [LICENSE](LICENSE).
Copyright (c) 2021 Gaztoof.

## Remerciements

Merci à **[Gaztoof](https://github.com/Gaztoof/GMod-SDK)** pour le projet de
base, dont ce dépôt est un fork.

Autres briques utilisées :

- [Dear ImGui](https://github.com/ocornut/imgui) — base de l'interface.
</content>
</invoke>
