# Changelog

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
