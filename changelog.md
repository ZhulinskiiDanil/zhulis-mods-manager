# v1.3.4

 * Release notes in the game leave out their Install section: it tells how to download the release from GitHub, which the manager just did

# v1.3.3

 * The versions list has an info button on every release with its notes, to see what changed before installing or rolling back

# v1.3.2

 * A progress bar next to Downloading, the Update all button counts the updates, an Issues button in the versions list
 * Checks for new releases every 30 minutes while playing (a setting), with a notification for the ones that came out since
 * A build newer than the latest release (a local one) says so, instead of when that release came out
 * The versions list shows the installed version when it isn't one of the releases

# v1.3.1

 * After the restart that loads an update, a What's new popup shows the notes of what was installed
 * The versions list links to the mod's repo on GitHub
 * Download size of every release in the versions list
 * Mods waiting for something (an update, Enable, a restart) come first in the list, after the manager

# v1.3.0

 * Every mod in the list shows its logo and a short description
 * When the latest release came out ("2 days ago"), also in the versions list
 * A mod turned off in Geode shows as Disabled with an Enable button, instead of an update that can't load
 * Uninstall from the versions list (settings and saves stay), with a Restart button afterwards
 * Debug builds of the manager are built as RelWithDebInfo, a Debug build crashed with Geode on Windows

# v1.2.0

 * Nightly builds: the versions list offers the build of the latest commit that passed CI, from its "Build Output" artifact
 * With a nightly installed, newer nightlies are offered as updates until a newer release comes out
 * Nightly downloads are verified with the artifact's SHA-256 digest from GitHub
 * The versions list is available for mods without releases too

# v1.1.1

 * Fixed a crash when opening the Geode mods page

# v1.1.0

 * Checks that a release is compatible with your GD and Geode versions before installing it
 * Required dependencies that are missing get a prompt to install them from the Geode Index
 * Downloads are verified with the SHA-256 digest from GitHub
 * Only repos of ZhulinskiiDanil are trusted, even with a custom registry URL
 * Releases are cached for 15 minutes to save the GitHub rate limit, the refresh button skips the cache. Offline or rate limited, the last known releases are used
 * Download progress in percent
 * "Update all" button, and one restart prompt for the whole batch
 * The "GitHub vX" label in the mods list and the update button in mod popups now appear as soon as the check finishes
 * The changelog shows every release newer than the installed one
 * New versions list: install any release or roll back
 * Mods that are on the Geode Index with the same or a newer version are left to Geode
 * The mods page button is added when the scene opens instead of by polling

# v1.0.1

 * New logo
 * The manager now updates itself: it's always first in the list, and on startup it asks to update when a new version is out
 * Startup notifications wait for the main menu instead of getting lost on the loading screen
 * Community and issues links

# v1.0.0

 * Initial release: install and update Blitzkrieg and AskDash from GitHub releases
