# Zhulis Mods Manager

Install and update <cg>Zhulis</c> mods straight from their <cy>GitHub releases</c>, even before a new version is accepted on the Geode Index.

## Where to find it

Open <cg>Geode → Mods</c> and press the <cy>GitHub</c> button on the left side. A badge shows up on it when updates are available.

The list shows every Zhulis mod with:
- the installed and the latest version
- an <cg>Install</c> / <cg>Update</c> button
- the release changelog

Mods with a newer GitHub release also get a <cy>GitHub vX</c> label in the mods list and an update button in their info popup.

## Tracked mods

- <cj>Blitzkrieg</c>
- <cj>AskDash</c>
- <cj>Zhulis Mods Manager</c> itself, so it keeps itself up to date

New mods are added to the list online, no manager update needed.

## How it works

On startup the mod checks the GitHub releases of every tracked mod. Installing downloads the mod's <cy>.geode</c> file into your mods folder, then you <cr>restart the game</c> to load it.

If you already have a newer version (for example from the Geode Index), the manager leaves it alone.

## Settings

- **Check for updates on startup**: on by default
- **Include pre-releases**: offer GitHub pre-releases too
- **Registry URL**: where the list of tracked mods comes from. Only change it if you know what you're doing
