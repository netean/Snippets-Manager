cmake_minimum_required(VERSION 3.16)
project(SnippetManager)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Core Widgets Sql)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

# Platform-specific icon handling
if(WIN32)
    # Windows icon resource
    set(ICON_RC "${CMAKE_CURRENT_SOURCE_DIR}/resources/icon.rc")
    if(EXISTS ${ICON_RC})
        list(APPEND snippetmanager_SRCS ${ICON_RC})
    endif()
elseif(APPLE)
    # macOS icon
    set(MACOSX_BUNDLE_ICON_FILE icon.png)
    set(ICON_FILE "${CMAKE_CURRENT_SOURCE_DIR}/resources/icon.png")
    if(EXISTS ${ICON_FILE})
        set_source_files_properties(${ICON_FILE} PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
        list(APPEND snippetmanager_SRCS ${ICON_FILE})
    endif()
endif()

set(snippetmanager_SRCS
    src/main.cpp
    src/mainwindow.cpp
    src/mainwindow.h
    src/snippetmodel.cpp
    src/snippetmodel.h
    src/database.cpp
    src/database.h
    resources/resources.qrc
    ${snippetmanager_SRCS}
)

if(APPLE)
    add_executable(snippetmanager MACOSX_BUNDLE ${snippetmanager_SRCS})
    set_target_properties(snippetmanager PROPERTIES
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.snippetmanager.app"
        MACOSX_BUNDLE_BUNDLE_NAME "Snippet Manager"
        MACOSX_BUNDLE_DISPLAY_NAME "Snippet Manager"
        MACOSX_BUNDLE_INFO_STRING "Text and Code Snippet Manager"
        MACOSX_BUNDLE_LONG_VERSION_STRING "1.0.0"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "1.0"
        MACOSX_BUNDLE_BUNDLE_VERSION "1.0"
    )
else()
    add_executable(snippetmanager ${snippetmanager_SRCS})
endif()

target_link_libraries(snippetmanager
    Qt6::Core
    Qt6::Widgets
    Qt6::Sql
)

# Install rules
if(UNIX AND NOT APPLE)
    # Install the executable
    install(TARGETS snippetmanager DESTINATION bin)
    
    # Install desktop file
    install(FILES resources/snippetmanager.desktop DESTINATION share/applications)
    
    # Install icon
    install(FILES resources/icon.png DESTINATION share/icons/hicolor/128x128/apps RENAME snippetmanager.png)
    install(FILES resources/icon-64.png DESTINATION share/icons/hicolor/64x64/apps RENAME snippetmanager.png)
    install(FILES resources/icon-32.png DESTINATION share/icons/hicolor/32x32/apps RENAME snippetmanager.png)
    install(FILES resources/icon.svg DESTINATION share/icons/hicolor/scalable/apps RENAME snippetmanager.svg)
endif()