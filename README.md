# Snippet Manager

**Current version: 1.2** — see [CHANGELOG.md](CHANGELOG.md) for what's new.

A very simple, very basic, KDE desktop application for storing and managing text snippets, code snippets, and short phrases with full Unicode support.

I couldn't find anything that suited my needs: just a dead simple, very basic string snippets tools. So I built one.. actually Claude Sonnet did most of the coding, I just "supervised"



## Features

- **Add, Edit, Delete**: Full CRUD operations for text snippets
- **Search**: Real-time search through titles and content
- **Unicode Support**: Store and display any Unicode characters
- **Copy to Clipboard**: Easy copying of snippets to other applications
- **SQLite Backend**: Self-contained database storage
- **Native KDE Integration**: Built with Qt and KDE Frameworks

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
chmod +x SnippetManager-1.2-x86_64.AppImage
./SnippetManager-1.2-x86_64.AppImage
```

The AppImage needs a glibc at least as new as the machine that built it, so build it on the oldest distro you want to support. Running an AppImage needs FUSE 2 (`libfuse2`, `fuse2` or `fuse` depending on the distro); without it, run it with `--appimage-extract-and-run`.

## Building manually

### Prerequisites

- CMake 3.16+
- Qt6 (Core, Widgets, Sql with the SQLite driver)
- C++17 compiler

### Ubuntu/Debian
```bash
sudo apt install cmake build-essential qt6-base-dev libqt6sql6-sqlite
```

### Build Steps
```bash
cmake -S ./ -B ./build
cmake --build ./build
```

### Run
```bash
./build/snippetmanager
```

Or just download the binary release from the releases page.

## Usage

1. **Adding Snippets**: Click "Add" to create a new snippet
2. **Renaming**: The title field is always editable - type a new title and press Enter (or select another snippet) to save
3. **Editing Content**: Select a snippet, click "Edit", make changes and click "Save"
4. **Searching**: Type in the search box to filter snippets
5. **Sorting**: Use the "Sort" drop-down to order snippets by title (A-Z / Z-A) or by date created (oldest / newest first)
6. **Copying**: Selecting a snippet copies its content to the clipboard. Clicking anywhere in the content area copies it again (drag to select part of the text to copy just that part)
7. **Deleting**: Select a snippet and click "Delete" (with confirmation)
8. **Settings**: File > Settings... shows where the database is stored and lets you move it, open a different database, or create a new one

## Data Storage

By default, snippets are stored in a SQLite database located at:
`~/.local/share/SnippetManager/Snippet Manager/snippets.db`

You can move it, or switch to another database file, from File > Settings.... The chosen location is remembered between runs.

Change the location in the settings

The application itself is completely self-contained and portable (exluding the database)


## Screenshots

<img width="820" height="640" alt="image" src="https://github.com/user-attachments/assets/4b06dcdc-bbd8-41fc-a6dc-19217ebd7ccd" />
<img width="1024" height="700" alt="image" src="https://github.com/user-attachments/assets/cd1bb4b1-20dc-45ce-a06e-62f6ccc0c3dc" />
<img width="820" height="640" alt="image" src="https://github.com/user-attachments/assets/d165547e-aad1-4add-91f3-fe5180ad5d4c" />

## Version

The version number is set in one place, the `project(SnippetManager VERSION 1.2 ...)` line in `CMakeLists.txt`. The app (window title, status bar, Help > About and `snippetmanager --version`), the Windows and macOS metadata, `install.sh` and the AppImage file name all pick it up from there. To release a new version, change that line, add an entry to `CHANGELOG.md`, and rebuild.
