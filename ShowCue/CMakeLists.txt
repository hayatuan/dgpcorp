cmake_minimum_required(VERSION 3.15)

# Đặt tên cho dự án phần mềm của anh
project(Show_Control VERSION 1.0.0 LANGUAGES C CXX)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Giúp clangd / Cursor Problems đọc đúng include JUCE
set(CMAKE_EXPORT_COMPILE_COMMANDS ON CACHE BOOL "Export compile_commands.json for clangd" FORCE)

# Chỉ đường dẫn cho VS Code tìm thấy bộ thư viện JUCE nằm bên cạnh
add_subdirectory(JUCE)

# Khởi tạo cấu hình ứng dụng Âm thanh (Audio Application)
# ICON_BIG / ICON_SMALL: JUCE tự tạo .icns (macOS) và .ico (Windows) qua juceaide.
# PNG 1024×1024 cho macOS; ICO riêng cho Windows khi cross-build.
juce_add_gui_app(Show_Control
    PRODUCT_NAME "Show Control"
    VERSION 1.0.0
    BUNDLE_ID "com.dgpco.showcontrol"
    ICON_BIG   "${CMAKE_CURRENT_SOURCE_DIR}/Resources/AppIcon.png"
    ICON_SMALL "${CMAKE_CURRENT_SOURCE_DIR}/Resources/AppIcon.ico"
    IS_AUDIO_APPLICATION TRUE)

# Khai báo các file code C++ nằm trong thư mục Source
target_sources(Show_Control PRIVATE
    Source/Main.cpp
    Source/ShowControlMacWindow.h
    Source/ShowControlIDEIndex.cpp
    Source/ShowTheme.h
    Source/MasterDeckComponent.h
    Source/ShowControlLookAndFeel.h
    Source/ShowControlLookAndFeel.cpp
    Source/MainComponent.h
    Source/MainComponent.cpp
    Source/StageMonitorComponent.h
    Source/StageMonitorComponent.cpp
    Source/SoundPad.h
    Source/PadRealtimeSource.h
    Source/ShowOutputRouting.h
    Source/ShowDsp.h
    Source/ShowGraphicsSafe.h
    Source/ShowFlatIcons.h
    Source/PadCueState.h
    Source/SidebarPanel.h
    Source/InspectorPanel.h
    Source/MasterDeckPanel.h
    Source/AudioAnalyzer.h
    Source/ErrorHandler.h
    Source/CueListPanel.h
    Source/CueListPanel.cpp
    Source/AudioEngine.h
    Source/AudioEngine.cpp
    Source/HotkeyManager.h
    Source/HotkeyAssignDialog.h
    Source/PadEqualizerDialog.h
    Source/VideoAudioExtractor.h
    Source/FfmpegSetupDialog.h
    Source/AudioDeviceSettingsPanel.h
    Source/GlobalPreferencesDialog.h
    Source/ShowAboutDialog.h
    Source/ShowUpdateChecker.h
    Source/BusMixerPanel.h
    Source/AudioMetadataReader.h
    Source/ShowWaveformCache.h
    )

if (APPLE)
    target_sources(Show_Control PRIVATE Source/ShowControlMacWindow.mm)

    set(_showcontrol_app_icon_png "${CMAKE_CURRENT_SOURCE_DIR}/Resources/AppIcon.png")
    if(EXISTS "${_showcontrol_app_icon_png}")
        target_sources(Show_Control PRIVATE "${_showcontrol_app_icon_png}")
        set_source_files_properties("${_showcontrol_app_icon_png}"
            PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    endif()
endif()

# Nạp tất cả các module xử lý Mixer và driver phần cứng âm thanh
target_link_libraries(Show_Control PRIVATE
    juce::juce_audio_utils
    juce::juce_audio_devices
    juce::juce_audio_formats
    juce::juce_audio_basics
    juce::juce_dsp
    juce::juce_gui_extra
    juce::juce_gui_basics
    juce::juce_graphics
    juce::juce_data_structures
    juce::juce_core)

# Tự động tạo file Header hệ thống
juce_generate_juce_header(Show_Control)

# Bundle ffmpeg cạnh binary (.app/Contents/MacOS) — ưu tiên ThirdParty tĩnh, rồi ffmpeg hệ thống lúc build.
set(_showcontrol_ffmpeg_src "")
if(APPLE AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/ffmpeg/mac/ffmpeg")
    set(_showcontrol_ffmpeg_src "${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/ffmpeg/mac/ffmpeg")
elseif(WIN32 AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/ffmpeg/win/ffmpeg.exe")
    set(_showcontrol_ffmpeg_src "${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/ffmpeg/win/ffmpeg.exe")
else()
    find_program(_showcontrol_ffmpeg_system ffmpeg
        HINTS /opt/homebrew/bin /usr/local/bin)
    if(_showcontrol_ffmpeg_system)
        set(_showcontrol_ffmpeg_src "${_showcontrol_ffmpeg_system}")
        message(STATUS "Show Control: bundle ffmpeg from ${_showcontrol_ffmpeg_system}")
    endif()
endif()

if(_showcontrol_ffmpeg_src)
  if(WIN32)
    set(_showcontrol_ffmpeg_dest_name "ffmpeg.exe")
  else()
    set(_showcontrol_ffmpeg_dest_name "ffmpeg")
  endif()
  if(WIN32)
    add_custom_command(TARGET Show_Control POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${_showcontrol_ffmpeg_src}"
        "$<TARGET_FILE_DIR:Show_Control>/${_showcontrol_ffmpeg_dest_name}"
      COMMENT "Bundle ${_showcontrol_ffmpeg_dest_name} into app")
  else()
    add_custom_command(TARGET Show_Control POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${_showcontrol_ffmpeg_src}"
        "$<TARGET_FILE_DIR:Show_Control>/${_showcontrol_ffmpeg_dest_name}"
      COMMAND chmod +x "$<TARGET_FILE_DIR:Show_Control>/${_showcontrol_ffmpeg_dest_name}"
      COMMENT "Bundle ${_showcontrol_ffmpeg_dest_name} into app")
  endif()
endif()

# compile_commands.json được CMake ghi ở bước *generate*, không phải configure —
# copy lúc configure dễ ghi đè root bằng file 0 byte → clangd báo juce_* not found.
if(CMAKE_EXPORT_COMPILE_COMMANDS)
    find_package(Python3 COMPONENTS Interpreter QUIET)

    set(_sync_ide_db_script "${CMAKE_CURRENT_SOURCE_DIR}/scripts/sync_ide_compile_db.cmake")
    set(_sync_ide_db_args
        -DCC_SRC="${CMAKE_BINARY_DIR}/compile_commands.json"
        -DCC_DST="${CMAKE_CURRENT_SOURCE_DIR}/compile_commands.json"
        -DFLAGS_SCRIPT="${CMAKE_CURRENT_SOURCE_DIR}/scripts/generate_compile_flags.py"
        -DFLAGS_OUT="${CMAKE_CURRENT_SOURCE_DIR}/compile_flags.txt"
        -DPython3_EXECUTABLE="${Python3_EXECUTABLE}"
        -P "${_sync_ide_db_script}")

    add_custom_target(sync_clangd_db
        COMMAND ${CMAKE_COMMAND} ${_sync_ide_db_args}
        COMMENT "Sync compile_commands.json + compile_flags.txt for clangd")

    add_custom_command(
        TARGET Show_Control POST_BUILD
        COMMAND ${CMAKE_COMMAND} ${_sync_ide_db_args}
        COMMENT "Sync compile_commands.json + compile_flags.txt for clangd")

    # Sau bước generate, copy DB nếu đã có (tránh root 0 byte → hàng trăm lỗi clangd).
    if(EXISTS "${CMAKE_BINARY_DIR}/compile_commands.json")
        file(SIZE "${CMAKE_BINARY_DIR}/compile_commands.json" _cc_len)
        if(_cc_len GREATER 64)
            execute_process(COMMAND ${CMAKE_COMMAND} ${_sync_ide_db_args})
        endif()
    endif()
endif()