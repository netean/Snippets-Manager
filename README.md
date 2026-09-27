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
2. **Editing**: Select a snippet from the list and edit in the right panel
3. **Searching**: Type in the search box to filter snippets
4. **Copying**: Select a snippet and click "Copy" to copy content to clipboard
5. **Deleting**: Select a snippet and click "Delete" (with confirmation)

## Data Storage

By default, Snippets are stored in a SQLite database located at:
`~/.local/share/SnippetManager/snippets.db`

Change the location in the settings

The application itself is completely self-contained and portable (exluding the database)


## Screenshots

<img width="820" height="640" alt="image" src="https://github.com/user-attachments/assets/4b06dcdc-bbd8-41fc-a6dc-19217ebd7ccd" />
<img width="1024" height="700" alt="image" src="https://github.com/user-attachments/assets/cd1bb4b1-20dc-45ce-a06e-62f6ccc0c3dc" />
<img width="820" height="640" alt="image" src="https://github.com/user-attachments/assets/d165547e-aad1-4add-91f3-fe5180ad5d4c" />


