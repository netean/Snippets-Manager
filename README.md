# Snippet Manager

**Current release: 1.3** — see [CHANGELOG.md](CHANGELOG.md) for what's new. `main` is 2.0, in development.

## Versions

- **1.x (stable)**: the simple version. It lives on the [`v1` branch](../../tree/v1) and gets bug and security fixes only. Download it from [Releases](../../releases).
- **2.0 (in development)**: what this README describes, on `main`. 2.0 upgrades 1.x databases when it first opens them (keeping a backup copy), so you can move up without losing your snippets. Until 2.0 is released, use a release or the `v1` branch if you just want a working app.

A very simple, very basic, KDE desktop application for storing and managing text snippets, code snippets, and short phrases with full Unicode support.

I couldn't find anything that suited my needs: just a dead simple, very basic snippets tools for storing string. So I built one.. actually Claude did most of the coding, I just "directed it".

It's about 5000 lines of C++ and runs as a native QT application (supports themes)

Download and use the install script to compile it yourself, or use the AppImage binary release

Used and tested on Fedora and CachyOs




## Features

- **Add, Edit, Delete**: Full CRUD operations for text snippets, with a trash and undo
- **Search**: Fast, ranked search through titles, tags and content (matches part of a word)
- **Tags**: Group snippets with tags and filter the list by tag
- **Plain text, rich text or code**: A small formatting toolbar for rich text, and syntax highlighting for code (both can be turned off)
- **Copy to Clipboard**: Select a snippet to copy it, with placeholders like `{{date}}` filled in
- **Quick search**: A popup from the tray or a global keyboard shortcut, to find and copy a snippet from any app
- **Import and export**: JSON, Markdown and CSV
- **Unicode Support**: Store and display any Unicode characters
- **SQLite Backend**: Self-contained database storage, private to your user account
- **Native KDE Integration**: Built with Qt, and KDE Frameworks when available

## Installing (Linux)

The easiest way is the install script, which builds the app and adds it to your application menu:

```bash
./install.sh --install-deps   # first time: also installs the build dependencies (apt, dnf, pacman or zypper)
./install.sh                  # build and install for the current user (into ~/.local, no sudo)
./install.sh --system         # or install for all users (into /usr/local, uses sudo)
./install.sh --uninstall      # remove it again (add --system if you installed with it)
```

Run `./install.sh --help` for all options. Uninstalling never touches your snippets database.

## Ready-to-run AppImage

`package-appimage.sh` builds a single self-contained file, with Qt bundled, that runs on other Linux machines without installing anything:

```bash
./package-appimage.sh
chmod +x SnippetManager-2.0-x86_64.AppImage
./SnippetManager-2.0-x86_64.AppImage
```

It builds inside an Ubuntu 24.04 container (it needs podman or docker), so the AppImage runs on Ubuntu 24.04 and newer, Debian 13, Fedora 40+ and current rolling distros, whatever you build it on. `./package-appimage.sh --native` builds with your own Qt instead, but the result then only runs on distros at least as new as yours. The AppImage doesn't include syntax highlighting (Ubuntu 24.04 doesn't have the library); builds from source do.

Running an AppImage needs FUSE 2 (`libfuse2`, `fuse2` or `fuse` depending on the distro); without it, run it with `--appimage-extract-and-run`.

 -- I do not like Flatpaks so I will never be making a Flatpak for  this 

## Building manually

### Prerequisites

- CMake 3.19+
- Qt 6.4+ (Core, Gui, Widgets, Sql with the SQLite driver, Network, and DBus on Linux)
- C++17 compiler
- Optional: KDE's KSyntaxHighlighting (KF6) for syntax highlighting

### Ubuntu/Debian
```bash
sudo apt install cmake build-essential qt6-base-dev libqt6sql6-sqlite
sudo apt install libkf6syntaxhighlighting-dev   # optional; Debian 13 / Ubuntu 25.04 and newer
```

### Fedora
```bash
sudo dnf install cmake gcc-c++ qt6-qtbase-devel kf6-syntax-highlighting-devel
```

### Build Steps
```bash
cmake -S ./ -B ./build
cmake --build ./build
ctest --test-dir ./build     # run the tests
```

### Run
```bash
./build/snippetmanager
```

Or just download the binary release from the releases page.

## Usage

1. **Adding Snippets**: Click "Add" (Ctrl+N) to create a new snippet
2. **Renaming and tagging**: The title and tags are always editable. Type a new title or some comma-separated tags and press Enter (or select another snippet) to save
3. **Editing Content**: Select a snippet, click "Edit" (Ctrl+E), make changes and click "Save" (Ctrl+S)
4. **Type**: While editing, pick Plain text, Rich text (with a formatting toolbar: bold, italic, strikethrough, code and lists) or a programming language (highlighted, in a fixed-width font)
5. **Searching**: Type in the search box (Ctrl+F) to find snippets by title, tags or content. Results are ranked by how well they match. Escape clears the search, and the Down arrow moves into the list
6. **Filtering**: Use "Show" to list all snippets, only untagged ones, only those with a tag, or the trash
7. **Sorting**: Use the "Sort" drop-down to order snippets by title (A-Z / Z-A) or by date created (oldest / newest first)
8. **Copying**: Selecting a snippet copies its content to the clipboard (rich text is copied with its formatting). Clicking anywhere in the content area copies it again (drag to select part of the text to copy just that part). Ctrl+Shift+C or the "Copy" button copies too
9. **Placeholders**: `{{date}}`, `{{time}}`, `{{datetime}}`, `{{clipboard}}` and `{{uuid}}` are filled in when a snippet is copied. Dates and times can take a format, like `{{date:dd/MM/yyyy}}`. Put a backslash before one to copy it as written: `\{{date}}`
10. **Details**: Right-click a snippet and choose "Details..." (or press Alt+Enter) to see when it was created and last changed
11. **Deleting**: Delete (or the Delete key in the list) moves a snippet to the trash. Ctrl+Z brings back the last one deleted. In "Show: Trash" you can restore snippets or delete them forever
12. **Quick search**: From the tray icon (middle-click), the global shortcut or `snippetmanager --popup`, a small window opens: type to find a snippet, press Enter to copy it, Ctrl+Enter to open it in the main window, Escape to close
13. **Import and export**: File > Import... reads JSON, Markdown and CSV files (CSV files from other apps work too: the columns are worked out from the header). File > Export... writes all your snippets, or just those shown, as JSON (best for backups), Markdown or CSV
14. **Settings**: File > Settings... has four tabs:
    - **Editor**: turn rich text editing or syntax highlighting off
    - **Clipboard**: copying on select, keeping copied snippets out of clipboard history (for clipboard managers that support it, such as KDE's Klipper), and placeholders
    - **Quick Access**: the tray icon, starting at login, and the global shortcut
    - **Database**: where the database is stored. Move it, open a different database, or create a new one

### The global shortcut

Turn on "Open the quick search with a keyboard shortcut" in Settings > Quick Access. Your desktop asks you to confirm the keys (Meta+Alt+V is suggested), and you can change them later in the desktop's shortcut settings. This uses the desktop's global shortcuts portal, which KDE Plasma 6, GNOME 48+ and Hyprland have. The app needs to be installed with `install.sh` (or have its menu entry added) for the desktop to recognise it.

On other desktops, or with the AppImage, add a custom shortcut in your desktop's keyboard settings that runs `snippetmanager --popup` (Settings > Quick Access shows the exact command). It opens the quick search in the copy that's already running.

## Data Storage

By default, snippets are stored in a SQLite database located at:
`~/.local/share/SnippetManager/Snippet Manager/snippets.db`

You can move it, or switch to another database file, from File > Settings.... The chosen location is remembered between runs.

The database file is only readable by your user account, because snippets often hold things like tokens and passwords. Snippet Manager won't open, or write to, a SQLite file that isn't a snippets database. Exported files are private to your account too.

When 2.0 first opens a 1.x database, it upgrades it and saves a copy of the original next to it (for example `snippets-v0-backup.db`). 1.3 can still open an upgraded database, but it doesn't show tags or formatting, and it shows snippets that are in the trash.


## Screenshots

<img width="820" height="640" alt="image" src="https://github.com/user-attachments/assets/4b06dcdc-bbd8-41fc-a6dc-19217ebd7ccd" />
<img width="1024" height="700" alt="image" src="https://github.com/user-attachments/assets/cd1bb4b1-20dc-45ce-a06e-62f6ccc0c3dc" />
<img width="820" height="640" alt="image" src="https://github.com/user-attachments/assets/d165547e-aad1-4add-91f3-fe5180ad5d4c" />


