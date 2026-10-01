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
