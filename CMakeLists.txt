cmake_minimum_required(VERSION 3.22)
project(. VERSION 0.0.1)
set(CMAKE_CXX_STANDARD 17)

add_subdirectory(JUCE)

juce_add_binary_data(Tascam424BinaryData
    SOURCES
        SimData/u101a_transfer_lut.csv
)

juce_add_plugin(Tascam424Plugin
    PLUGIN_NAME             "fourtwofour"
    PRODUCT_NAME            "fourtwofour"
    COMPANY_NAME            "wildli"
    BUNDLE_ID               "com.wildli.fourtwofour"
    PLUGIN_MANUFACTURER_CODE Wldi
    PLUGIN_CODE             F424
    FORMATS                 VST3 AU Standalone
    VST3_AUTO_MANIFEST      TRUE
    IS_SYNTH                FALSE
    NEEDS_MIDI_INPUT        FALSE
    NEEDS_MIDI_OUTPUT       FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
)

target_sources(Tascam424Plugin PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginProcessor.h
        Source/PluginEditor.cpp
        Source/PluginEditor.h
)

target_compile_definitions(Tascam424Plugin PUBLIC
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_WEB_BROWSER=0
    JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(Tascam424Plugin PRIVATE
    Tascam424BinaryData
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_dsp
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)
