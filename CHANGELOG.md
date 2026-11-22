# Changelog

## 2.0 (in development)

New features are being built on `main`. Fixes for 1.x go on the `v1` branch.

### New
- **Folders**: right-click the list to add folders (they can be nested), then drag snippets into them or use Move to Folder. Deleting a folder moves what's in it up a level; snippets are never deleted with it. Exports and imports keep each snippet's folder.
- **Tags**: add comma-separated tags to snippets (existing tags are suggested), and filter the list by tag.
- **Snippet types**: plain text, rich text or a programming language.
- **Rich text editor**: a small formatting toolbar (bold, italic, strikethrough, code, lists). Rich text is stored as Markdown, and copied with its formatting. Settings > Editor can turn it off, making everything plain text.
- **Syntax highlighting** for code snippets, using KDE's KSyntaxHighlighting when it's installed. Can be turned off.
- **Better search**: results are ranked (title matches first, then tags, then content), and search covers tags.
- **Trash**: deleting moves a snippet to the trash, Ctrl+Z brings it back, and "Show: Trash" restores snippets or deletes them for good.
- **Placeholders**: `{{date}}`, `{{time}}`, `{{datetime}}`, `{{clipboard}}` and `{{uuid}}` are filled in when copying.
- **Import and export**: JSON, Markdown and CSV, including CSV files from other apps.
- **Quick search**: a popup to find and copy a snippet, from the new tray icon, a global keyboard shortcut (through the desktop's portal) or `snippetmanager --popup`.
- **Start at login** in the tray.
- **Keyboard shortcuts**: Ctrl+N, Ctrl+E, Ctrl+S, Ctrl+Shift+C, Ctrl+F, Delete, Ctrl+Z and Alt+Enter.
- **Snippet details**: created and modified dates, from the right-click menu or Alt+Enter.
- The window size and layout are remembered.

### Changed
- 1.x databases are upgraded when 2.0 first opens them, after saving a backup copy. Databases from newer versions are refused rather than changed.
- The AppImage is built in an Ubuntu 24.04 container with pinned, checksum-verified tools.
- The menu entry is installed as `io.github.netean.SnippetManager.desktop`, with a Quick Search action.
- Builds and tests run on GitHub Actions for Linux, Windows and macOS.

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
