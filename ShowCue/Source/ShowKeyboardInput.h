#pragma once
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_core/juce_core.h>

/** Raw keyCode — bất biến trước bộ gõ Telex/VNI (không dùng getTextCharacter). */
namespace showcontrol::keyboard
{
inline int physicalKeyCode (const juce::KeyPress& key) noexcept
{
    return key.getKeyCode();
}

inline bool isPhysicalDeleteOrBackspace (const juce::KeyPress& key) noexcept
{
    const int code = physicalKeyCode (key);
    return code == juce::KeyPress::deleteKey || code == juce::KeyPress::backspaceKey;
}

inline bool isAsciiLetterKeyCode (int keyCode) noexcept
{
    return (keyCode >= 'A' && keyCode <= 'Z')
        || (keyCode >= 'a' && keyCode <= 'z');
}

/** TextEditor / ComboBox (Tìm kiếm, Đổi tên, Inspector…) — kể cả con trong Label inline. */
inline bool isTextEntryComponent (const juce::Component* component) noexcept
{
    if (component == nullptr)
        return false;

    return dynamic_cast<const juce::TextEditor*> (component) != nullptr
        || dynamic_cast<const juce::ComboBox*> (component) != nullptr;
}

/** Focus Guard: ô nhập liệu đang chiếm bàn phím → khóa hotkey phát nhạc. */
inline bool isKeyboardFocusInTextInput() noexcept
{
    for (auto* walk = juce::Component::getCurrentlyFocusedComponent();
         walk != nullptr;
         walk = walk->getParentComponent())
    {
        if (isTextEntryComponent (walk))
            return true;
    }

    return false;
}

/** Hàng Q–P và 0–9 — luôn nuốt KeyDown trên macOS (chặn tiếng bíp hệ thống). */
inline bool isFarragoTopRowMatrixKeyCode (int keyCode) noexcept
{
    if (keyCode >= (int) '0' && keyCode <= (int) '9')
        return true;

    if (keyCode >= juce::KeyPress::numberPad0 && keyCode <= juce::KeyPress::numberPad9)
        return true;

    int upper = keyCode;

    if (keyCode >= (int) 'a' && keyCode <= (int) 'z')
        upper = (int) juce::CharacterFunctions::toUpperCase ((juce::juce_wchar) keyCode);

    return upper >= (int) 'Q' && upper <= (int) 'P';
}

/** Đảo ngược ký tự Telex/EVKey → mã phím vật lý Farrago (0 = không khớp hàng đầu/số). */
inline int resolveTelexAwareTopRowKeyCode (const juce::KeyPress& key) noexcept
{
    const int keyCode = physicalKeyCode (key);
    const juce::String textChar = juce::String::charToString (key.getTextCharacter()).toLowerCase();

    if (keyCode == (int) 'Q' || keyCode == (int) 'q' || textChar == "q")
        return (int) 'Q';

    if (keyCode == (int) 'W' || keyCode == (int) 'w' || textChar == "w" || textChar == juce::String::charToString (L'ư'))
        return (int) 'W';

    if (keyCode == (int) 'E' || keyCode == (int) 'e' || textChar == "e" || textChar == juce::String::charToString (L'ê'))
        return (int) 'E';

    if (keyCode == (int) 'R' || keyCode == (int) 'r' || textChar == "r")
        return (int) 'R';

    if (keyCode == (int) 'T' || keyCode == (int) 't' || textChar == "t")
        return (int) 'T';

    if (keyCode == (int) 'Y' || keyCode == (int) 'y' || textChar == "y")
        return (int) 'Y';

    if (keyCode == (int) 'U' || keyCode == (int) 'u' || textChar == "u")
        return (int) 'U';

    if (keyCode == (int) 'I' || keyCode == (int) 'i' || textChar == "i")
        return (int) 'I';

    if (keyCode == (int) 'O' || keyCode == (int) 'o' || textChar == "o"
        || textChar == juce::String::charToString (L'ô')
        || textChar == juce::String::charToString (L'ơ'))
        return (int) 'O';

    if (keyCode == (int) 'P' || keyCode == (int) 'p' || textChar == "p")
        return (int) 'P';

    if (! key.getModifiers().isCommandDown())
    {
        if (keyCode >= (int) '0' && keyCode <= (int) '9')
            return keyCode;

        if (keyCode >= juce::KeyPress::numberPad0 && keyCode <= juce::KeyPress::numberPad9)
            return keyCode;

        if (textChar.length() == 1)
        {
            const auto ch = textChar[0];

            if (ch >= '0' && ch <= '9')
                return (int) ch;
        }
    }

    return 0;
}

inline bool isUndoKeyPress (const juce::KeyPress& key) noexcept
{
    const int code = physicalKeyCode (key);
    return key.getModifiers().isCommandDown()
        && ! key.getModifiers().isShiftDown()
        && (code == (int) 'Z' || code == (int) 'z');
}

inline bool isRedoKeyPress (const juce::KeyPress& key) noexcept
{
    const int code = physicalKeyCode (key);
    return key.getModifiers().isCommandDown()
        && key.getModifiers().isShiftDown()
        && (code == (int) 'Z' || code == (int) 'z');
}

inline bool isUndoRedoKeyPress (const juce::KeyPress& key) noexcept
{
    return isUndoKeyPress (key) || isRedoKeyPress (key);
}

inline bool hasSystemModifierKeysDown() noexcept
{
    const auto mods = juce::ModifierKeys::getCurrentModifiers();
    return mods.isCommandDown() || mods.isCtrlDown() || mods.isAltDown();
}

/** Bọc thép: chặn hotkey phát nhạc khi đang gõ chữ hoặc giữ modifier. */
inline bool shouldBlockPlaybackHotkey() noexcept
{
    if (isKeyboardFocusInTextInput())
        return true;

    if (hasSystemModifierKeysDown())
        return true;

    return false;
}

inline bool keyPressMatchesRaw (const juce::KeyPress& a, const juce::KeyPress& b) noexcept
{
    if (a.getModifiers().getRawFlags() != b.getModifiers().getRawFlags())
        return false;

    const int codeA = a.getKeyCode();
    const int codeB = b.getKeyCode();

    if (codeA == codeB)
        return true;

    if (codeA > 0 && codeA < 256 && codeB > 0 && codeB < 256)
        return juce::CharacterFunctions::toLowerCase ((juce::juce_wchar) codeA)
            == juce::CharacterFunctions::toLowerCase ((juce::juce_wchar) codeB);

    return false;
}

inline juce::KeyPress normalizedForHotkeyMatch (const juce::KeyPress& key) noexcept
{
    return juce::KeyPress (key.getKeyCode(), key.getModifiers(), 0);
}

/** Cuốn chiếu phím tắt playlist sidebar: 1–9 → 0 → Q…M (tối đa 36 danh sách / phân khu). */
inline constexpr int kPlaylistHotkeyMapSize = 36;

/** Mac: ⌘ chuyển BGM grid, ^ chuyển Cue list. Windows: commandModifier ≡ ctrlModifier → Ctrl = grid, Alt = cue. */
inline bool playlistModifierTargetsGrid (const juce::ModifierKeys& mods) noexcept
{
   #if JUCE_MAC
    return mods.isCommandDown() && ! mods.isCtrlDown() && ! mods.isAltDown();
   #else
    return mods.isCtrlDown() && ! mods.isAltDown();
   #endif
}

inline bool playlistModifierTargetsCueList (const juce::ModifierKeys& mods) noexcept
{
   #if JUCE_MAC
    return mods.isCtrlDown() && ! mods.isCommandDown() && ! mods.isAltDown();
   #else
    return mods.isAltDown() && ! mods.isCtrlDown() && ! mods.isShiftDown();
   #endif
}

inline juce::String playlistHotkeyPrefix (bool isGridMode) noexcept
{
   #if JUCE_MAC
    return isGridMode ? juce::String::fromUTF8 (u8"⌘") : juce::String::fromUTF8 (u8"^");
   #else
    return isGridMode ? "Ctrl+" : "Alt+";
   #endif
}

inline juce::String getHotkeyCharForIndex (int index) noexcept
{
    static constexpr char hotkeyMap[] = { '1', '2', '3', '4', '5', '6', '7', '8', '9', '0',
                                          'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P',
                                          'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L',
                                          'Z', 'X', 'C', 'V', 'B', 'N', 'M' };

    if (index >= 0 && index < (int) sizeof (hotkeyMap))
        return juce::String::charToString (hotkeyMap[index]);

    return {};
}

/** Ánh xạ phím bấm (kể cả numpad) → chỉ số 0-based trong phân khu BGM/CUE; -1 nếu không khớp. */
inline int hotkeyIndexForKeyPress (const juce::KeyPress& key) noexcept
{
    const int code = physicalKeyCode (key);
    juce::String pressedChar;

    if (code >= (int) '0' && code <= (int) '9')
        pressedChar = juce::String::charToString ((juce::juce_wchar) code);
    else if (code >= juce::KeyPress::numberPad0 && code <= juce::KeyPress::numberPad9)
        pressedChar = juce::String::charToString ((juce::juce_wchar) ('0' + (code - juce::KeyPress::numberPad0)));
    else if (isAsciiLetterKeyCode (code))
        pressedChar = juce::String::charToString (juce::CharacterFunctions::toUpperCase ((juce::juce_wchar) code));
    else
        pressedChar = juce::String::charToString ((juce::juce_wchar) code).toUpperCase();

    for (int i = 0; i < kPlaylistHotkeyMapSize; ++i)
    {
        if (pressedChar == getHotkeyCharForIndex (i))
            return i;
    }

    return -1;
}

/** Ma trận Farrago đầy đủ — quét isKeyCurrentlyDown (bypass Telex textCharacter). */
inline void forEachFarragoMatrixKeyCode (const std::function<void (int)>& visit)
{
    static const juce::String matrix = "1234567890QWERTYUIOPASDFGHJKL;ZXCVBNM,.";
    for (int i = 0; i < matrix.length(); ++i)
        visit ((int) matrix[i]);

    for (int f = 0; f < 8; ++f)
        visit (juce::KeyPress::F1Key + f);
}

} // namespace showcontrol::keyboard
