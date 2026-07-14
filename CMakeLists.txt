cmake_minimum_required(VERSION 3.16)
project(AudioBitrateGUI VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# Qt 6 (fall back to a Qt 5 build if that is all that is available).
find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets LinguistTools)

add_executable(AudioBitrateGUI WIN32
    src/main.cpp
    src/MainWindow.cpp   src/MainWindow.h
    src/ScanWorker.cpp   src/ScanWorker.h
    src/FfprobeAnalyzer.cpp src/FfprobeAnalyzer.h
    src/ReportWriter.cpp src/ReportWriter.h
)

target_link_libraries(AudioBitrateGUI PRIVATE Qt${QT_VERSION_MAJOR}::Widgets)

# Single source of truth for the version string (from project() above).
target_compile_definitions(AudioBitrateGUI PRIVATE APP_VERSION="${PROJECT_VERSION}")

# --- Translations (German / Japanese / Italian) -------------------------------
# Compiles the .ts files to .qm via lrelease at build time and embeds them under
# :/i18n/ . (lupdate is NOT invoked, so a missing Qt6Qml.dll can't break builds.)
qt_add_translations(AudioBitrateGUI
    TS_FILES
        translations/abr_de.ts
        translations/abr_ja.ts
        translations/abr_it.ts
    RESOURCE_PREFIX "/i18n")

# --- Windows: bundle the Qt runtime next to the .exe via windeployqt ----------
if(WIN32)
    get_target_property(_qmake_exe Qt${QT_VERSION_MAJOR}::qmake IMPORTED_LOCATION)
    get_filename_component(_qt_bin "${_qmake_exe}" DIRECTORY)
    find_program(WINDEPLOYQT_EXE windeployqt HINTS "${_qt_bin}")
    if(WINDEPLOYQT_EXE)
        add_custom_command(TARGET AudioBitrateGUI POST_BUILD
            COMMAND "${WINDEPLOYQT_EXE}"
                    --no-translations --no-compiler-runtime
                    "$<TARGET_FILE:AudioBitrateGUI>"
            COMMENT "Deploying Qt runtime with windeployqt")
    endif()
endif()

install(TARGETS AudioBitrateGUI
        RUNTIME DESTINATION bin
        BUNDLE  DESTINATION .)

# --- Optional headless smoke test for the scan backend (developers only) ------
option(BUILD_SCAN_SMOKE "Build the headless scan smoke test" OFF)
if(BUILD_SCAN_SMOKE)
    find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core)
    add_executable(scan_smoke
        tests/scan_smoke.cpp
        src/ScanWorker.cpp   src/ScanWorker.h
        src/FfprobeAnalyzer.cpp src/FfprobeAnalyzer.h)
    target_include_directories(scan_smoke PRIVATE src)
    target_link_libraries(scan_smoke PRIVATE Qt${QT_VERSION_MAJOR}::Core)
endif()
