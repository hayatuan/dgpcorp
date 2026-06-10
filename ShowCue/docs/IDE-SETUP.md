# Cấu hình IDE (Cursor / VS Code) — hết Problems giả

Problems đỏ thường do **clangd không có `compile_commands.json`** (file 0 byte hoặc chưa chạy CMake), không phải lỗi build thật.

## Lần đầu / sau khi xóa `build/`

```bash
./scripts/refresh-ide-db.sh
```

Hoặc:

```bash
cmake -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build -j
```

Sinh ra:

- `build/compile_commands.json` (bản gốc, sau bước **Generating** của CMake)
- `compile_commands.json` ở root (thường là symlink → `build/`)
- `compile_flags.txt` (fallback cho `.h` header-only)

**Lưu ý:** Không chạy `cmake` configure rồi bỏ qua bước generate — trước đây copy DB lúc configure có thể ghi đè file root **0 byte**. Giờ dùng target `sync_clangd_db` hoặc `./scripts/refresh-ide-db.sh`.

## Trong Cursor

1. Cài extension **clangd** (khuyến nghị), **tắt** Microsoft C/C++ IntelliSense (đã cấu hình trong `.vscode/settings.json`).
2. **Command Palette** → `clangd: Restart language server`.
3. Mở lại project nếu Problems vẫn cũ.

## Mỗi khi đổi `CMakeLists.txt` hoặc module JUCE

Chạy lại `cmake -B build` (hoặc build task **CMake: Configure**).

## Kiểm tra nhanh

```bash
wc -c build/compile_commands.json   # phải > 0 (thường ~50KB)
```

Nếu vẫn 0: xóa `build/CMakeCache.txt` rồi configure lại.
