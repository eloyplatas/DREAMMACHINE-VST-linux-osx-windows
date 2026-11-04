cmake_minimum_required(VERSION 3.22)
project(DreamMachine VERSION 1.3.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Ruta a JUCE: -DJUCE_DIR=/ruta/a/JUCE (por defecto ../JUCE)
if(NOT DEFINED JUCE_DIR)
    set(JUCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../JUCE")
endif()
add_subdirectory(${JUCE_DIR} ${CMAKE_BINARY_DIR}/JUCE)

# Formatos: VST3 y Standalone en todas las plataformas; AU además en macOS
set(DM_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND DM_FORMATS AU)
endif()

juce_add_plugin(DreamMachine
    COMPANY_NAME "Eloy Platas"
    COMPANY_WEBSITE "https://github.com/eloyplatas"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    PLUGIN_MANUFACTURER_CODE Elpl
    PLUGIN_CODE DrMc
    FORMATS ${DM_FORMATS}
    VST3_CATEGORIES Instrument Synth
    AU_MAIN_TYPE kAudioUnitType_MusicDevice
    BUNDLE_ID com.eloyplatas.dreammachine
    HARDENED_RUNTIME_ENABLED FALSE
    PRODUCT_NAME "Dream Machine"
    COPY_PLUGIN_AFTER_BUILD FALSE)

target_sources(DreamMachine PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/Wavetables.cpp)

target_compile_definitions(DreamMachine PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

# Los textos de la interfaz llevan caracteres UTF-8 (superíndices de la factorización)
if(MSVC)
    target_compile_options(DreamMachine PRIVATE /utf-8 /bigobj)
endif()

target_link_libraries(DreamMachine
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# Prueba offline (opcional): cmake -DDM_TESTS=ON
option(DM_TESTS "Compilar prueba offline" OFF)
if(DM_TESTS)
    juce_add_console_app(RenderTest PRODUCT_NAME "RenderTest")
    target_sources(RenderTest PRIVATE tests/RenderTest.cpp Source/PluginProcessor.cpp Source/PluginEditor.cpp Source/Wavetables.cpp)
    target_compile_definitions(RenderTest PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JucePlugin_Name="Dream Machine")
    target_link_libraries(RenderTest PRIVATE juce::juce_audio_utils juce::juce_dsp juce::juce_gui_extra juce::juce_audio_processors)
endif()
if(DM_TESTS)
    juce_add_console_app(FuzzTest PRODUCT_NAME "FuzzTest")
    target_sources(FuzzTest PRIVATE tests/FuzzTest.cpp Source/PluginProcessor.cpp Source/PluginEditor.cpp Source/Wavetables.cpp)
    target_compile_definitions(FuzzTest PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JucePlugin_Name="Dream Machine")
    target_link_libraries(FuzzTest PRIVATE juce::juce_audio_utils juce::juce_dsp juce::juce_gui_extra juce::juce_audio_processors)
endif()
