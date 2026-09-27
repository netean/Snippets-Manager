# Snippet Manager

A very simple, very basic, KDE desktop application for storing and managing text snippets, code snippets, and short phrases with full Unicode support.

I couldn't find anything that suited my needs: just a dead simple, very basic string snippets tools. So I built one.. actually Claude Sonnet did most of the coding, I just "supervised"



## Features

- **Add, Edit, Delete**: Full CRUD operations for text snippets
- **Search**: Real-time search through titles and content
- **Unicode Support**: Store and display any Unicode characters
- **Copy to Clipboard**: Easy copying of snippets to other applications
- **SQLite Backend**: Self-contained database storage
- **Native KDE Integration**: Built with Qt and KDE Frameworks

## Building

### Prerequisites

- CMake 3.16+
- Qt6 (Core, Widgets, Sql)
- KDE Frameworks 6 (CoreAddons, I18n, XmlGui, ConfigWidgets)
- C++17 compiler

### Ubuntu/Debian
```bash
sudo apt install cmake build-essential qt6-base-dev qt6-sql-sqlite \
    libkf6coreaddons-dev libkf6i18n-dev libkf6xmlgui-dev libkf6configwidgets-dev
```

### Build Steps
```bash
mkdir build
cmake -S ./ -B ./build
cd build && make

### Run
```bash
./snippetmanager
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
`~/.local/share/SnippetManager/snippets.db`

You can move it, or switch to another database file, from File > Settings.... The chosen location is remembered between runs.

The application is completely self-contained and portable.
