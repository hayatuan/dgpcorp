#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "ShowKeyboardInput.h"

/** Ma trận Farrago cố định 8 hàng × 10 cột — hotkey theo tọa độ ô. */
namespace showcontrol::padgrid
{
inline constexpr int kRows = 8;
inline constexpr int kCols = 10;
inline constexpr int kMaxCells = kRows * kCols;
/** Giới hạn khóa cứng tối đa ô PAD hiển thị trên bàn cờ (8×10). */
inline constexpr int kMaxPadsAllowed = kMaxCells;
inline constexpr int kMinBoundedCols = 3;
inline constexpr int kMinBoundedRows = 3;
/** Tỷ lệ chiều cao/chiều rộng ô PAD landscape Farrago (hình chữ nhật ngang). */
inline constexpr float kFarragoLandscapeAspect = 0.62f;
/** Tỷ lệ ngang/dọc lý tưởng (width / height) — landscape box Farrago. */
inline constexpr float kFarragoLandscapeWidthOverHeight = 1.62f;

inline void computeBoundedLandscapeCellSize (int panelW,
                                             int panelH,
                                             int activeCols,
                                             int activeRows,
                                             int& outCellW,
                                             int& outCellH) noexcept
{
    const int maxCellW = juce::jmax (1, panelW / juce::jmax (1, activeCols));
    const int maxCellH = juce::jmax (1, panelH / juce::jmax (1, activeRows));

    outCellW = maxCellW;
    outCellH = juce::jmax (1, juce::roundToInt ((float) outCellW / kFarragoLandscapeWidthOverHeight));

    if (outCellH * activeRows > panelH)
    {
        outCellH = maxCellH;
        outCellW = juce::jmax (1, juce::roundToInt ((float) outCellH * kFarragoLandscapeWidthOverHeight));
    }
}

inline int landscapeCellHeight (int cellW, int viewportDerivedCellH) noexcept
{
    const int ideal = juce::jmax (1, juce::roundToInt ((float) cellW * kFarragoLandscapeAspect));
    return ideal < viewportDerivedCellH ? ideal : juce::jmax (1, viewportDerivedCellH);
}

inline juce::Rectangle<int> boundedCellBounds (int cellW,
                                                 int cellH,
                                                 int row,
                                                 int col,
                                                 int inset = 3) noexcept
{
    return { col * cellW + inset,
             row * cellH + inset,
             juce::jmax (1, cellW - inset * 2),
             juce::jmax (1, cellH - inset * 2) };
}

inline juce::Point<int> boundedGridCellAtLocalPoint (juce::Point<int> local,
                                                     int cellW,
                                                     int cellH,
                                                     int activeCols,
                                                     int activeRows) noexcept
{
    if (cellW <= 0 || cellH <= 0 || activeCols <= 0 || activeRows <= 0)
        return { 0, 0 };

    return { juce::jlimit (0, activeCols - 1, local.x / cellW),
             juce::jlimit (0, activeRows - 1, local.y / cellH) };
}

struct GridHotkey
{
    juce::String label;
    juce::KeyPress keyPress;
};

inline juce::juce_wchar baseKeyAt (int rowWithin4, int col) noexcept
{
    static const char* kRows[4] = {
        "1234567890",
        "QWERTYUIOP",
        "ASDFGHJKL;",
        "ZXCVBNM,./"
    };

    if (rowWithin4 < 0 || rowWithin4 > 3 || col < 0 || col >= kCols)
        return 0;

    return (juce::juce_wchar) kRows[rowWithin4][col];
}

inline GridHotkey hotkeyForCell (int row, int col) noexcept
{
    GridHotkey result;
    row = juce::jlimit (0, kRows - 1, row);
    col = juce::jlimit (0, kCols - 1, col);

    const int baseRow = row % 4;
    const auto ch = baseKeyAt (baseRow, col);

    if (ch == 0)
        return result;

    if (row < 4)
    {
        result.label    = juce::String::charToString (ch);
        result.keyPress = juce::KeyPress ((int) ch);
    }
    else
    {
        result.label    = showcontrol::keyboard::formatPadMatrixAltShortcut (ch);
        result.keyPress = juce::KeyPress ((int) ch, juce::ModifierKeys::altModifier, 0);
    }

    return result;
}

inline int linearSlotFromGrid (int row, int col) noexcept
{
    return juce::jlimit (0, kMaxCells - 1, row * kCols + col);
}

inline juce::Point<int> gridFromLinearSlot (int slot) noexcept
{
    slot = juce::jlimit (0, kMaxCells - 1, slot);
    return { slot % kCols, slot / kCols };
}

inline bool isValidGridCell (int row, int col) noexcept
{
    return row >= 0 && row < kRows && col >= 0 && col < kCols;
}

inline juce::Point<int> gridCellAtLocalPoint (juce::Point<int> local,
                                              int cellW,
                                              int cellH,
                                              int activeCols,
                                              int activeRows) noexcept
{
    if (cellW <= 0 || cellH <= 0 || activeCols <= 0 || activeRows <= 0)
        return { 0, 0 };

    return boundedGridCellAtLocalPoint (local, cellW, cellH, activeCols, activeRows);
}

inline juce::Rectangle<int> cellBounds (int panelWidth,
                                        int panelHeight,
                                        int activeRows,
                                        int activeCols,
                                        int row,
                                        int col,
                                        int inset = 3) noexcept
{
    const int cellW = juce::jmax (1, panelWidth / juce::jmax (1, activeCols));
    const int cellH = juce::jmax (1, panelHeight / juce::jmax (1, activeRows));
    return boundedCellBounds (cellW, cellH, row, col, inset);
}

inline int normalizeMatrixKeyCode (int keyCode) noexcept
{
    if (keyCode >= juce::KeyPress::numberPad0 && keyCode <= juce::KeyPress::numberPad9)
        return (int) '0' + (keyCode - juce::KeyPress::numberPad0);

    if (keyCode >= (int) 'a' && keyCode <= (int) 'z')
        return (int) juce::CharacterFunctions::toUpperCase ((juce::juce_wchar) keyCode);

    return keyCode;
}

inline bool isMatrixPhysicalKeyCode (int keyCode) noexcept
{
    const int normalized = normalizeMatrixKeyCode (keyCode);

    for (int row = 0; row < 4; ++row)
        for (int col = 0; col < kCols; ++col)
            if (baseKeyAt (row, col) == (juce::juce_wchar) normalized)
                return true;

    return false;
}

/** Dịch ngược mã phím vật lý (+ Option ⌥) → tọa độ ma trận { col, row }. */
inline juce::Point<int> gridCellForPhysicalKey (int keyCode, bool optionModifier) noexcept
{
    const int normalized = normalizeMatrixKeyCode (keyCode);

    for (int baseRow = 0; baseRow < 4; ++baseRow)
    {
        for (int col = 0; col < kCols; ++col)
        {
            if (baseKeyAt (baseRow, col) != (juce::juce_wchar) normalized)
                continue;

            const int row = baseRow + (optionModifier ? 4 : 0);
            return { col, row };
        }
    }

    return { -1, -1 };
}

} // namespace showcontrol::padgrid
