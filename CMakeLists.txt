cmake_minimum_required(VERSION 3.22)
project(DGPCorpRoot)
# KHÓA CHẶT BIÊN DỊCH UNIVERSAL BINARY CHO CẢ CHIP INTEL VÀ APPLE SILICON
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Build Universal Binary" FORCE)

# HẠ THẤP TRẦN ĐÒI HỎI SYSTEM ĐỂ CHẠY ĐƯỢC TỪ MACOS 11.0 (BIG SUR) TRỞ LÊN
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS Target" FORCE)

# Triển khai gọi thẳng vào thư mục ShowCue vừa đổi tên mới
add_subdirectory(ShowCue)
