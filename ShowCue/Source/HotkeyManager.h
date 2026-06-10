#pragma once
#include <atomic>
#include <juce_core/juce_core.h>
#include "SoundPad.h"

//==============================================================================
// HotkeyManager: Quản lý phím tắt toàn cục theo phong cách Farrago
// - Hỗ trợ gán phím tắt tùy chỉnh cho từng pad/cue
// - Hỗ trợ modifier keys (Cmd, Ctrl, Shift, Alt)
// - Hỗ trợ MIDI trigger (note on/off → trigger pad)
// - Lưu/tải cấu hình phím tắt vào XML
//==============================================================================

struct HotkeyBinding
{
    int         padListIndex = -1;   // Index của danh sách
    int         padIndex     = -1;   // Index của pad trong danh sách
    juce::KeyPress keyPress;         // Phím tắt keyboard
    int         midiNote     = -1;   // MIDI note (-1 = không dùng MIDI)
    int         midiChannel  = 0;    // MIDI channel (0 = all channels)
    juce::String description;        // Mô tả ngắn
};

class HotkeyManager
{
public:
    HotkeyManager() = default;
    ~HotkeyManager() = default;

    // Thêm binding mới
    void addBinding (const HotkeyBinding& binding)
    {
        bindings.add (binding);
    }

    // Xóa binding theo index
    void removeBinding (int index)
    {
        if (index >= 0 && index < bindings.size())
            bindings.remove (index);
    }

    // Xóa tất cả binding của một pad (padIndex < 0 → xóa mọi pad trong list)
    void removeBindingsForPad (int listIndex, int padIndex)
    {
        for (int i = bindings.size() - 1; i >= 0; --i)
        {
            const auto& b = bindings[i];

            if (b.padListIndex == listIndex && (padIndex < 0 || b.padIndex == padIndex))
                bindings.remove (i);
        }
    }

    // Tìm binding theo KeyPress
    const HotkeyBinding* findByKeyPress (const juce::KeyPress& key) const
    {
        for (auto& b : bindings)
            if (b.keyPress == key)
                return &b;
        return nullptr;
    }

    // Global mode: ưu tiên list đang active; fallback match đầu tiên còn lại.
    const HotkeyBinding* findByKeyPressPreferList (const juce::KeyPress& key, int preferredListIndex) const
    {
        const HotkeyBinding* fallback = nullptr;

        for (auto& b : bindings)
        {
            if (b.keyPress != key)
                continue;

            if (b.padListIndex == preferredListIndex)
                return &b;

            if (fallback == nullptr)
                fallback = &b;
        }

        return fallback;
    }

    // Tìm binding theo KeyPress trong đúng list đang active.
    const HotkeyBinding* findByKeyPressInList (const juce::KeyPress& key, int listIndex) const
    {
        for (auto& b : bindings)
            if (b.padListIndex == listIndex && b.keyPress == key)
                return &b;
        return nullptr;
    }

    // Tìm binding theo MIDI note + channel
    const HotkeyBinding* findByMidi (int note, int channel) const
    {
        for (auto& b : bindings)
        {
            if (b.midiNote == note && (b.midiChannel == 0 || b.midiChannel == channel))
                return &b;
        }
        return nullptr;
    }

    // Global mode: ưu tiên list đang active; fallback match đầu tiên còn lại.
    const HotkeyBinding* findByMidiPreferList (int note, int channel, int preferredListIndex) const
    {
        const HotkeyBinding* fallback = nullptr;

        for (auto& b : bindings)
        {
            if (b.midiNote != note || (b.midiChannel != 0 && b.midiChannel != channel))
                continue;

            if (b.padListIndex == preferredListIndex)
                return &b;

            if (fallback == nullptr)
                fallback = &b;
        }

        return fallback;
    }

    // Tìm MIDI binding trong đúng list đang active.
    const HotkeyBinding* findByMidiInList (int note, int channel, int listIndex) const
    {
        for (auto& b : bindings)
        {
            if (b.padListIndex != listIndex)
                continue;

            if (b.midiNote == note && (b.midiChannel == 0 || b.midiChannel == channel))
                return &b;
        }
        return nullptr;
    }

    int getBindingCount() const { return bindings.size(); }
    const HotkeyBinding& getBinding (int index) const { return bindings.getReference (index); }

    // Tránh tạo trùng binding khi bổ sung theo kiểu "ensure" sau khi pad load xong.
    bool hasBinding (int listIndex, int padIndex, const juce::KeyPress& key) const
    {
        for (auto& b : bindings)
        {
            if (b.padListIndex == listIndex && b.padIndex == padIndex && b.keyPress == key)
                return true;
        }
        return false;
    }

    bool hasKeyboardBindingForPad (int listIndex, int padIndex) const
    {
        for (auto& b : bindings)
            if (b.padListIndex == listIndex && b.padIndex == padIndex)
                return true;
        return false;
    }

    const HotkeyBinding* findKeyboardBindingForPad (int listIndex, int padIndex) const
    {
        for (auto& b : bindings)
            if (b.padListIndex == listIndex && b.padIndex == padIndex)
                return &b;
        return nullptr;
    }

    /** Kiểm tra phím đã gán cho pad khác (theo scope Active list / Global). */
    const HotkeyBinding* findConflictingKey (const juce::KeyPress& key,
                                             bool globalScope,
                                             int activeListIndex,
                                             int exceptListIndex,
                                             int exceptPadIndex) const
    {
        for (auto& b : bindings)
        {
            if (b.keyPress != key)
                continue;

            if (b.padListIndex == exceptListIndex && b.padIndex == exceptPadIndex)
                continue;

            if (globalScope)
                return &b;

            if (b.padListIndex == activeListIndex)
                return &b;
        }
        return nullptr;
    }

    /** Gán / thay phím keyboard cho pad; trả false nếu trùng. */
    bool assignKeyboardToPad (const juce::KeyPress& key,
                              int listIndex,
                              int padIndex,
                              const juce::String& description,
                              bool globalScope,
                              int activeListIndex,
                              juce::String* conflictMessage = nullptr)
    {
        if (key.getKeyCode() == 0)
            return false;

        if (const auto* conflict = findConflictingKey (key, globalScope, activeListIndex, listIndex, padIndex))
        {
            if (conflictMessage != nullptr)
            {
                *conflictMessage = juce::String::fromUTF8 (u8"Phím ")
                                 + describeKeyPress (key)
                                 + juce::String::fromUTF8 (u8" đã gán cho: ")
                                 + conflict->description;
            }
            return false;
        }

        removeBindingsForPad (listIndex, padIndex);

        HotkeyBinding binding;
        binding.padListIndex = listIndex;
        binding.padIndex     = padIndex;
        binding.keyPress     = key;
        binding.description  = description;
        bindings.add (binding);
        return true;
    }

    void clearKeyboardBindingForPad (int listIndex, int padIndex)
    {
        removeBindingsForPad (listIndex, padIndex);
    }

    /** Ma trận phím mặc định Farrago (48 phím). */
    static juce::KeyPress keyPressForMatrixIndex (int index)
    {
        const juce::String keyMatrix = "1234567890QWERTYUIOPASDFGHJKL;ZXCVBNM,.";
        if (index >= 0 && index < keyMatrix.length())
            return juce::KeyPress (keyMatrix[index]);
        const int fIdx = index - (int) keyMatrix.length();
        if (fIdx >= 0 && fIdx < 8)
            return juce::KeyPress (juce::KeyPress::F1Key + fIdx);
        return {};
    }

    static juce::String matrixKeyLabelForIndex (int index)
    {
        const juce::String keyMatrix = "1234567890QWERTYUIOPASDFGHJKL;ZXCVBNM,.";
        if (index >= 0 && index < keyMatrix.length())
            return juce::String::charToString (keyMatrix[index]);
        const int fIdx = index - (int) keyMatrix.length();
        if (fIdx >= 0 && fIdx < 8)
            return "F" + juce::String (fIdx + 1);
        return {};
    }

    // Lưu cấu hình vào XML element
    void saveToXml (juce::XmlElement& parent) const
    {
        auto* hotkeysElem = parent.createNewChildElement ("Hotkeys");
        for (auto& b : bindings)
        {
            auto* elem = hotkeysElem->createNewChildElement ("Binding");
            elem->setAttribute ("listIndex",   b.padListIndex);
            elem->setAttribute ("padIndex",    b.padIndex);
            elem->setAttribute ("keyCode",     b.keyPress.getKeyCode());
            elem->setAttribute ("modifiers",   b.keyPress.getModifiers().getRawFlags());
            elem->setAttribute ("midiNote",    b.midiNote);
            elem->setAttribute ("midiChannel", b.midiChannel);
            elem->setAttribute ("description", b.description);
        }
    }

    // Tải cấu hình từ XML element
    void loadFromXml (const juce::XmlElement& parent)
    {
        bindings.clear();
        if (auto* hotkeysElem = parent.getChildByName ("Hotkeys"))
        {
            for (auto* elem : hotkeysElem->getChildIterator())
            {
                if (elem->hasTagName ("Binding"))
                {
                    HotkeyBinding b;
                    b.padListIndex = elem->getIntAttribute ("listIndex", -1);
                    b.padIndex     = elem->getIntAttribute ("padIndex",  -1);
                    int keyCode    = elem->getIntAttribute ("keyCode",   0);
                    int modFlags   = elem->getIntAttribute ("modifiers", 0);
                    b.keyPress     = juce::KeyPress (keyCode, juce::ModifierKeys (modFlags), 0);
                    b.midiNote     = elem->getIntAttribute ("midiNote",    -1);
                    b.midiChannel  = elem->getIntAttribute ("midiChannel",  0);
                    b.description  = elem->getStringAttribute ("description", "");
                    bindings.add (b);
                }
            }
        }
    }

    // Tạo mô tả chuỗi cho binding (hiển thị trong UI)
    /** Phím điều khiển phát/dừng/fade — debounce để chặn macOS/JUCE gửi nhiều KeyDown trong một nhịp. */
    static bool isPlaybackTransportKeyCode (int keyCode) noexcept
    {
        return keyCode == juce::KeyPress::spaceKey
            || keyCode == juce::KeyPress::returnKey
            || keyCode == juce::KeyPress::escapeKey
            || keyCode == (int) 's'
            || keyCode == (int) 'S'
            || keyCode == (int) 'n'
            || keyCode == (int) 'N';
    }

    /** Matrix / số list / transport (không gồm mũi tên — mũi tên bubble khi không có binding). */
    static bool isManagedApplicationKeyCode (int keyCode) noexcept
    {
        if (keyCode == juce::KeyPress::spaceKey)
            return true;

        if (keyCode == juce::KeyPress::returnKey
            || keyCode == juce::KeyPress::escapeKey
            || keyCode == (int) 's'
            || keyCode == (int) 'S'
            || keyCode == (int) 'n'
            || keyCode == (int) 'N')
            return true;

        if (keyCode >= (int) '1' && keyCode <= (int) '9')
            return true;

        if (keyCode >= juce::KeyPress::numberPad0 && keyCode <= juce::KeyPress::numberPad9)
            return true;

        static const juce::String matrixKeys = "1234567890QWERTYUIOPASDFGHJKL;ZXCVBNM,.";
        for (int i = 0; i < matrixKeys.length(); ++i)
        {
            if (keyCode == (int) matrixKeys[i])
                return true;
        }

        for (int f = 0; f < 8; ++f)
        {
            if (keyCode == juce::KeyPress::F1Key + f)
                return true;
        }

        return false;
    }

    /** Mũi tên (kể cả mã macOS 0xF700…) — chỉ nuốt khi có binding; không thì bubble xuống con. */
    static bool isArrowNavigationKeyCode (int keyCode) noexcept
    {
        if (keyCode >= juce::KeyPress::upKey && keyCode <= juce::KeyPress::rightKey)
            return true;

        if (keyCode >= 0xf700 && keyCode <= 0xf707)
            return true;

        return false;
    }

    /** Chuẩn hóa mã mũi tên macOS (63232 = 0xF700…) về KeyPress::upKey… để so sánh nhất quán. */
    static int normalizeArrowKeyCode (int keyCode) noexcept
    {
        switch (keyCode)
        {
            case 0xf700: return juce::KeyPress::upKey;
            case 0xf701: return juce::KeyPress::downKey;
            case 0xf702: return juce::KeyPress::leftKey;
            case 0xf703: return juce::KeyPress::rightKey;
            default: break;
        }

        return keyCode;
    }

    struct KeyPressGateResult
    {
        bool isDuplicate   = false;
        bool shouldExecute = false;
    };

    /**
     * Debounce KeyDown (message thread). Không reset khi nhả phím.
     * shouldExecute=false khi keyCode==0 hoặc trùng trong cửa sổ debounce.
     */
    KeyPressGateResult evaluateKeyPressGate (int keyCode, juce::uint32 nowMs) noexcept
    {
        KeyPressGateResult result;

        if (keyCode == 0)
            return result;

        const juce::ScopedLock lock (keyDebounceLock);

        if (keyCode == lastKeyCode
            && nowMs - lastKeyPressTime < kKeyDebounceMs)
        {
            result.isDuplicate = true;
            return result;
        }

        lastKeyCode        = keyCode;
        lastKeyPressTime   = nowMs;
        result.shouldExecute = true;
        return result;
    }

    /** Nhả phím: không reset lastKeyPressTime (debounce chỉ dựa timestamp KeyDown). */
    void notifyKeyReleased (int keyCode) noexcept
    {
        const juce::ScopedLock lock (keyDebounceLock);
        juce::ignoreUnused (keyCode);
    }

    static juce::String describeKeyPress (const juce::KeyPress& key)
    {
        juce::String desc;
        auto mods = key.getModifiers();
        #if JUCE_MAC
            if (mods.isCommandDown())  desc += juce::String::fromUTF8 (u8"⌘");
            if (mods.isCtrlDown())     desc += juce::String::fromUTF8 (u8"⌃");
            if (mods.isAltDown())      desc += juce::String::fromUTF8 (u8"⌥");
            if (mods.isShiftDown())    desc += juce::String::fromUTF8 (u8"⇧");
        #else
            if (mods.isCommandDown())  desc += "Cmd+";
            if (mods.isCtrlDown())     desc += "Ctrl+";
            if (mods.isAltDown())      desc += "Alt+";
            if (mods.isShiftDown())    desc += "Shift+";
        #endif
        desc += key.getTextDescription().toUpperCase();
        return desc;
    }

private:
    static constexpr juce::uint32 kKeyDebounceMs = 250;

    juce::CriticalSection keyDebounceLock;
    int           lastKeyCode      = 0;
    juce::uint32  lastKeyPressTime = 0;

    juce::Array<HotkeyBinding> bindings;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HotkeyManager)
};
