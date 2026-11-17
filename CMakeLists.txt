# AuraRack — NextDSP · rack multi-effets DXD (VST3 / AU)
cmake_minimum_required(VERSION 3.22)
project(AuraRack VERSION 0.3.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "macOS minimum")
if(APPLE)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Universal binary")
endif()

option(AURA_BUILD_PLUGIN "Compiler le plugin JUCE (VST3 / AU)" ON)
option(AURA_BUILD_TESTS  "Compiler le banc d'essai DSP (sans JUCE)" ON)
set(AURA_JUCE_DIR "" CACHE PATH "Dossier JUCE local (sinon téléchargement de JUCE 8.0.15)")

# ---------------------------------------------------------------------------------------------
# Noyau DSP + moteur du rack : C++20 pur, partagé par le plugin et le banc d'essai.
add_library(aura_rack_core STATIC Source/Engine/RackEngine.cpp)
target_include_directories(aura_rack_core PUBLIC Source)
target_compile_features(aura_rack_core PUBLIC cxx_std_20)
set_target_properties(aura_rack_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(MSVC)
    target_compile_options(aura_rack_core PRIVATE /W4 /fp:precise)
else()
    target_compile_options(aura_rack_core PRIVATE -Wall -Wextra -Wshadow -Wconversion -Wno-sign-conversion)
endif()

if(AURA_BUILD_TESTS)
    enable_testing()
    add_executable(aurarack_dsp_test Tests/DspSelfTest.cpp)
    find_package(Threads REQUIRED)
    target_link_libraries(aurarack_dsp_test PRIVATE aura_rack_core Threads::Threads)
    if(NOT MSVC)
        target_compile_options(aurarack_dsp_test PRIVATE -Wno-mismatched-new-delete)
    endif()
    add_test(NAME aurarack_dsp_test COMMAND aurarack_dsp_test)
endif()

# ---------------------------------------------------------------------------------------------
if(AURA_BUILD_PLUGIN)
    if(AURA_JUCE_DIR)
        add_subdirectory(${AURA_JUCE_DIR} ${CMAKE_BINARY_DIR}/JUCE)
    else()
        include(FetchContent)
        FetchContent_Declare(JUCE
            GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
            GIT_TAG 8.0.15
            GIT_SHALLOW ON)
        FetchContent_MakeAvailable(JUCE)
    endif()

    set(AURA_FORMATS VST3)
    if(APPLE)
        list(APPEND AURA_FORMATS AU)
    endif()

    juce_add_plugin(AuraRack
        COMPANY_NAME "NextDSP"
        COMPANY_WEBSITE "https://nextdsp.com"
        BUNDLE_ID "com.nextdsp.aurarack"
        PLUGIN_MANUFACTURER_CODE Nxds
        PLUGIN_CODE ArRk
        PRODUCT_NAME "AuraRack"
        FORMATS ${AURA_FORMATS}
        IS_SYNTH FALSE
        NEEDS_MIDI_INPUT FALSE
        NEEDS_MIDI_OUTPUT FALSE
        IS_MIDI_EFFECT FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS FALSE
        VST3_CATEGORIES Fx Dynamics EQ Reverb Delay
        AU_MAIN_TYPE kAudioUnitType_Effect
        COPY_PLUGIN_AFTER_BUILD FALSE)

    target_sources(AuraRack PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Source/Parameters.cpp
        Source/UI/NextLookAndFeel.cpp
        Source/UI/SlotComponent.cpp
        Source/UI/RackView.cpp
        Source/UI/TopBar.cpp)

    target_compile_definitions(AuraRack PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0)

    target_link_libraries(AuraRack
        PRIVATE
            aura_rack_core
            juce::juce_audio_processors
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)
endif()

# ---------------------------------------------------------------------------------------------
# Hôte de test : charge le binaire VST3 compilé, comme un DAW.
if(AURA_BUILD_PLUGIN AND AURA_BUILD_TESTS)
    juce_add_console_app(aurarack_host_test PRODUCT_NAME "aurarack_host_test")
    target_sources(aurarack_host_test PRIVATE Tests/HostTest.cpp)
    target_compile_definitions(aurarack_host_test PRIVATE
        JUCE_PLUGINHOST_VST3=1
        JUCE_PLUGINHOST_AU=0
        JUCE_MODAL_LOOPS_PERMITTED=1
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0)
    target_link_libraries(aurarack_host_test PRIVATE
        juce::juce_audio_processors
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
    add_dependencies(aurarack_host_test AuraRack_VST3)
    add_test(NAME aurarack_host_test
             COMMAND aurarack_host_test "$<GENEX_EVAL:$<TARGET_PROPERTY:AuraRack_VST3,JUCE_PLUGIN_ARTEFACT_FILE>>")
endif()

# ---------------------------------------------------------------------------------------------
# Test de l'éditeur : glisser-déposer et rendu PNG, sans fenêtre.
if(AURA_BUILD_PLUGIN AND AURA_BUILD_TESTS)
    juce_add_console_app(aurarack_ui_test PRODUCT_NAME "aurarack_ui_test")
    target_sources(aurarack_ui_test PRIVATE
        Tests/UiTest.cpp
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Source/Parameters.cpp
        Source/UI/NextLookAndFeel.cpp
        Source/UI/SlotComponent.cpp
        Source/UI/RackView.cpp
        Source/UI/TopBar.cpp)
    target_compile_definitions(aurarack_ui_test PRIVATE
        JucePlugin_Name="AuraRack"
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0)
    target_link_libraries(aurarack_ui_test PRIVATE
        aura_rack_core
        juce::juce_audio_processors
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
    add_test(NAME aurarack_ui_test COMMAND aurarack_ui_test)
endif()
