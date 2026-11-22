# Changelog

## 2.0 (in development)

New features are being built on `main`. Fixes for 1.x go on the `v1` branch.

## 1.3

Safety and reliability fixes.

### New
- **Clipboard options** (File > Settings...): turn off copying a snippet when it's selected or clicked, and keep copied snippets out of clipboard history (for clipboard managers that support it, such as KDE's Klipper).

### Fixed
- Edits are no longer lost when a snippet can't be saved (for example, when the database's drive is unavailable). You're told what went wrong, the changes stay in the editor, and quitting asks before discarding them.
- If the database can't be opened at startup, you're told why instead of getting an app that silently can't save. If the app has to fall back to the default database, it says so.
- The database file is now only readable by your user account. Existing default databases are tightened on the next start, and moved or newly created databases are made private too.
- Opening a SQLite file that isn't a snippets database no longer adds a table to it. The file is left unchanged.
- Searching for `_` or `%` finds those characters, instead of treating them as wildcards.
- Snippet titles and file paths in dialogs are shown exactly as written, never as formatting.
- The application menu entry no longer offers to open text files (which it couldn't do).
- `install.sh` writes a working menu entry when the install path contains spaces or special characters.

## 1.2

### New
- **Settings** (File > Settings...): see where the database is stored, move it, open a different database, or create a new one. The choice is remembered.
- **Click to copy**: clicking in the content area copies the snippet to the clipboard, or just the selected text if you drag to select part of it.
- **Edit titles directly**: the title can be changed without clicking "Edit" first. Press Enter or pick another snippet to save.
- **Sorting**: order the list by title (A-Z / Z-A) or by date created (oldest / newest first). The choice is remembered.
- **Version shown everywhere**: in the window title, the status bar, Help > About, and `snippetmanager --version`.
- **Install script** (`install.sh`) for Linux, with uninstall support.
- **Ready-to-run AppImage** built with `package-appimage.sh`.

### Fixed
- Switching snippets with unsaved changes no longer clears the selection.
- The project now builds from a fresh clone (the application icons are included in the repository).
