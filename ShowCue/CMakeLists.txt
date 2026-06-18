cmake_minimum_required(VERSION 3.15)

# ShowCue — trình phát sự kiện cue-based (JUCE 8)
project(ShowCue VERSION 1.0.0 LANGUAGES C CXX)

if(NOT DEFINED CMAKE_EXPORT_COMPILE_COMMANDS)
    set(CMAKE_EXPORT_COMPILE_COMMANDS ON CACHE BOOL "Export compile_commands.json for clangd")
endif()

# Chỉ đường dẫn cho VS Code tìm thấy bộ thư viện JUCE nằm bên cạnh
add_subdirectory(JUCE)

# Khởi tạo cấu hình ứng dụng Âm thanh (Audio Application)
# ICON_BIG / ICON_SMALL: JUCE tự tạo .icns (macOS) và .ico (Windows) qua juceaide.
# PNG 1024×1024 cho macOS; ICO riêng cho Windows khi cross-build.
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/Resources/InfoPlistExtra.plist" SHOWCUE_PLIST_EXTRA)

juce_add_gui_app(ShowCue
    PRODUCT_NAME "ShowCue"
    VERSION 1.0.0
    COMPANY_NAME "DGP Corp"
    BUNDLE_ID "com.dgpco.showcue"
    ICON_BIG   "${CMAKE_CURRENT_SOURCE_DIR}/Resources/AppIcon.png"
    ICON_SMALL "${CMAKE_CURRENT_SOURCE_DIR}/Resources/AppIcon.ico"
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "ShowCue cần truy cập thiết bị âm thanh để kết nối với Soundcard/Mixer sân khấu của bạn."
    LOCAL_NETWORK_PERMISSION_ENABLED TRUE
    LOCAL_NETWORK_PERMISSION_TEXT "ShowCue cần Mạng cục bộ (LAN/Wi‑Fi) để quét máy Primary/Backup và đồng bộ show."
    PLIST_TO_MERGE "${SHOWCUE_PLIST_EXTRA}"
    IS_AUDIO_APPLICATION TRUE)

# Chỉ khai báo translation units (.cpp/.mm) — header được kéo qua #include.
target_sources(ShowCue PRIVATE
    Source/Main.cpp
    Source/ShowTypography.cpp
    Source/ShowControlLookAndFeel.cpp
    Source/MainComponent.cpp
    Source/StageMonitorComponent.cpp
    Source/CueListPanel.cpp
    Source/AudioEngine.cpp
    Source/ShowUpdateChecker.cpp
    Source/SystemPermissionsPanel.cpp
    Source/PadPanel.cpp
    )

# Nhúng font Roboto (Unicode tiếng Việt) — đồng bộ macOS + Windows LTSC, không phụ thuộc font hệ thống.
juce_add_binary_data (ShowCueAssets SOURCES
    Resources/Fonts/Roboto-Regular.ttf
    Resources/Fonts/Roboto-Bold.ttf)

if (APPLE)
    target_sources(ShowCue PRIVATE Source/ShowControlMacWindow.mm
                                 Source/ShowBackupMacNetworkBonjour.mm
                                 Source/ShowDisplaySleepPreventerMac.mm)

    target_link_libraries(ShowCue PRIVATE "-framework IOKit")

    set(_showcontrol_app_icon_png "${CMAKE_CURRENT_SOURCE_DIR}/Resources/AppIcon.png")
    if(EXISTS "${_showcontrol_app_icon_png}")
        target_sources(ShowCue PRIVATE "${_showcontrol_app_icon_png}")
        set_source_files_properties("${_showcontrol_app_icon_png}"
            PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    endif()

    set(_showcue_donate_qr_png "${CMAKE_CURRENT_SOURCE_DIR}/Resources/DonateQR.png")
    if(EXISTS "${_showcue_donate_qr_png}")
        target_sources(ShowCue PRIVATE "${_showcue_donate_qr_png}")
        set_source_files_properties("${_showcue_donate_qr_png}"
            PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    endif()
endif()

if(WIN32)
    target_sources(ShowCue PRIVATE Source/ShowBackupLanDiscoveryWin.cpp
                                 Source/ShowDisplaySleepPreventerWin.cpp)

    target_link_libraries(ShowCue PRIVATE iphlpapi)

    set(_showcue_resources_dir "${CMAKE_CURRENT_SOURCE_DIR}/Resources")
    foreach(_runtime_asset IN ITEMS AppIcon.png DonateQR.png)
        set(_runtime_src "${_showcue_resources_dir}/${_runtime_asset}")
        if(EXISTS "${_runtime_src}")
            add_custom_command(TARGET ShowCue POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:ShowCue>/Resources"
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${_runtime_src}"
                    "$<TARGET_FILE_DIR:ShowCue>/Resources/${_runtime_asset}"
                COMMENT "Bundle Resources/${_runtime_asset} for Windows")
        endif()
    endforeach()
elseif(NOT APPLE)
    target_sources(ShowCue PRIVATE Source/ShowDisplaySleepPreventerStub.cpp)
endif()

# Nạp tất cả các module xử lý Mixer và driver phần cứng âm thanh
target_link_libraries(ShowCue PRIVATE
    ShowCueAssets
    juce::juce_audio_utils
    juce::juce_audio_devices
    juce::juce_audio_formats
    juce::juce_audio_basics
    juce::juce_dsp
    juce::juce_gui_extra
    juce::juce_gui_basics
    juce::juce_graphics
    juce::juce_data_structures
    juce::juce_osc
    juce::juce_core)

# Tự động tạo file Header hệ thống
juce_generate_juce_header(ShowCue)

# --- P4: target tùy chọn cho clangd (không link vào app, EXCLUDE_FROM_ALL) ---
option(SHOWCUE_BUILD_IDE_INDEX "Build ShowCueIDEIndex for clangd header indexing" ON)

if(SHOWCUE_BUILD_IDE_INDEX)
    add_library(ShowCueIDEIndex STATIC EXCLUDE_FROM_ALL
        Source/ShowControlIDEIndex.cpp)

    set_target_properties(ShowCueIDEIndex PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF)

    target_include_directories(ShowCueIDEIndex PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/Source"
        "${CMAKE_CURRENT_BINARY_DIR}/ShowCue_artefacts/JuceLibraryCode")

    target_link_libraries(ShowCueIDEIndex PRIVATE
        ShowCueAssets
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

    add_dependencies(ShowCueIDEIndex ShowCue)

    add_custom_target(showcue_ide_index
        DEPENDS ShowCueIDEIndex sync_clangd_db
        COMMENT "Build optional TU + sync compile_commands for clangd")
endif()

# MSVC: đọc nguồn UTF-8 đúng — u8"..." và chuỗi tiếng Việt không bị mojibake trên Windows.
if(MSVC)
    target_compile_options(ShowCue PRIVATE /utf-8)
endif()

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
        message(STATUS "ShowCue: bundle ffmpeg from ${_showcontrol_ffmpeg_system}")
    endif()
endif()

if(_showcontrol_ffmpeg_src)
  if(WIN32)
    set(_showcontrol_ffmpeg_dest_name "ffmpeg.exe")
  else()
    set(_showcontrol_ffmpeg_dest_name "ffmpeg")
  endif()
  if(WIN32)
    add_custom_command(TARGET ShowCue POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${_showcontrol_ffmpeg_src}"
        "$<TARGET_FILE_DIR:ShowCue>/${_showcontrol_ffmpeg_dest_name}"
      COMMENT "Bundle ${_showcontrol_ffmpeg_dest_name} into app")
  else()
    add_custom_command(TARGET ShowCue POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${_showcontrol_ffmpeg_src}"
        "$<TARGET_FILE_DIR:ShowCue>/${_showcontrol_ffmpeg_dest_name}"
      COMMAND chmod +x "$<TARGET_FILE_DIR:ShowCue>/${_showcontrol_ffmpeg_dest_name}"
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

    if(SHOWCUE_BUILD_IDE_INDEX)
        add_dependencies(sync_clangd_db ShowCueIDEIndex)
    endif()

    add_custom_command(
        TARGET ShowCue POST_BUILD
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

# --- Phase A: unit tests (ctest / CI) ---
option(SHOWCUE_BUILD_TESTS "Build ShowCue unit tests" ON)

if(SHOWCUE_BUILD_TESTS)
    add_subdirectory(Tests)
endif()