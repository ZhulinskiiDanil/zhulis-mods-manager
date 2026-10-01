# Zhulis Mods Manager

A [Geode](https://geode-sdk.org) mod that installs and updates **Zhulis** mods straight from their GitHub releases, even before a new version is accepted on the Geode Index.

<img src="logo.png" width="128" alt="logo">

## Features

- A **GitHub** button on the Geode mods page (left side) opens the list of Zhulis mods with their installed and latest versions, Install / Update buttons and release changelogs. A badge shows up on it when updates are available
- Mods with a newer GitHub release get a **GitHub vX** label in the mods list and an update button in their info popup
- Checks for updates on startup and shows a notification
- Updates itself: the manager is always tracked and asks to update when a new version is out
- Never downgrades: if the installed version (for example from the Geode Index) is newer than the GitHub release, it's left alone

## Installation

Download `zhulis.mods-manager.geode` from the [latest release](https://github.com/ZhulinskiiDanil/zhulis-mods-manager/releases/latest) and put it into your Geode mods folder (or use **Install from file** on the Geode mods page), then restart the game.

## Tracked mods

The list lives in [`mods.json`](mods.json) and is fetched at runtime, so adding a mod doesn't need a new manager release:

```json
{
  "mods": [
    { "id": "zhulis.blitzkrieg", "name": "Blitzkrieg", "repo": "ZhulinskiiDanil/blitzkrieg" }
  ]
}
```

To add a mod:

1. Add an entry with the mod `id`, display `name` and GitHub `repo` (`owner/name`, must be public)
2. Publish releases in that repo with a tag the mod version can be parsed from (`v1.2.3`) and the asset named `<mod-id>.geode`

If the registry can't be fetched, a built-in copy from [`src/registry/index.cpp`](src/registry/index.cpp) is used.

## Settings

| Setting | Default | Description |
| --- | --- | --- |
| Check for updates on startup | on | Fetch the latest releases when the game starts |
| Include pre-releases | off | Offer GitHub pre-releases too |
| Registry URL | this repo's `mods.json` | Where the list of tracked mods comes from |

## Building

Requires the [Geode SDK](https://docs.geode-sdk.org/getting-started/) and Geode CLI.

```sh
geode build --ninja
```

On Windows build with clang (as `geode build --ninja` does), MSVC 19.50 crashes with an internal compiler error on Geode's async tasks.

## Releasing

1. Bump `version` in `mod.json` and `CMakeLists.txt`, add a section to `changelog.md`
2. Push, wait for the **Build Geode Mod** workflow to pass on all platforms
3. Create a GitHub release with the tag `vX.Y.Z` and attach `zhulis.mods-manager.geode` from the workflow's **Build Output** artifact

## Project layout

```
src/
  main.cpp                  startup update check and notifications
  registry/                 tracked mods list (remote + built-in)
  github/                   GitHub releases API and downloads
  manager/                  mod states, install/update logic
  hooks/ModsLayer.cpp       Geode mods page button and UI events
  popups/ManagerPopup/      the mods list popup
mods.json                   tracked mods registry
```
