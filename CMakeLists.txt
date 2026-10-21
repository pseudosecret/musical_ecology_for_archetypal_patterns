cmake_minimum_required(VERSION 3.22)
project(MusicAsBecoming VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 6.2 REQUIRED COMPONENTS Core Gui Qml Quick Xml Test)

# Platform plugins are runtime dependencies; finding Qt's libraries is not enough.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND NOT CMAKE_CROSSCOMPILING
   AND QT6_IS_SHARED_LIBS_BUILD
   AND (NOT "$ENV{WAYLAND_DISPLAY}" STREQUAL "" OR "$ENV{XDG_SESSION_TYPE}" STREQUAL "wayland"))
    set(qt_platform_plugins "${QT6_INSTALL_PREFIX}/${QT6_INSTALL_PLUGINS}/platforms")
    if(NOT EXISTS "${qt_platform_plugins}/libqwayland-generic.so"
       AND NOT EXISTS "${qt_platform_plugins}/libqwayland-egl.so")
        message(WARNING
            "This is a Wayland session, but Qt ${Qt6_VERSION} has no Wayland platform plugin in "
            "${qt_platform_plugins}. On Ubuntu, install it with: sudo apt-get install qt6-wayland. "
            "The application needs a working display backend; offscreen tests do not verify this dependency. "
            "See README.md for runtime requirements.")
    endif()
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(FLUIDSYNTH REQUIRED IMPORTED_TARGET fluidsynth)

add_library(music_core
    src/Score.cpp
    src/Tonality.cpp
    src/Transformations.cpp
    src/MelodyAnalysis.cpp
    src/ScoreRange.cpp
    src/ScoreClipboard.cpp
    src/MusicXml.cpp
    src/Engraving.cpp
)
target_include_directories(music_core PUBLIC src)
target_link_libraries(music_core PUBLIC Qt6::Core Qt6::Gui Qt6::Xml)
target_compile_options(music_core PRIVATE -Wall -Wextra -Wpedantic)

add_library(music_editor src/Editor.cpp src/Playback.cpp src/TransformWorkspace.cpp src/TransformPreview.cpp)
target_link_libraries(music_editor PUBLIC music_core PkgConfig::FLUIDSYNTH)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    pkg_check_modules(PULSE REQUIRED IMPORTED_TARGET libpulse)
    target_sources(music_editor PRIVATE src/PulseOutput.cpp)
    target_link_libraries(music_editor PRIVATE PkgConfig::PULSE)
    target_compile_definitions(music_editor PRIVATE MUSIC_PULSE_OUTPUT)
endif()
target_compile_options(music_editor PRIVATE -Wall -Wextra -Wpedantic)
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/python")
configure_file(python/worker.py python/worker.py COPYONLY)
configure_file(python/melody_analysis.py python/melody_analysis.py COPYONLY)
configure_file(python/musical_patterns.py python/musical_patterns.py COPYONLY)

add_executable(music_as_becoming
    src/main.cpp
    src/ScoreView.cpp
)
target_link_libraries(music_as_becoming PRIVATE music_editor Qt6::Qml Qt6::Quick PkgConfig::FLUIDSYNTH)
target_compile_options(music_as_becoming PRIVATE -Wall -Wextra -Wpedantic)
qt_add_resources(music_as_becoming "app" PREFIX "/" FILES qml/Main.qml qml/KeyChoice.qml qml/TransformDialog.qml qml/TransformSteps.qml qml/TransformKeys.qml qml/PreviewPane.qml qml/ResultPane.qml qml/AnalysisPane.qml qml/ScorePane.qml assets/Bravura.otf)

enable_testing()
add_executable(music_tests
    tests/ScoreTests.cpp
)
target_link_libraries(music_tests PRIVATE music_editor Qt6::Test PkgConfig::FLUIDSYNTH)
qt_add_resources(music_tests "fonts" PREFIX "/" FILES assets/Bravura.otf)
add_test(NAME music_tests COMMAND music_tests)
set_tests_properties(music_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(music_tuplet_tests tests/TupletTests.cpp)
target_link_libraries(music_tuplet_tests PRIVATE music_editor Qt6::Test PkgConfig::FLUIDSYNTH)
target_compile_options(music_tuplet_tests PRIVATE -Wall -Wextra -Wpedantic)
target_compile_definitions(music_tuplet_tests PRIVATE MUSIC_TUPLET_ARTIFACT_DIR="${CMAKE_CURRENT_BINARY_DIR}/tuplet-checks")
qt_add_resources(music_tuplet_tests "fonts" PREFIX "/" FILES assets/Bravura.otf)
add_test(NAME music_tuplet_tests COMMAND music_tuplet_tests)
set_tests_properties(music_tuplet_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(music_ui_tests
    tests/UiTests.cpp
    src/ScoreView.cpp
)
target_link_libraries(music_ui_tests PRIVATE music_editor Qt6::Qml Qt6::Quick Qt6::Test PkgConfig::FLUIDSYNTH)
target_compile_definitions(music_ui_tests PRIVATE MUSIC_UI_ARTIFACT_DIR="${CMAKE_CURRENT_BINARY_DIR}/ui-checks" MUSIC_TEST_WORKER="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/transform_worker.py" MUSIC_REAL_WORKER="${CMAKE_CURRENT_SOURCE_DIR}/python/worker.py")
qt_add_resources(music_ui_tests "app" PREFIX "/" FILES qml/Main.qml qml/KeyChoice.qml qml/TransformDialog.qml qml/TransformSteps.qml qml/TransformKeys.qml qml/PreviewPane.qml qml/ResultPane.qml qml/AnalysisPane.qml qml/ScorePane.qml assets/Bravura.otf)
add_test(NAME music_ui_tests COMMAND music_ui_tests)
set_tests_properties(music_ui_tests PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")

add_executable(music_copy_tests tests/CopyPasteTests.cpp)
target_link_libraries(music_copy_tests PRIVATE music_editor Qt6::Test PkgConfig::FLUIDSYNTH)
target_compile_options(music_copy_tests PRIVATE -Wall -Wextra -Wpedantic)
target_compile_definitions(music_copy_tests PRIVATE MUSIC_COPY_ARTIFACT_DIR="${CMAKE_CURRENT_BINARY_DIR}/copy-checks")
qt_add_resources(music_copy_tests "fonts" PREFIX "/" FILES assets/Bravura.otf)
add_test(NAME music_copy_tests COMMAND music_copy_tests)
set_tests_properties(music_copy_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(music_audio_tests tests/AudioTests.cpp)
target_link_libraries(music_audio_tests PRIVATE music_editor Qt6::Test PkgConfig::FLUIDSYNTH)
target_compile_options(music_audio_tests PRIVATE -Wall -Wextra -Wpedantic)
target_compile_definitions(music_audio_tests PRIVATE MUSIC_AUDIO_ARTIFACT_DIR="${CMAKE_CURRENT_BINARY_DIR}/audio-checks")
add_test(NAME music_audio_tests COMMAND music_audio_tests)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_executable(music_pulse_tests tests/PulseOutputTests.cpp tests/fixtures/PulseDevice.cpp)
    target_link_libraries(music_pulse_tests PRIVATE music_editor Qt6::Test PkgConfig::PULSE)
    target_compile_options(music_pulse_tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME music_pulse_tests COMMAND music_pulse_tests)
endif()

add_executable(music_transform_tests tests/TransformationTests.cpp)
target_link_libraries(music_transform_tests PRIVATE music_editor Qt6::Test)
target_compile_options(music_transform_tests PRIVATE -Wall -Wextra -Wpedantic)
target_compile_definitions(music_transform_tests PRIVATE
    MUSIC_TEST_WORKER="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/transform_worker.py"
    MUSIC_REAL_WORKER="${CMAKE_CURRENT_SOURCE_DIR}/python/worker.py")
qt_add_resources(music_transform_tests "fonts" PREFIX "/" FILES assets/Bravura.otf)
add_test(NAME music_transform_tests COMMAND music_transform_tests)
set_tests_properties(music_transform_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(music_preview_tests tests/PreviewTests.cpp)
target_link_libraries(music_preview_tests PRIVATE music_editor Qt6::Test)
target_compile_options(music_preview_tests PRIVATE -Wall -Wextra -Wpedantic)
target_compile_definitions(music_preview_tests PRIVATE MUSIC_TEST_WORKER="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/transform_worker.py" MUSIC_REAL_WORKER="${CMAKE_CURRENT_SOURCE_DIR}/python/worker.py")
qt_add_resources(music_preview_tests "fonts" PREFIX "/" FILES assets/Bravura.otf)
add_test(NAME music_preview_tests COMMAND music_preview_tests)
set_tests_properties(music_preview_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

find_package(Python3 3.11 COMPONENTS Interpreter)
add_executable(music_analysis_tests tests/MelodyAnalysisTests.cpp)
target_link_libraries(music_analysis_tests PRIVATE music_editor Qt6::Test)
target_compile_options(music_analysis_tests PRIVATE -Wall -Wextra -Wpedantic)
target_compile_definitions(music_analysis_tests PRIVATE
    MUSIC_TEST_WORKER="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/transform_worker.py"
    MUSIC_REAL_WORKER="${CMAKE_CURRENT_SOURCE_DIR}/python/worker.py")
qt_add_resources(music_analysis_tests "fonts" PREFIX "/" FILES assets/Bravura.otf)
add_test(NAME music_analysis_tests COMMAND music_analysis_tests)
set_tests_properties(music_analysis_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
add_executable(music_tonality_tests tests/TonalityTests.cpp)
target_link_libraries(music_tonality_tests PRIVATE music_editor Qt6::Test)
target_compile_options(music_tonality_tests PRIVATE -Wall -Wextra -Wpedantic)
target_compile_definitions(music_tonality_tests PRIVATE
    MUSIC_TONAL_ARTIFACT_DIR="${CMAKE_CURRENT_BINARY_DIR}/tonal-checks"
    MUSIC_TEST_WORKER="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/transform_worker.py"
    MUSIC_REAL_WORKER="${CMAKE_CURRENT_SOURCE_DIR}/python/worker.py")
qt_add_resources(music_tonality_tests "fonts" PREFIX "/" FILES assets/Bravura.otf)
add_test(NAME music_tonality_tests COMMAND music_tonality_tests)
set_tests_properties(music_tonality_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
if(Python3_Interpreter_FOUND)
    # Use the same PATH-based interpreter as the worker under poetry run.
    add_test(NAME music_python_tests COMMAND python3 -m unittest discover -s "${CMAKE_CURRENT_SOURCE_DIR}/python" -p "test_*.py" -v)
endif()
