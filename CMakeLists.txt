cmake_minimum_required(VERSION 3.25)
project(wots4_mod_autosave LANGUAGES C CXX)

# When building standalone, pass -DWOTS4_SDK_DIR=<path to wots4-mods>
if(NOT COMMAND wots4_add_mod)
    set(WOTS4_SDK_DIR "" CACHE PATH "Path to wots4-mods (ModLoader)")
    if(NOT EXISTS "${WOTS4_SDK_DIR}/cmake/Wots4.cmake")
        message(FATAL_ERROR "Set WOTS4_SDK_DIR to the path of wots4-mods")
    endif()
    include(${WOTS4_SDK_DIR}/cmake/Wots4.cmake)
    enable_testing()
    set(WOTS4_BUILD_TESTS ON)
endif()

# Use the Mod's folder name in the game folder (mods/<name>/) as <name>
wots4_use_minhook()
wots4_add_mod(autosave
    SOURCES src/mod.cpp src/settings.cpp src/game_hooks.cpp src/game_save.cpp src/save_file_writer.cpp
        src/save_slot_record.cpp src/retry_save.cpp
    DEF src/mod.def LIBS wots4_minhook
    FILES config.ini README.md README.en.md
    LICENSES docs/licenses/minhook.txt)
target_include_directories(mod_autosave PRIVATE src)
target_compile_definitions(mod_autosave PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)

if(WOTS4_BUILD_TESTS)
    wots4_add_probe(autosave_offline_tests
        SOURCES tests/offline_tests.cpp src/settings.cpp src/save_file_writer.cpp src/save_slot_record.cpp src/retry_save.cpp
        INCLUDES src ARGS "${CMAKE_CURRENT_SOURCE_DIR}/config.ini")
    set(AUTOSAVE_TEST_EXE "${WOTS4_ROOT}/research/bin/WayOfTheSamurai4.unpacked.exe"
        CACHE FILEPATH "Decrypted game copy for offline hook tests")
    wots4_add_probe(autosave_exe_tests SOURCES tests/exe_tests.cpp INCLUDES src
        LINK_OPTIONS /BASE:0x10000000 /DYNAMICBASE
        ARGS "${AUTOSAVE_TEST_EXE}" $<TARGET_FILE:mod_autosave> NEEDS_EXE)
    add_dependencies(autosave_exe_tests mod_autosave)
    wots4_add_probe(autosave_retry_native_tests SOURCES tests/retry_native_tests.cpp src/retry_save.cpp INCLUDES src
        LINK_OPTIONS /BASE:0x10000000 /DYNAMICBASE
        ARGS "${AUTOSAVE_TEST_EXE}" NEEDS_EXE)
    # Each scenario runs in its own process: an accepted initialization keeps its hooks.
    foreach(scenario IN ITEMS unsupported conflict slot_conflict checkpoint_conflict list_made)
        add_test(NAME autosave_exe_${scenario} COMMAND autosave_exe_tests
            "${AUTOSAVE_TEST_EXE}" $<TARGET_FILE:mod_autosave> ${scenario})
        set_tests_properties(autosave_exe_${scenario} PROPERTIES LABELS needs_exe)
    endforeach()
endif()
