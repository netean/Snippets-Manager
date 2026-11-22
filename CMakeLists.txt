cmake_minimum_required(VERSION 3.19)
# The application version. This is the only place it's set: the app, the
# Windows and macOS metadata and the AppImage file name all read it from here.
project(SnippetManager VERSION 2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Qt 6.4 is the oldest supported version (Ubuntu 24.04, which the AppImage is built on)
find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Widgets Sql Network)
qt_standard_project_setup()
# qt_standard_project_setup() turns on moc and uic, but not rcc, which
# compiles the icons in resources.qrc into the app
set(CMAKE_AUTORCC ON)

include(GNUInstallDirs)

# Reverse-DNS application ID: names the desktop file, and identifies the app
# to the desktop (e.g. for the global shortcut portal)
set(APP_ID "io.github.netean.SnippetManager")

# ---------------------------------------------------------------------------
# Optional features
# ---------------------------------------------------------------------------
option(WITH_SYNTAX_HIGHLIGHTING "Highlight code snippets with KDE's KSyntaxHighlighting, if it's installed" ON)
if(WITH_SYNTAX_HIGHLIGHTING)
    find_package(KF6SyntaxHighlighting QUIET)
endif()
if(KF6SyntaxHighlighting_FOUND)
    message(STATUS "Syntax highlighting: enabled (KF6SyntaxHighlighting ${KF6SyntaxHighlighting_VERSION})")
else()
    message(STATUS "Syntax highlighting: disabled (install KF6SyntaxHighlighting to enable it)")
endif()

# D-Bus is used for the global shortcut portal on Linux
if(UNIX AND NOT APPLE)
    find_package(Qt6 COMPONENTS DBus QUIET)
endif()

# ---------------------------------------------------------------------------
# Core library: everything except main(), shared by the app and the tests
# ---------------------------------------------------------------------------
add_library(snippetcore STATIC
    src/database.cpp
    src/database.h
    src/messagebox.h
    src/snippetmodel.cpp
    src/snippetmodel.h
    src/snippeteditor.cpp
    src/snippeteditor.h
    src/tagedit.cpp
    src/tagedit.h
    src/clipboard.cpp
    src/clipboard.h
    src/languages.cpp
    src/languages.h
    src/richtext.cpp
    src/richtext.h
    src/importexport.cpp
    src/importexport.h
    src/placeholders.cpp
    src/placeholders.h
    src/quicksearch.cpp
    src/quicksearch.h
    src/singleinstance.cpp
    src/singleinstance.h
    src/globalshortcut.cpp
    src/globalshortcut.h
    src/autostart.cpp
    src/autostart.h
    src/mainwindow.cpp
    src/mainwindow.h
    src/settingsdialog.cpp
    src/settingsdialog.h
)
target_include_directories(snippetcore PUBLIC src)
target_compile_definitions(snippetcore PUBLIC
    APP_VERSION="${PROJECT_VERSION}"
    APP_ID="${APP_ID}"
    # qDebug() output is for development only
    $<$<NOT:$<CONFIG:Debug>>:QT_NO_DEBUG_OUTPUT>
)
target_link_libraries(snippetcore PUBLIC
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Sql
    Qt6::Network
)
if(KF6SyntaxHighlighting_FOUND)
    target_link_libraries(snippetcore PUBLIC KF6::SyntaxHighlighting)
    target_compile_definitions(snippetcore PUBLIC HAVE_SYNTAX_HIGHLIGHTING)
endif()
if(TARGET Qt6::DBus)
    target_link_libraries(snippetcore PUBLIC Qt6::DBus)
    target_compile_definitions(snippetcore PUBLIC HAVE_DBUS)
endif()

# ---------------------------------------------------------------------------
# Application
# ---------------------------------------------------------------------------
set(app_extra_sources resources/resources.qrc)
if(WIN32)
    # Windows icon and version resource
    configure_file(resources/icon.rc.in "${CMAKE_CURRENT_BINARY_DIR}/icon.rc" @ONLY)
    list(APPEND app_extra_sources "${CMAKE_CURRENT_BINARY_DIR}/icon.rc")
elseif(APPLE)
    set(MACOSX_BUNDLE_ICON_FILE icon.png)
    set_source_files_properties(resources/icon.png PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
    list(APPEND app_extra_sources resources/icon.png)
endif()

qt_add_executable(snippetmanager WIN32 MACOSX_BUNDLE
    src/main.cpp
    ${app_extra_sources}
)
target_link_libraries(snippetmanager PRIVATE snippetcore)

if(APPLE)
    set_target_properties(snippetmanager PROPERTIES
        MACOSX_BUNDLE_GUI_IDENTIFIER "${APP_ID}"
        MACOSX_BUNDLE_BUNDLE_NAME "Snippet Manager"
        MACOSX_BUNDLE_DISPLAY_NAME "Snippet Manager"
        MACOSX_BUNDLE_INFO_STRING "Text and Code Snippet Manager"
        MACOSX_BUNDLE_LONG_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    )
endif()

# ---------------------------------------------------------------------------
# Install (Linux)
# ---------------------------------------------------------------------------
if(UNIX AND NOT APPLE)
    install(TARGETS snippetmanager DESTINATION ${CMAKE_INSTALL_BINDIR})
    install(FILES resources/snippetmanager.desktop
            DESTINATION ${CMAKE_INSTALL_DATADIR}/applications RENAME ${APP_ID}.desktop)
    install(FILES resources/icon.png DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/128x128/apps RENAME snippetmanager.png)
    install(FILES resources/icon-64.png DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/64x64/apps RENAME snippetmanager.png)
    install(FILES resources/icon-32.png DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/32x32/apps RENAME snippetmanager.png)
    install(FILES resources/icon.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps RENAME snippetmanager.svg)
endif()

# ---------------------------------------------------------------------------
# Tests (run with: ctest --test-dir <build dir>)
# ---------------------------------------------------------------------------
option(BUILD_TESTING "Build the tests" ON)
if(BUILD_TESTING)
    enable_testing()
    add_subdirectory(tests)
endif()
