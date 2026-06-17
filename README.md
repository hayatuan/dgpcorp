# ShowCue

Trình điều khiển cue âm thanh cho sân khấu / sự kiện — **ShowCue v1.0.0**.

- **Nền tảng:** macOS 11+ (Universal), Windows 10/11
- **Framework:** JUCE 8 (vendored trong `ShowCue/JUCE`)
- **Ngôn ngữ UI:** Tiếng Việt / English

## Build nhanh

```bash
# macOS
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --target ShowCue -j

# Windows (VS 2022 x64)
cmake -S . -B build-win -G "Visual Studio 17 2022" -A x64
cmake --build build-win --config Release --target ShowCue -j
```

Chi tiết: [ShowCue/docs/BUILD.md](ShowCue/docs/BUILD.md)

## Cấu trúc repo

```
ShowControl/
├── CMakeLists.txt          # Root — gọi ShowCue/
├── ShowCue/
│   ├── CMakeLists.txt
│   ├── JUCE/               # JUCE 8 (bắt buộc khi clone)
│   ├── Source/             # Mã nguồn app
│   ├── Resources/          # Icon, font Roboto
│   └── docs/               # BUILD, RELEASE notes
└── README.md
```

## License

Proprietary — DGP Corp / Hayatuan.
