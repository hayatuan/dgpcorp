juce_add_console_app(ShowCueTests
    PRODUCT_NAME "ShowCueTests"
    VERSION 1.0.0
    COMPANY_NAME "DGP Corp"
    BUNDLE_ID "com.dgpco.showcuetests")

target_sources(ShowCueTests PRIVATE
    ShowCueTestsMain.cpp
    ShowUpdateCheckerTests.cpp
    ShowApplicationStateTests.cpp
    ShowKeyboardInputTests.cpp
    ShowProjectPersistenceTests.cpp
    ShowBackupSyncTests.cpp
    ../Source/ShowUpdateChecker.cpp)

if(WIN32)
    target_sources(ShowCueTests PRIVATE ../Source/ShowBackupLanDiscoveryWin.cpp)
    target_link_libraries(ShowCueTests PRIVATE iphlpapi)
endif()

target_compile_definitions(ShowCueTests PRIVATE
    JUCE_UNIT_TESTS=1
    JUCE_USE_CURL=0
    JUCE_WEB_BROWSER=0
    JUCE_STANDALONE_APPLICATION=1)

target_link_libraries(ShowCueTests PRIVATE
    juce::juce_core
    juce::juce_data_structures
    juce::juce_events
    juce::juce_graphics
    juce::juce_gui_basics
    juce::juce_osc)

juce_generate_juce_header(ShowCueTests)

add_test(NAME ShowCueTests COMMAND $<TARGET_FILE:ShowCueTests>)
