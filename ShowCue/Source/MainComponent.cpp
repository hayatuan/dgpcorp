#include "MainComponent.h"
#include <algorithm>
#include <functional>
#include <memory>
#include "GlobalPreferencesDialog.h"
#include "ShowLocalization.h"
#include "ShowAboutDialog.h"
#include "ShowUpdateChecker.h"
#include "ShowWaveformCache.h"
#include "ShowAudioEditor.h"
#include "ShowControlMacWindow.h"
#include "ShowAppPreferences.h"
#include "ShowApplicationState.h"
#include "ShowProjectPersistence.h"
#include "ShowOscListener.h"
#include "ShowBackupSync.h"
#include "ShowBackupLanDiscovery.h"
#include <cstdlib>

namespace
{
juce::String importPackageErrorMessage (showcontrol::persistence::ImportPackageError error)
{
    using E = showcontrol::persistence::ImportPackageError;

    switch (error)
    {
        case E::fileNotFound:        return showcontrol::localization::tr (u8"Không tìm thấy file");
        case E::emptyPackage:        return showcontrol::localization::tr (u8"Gói cấu hình không hợp lệ hoặc rỗng");
        case E::missingEntries:      return showcontrol::localization::tr (u8"Thiếu manifest, config hoặc project trong gói");
        case E::wrongFormat:         return showcontrol::localization::tr (u8"Không phải gói cấu hình ShowCue");
        case E::unsupportedSchema:   return showcontrol::localization::tr (u8"Phiên bản gói cấu hình không được hỗ trợ");
        case E::invalidManifest:     return showcontrol::localization::tr (u8"manifest.json không hợp lệ");
        case E::invalidConfig:       return showcontrol::localization::tr (u8"config.json không hợp lệ");
        case E::invalidProject:      return showcontrol::localization::tr (u8"project.xml không hợp lệ");
        case E::none:
        default:                     return showcontrol::localization::tr (u8"File không hợp lệ.");
    }
}

class StateOperationScope
{
public:
    explicit StateOperationScope (std::atomic<bool>& flagIn)
        : flag (flagIn), entered (! flag.exchange (true, std::memory_order_acq_rel))
    {
    }

    bool enteredSuccessfully() const noexcept { return entered; }

    ~StateOperationScope()
    {
        if (entered)
            flag.store (false, std::memory_order_release);
    }

private:
    std::atomic<bool>& flag;
    bool entered = false;
};

class InterProcessUnlockScope
{
public:
    explicit InterProcessUnlockScope (juce::InterProcessLock& lockIn) : lock (lockIn) {}
    ~InterProcessUnlockScope() { lock.exit(); }

private:
    juce::InterProcessLock& lock;
};

#if JUCE_MAC
constexpr int kMacUnifiedTitleBarInset = 14;
#else
constexpr int kMacUnifiedTitleBarInset = 0;
#endif

/** JUCE 8 không có Desktop::sendLookAndFeelChange — quét mọi top-level window trên Desktop. */
void broadcastLookAndFeelToAllWindows()
{
    for (int i = 0; i < juce::Desktop::getInstance().getNumComponents(); ++i)
        if (auto* top = juce::Desktop::getInstance().getComponent (i))
            top->sendLookAndFeelChange();
}

#if JUCE_DEBUG
constexpr bool kEnableHotkeyTrace = true;
#else
constexpr bool kEnableHotkeyTrace = false;
#endif

void logHotkeyTrace (const juce::String& msg)
{
    if (kEnableHotkeyTrace)
        ErrorHandler::log ("[HOTKEY] " + msg, ErrorHandler::Severity::Info);
}

bool writeXmlAtomically (const juce::File& destination, const juce::XmlElement& xml)
{
    const auto tmpFile = destination.getSiblingFile (destination.getFileName() + ".tmp");
    tmpFile.deleteFile();

    if (! xml.writeTo (tmpFile))
        return false;

    if (tmpFile.replaceFileIn (destination))
        return true;

    if (destination.existsAsFile())
        destination.deleteFile();

    return tmpFile.moveFileTo (destination);
}

// ── Spacebar global debounce (file-scope, không nằm trong header)
// Msg thread only: chỉ dùng atomic timestamp + không ảnh hưởng RT audio thread.
static std::atomic<juce::uint32> g_lastGlobalSpacebarPressTime { 0 };
static std::atomic<juce::uint32> g_gateQuietUntilMs { 0 };
static constexpr juce::uint32 kGlobalSpacebarDebounceMs = 350;

/** Debounce Cmd+Delete / Cmd+Backspace — tránh kích đúp hộp thoại xóa. */
static int g_lastDeleteKeyCode = 0;
static juce::uint32 g_lastDeleteKeyMs = 0;
static constexpr juce::uint32 kDeleteKeyDebounceMs = 120;

bool shouldDebounceDeleteKey (int deleteKeyCode, juce::uint32 nowMs) noexcept
{
    if (deleteKeyCode == g_lastDeleteKeyCode
        && g_lastDeleteKeyMs != 0
        && (nowMs - g_lastDeleteKeyMs) < kDeleteKeyDebounceMs)
        return true;

    g_lastDeleteKeyCode = deleteKeyCode;
    g_lastDeleteKeyMs   = nowMs;
    return false;
}

/** JUCE 8 không expose isKeyRepeat — lọc burst KeyDown <45ms cùng keyCode (OS auto-repeat). */
bool swallowLikelyOsKeyRepeat (const juce::KeyPress& key) noexcept
{
    static int lastCode = 0;
    static juce::uint32 lastMs = 0;
    const int code = showcontrol::keyboard::physicalKeyCode (key);
    const auto now = juce::Time::getMillisecondCounter();
    const bool isRepeat = (code != 0 && code == lastCode && lastMs != 0 && (now - lastMs) < 45);
    lastCode = code;
    lastMs = now;
    return isRepeat;
}
} // namespace
#include "ConfirmDeleteDialog.h"
#include <limits>

namespace
{
    /** Giới hạn cứng ma trận PAD — chỉ áp dụng nhánh CUE (isGrid). BGM không giới hạn. */
    constexpr int kMaxCuePadsPerList = 80;

    void showCueListCapacityAlert()
    {
        juce::AlertWindow::showMessageBoxAsync (
            juce::AlertWindow::WarningIcon,
            "Giới hạn danh sách CUE",
            "Mỗi bộ Nhạc CUE chỉ cho phép chứa tối đa "
                + juce::String (kMaxCuePadsPerList)
                + " ô PAD biểu diễn để đảm bảo hiệu năng và bố cục hiển thị.",
            "Đã hiểu");
    }

    void showCueGridFullAlert()
    {
        juce::AlertWindow::showMessageBoxAsync (
            juce::AlertWindow::WarningIcon,
            juce::String::fromUTF8 (u8"Thông báo hệ thống"),
            juce::String::fromUTF8 (u8"Bàn cờ ShowCue đã đầy! Không còn vị trí trống để thực hiện lệnh nhân bản."),
            juce::String::fromUTF8 (u8"Xác nhận"));
    }

    void showCueBatchDuplicatePartialAlert (int successCount, int failCount)
    {
        juce::AlertWindow::showMessageBoxAsync (
            juce::AlertWindow::WarningIcon,
            juce::String::fromUTF8 (u8"Thông báo hệ thống"),
            juce::String::fromUTF8 (u8"Đã nhân bản thành công ")
                + juce::String (successCount)
                + juce::String::fromUTF8 (u8" ô PAD. Có ")
                + juce::String (failCount)
                + juce::String::fromUTF8 (u8" ô bị bỏ qua do bàn cờ đã đầy khít khao."),
            juce::String::fromUTF8 (u8"Xác nhận"));
    }

    /** Grid CUE động: chỉ đếm slot còn lại đến trần 48 (không ô trống cố định). */
    int maxCueIngestSlots (const juce::OwnedArray<SoundPad>& pads)
    {
        return juce::jmax (0, kMaxCuePadsPerList - pads.size());
    }

    bool cueListCannotAcceptAnyFile (const juce::OwnedArray<SoundPad>& pads)
    {
        return pads.size() >= kMaxCuePadsPerList;
    }
}

static void writeCueMetaToPadElem (juce::XmlElement& padElem, const CueItem& cue);
static void writePadProjectState (juce::XmlElement& padElem, const SoundPad& pad);
static void readCueMetaFromPadElem (const juce::XmlElement& padElem, CueItem& cue);
static SoundPad::PadProjectState readPadProjectState (const juce::XmlElement& padElem);

//==============================================================================
/** Timer pre-wait QLab — chỉ chạy trên message thread. */
class PendingCueGoTimer final : public juce::Timer
{
public:
    std::function<void()> onFire;

    void startMs (double delayMs)
    {
        startTimer (juce::jmax (1, (int) std::round (delayMs)));
    }

    void timerCallback() override
    {
        stopTimer();
        if (onFire)
            onFire();
    }

    ~PendingCueGoTimer() { stopTimer(); }
};

//==============================================================================
/** One-shot timer thuộc MainComponent — hủy an toàn qua shutdownActiveTimers(). */
class OneShotApplicationTimer final : public juce::Timer
{
public:
    std::function<void()> onFire;

    void startMs (int delayMs)
    {
        startTimer (juce::jmax (1, delayMs));
    }

    void timerCallback() override
    {
        stopTimer();
        if (onFire)
            onFire();
    }

    ~OneShotApplicationTimer() override
    {
        stopTimer();
    }
};

//==============================================================================
class MainComponent::PadReorderOverlay : public juce::Component,
                                         private juce::Timer
{
public:
    explicit PadReorderOverlay (MainComponent& ownerIn) : owner (ownerIn)
    {
        setInterceptsMouseClicks (false, false);
        setOpaque (false);
    }

    ~PadReorderOverlay() override { stopTimer(); }

    void visibilityChanged() override
    {
        if (isVisible())
            startTimerHz (60);
        else
            stopTimer();
    }

    void timerCallback() override
    {
        if (! owner.padReorderActive)
            return;

        owner.autoScrollViewportForPadReorder (owner.padReorderPointerPos);
        repaint();
    }

    void paint (juce::Graphics& g) override { owner.paintPadReorderOverlay (g); }

private:
    MainComponent& owner;
};

//==============================================================================
class MainComponent::EmptyProjectPlaceholder : public juce::Component
{
public:
    EmptyProjectPlaceholder() = default;

    void setDarkMode (bool dark)
    {
        isDarkMode = dark;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        const auto pal = ShowTheme::get (isDarkMode);
        g.fillAll (pal.centerBg);
        g.setColour (pal.textMuted);
        g.setFont (showcontrol::ui::emptyHintFont());
        g.drawFittedText (juce::String::fromUTF8 (u8"Không có kịch bản nào được mở. Bấm nút [+] ở góc dưới Sidebar để tạo mới."),
                          getLocalBounds().reduced (32),
                          juce::Justification::centred,
                          4);
    }

private:
    bool isDarkMode = true;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EmptyProjectPlaceholder)
};

class ListHeaderComponent : public juce::Component
{
public:
    ListHeaderComponent() : isDarkMode (true)
    {
        setOpaque (true);
        setInterceptsMouseClicks (false, false);
    }

    void setDarkMode (bool dark) { isDarkMode = dark; repaint(); }

    void paint (juce::Graphics& g) override
    {
        const auto pal = ShowTheme::get (isDarkMode);
        g.setColour (pal.panelElevated);
        g.fillRect (getLocalBounds());

        g.setColour (pal.border);
        g.drawHorizontalLine ((float) getHeight() - 1.0f, 0.0f, (float) getWidth());
        g.setColour (showcontrol::bgmList::playlistHeaderTextColour (isDarkMode));
        g.setFont (showcontrol::bgmList::playlistHeaderFont());

        const auto titleRect     = showcontrol::bgmList::titleBounds (getWidth(), getHeight());
        const auto remainingRect = showcontrol::bgmList::timeRemainingBounds (getWidth(), getHeight());
        const auto totalRect     = showcontrol::bgmList::totalDurationBounds (getWidth(), getHeight());

        g.drawText (showcontrol::localization::tr (u8"TÊN BÀI HÁT BGM"), titleRect, juce::Justification::centredLeft);
        showcontrol::bgmList::drawPlaylistTimeCell (g, showcontrol::localization::tr (u8"CÒN LẠI"), remainingRect);
        showcontrol::bgmList::drawPlaylistTimeCell (g, showcontrol::localization::tr (u8"THỜI LƯỢNG"), totalRect);
    }

private:
    bool isDarkMode;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ListHeaderComponent)
};

//==============================================================================
class MainComponent::SplitterHandle : public juce::Component
{
public:
    explicit SplitterHandle (bool isLeft) : isLeftHandle (isLeft) {}

    std::function<void(int)> onDragDelta;
    std::function<void()> onDragFinished;

    void setDarkMode (bool dark)
    {
        isDarkMode = dark;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        const auto pal = ShowTheme::get (isDarkMode);
        g.fillAll (pal.borderSubtle.withAlpha (isHovered || isDragging ? 0.62f : 0.30f));

        const int cx = getWidth() / 2;
        g.setColour (pal.textMuted.withAlpha (isHovered || isDragging ? 0.70f : 0.45f));
        g.drawVerticalLine (cx, 14.0f, (float) getHeight() - 14.0f);

        // Grip dots subtle kiểu Farrago
        const float dotR = 1.2f;
        const float cy = (float) getHeight() * 0.5f;
        for (int i = -2; i <= 2; ++i)
            g.fillEllipse ((float) cx - dotR, cy + (float) i * 5.0f - dotR, dotR * 2.0f, dotR * 2.0f);
    }

    void mouseEnter (const juce::MouseEvent&) override
    {
        isHovered = true;
        setMouseCursor (juce::MouseCursor::LeftRightResizeCursor);
        repaint();
    }

    void mouseExit (const juce::MouseEvent&) override
    {
        isHovered = false;
        if (! isDragging)
            setMouseCursor (juce::MouseCursor::NormalCursor);
        repaint();
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        isDragging = true;
        dragStartX = e.getScreenX();
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! isDragging || onDragDelta == nullptr)
            return;

        const int delta = e.getScreenX() - dragStartX;
        dragStartX = e.getScreenX();
        onDragDelta (isLeftHandle ? delta : -delta);
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        isDragging = false;
        repaint();

        if (onDragFinished)
            onDragFinished();
    }

private:
    bool isLeftHandle = true;
    bool isDarkMode = true;
    bool isHovered = false;
    bool isDragging = false;
    int dragStartX = 0;
};

//==============================================================================
class MainComponent::SplitterButtonLookAndFeel : public juce::LookAndFeel_V4
{
public:
    void setDarkMode (bool dark) { isDarkMode = dark; }

    void drawButtonBackground (juce::Graphics& g, juce::Button& button,
                               const juce::Colour& /*backgroundColour*/,
                               bool isMouseOverButton, bool isButtonDown) override
    {
        const auto pal = ShowTheme::get (isDarkMode);
        auto b = button.getLocalBounds().toFloat();
        const float r = b.getHeight() * 0.45f;

        juce::Colour fill = pal.panelElevated.withAlpha (0.92f);
        if (isMouseOverButton) fill = pal.panelElevated.brighter (0.05f);
        if (isButtonDown)      fill = pal.panelElevated.darker (0.05f);

        g.setColour (fill);
        g.fillRoundedRectangle (b, r);
        g.setColour (pal.border.withAlpha (0.75f));
        g.drawRoundedRectangle (b.reduced (0.5f), r, 1.0f);
    }

    void drawButtonText (juce::Graphics& g, juce::TextButton& button,
                         bool /*isMouseOverButton*/, bool /*isButtonDown*/) override
    {
        const auto pal = ShowTheme::get (isDarkMode);
        g.setColour (pal.textPrimary);
        g.setFont (ShowTheme::fontBold (13.5f));
        g.drawText (button.getButtonText(), button.getLocalBounds(), juce::Justification::centred);
    }

private:
    bool isDarkMode = true;
};

//==============================================================================
namespace
{
    void paintPadGridModeIcon (juce::Graphics& g, juce::Rectangle<float> area, juce::Colour colour)
    {
        constexpr int kGrid = 3;
        const float gap = 2.5f;
        const float cell = juce::jmin (area.getWidth(), area.getHeight()) / (float) kGrid - gap;
        const float ox = area.getCentreX() - (cell * (float) kGrid + gap * (float) (kGrid - 1)) * 0.5f;
        const float oy = area.getCentreY() - (cell * (float) kGrid + gap * (float) (kGrid - 1)) * 0.5f;

        g.setColour (colour);

        for (int row = 0; row < kGrid; ++row)
            for (int col = 0; col < kGrid; ++col)
            {
                const float x = ox + (float) col * (cell + gap);
                const float y = oy + (float) row * (cell + gap);
                g.fillRoundedRectangle (x, y, cell, cell, 1.6f);
            }
    }

    void paintCueListModeIcon (juce::Graphics& g, juce::Rectangle<float> area, juce::Colour colour)
    {
        const float lineH = 2.0f;
        const float bulletR = 2.2f;
        const float left = area.getX() + area.getWidth() * 0.18f;
        const float right = area.getRight() - area.getWidth() * 0.14f;
        const float midY = area.getCentreY();
        const float rowGap = area.getHeight() * 0.22f;

        g.setColour (colour);

        for (int i = -1; i <= 1; ++i)
        {
            const float y = midY + (float) i * rowGap;
            g.fillEllipse (left, y - bulletR, bulletR * 2.0f, bulletR * 2.0f);
            g.fillRect (left + bulletR * 2.0f + 5.0f, y - lineH * 0.5f, right - left - bulletR * 2.0f - 5.0f, lineH);
        }
    }
}

/** Nút phẳng cho Bottom Mode Selector — biểu tượng vector, active = nền mờ / viền accent. */
class MainComponent::PlayoutModeButtonLookAndFeel : public juce::LookAndFeel_V4
{
public:
    void setDarkMode (bool dark) noexcept { isDarkMode = dark; }

    void drawButtonBackground (juce::Graphics& g, juce::Button& button,
                               const juce::Colour& /*backgroundColour*/,
                               bool isMouseOverButton, bool isButtonDown) override
    {
        const auto pal = ShowTheme::get (isDarkMode);
        auto b = button.getLocalBounds().toFloat().reduced (1.0f);
        const float r = 7.0f;

        const bool isActive = button.getToggleState();

        if (isActive)
        {
            g.setColour (juce::Colours::white.withAlpha (0.15f));
            g.fillRoundedRectangle (b, r);
            g.setColour (pal.accent.withAlpha (0.80f));
            g.drawRoundedRectangle (b, r, 1.0f);
        }
        else if (isMouseOverButton || isButtonDown)
        {
            g.setColour (juce::Colours::white.withAlpha (isButtonDown ? 0.08f : 0.05f));
            g.fillRoundedRectangle (b, r);
        }
    }

    void drawButtonText (juce::Graphics& g, juce::TextButton& button,
                         bool isMouseOverButton, bool /*isButtonDown*/) override
    {
        const auto pal = ShowTheme::get (isDarkMode);
        const bool isActive = button.getToggleState();
        juce::Colour iconCol = isActive ? pal.textPrimary
                                        : pal.textSecondary.withAlpha (isMouseOverButton ? 0.92f : 0.62f);

        auto iconArea = button.getLocalBounds().toFloat().reduced (10.0f, 6.0f);

        if (button.getComponentID() == "playoutCueList")
            paintCueListModeIcon (g, iconArea, iconCol);
        else
            paintPadGridModeIcon (g, iconArea, iconCol);
    }

private:
    bool isDarkMode = true;
};

//==============================================================================
/** Bottom Mode Selector: BÀN PAD | DANH SÁCH CUE */
class MainComponent::PlayoutModeBar : public juce::Component
{
public:
    std::function<void(bool)> onPadModeSelected;

    PlayoutModeBar()
    {
        for (auto* btn : { &padModeBtn, &cueListModeBtn })
        {
            btn->setClickingTogglesState (true);
            btn->setRadioGroupId (88001);
            addAndMakeVisible (*btn);
        }

        padModeBtn.setComponentID ("playoutPadGrid");
        cueListModeBtn.setComponentID ("playoutCueList");
        padModeBtn.setButtonText ({});
        cueListModeBtn.setButtonText ({});
        padModeBtn.setToggleState (true, juce::dontSendNotification);

        padModeBtn.onClick = [this]
        {
            if (! padModeBtn.getToggleState())
                padModeBtn.setToggleState (true, juce::dontSendNotification);

            cueListModeBtn.setToggleState (false, juce::dontSendNotification);

            if (onPadModeSelected)
                onPadModeSelected (true);
        };

        cueListModeBtn.onClick = [this]
        {
            if (! cueListModeBtn.getToggleState())
                cueListModeBtn.setToggleState (true, juce::dontSendNotification);

            padModeBtn.setToggleState (false, juce::dontSendNotification);

            if (onPadModeSelected)
                onPadModeSelected (false);
        };
    }

    void setLookAndFeelPtr (juce::LookAndFeel* laf)
    {
        padModeBtn.setLookAndFeel (laf);
        cueListModeBtn.setLookAndFeel (laf);
    }

    void setPadModeActive (bool isPadMode)
    {
        padModeBtn.setToggleState (isPadMode, juce::dontSendNotification);
        cueListModeBtn.setToggleState (! isPadMode, juce::dontSendNotification);
        padModeBtn.repaint();
        cueListModeBtn.repaint();
    }

    /** Layout lại thanh đáy ngay lập tức (0 ms). */
    void setViewMode (bool isPadGridMode)
    {
        juce::ignoreUnused (isPadGridMode);
        resized();
        repaint();
    }

    void setDarkMode (bool dark)
    {
        isDarkMode = dark;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        const auto pal = ShowTheme::get (isDarkMode);
        g.fillAll (pal.centerBg);
        g.setColour (pal.borderSubtle.withAlpha (0.55f));
        g.drawHorizontalLine (0, 0.0f, (float) getWidth());
    }

    void resized() override
    {
        constexpr int buttonW = 44;
        constexpr int buttonH = 26;
        constexpr int buttonGap = 4;
        constexpr int rightMargin = 12;

        auto area = getLocalBounds();

        area.removeFromRight (rightMargin);

        auto listBtnBounds = area.removeFromRight (buttonW);
        cueListModeBtn.setBounds (listBtnBounds.withSizeKeepingCentre (buttonW, buttonH));

        area.removeFromRight (buttonGap);

        auto padBtnBounds = area.removeFromRight (buttonW);
        padModeBtn.setBounds (padBtnBounds.withSizeKeepingCentre (buttonW, buttonH));
    }

private:
    juce::TextButton padModeBtn;
    juce::TextButton cueListModeBtn;
    bool isDarkMode = true;
};

//==============================================================================
// MultiOutputAudioCallback — Implementation
//==============================================================================

int MainComponent::MultiOutputAudioCallback::findEmptySlot() const noexcept
{
    for (int i = 0; i < kMaxPadSlots; ++i)
        if (slots[(size_t) i].load (std::memory_order_relaxed) == nullptr)
            return i;

    return -1;
}

int MainComponent::MultiOutputAudioCallback::findSlotOf (const PadRealtimeSource* src) const noexcept
{
    for (int i = 0; i < kMaxPadSlots; ++i)
        if (slots[(size_t) i].load (std::memory_order_relaxed) == src)
            return i;

    return -1;
}

void MainComponent::MultiOutputAudioCallback::registerSource (PadRealtimeSource* src)
{
    if (src == nullptr)
        return;

    if (isPrepared.load (std::memory_order_relaxed))
        src->prepareToPlay (currentBlockSize, currentSampleRate);

    // Publish fully-initialized source state before making the pointer visible to audio thread.
    std::atomic_thread_fence (std::memory_order_release);

    for (int i = 0; i < kMaxPadSlots; ++i)
    {
        PadRealtimeSource* expected = nullptr;

        if (slots[(size_t) i].compare_exchange_strong (expected, src, std::memory_order_release, std::memory_order_relaxed))
            return;
    }
}

void MainComponent::MultiOutputAudioCallback::unregisterSource (PadRealtimeSource* src)
{
    if (src == nullptr)
        return;

    for (int i = 0; i < kMaxPadSlots; ++i)
    {
        PadRealtimeSource* expected = src;

        if (slots[(size_t) i].compare_exchange_strong (expected, nullptr, std::memory_order_acq_rel))
        {
            src->waitUntilAudioIdle();
            return;
        }
    }
}

void MainComponent::MultiOutputAudioCallback::removeAllSources()
{
    if (isPrepared.load (std::memory_order_relaxed))
    {
        for (auto& slot : slots)
            if (auto* src = slot.load (std::memory_order_relaxed))
                src->releaseResources();
    }

    for (auto& slot : slots)
        slot.store (nullptr, std::memory_order_release);

    isPrepared.store (false, std::memory_order_relaxed);
}

void MainComponent::MultiOutputAudioCallback::setBusName (int bus, const juce::String& name)
{
    if (bus >= 0 && bus < kMaxBuses)
        buses[bus].name = name;
}

juce::String MainComponent::MultiOutputAudioCallback::getBusName (int bus) const
{
    if (bus >= 0 && bus < kMaxBuses)
        return buses[bus].name;
    return {};
}

juce::StringArray MainComponent::MultiOutputAudioCallback::getAllBusNames() const
{
    juce::StringArray names;
    for (int i = 0; i < kMaxBuses; ++i)
        names.add (buses[i].name.isNotEmpty() ? buses[i].name : ("Bus " + juce::String (i)));
    return names;
}

void MainComponent::MultiOutputAudioCallback::audioDeviceAboutToStart (juce::AudioIODevice* device)
{
    const double sr        = device->getCurrentSampleRate();
    const int    blockSize = device->getCurrentBufferSizeSamples();
    currentSampleRate = sr;
    currentBlockSize  = blockSize;

    // Pre-alloc render buffer (2ch × blockSize) — an toàn: không trong RT context.
    tempBuffer.setSize (2, blockSize, false, true, false);

    for (auto& slot : slots)
        if (auto* src = slot.load (std::memory_order_relaxed))
            src->prepareToPlay (blockSize, sr);

    masterDynamics.prepare (sr, blockSize);

    isPrepared.store (true, std::memory_order_relaxed);
}

void MainComponent::forceAllPadsIdleAtStartup()
{
    for (auto* list : allLists)
    {
        if (list == nullptr)
            continue;

        for (auto* pad : list->pads)
            if (pad != nullptr)
                pad->forceIdleAtStartup();
    }

    lastUiSyncedPlayingPad = nullptr;
    resetPlaybackDisplayCaches();
    refreshSidebarPlayingStatus();
    updateCuePlaybackIndicators();
}

void MainComponent::MultiOutputAudioCallback::audioDeviceStopped()
{
    isPrepared.store (false, std::memory_order_relaxed);

    for (auto& slot : slots)
        if (auto* src = slot.load (std::memory_order_relaxed))
            src->releaseResources();
}

void MainComponent::MultiOutputAudioCallback::audioDeviceIOCallbackWithContext (
    const float* const* /*inputChannelData*/, int /*numInputChannels*/,
    float* const*       outputChannelData,    int numOutputChannels,
    int numSamples,
    const juce::AudioIODeviceCallbackContext& /*context*/)
{
    juce::ScopedNoDenormals noDenormals;

    PadRealtimeSource* activeSources[kMaxPadSlots];
    int numActiveSources = 0;

    for (int i = 0; i < kMaxPadSlots; ++i)
        if (auto* src = slots[(size_t) i].load (std::memory_order_acquire))
            activeSources[numActiveSources++] = src;

    // Xóa toàn bộ output channels
    for (int ch = 0; ch < numOutputChannels; ++ch)
        juce::FloatVectorOperations::clear (outputChannelData[ch], numSamples);

    // Decay peak meter từng bus (không alloc, không lock)
    constexpr float kDecay = 0.80f;
    constexpr float kBoost = 1.00f; // Peak bus nội bộ.
    for (auto& bus : buses)
    {
        bus.peakL.store (bus.peakL.load (std::memory_order_relaxed) * kDecay, std::memory_order_relaxed);
        bus.peakR.store (bus.peakR.load (std::memory_order_relaxed) * kDecay, std::memory_order_relaxed);
    }

    if (tempBuffer.getNumSamples() < numSamples)
        return;

    for (int i = 0; i < numActiveSources; ++i)
    {
        auto* src = activeSources[i];
        if (src == nullptr)
            continue;

        const int busIdx = juce::jlimit (0, kMaxBuses - 1, src->getOutputBus());

        int ch0 = 0;
        int ch1 = 1;
        showcontrol::routing::resolveHardwareStereoChannels (busIdx, numOutputChannels, ch0, ch1);

        // Render pad vào pre-allocated tempBuffer (2ch) — không alloc
        tempBuffer.clear (0, numSamples);
        juce::AudioSourceChannelInfo info (&tempBuffer, 0, numSamples);
        src->getNextAudioBlock (info);

        const float busGain = buses[busIdx].gain.load (std::memory_order_relaxed);

        if (ch0 < numOutputChannels)
        {
            const float* dataL = tempBuffer.getReadPointer (0);
            juce::FloatVectorOperations::addWithMultiply (outputChannelData[ch0], dataL, busGain, numSamples);

            float peakL = 0.0f;
            for (int s = 0; s < numSamples; ++s) peakL = std::max (peakL, std::abs (dataL[s]));
            peakL *= busGain * kBoost;
            if (peakL > buses[busIdx].peakL.load (std::memory_order_relaxed))
                buses[busIdx].peakL.store (peakL, std::memory_order_relaxed);
        }

        if (ch1 < numOutputChannels)
        {
            const int srcCh    = (tempBuffer.getNumChannels() >= 2) ? 1 : 0;
            const float* dataR = tempBuffer.getReadPointer (srcCh);
            juce::FloatVectorOperations::addWithMultiply (outputChannelData[ch1], dataR, busGain, numSamples);

            float peakR = 0.0f;
            for (int s = 0; s < numSamples; ++s) peakR = std::max (peakR, std::abs (dataR[s]));
            peakR *= busGain * kBoost;
            if (peakR > buses[busIdx].peakR.load (std::memory_order_relaxed))
                buses[busIdx].peakR.store (peakR, std::memory_order_relaxed);
        }
    }

    // Master dynamics (juce::dsp::Limiter) — bus 0 Main FOH, cuối chuỗi output
    if (numOutputChannels > 0 && outputChannelData[0] != nullptr)
    {
        float* outL = outputChannelData[0];
        float* outR = (numOutputChannels > 1 && outputChannelData[1] != nullptr)
            ? outputChannelData[1] : nullptr;
        masterDynamics.process (outL, outR, numSamples);

        // Meter MasterDeck: đo POST-master (sau limiter) để khớp tai nghe thực tế.
        float peakL = 0.0f, peakR = 0.0f;
        float rmsAccL = 0.0f, rmsAccR = 0.0f;

        for (int s = 0; s < numSamples; ++s)
        {
            const float l = std::abs (outL[s]);
            peakL = juce::jmax (peakL, l);
            rmsAccL += l * l;

            const float r = (outR != nullptr) ? std::abs (outR[s]) : l;
            peakR = juce::jmax (peakR, r);
            rmsAccR += r * r;
        }

        const float invN = numSamples > 0 ? (1.0f / (float) numSamples) : 1.0f;
        const float rmsL = std::sqrt (rmsAccL * invN);
        const float rmsR = std::sqrt (rmsAccR * invN);

        constexpr float kMeterCalib = 0.58f; // hạ độ nhạy để khớp mức nghe thực tế hơn.
        const float meterL = juce::jlimit (0.0f, 1.4f, (0.42f * peakL + 0.58f * rmsL) * kMeterCalib);
        const float meterR = juce::jlimit (0.0f, 1.4f, (0.42f * peakR + 0.58f * rmsR) * kMeterCalib);

        masterMeterL.store (meterL, std::memory_order_relaxed);
        masterMeterR.store (meterR, std::memory_order_relaxed);
    }
    else
    {
        masterMeterL.store (0.0f, std::memory_order_relaxed);
        masterMeterR.store (0.0f, std::memory_order_relaxed);
    }
}

void MainComponent::MultiOutputAudioCallback::setMasterLimiterEnabled (bool on) noexcept
{
    masterDynamics.setEnabled (on);
}

void MainComponent::MultiOutputAudioCallback::setMasterLimiterThresholdDb (float db) noexcept
{
    masterDynamics.setThresholdDb (db);
}

void MainComponent::MultiOutputAudioCallback::setMasterLimiterReleaseMs (float ms) noexcept
{
    masterDynamics.setReleaseMs (ms);
}

bool MainComponent::MultiOutputAudioCallback::getMasterLimiterEnabled() const noexcept
{
    return masterDynamics.isEnabled();
}

float MainComponent::MultiOutputAudioCallback::getMasterLimiterThresholdDb() const noexcept
{
    return masterDynamics.getThresholdDb();
}

float MainComponent::MultiOutputAudioCallback::getMasterLimiterReleaseMs() const noexcept
{
    return masterDynamics.getReleaseMs();
}

//==============================================================================
void MainComponent::syncSidebarFromAllLists (const juce::Array<juce::String>& names)
{
    sidebarPanel.clearAllLists();

    for (int i = 0; i < allLists.size(); ++i)
    {
        if (auto* list = allLists[i])
        {
            const juce::String listName = (i < names.size() && names[i].isNotEmpty())
                                              ? names[i]
                                              : (list->isGrid ? showcontrol::localization::defaultCueListName()
                                                              : showcontrol::localization::defaultBgmListName());
            sidebarPanel.addSet (listName, list->pads.size(), list->isGrid,
                                 list->useCueListPanel, list->isLocked, list->themeColour);
            sidebarPanel.setListLooping (i, list->isLooping);
        }
    }
}

void MainComponent::enterEmptyProjectState()
{
    if (soloPad != nullptr)
        setSoloPad (nullptr, false);

    saveActiveListSelection();

    if (activeListIndex >= 0 && activeListIndex < allLists.size())
    {
        if (auto* prev = allLists[activeListIndex])
        {
            for (auto* p : prev->pads)
                if (p != nullptr)
                    p->setVisible (false);
        }
    }

    activeListIndex = -1;
    selectedPadIndices.clear();
    selectedBgmIndex = 0;
    lastUiSyncedPlayingPad = nullptr;

    if (cueListPanel != nullptr)
    {
        cueListPanel->haltActiveTimers();
        cueListPanel->setVisible (false);
    }

    if (scrollContent != nullptr)
    {
        if (auto* scrollContainer = getPadPanel())
            scrollContainer->setEmptyListHint (PadPanel::EmptyListHint::none);

        for (int i = 0; i < scrollContent->getNumChildComponents(); ++i)
        {
            if (auto* child = scrollContent->getChildComponent (i))
            {
                if (child != cueListPanel.get())
                    child->setVisible (false);
            }
        }
    }

    inspectorPanel.selectPad (nullptr);
    masterDeckPanel.setListMode (true);

    if (listHeaderComponent != nullptr)
        listHeaderComponent->setVisible (false);

    viewScroller.setVisible (false);

    if (playoutModeBar != nullptr)
        playoutModeBar->setVisible (false);

    if (emptyStatePanel != nullptr)
        emptyStatePanel->setVisible (true);

    resized();
    repaint();
    updateMainDeskDisplay();
}

MainComponent::ListData* MainComponent::getActiveListSafe() noexcept
{
    if (activeListIndex < 0 || activeListIndex >= allLists.size())
        return nullptr;

    return allLists[activeListIndex];
}

const MainComponent::ListData* MainComponent::getActiveListSafe() const noexcept
{
    if (activeListIndex < 0 || activeListIndex >= allLists.size())
        return nullptr;

    return allLists[activeListIndex];
}

void MainComponent::detachInspectorFromPadsInList (ListData* list) noexcept
{
    if (list == nullptr)
        return;

    if (auto* current = inspectorPanel.getCurrentPad())
    {
        for (auto* p : list->pads)
        {
            if (p == current)
            {
                inspectorPanel.selectPad (nullptr);
                return;
            }
        }
    }
}

void MainComponent::releaseAllPadResources() noexcept
{
    inspectorPanel.detachFromPadResources();
    masterDeckPanel.setActivePad (nullptr);
    lastUiSyncedPlayingPad = nullptr;

    for (auto* list : allLists)
    {
        if (list == nullptr)
            continue;

        for (auto* pad : list->pads)
        {
            if (pad != nullptr)
                pad->releaseThumbnailResources();
        }
    }
}

void MainComponent::shutdownAudioAndPads() noexcept
{
    deviceManager.removeAudioCallback (&multiOutputCallback);
    multiOutputCallback.removeAllSources();
    resetPlaybackDisplayCaches();
    allLists.clear();
    timeSliceThread.stopThread (500);
    showcontrol::background::shutdownPool();
    showcontrol::waveform::shutdownSharedCache();
}

namespace
{
    void detachLookAndFeelRecursive (juce::Component& component)
    {
        for (int i = 0; i < component.getNumChildComponents(); ++i)
            if (auto* child = component.getChildComponent (i))
                detachLookAndFeelRecursive (*child);

        component.setLookAndFeel (nullptr);
    }
}

void MainComponent::releaseAllLookAndFeelAttachments() noexcept
{
    showSidebarBtn.setLookAndFeel (nullptr);
    showInspectorBtn.setLookAndFeel (nullptr);

    if (playoutModeBar != nullptr)
        playoutModeBar->setLookAndFeelPtr (nullptr);

    splitterButtonLaf.reset();
    playoutModeButtonLaf.reset();

    detachLookAndFeelRecursive (*this);
}

void MainComponent::shutdownActiveTimers() noexcept
{
    stopTimer();
    pendingGoTimer.reset();
    startupReassertTimer.reset();
    startupGuardTimer.reset();
    deferredIdlePadsTimer.reset();
    panicFadeDispatchScheduled.store (false, std::memory_order_release);

    if (padReorderOverlay != nullptr)
        padReorderOverlay->setVisible (false);

    if (cueListPanel != nullptr)
        cueListPanel->haltActiveTimers();

    if (stageMonitorWindow != nullptr)
        stageMonitorWindow.reset();
}

void MainComponent::prepareForApplicationShutdown()
{
    shutdownActiveTimers();
    OutputBusNamingOverlay::dismissActive (false);

    if (updateChecker != nullptr)
        updateChecker.reset();

    cancelPendingUpdate();

    const bool wasBusy = isPerformingStateOperation.exchange (true, std::memory_order_acq_rel);
    saveApplicationStateInternal();

    if (! wasBusy)
        isPerformingStateOperation.store (false, std::memory_order_release);
}


MainComponent::~MainComponent()
{
    shutdownActiveTimers();
    oscListener.reset();
    backupBroadcaster.reset();
    updateChecker.reset();

    juce::Desktop::getInstance().removeDarkModeSettingListener (this);

    if (soloPad != nullptr)
        setSoloPad (nullptr, false);

    releaseAllPadResources();
    shutdownAudioAndPads();

    formatManager.clearFormats();
    showcontrol::audio::unbindActiveFormatManager();

    releaseAllLookAndFeelAttachments();
    juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
    ShowControlLookAndFeel::shutdownTypographyCaches();

    viewScroller.setViewedComponent (nullptr);
    cueListPanel.reset();
    scrollContent.reset();
}

void MainComponent::visibilityChanged()
{
    // Đảm bảo hotkey có thể nhận ngay sau khi app hiển thị.
    if (isShowing())
        grabKeyboardFocus();
}

SoundPad* MainComponent::createSoundPad()
{
    auto* pad = new SoundPad (formatManager);
    pad->setSharedTimeSliceThread (&timeSliceThread);

    if (activeListIndex >= 0 && activeListIndex < allLists.size() && allLists[activeListIndex] != nullptr)
        pad->setCueListPlayback (allLists[activeListIndex]->isGrid);

    wireSoundPad (pad);
    registerPadWithMixer (pad);
    return pad;
}

void MainComponent::wireSoundPad (SoundPad* pad)
{
    if (pad == nullptr)
        return;

    if (pad->isUiCallbacksWired)
        return;

    pad->isUiCallbacksWired = true;

    pad->onSelected = [this] (SoundPad* selected, const juce::ModifierKeys& mods)
    {
        const bool isDirectMouseSelection = mods.isAnyMouseButtonDown() || mods.isPopupMenu();
        if (! isDirectMouseSelection)
            return;
        applySelectionForPadClick (selected->getPadIndex(), mods);

        if (activeListIndex >= 0 && activeListIndex < allLists.size())
        {
            if (auto* list = allLists[activeListIndex])
            {
                syncPadTagColourFromCueMeta (*list, selected);

                if (list->isGrid)
                {
                    if (list->autoArmOnSelect)
                        armPad (selected);

                    if (cueListPanel != nullptr)
                    {
                        cueListPanel->setSelectedIndex (selectedBgmIndex);
                        cueListPanel->repaint();
                    }
                }
            }
        }

        presentPadInInspector (selected);

        if (isShowing())
            grabKeyboardFocus();
    };

    pad->isPlaybackCommandBlocked = [this] { return isPlaybackCommandBlocked(); };

    pad->isActivePlaybackUiOwner = [this, pad]
    {
        return uiPlaybackFocusPad == nullptr || uiPlaybackFocusPad == pad;
    };

    pad->onRequestGo = [this] (SoundPad* p)
    {
        if (isPlaybackCommandBlocked())
            return;

        if (p != nullptr)
            triggerCueGo (p->getPadIndex());
    };

    pad->onPlaybackStateChanged = [this, pad]
    {
        juce::Component::SafePointer<SoundPad> safePad (pad);
        const auto refreshUi = [this, safePad]
        {
            if (isOperatingState())
                return;

            if (auto* p = safePad.getComponent())
            {
                // Pad fade-out cũ — im lặng hoàn toàn, không kéo con trỏ/UI ngược.
                if (! shouldAcceptPlaybackUiEventFromPad (p)
                    && (p->isFadeOutInProgress() || p->isStopping()))
                    return;

                const int listIdx = findListIndexForPad (allLists, p);

                if (listIdx == activeListIndex && listIdx >= 0)
                {
                    if ((p->isPlaying() || p->isPaused()) && shouldAcceptPlaybackUiEventFromPad (p))
                        syncUiToPlayingPad (p, false);
                }

                masterDeckPanel.refreshTransportLabels();
                masterDeckPanel.repaint();
                inspectorPanel.refreshTransportUi();
                refreshSidebarPlayingStatus();
                updateCuePlaybackIndicators();
            }
        };

        if (juce::MessageManager::getInstance()->isThisTheMessageThread())
            refreshUi();
        else
            juce::MessageManager::callAsync (refreshUi);
    };

    pad->onWillStartPlay = [this] (SoundPad* starter)
    {
        const int listIdx = findListIndexForPad (allLists, starter);
        if (listIdx < 0 || listIdx >= allLists.size())
            return;

        auto* list = allLists[listIdx];
        if (list == nullptr)
            return;

        if (list->isGrid)
        {
            // Polyphony: không fade/stop pad khác — chỉ dạt highlight UI sang pad mới ngay lập tức.
            forwardUiSelectionToPad (starter, false);
            return;
        }

        for (auto* p : list->pads)
        {
            if (p != nullptr && p != starter)
                p->triggerStop();
        }

        syncUiToPlayingPad (starter, true);
    };

    pad->onContextMenuRequested = [this] (SoundPad* p) { showTrackContextMenu (p); };

    pad->onTrackNameEditBegan = [this] (SoundPad* p)
    {
        if (p == nullptr)
            return;

        pendingPadRenameOldNames.set ((juce::int64) (intptr_t) p, p->getPadName());
    };

    pad->onTrackNameChanged = [this] (SoundPad* p)
    {
        if (isOperatingState() || p == nullptr)
            return;

        const int listIdx = findListIndexForPad (allLists, p);
        if (listIdx < 0 || listIdx >= allLists.size())
            return;

        auto* list = allLists[listIdx];
        if (list == nullptr)
            return;

        const int padIdx = list->pads.indexOf (p);
        if (padIdx < 0)
            return;

        juce::String oldName;
        const auto key = (juce::int64) (intptr_t) p;

        if (pendingPadRenameOldNames.contains (key))
            oldName = pendingPadRenameOldNames[key];

        pendingPadRenameOldNames.remove (key);

        if (oldName.isEmpty())
        {
            if (padIdx < list->cueMeta.size())
                oldName = list->cueMeta.getReference (padIdx).name;
        }

        const juce::String newName = p->getPadName();

        if (oldName.isNotEmpty() && oldName != newName)
        {
            applyTrackRenameWithUndo (listIdx, padIdx, newName, oldName);
            return;
        }

        applyTrackRenameAtIndex (listIdx, padIdx, newName, true);
    };

    pad->onPadReorderBegin = [this] (SoundPad* p) { beginPadReorder (p); };
    pad->onPadReorderMove = [this] (SoundPad* p, juce::Point<int> pos) { juce::ignoreUnused (p); updatePadReorder (pos); };
    pad->onPadReorderEnd = [this] (SoundPad* p) { juce::ignoreUnused (p); endPadReorder(); };
    pad->onBuildPadDragDescription = [this, pad] () -> juce::var
    {
        if (activeListIndex < 0 || activeListIndex >= allLists.size())
            return juce::var();

        auto* list = allLists[activeListIndex];
        if (list == nullptr)
            return juce::var();

        const int anchorIndex = list->pads.indexOf (pad);
        if (anchorIndex < 0)
            return juce::var();

        juce::Array<int> dragIndices = selectedPadIndices;

        if (dragIndices.isEmpty() || ! dragIndices.contains (anchorIndex))
        {
            dragIndices.clear();
            dragIndices.add (anchorIndex);
        }

        return buildPadPanelDragPayload (dragIndices, anchorIndex);
    };

    pad->onBuildSidebarListDragDescription = [this, pad] () -> juce::var
    {
        if (activeListIndex < 0 || activeListIndex >= allLists.size())
            return juce::var();

        auto* list = allLists[activeListIndex];
        if (list == nullptr)
            return juce::var();

        const int anchorIndex = list->pads.indexOf (pad);
        if (anchorIndex < 0)
            return juce::var();

        juce::Array<int> dragIndices = selectedPadIndices;

        if (dragIndices.isEmpty() || ! dragIndices.contains (anchorIndex))
        {
            dragIndices.clear();
            dragIndices.add (anchorIndex);
        }

        return buildSidebarListDragPayload (dragIndices);
    };

    pad->onGetRowReorderDragCount = [this] () -> int
    {
        return juce::jmax (1, selectedPadIndices.isEmpty() ? 1 : selectedPadIndices.size());
    };

    pad->onCreateMultiItemDragImage = [this] (int itemCount) -> juce::Image
    {
        juce::String title;

        if (auto* list = getActiveListSafe())
        {
            const int titleIdx = padReorderActive ? padReorderFromIndex : selectedBgmIndex;

            if (juce::isPositiveAndBelow (titleIdx, list->pads.size()))
            {
                if (auto* p = list->pads[titleIdx])
                    title = p->getPadName();
            }
        }

        if (title.isEmpty())
            title = juce::String::fromUTF8 (u8"Di chuyển...");

        return showcontrol::crossdrag::createPremiumDragImage (title, itemCount);
    };

    pad->onTrackFinished = [this] (SoundPad* finished)
    {
        juce::Component::SafePointer<SoundPad> safeFinished (finished);
        const auto run = [this, safeFinished]
        {
            if (auto* p = safeFinished.getComponent())
                handleTrackFinished (p);
        };

        if (juce::MessageManager::getInstance()->isThisTheMessageThread())
            run();
        else
            juce::MessageManager::callAsync (run);
    };

    pad->onAudioFileLoaded = [this] (SoundPad* loadedPad)
    {
        if (loadedPad != nullptr)
        {
            if (isInitialLoading.load (std::memory_order_relaxed))
                reloadPadWaveformFromConfig (loadedPad);

            const int listIdx = findListIndexForPad (allLists, loadedPad);
            if (listIdx >= 0)
            {
                ensureDefaultHotkeysForList (listIdx);

                if (auto* list = allLists[listIdx]; list != nullptr && list->isGrid)
                {
                    syncCueMetadataFromPads (*list);

                    if (listIdx == activeListIndex)
                        layoutActiveListPads();
                }
            }

            // Cập nhật InspectorPanel nếu pad đang được chọn — hiện metadata + waveform mới
            if (inspectorPanel.getCurrentPad() == loadedPad)
            {
                inspectorPanel.refreshWaveformFromPad();
                inspectorPanel.refreshMetadata();
                inspectorPanel.refreshLoudnessLabel();
            }

            if (loadedPad != nullptr && loadedPad == lastUiSyncedPlayingPad)
                masterDeckPanel.setTrackMetadata (loadedPad->getMetadata());
            else if (inspectorPanel.getCurrentPad() == loadedPad)
                masterDeckPanel.setTrackMetadata (loadedPad->getMetadata());
        }

        resized();
        repaint();
    };

    pad->onNormalizationComplete = [this] (SoundPad* p)
    {
        if (p != nullptr)
            p->applyVolumeSyncGainIfReady();

        if (p != nullptr && inspectorPanel.getCurrentPad() == p)
        {
            inspectorPanel.refreshLoudnessLabel();
            inspectorPanel.selectPad (p);
        }

        saveProject();
    };
}

void MainComponent::applyProjectDefaultsToPad (SoundPad* pad) const
{
    if (pad != nullptr)
        pad->setNormalizeUseLufs (projectDefaultNormalizeLufs);
}

void MainComponent::normalizeActiveListWithSettings (const showcontrol::loudness::LoudnessSettings& settings)
{
    if (activeListIndex < 0 || activeListIndex >= allLists.size())
        return;

    auto* list = allLists[activeListIndex];
    if (list == nullptr)
        return;

    for (auto* p : list->pads)
    {
        if (p == nullptr || ! p->hasAudioFile())
            continue;

        p->setLoudnessSettings (settings);

        if (settings.enabled)
        {
            if (! p->applyVolumeSyncGainIfReady())
                p->requestNormalization();
        }
        else
        {
            p->setOutputGain (1.0f);
        }
    }

    if (auto* pad = inspectorPanel.getCurrentPad())
        inspectorPanel.refreshLoudnessLabel();

    saveProject();
}

juce::Array<showcontrol::loudness::ListPreviewRow> MainComponent::buildLoudnessPreviewForActiveList (
    const showcontrol::loudness::LoudnessSettings& settings) const
{
    juce::Array<showcontrol::loudness::ListPreviewRow> rows;

    if (const auto* list = getActiveListSafe())
    {
        for (auto* p : list->pads)
        {
            if (p == nullptr || ! p->hasAudioFile())
                continue;

            rows.add (showcontrol::loudness::buildListPreviewRow (p->getPadName(),
                                                                  p->getFileLoudnessAnalysis(),
                                                                  p->isNormalizationInProgress(),
                                                                  settings));
        }
    }

    return rows;
}

void MainComponent::registerPadWithMixer (SoundPad* pad)
{
    if (pad == nullptr || pad->isRegisteredWithMasterMixer())
        return;

    multiOutputCallback.registerSource (&pad->getRealtimeSource());
    pad->markRegisteredWithMasterMixer();
}

void MainComponent::unregisterPadFromMixer (SoundPad* pad)
{
    if (pad == nullptr || ! pad->isRegisteredWithMasterMixer())
        return;

    // CS trong unregisterSource đảm bảo audio block hiện tại hoàn thành
    // trước khi source bị xóa khỏi danh sách → pad có thể destroy ngay sau đây.
    multiOutputCallback.unregisterSource (&pad->getRealtimeSource());
    pad->markUnregisteredFromMasterMixer();
}

void MainComponent::registerAllPadsWithMixer()
{
    for (auto* list : allLists)
    {
        if (list == nullptr)
            continue;

        for (auto* pad : list->pads)
            registerPadWithMixer (pad);
    }
}

void MainComponent::attachReadAheadToAllPads()
{
    for (auto* list : allLists)
    {
        if (list == nullptr)
            continue;

        for (auto* pad : list->pads)
            if (pad != nullptr)
                pad->setSharedTimeSliceThread (&timeSliceThread);
    }
}

static juce::String cleanVietnameseString (const juce::String& input)
{
    juce::String s = input.toLowerCase();
    const wchar_t* a = L"àáảãạâầấẩẫậăằắẳẵặáàảãạăắằẳẵặâấầẩẫậ";
    for (int i = 0; a[i] != 0; ++i) s = s.replaceCharacter (a[i], 'a');
    const wchar_t* e = L"èéẻẽẹêềếểễệéèẻẽẹêếềểễệ";
    for (int i = 0; e[i] != 0; ++i) s = s.replaceCharacter (e[i], 'e');
    const wchar_t* o = L"òóỏõọôồốổỗộơờớởỡợóòỏõọôốồổỗộơờớởỡợ";
    for (int i = 0; o[i] != 0; ++i) s = s.replaceCharacter (o[i], 'o');
    const wchar_t* u = L"ùúủũụưừứửữựúùủũụưứừửữự";
    for (int i = 0; u[i] != 0; ++i) s = s.replaceCharacter (u[i], 'u');
    const wchar_t* i_c = L"ìíỉĩịíìỉĩị";
    for (int i = 0; i_c[i] != 0; ++i) s = s.replaceCharacter (i_c[i], 'i');
    const wchar_t* y = L"ỳýỷỹỵýỳỷỹỵ";
    for (int i = 0; y[i] != 0; ++i) s = s.replaceCharacter (y[i], 'y');
    s = s.replaceCharacter (L'đ', 'd'); s = s.replaceCharacter (L'Đ', 'd');

    juce::String result;
    auto charPtr = s.getCharPointer();
    while (! charPtr.isEmpty()) {
        juce::juce_wchar c = charPtr.getAndAdvance(); 
        if (c < 0x0300 || c > 0x036F) result += c;
    }
    return result;
}

void MainComponent::timerCallback()
{
    if (isOperatingState())
        return;

    const SoundPad* prioritizedPad = findGloballyPrioritizedPlayingPad();

    if (prioritizedPad != nullptr
        && prioritizedPad != lastUiSyncedPlayingPad
        && shouldAcceptPlaybackUiEventFromPad (const_cast<SoundPad*> (prioritizedPad)))
    {
        syncUiToPlayingPad (const_cast<SoundPad*> (prioritizedPad), false);
    }
    else if (prioritizedPad == nullptr)
    {
        lastUiSyncedPlayingPad = nullptr;
    }

    updateMainDeskDisplay();

    pushStageMonitorUpdate();

    maybeRunAutosave();
    tickBackupHeartbeat();
    pollBackupDiscoverySocket();
}

SoundPad* MainComponent::findAnyActivePlayingPad() const
{
    return findGloballyPrioritizedPlayingPad();
}

StageMonitorSnapshot MainComponent::buildStageMonitorSnapshot (SoundPad* pad) const
{
    StageMonitorSnapshot snapshot;

    if (pad == nullptr || ! pad->hasAudioFile() || ! pad->isTransportActive())
        return snapshot;

    snapshot.isPlaying = true;
    snapshot.isLooping = pad->isLooping();
    snapshot.isCueMode = pad->usesCuePauseResume();

    const juce::File file (pad->getFilePath());
    snapshot.trackName = file.getFileName().isNotEmpty() ? file.getFileName() : pad->getPadName();

    double rangeStart = 0.0, rangeEnd = 0.0;
    pad->getTrimmedDisplayRange (rangeStart, rangeEnd);

    const double position = pad->getPlaybackPosition();
    const double fileDuration = pad->getEffectiveLength();

    snapshot.totalSeconds     = fileDuration;
    snapshot.elapsedSeconds   = juce::jmax (0.0, position - rangeStart);
    snapshot.remainingSeconds = pad->getRemainingSeconds();
    snapshot.progress         = (snapshot.totalSeconds > 0.0 && std::isfinite (snapshot.totalSeconds))
                                    ? (float) juce::jlimit (0.0, 1.0,
                                                            snapshot.elapsedSeconds / snapshot.totalSeconds)
                                    : 0.0f;

    return snapshot;
}

void MainComponent::pushStageMonitorUpdate()
{
    if (stageMonitorWindow == nullptr)
        return;

    if (auto* monitor = stageMonitorWindow->getMonitorComponent())
        monitor->updateDisplay (buildStageMonitorSnapshot (findAnyActivePlayingPad()));
}

void MainComponent::toggleStageMonitorWindow()
{
    if (stageMonitorWindow != nullptr)
    {
        stageMonitorWindow.reset();
        return;
    }

    juce::Component::SafePointer<MainComponent> safeThis (this);

    stageMonitorWindow = std::make_unique<StageMonitorWindow> ([safeThis]
    {
        juce::MessageManager::callAsync ([safeThis]
        {
            if (safeThis == nullptr)
                return;

            if (safeThis->stageMonitorWindow != nullptr)
                safeThis->stageMonitorWindow.reset();
        });
    });

    if (stageMonitorWindow == nullptr)
        return;

    stageMonitorWindow->positionToSecondaryDisplay();
    stageMonitorWindow->setVisible (true);

    if (auto* monitor = stageMonitorWindow->getMonitorComponent())
        monitor->grabKeyboardFocus();

    pushStageMonitorUpdate();
}

int MainComponent::findListIndexForPad (const juce::OwnedArray<ListData>& lists, SoundPad* pad)
{
    if (pad == nullptr)
        return -1;

    for (int i = 0; i < lists.size(); ++i)
    {
        auto* list = lists[i];
        if (list == nullptr)
            continue;

        for (auto* p : list->pads)
        {
            if (p == pad)
                return i;
        }
    }

    return -1;
}

int MainComponent::findNextBgmTrackIndex (const ListData& list, int afterIndex)
{
    const int n = list.pads.size();

    auto hasPlayableAudio = [&list] (int index) -> bool
    {
        if (index < 0 || index >= list.pads.size())
            return false;

        auto* pad = list.pads[index];
        return pad != nullptr && pad->hasAudioFile();
    };

    for (int i = afterIndex + 1; i < n; ++i)
    {
        if (hasPlayableAudio (i))
            return i;
    }

    if (! list.isLooping)
        return -1;

    for (int i = 0; i <= afterIndex; ++i)
    {
        if (hasPlayableAudio (i))
            return i;
    }

    return -1;
}

int MainComponent::findPrevBgmTrackIndex (const ListData& list, int beforeIndex)
{
    auto hasPlayableAudio = [&list] (int index) -> bool
    {
        if (index < 0 || index >= list.pads.size())
            return false;

        auto* pad = list.pads[index];
        return pad != nullptr && pad->hasAudioFile();
    };

    for (int i = beforeIndex - 1; i >= 0; --i)
    {
        if (hasPlayableAudio (i))
            return i;
    }

    if (! list.isLooping)
        return -1;

    for (int i = list.pads.size() - 1; i >= beforeIndex; --i)
    {
        if (hasPlayableAudio (i))
            return i;
    }

    return -1;
}

SoundPad* MainComponent::findPlayingPadInActiveBgmList() const
{
    if (activeListIndex < 0 || activeListIndex >= allLists.size())
        return nullptr;

    auto* list = allLists[activeListIndex];
    if (list == nullptr || list->isGrid)
        return nullptr;

    for (auto* pad : list->pads)
    {
        if (pad != nullptr && pad->isTransportActive())
            return pad;
    }

    return nullptr;
}

bool MainComponent::shouldAcceptPlaybackUiEventFromPad (SoundPad* pad) const noexcept
{
    if (pad == nullptr)
        return false;

    if (uiPlaybackFocusPad != nullptr && pad != uiPlaybackFocusPad)
    {
        if (pad->isFadeOutInProgress())
            return false;
    }

    return true;
}

SoundPad* MainComponent::findPrimaryPlaybackPadForActiveList() const noexcept
{
    if (activeListIndex < 0 || activeListIndex >= allLists.size())
        return nullptr;

    auto* list = allLists[activeListIndex];
    if (list == nullptr)
        return nullptr;

    if (uiPlaybackFocusPad != nullptr && list->pads.contains (uiPlaybackFocusPad))
    {
        if (uiPlaybackFocusPad->isPlaying() || uiPlaybackFocusPad->isPaused())
            return uiPlaybackFocusPad;
    }

    for (auto* pad : list->pads)
    {
        if (pad != nullptr && (pad->isPlaying() || pad->isPaused()))
            return pad;
    }

    return nullptr;
}

SoundPad* MainComponent::getAllActivePlayingPadTrackGlobal() const noexcept
{
    if (activeListIndex >= 0 && activeListIndex < allLists.size())
    {
        if (auto* activeList = allLists[activeListIndex];
            activeList != nullptr && activeList->isGrid && activeList->clickPadToTrigger)
        {
            if (auto* panel = getPadPanel())
            {
                if (auto* activePadTrack = panel->getCurrentlyPlayingPadTrack())
                    return activePadTrack;
            }
        }
    }

    for (auto* list : allLists)
    {
        if (list == nullptr || ! list->isGrid || ! list->clickPadToTrigger)
            continue;

        for (auto* pad : list->pads)
        {
            if (pad != nullptr && pad->isTransportActive())
                return pad;
        }
    }

    return nullptr;
}

SoundPad* MainComponent::getAllActivePlayingCueTrackGlobal() const noexcept
{
    for (auto* list : allLists)
    {
        if (list == nullptr || ! list->isGrid || list->clickPadToTrigger)
            continue;

        for (auto* pad : list->pads)
        {
            if (pad != nullptr && pad->isTransportActive())
                return pad;
        }
    }

    return nullptr;
}

SoundPad* MainComponent::getAllActivePlayingBGMTrackGlobal() const noexcept
{
    for (auto* list : allLists)
    {
        if (list == nullptr || list->isGrid)
            continue;

        for (auto* pad : list->pads)
        {
            if (pad != nullptr && pad->isTransportActive())
                return pad;
        }
    }

    return nullptr;
}

SoundPad* MainComponent::findGloballyPrioritizedPlayingPad() const noexcept
{
    if (auto* pad = getAllActivePlayingPadTrackGlobal())
        return pad;

    if (auto* pad = getAllActivePlayingCueTrackGlobal())
        return pad;

    return getAllActivePlayingBGMTrackGlobal();
}

void MainComponent::updateTrackPlayingInfo (SoundPad* pad)
{
    if (pad == nullptr)
    {
        showNoTrackPlayingState();
        return;
    }

    const int listIdx = findListIndexForPad (allLists, pad);

    if (listIdx >= 0 && listIdx < allLists.size())
    {
        if (auto* list = allLists[listIdx])
            masterDeckPanel.setListMode (! list->isGrid);
    }

    masterDeckPanel.setActivePad (pad);
    pad->supplementBpmFromFileIfMissing();
    masterDeckPanel.setTrackMetadata (pad->getMetadata());
    masterDeckPanel.refreshTransportLabels();
    refreshMasterDeckBgmTransportState();
}

void MainComponent::showNoTrackPlayingState()
{
    masterDeckPanel.setActivePad (nullptr);
    masterDeckPanel.setTrackMetadata ({});
    refreshMasterDeckBgmTransportState();
}

void MainComponent::refreshMasterDeckBgmTransportState()
{
    if (activeListIndex < 0 || activeListIndex >= allLists.size())
        return;

    if (auto* bgmList = allLists[activeListIndex])
    {
        if (! bgmList->isGrid)
        {
            auto* bgmPlaying = findPlayingPadInActiveBgmList();
            const bool isPlaying = (bgmPlaying != nullptr);
            const int curIdx = isPlaying
                                   ? bgmList->pads.indexOf (bgmPlaying)
                                   : selectedBgmIndex;
            const bool hasPrev = (findPrevBgmTrackIndex (*bgmList, curIdx) >= 0);
            const bool hasNext = (findNextBgmTrackIndex (*bgmList, curIdx) >= 0);
            masterDeckPanel.setBgmState (isPlaying, hasPrev, hasNext);
        }
    }
}

void MainComponent::resetPlaybackDisplayCaches() noexcept
{
    lastDeskDisplayPad = nullptr;
    cachedSidebarListPlayingActive.clear();
    lastCuePlaybackListIndex = -1;
    lastCuePlaybackPlayingIdx = -1;
    lastCuePlaybackArmedIdx = -1;
}

void MainComponent::updateMainDeskDisplay()
{
    SoundPad* displayPad = nullptr;

    if (auto* activePadTrack = getAllActivePlayingPadTrackGlobal())
        displayPad = activePadTrack;
    else if (auto* activeCueTrack = getAllActivePlayingCueTrackGlobal())
        displayPad = activeCueTrack;
    else if (auto* activeBgmTrack = getAllActivePlayingBGMTrackGlobal())
        displayPad = activeBgmTrack;

    if (displayPad != lastDeskDisplayPad)
    {
        lastDeskDisplayPad = displayPad;

        if (displayPad != nullptr)
            updateTrackPlayingInfo (displayPad);
        else
            showNoTrackPlayingState();
    }
    else if (displayPad != nullptr)
    {
        refreshMasterDeckBgmTransportState();
    }

    refreshSidebarPlayingStatus();
    updateCuePlaybackIndicators();
}

bool MainComponent::allowTransportCommand (TransportCommandKind next,
                                           TransportCommandKind& lastKind,
                                           juce::uint32& lastCommandMs) noexcept
{
    if (next == TransportCommandKind::none)
        return true;

    const juce::uint32 nowMs = juce::Time::getMillisecondCounter();

    if (next == lastKind && nowMs - lastCommandMs < kTransportCommandGuardMs)
        return false;

    lastKind      = next;
    lastCommandMs = nowMs;
    return true;
}

void MainComponent::prefetchBgmPadAtIndex (int index)
{
    if (activeListIndex < 0 || activeListIndex >= allLists.size())
        return;

    auto* list = allLists[activeListIndex];
    if (list == nullptr || list->isGrid)
        return;

    auto prefetchOne = [list] (int idx)
    {
        if (idx < 0 || idx >= list->pads.size())
            return;

        if (auto* pad = list->pads[idx])
        {
            pad->setThumbnailLoadAllowed (true);
            pad->requestPreloadForPlayback();
        }
    };

    prefetchOne (index);
    prefetchOne (index - 1);
    prefetchOne (index + 1);
}

void MainComponent::triggerBgmPlayPause()
{
    if (isPlaybackCommandBlocked())
        return;

    if (activeListIndex < 0 || activeListIndex >= allLists.size())
        return;

    auto* list = allLists[activeListIndex];
    if (list == nullptr || list->isGrid)
        return;

    const int targetIndex = selectedBgmIndex;
    if (targetIndex < 0 || targetIndex >= list->pads.size())
        return;

    auto* targetPad = list->pads[targetIndex];
    if (targetPad == nullptr || ! targetPad->hasAudioFile())
        return;

    targetPad->prepareForInstantPlay();

    SoundPad* playing = findPlayingPadInActiveBgmList();
    const int playingIndex = (playing != nullptr) ? list->pads.indexOf (playing) : -1;

    // Chuyển bài gối đầu: bài khác đang Playing/FadingOut → fade cũ, phát dòng đang chọn.
    if (playing != nullptr && playingIndex != targetIndex)
    {
        if (! allowTransportCommand (TransportCommandKind::play,
                                     lastBgmTransportKind,
                                     lastBgmTransportCommandMs))
            return;

        forwardUiSelectionToPad (targetPad, true);

        if (! playing->isFadeOutInProgress())
            playing->startFadeOut();

        targetPad->triggerPlay();
        syncUiToPlayingPad (targetPad, false);
        return;
    }

    // Cùng dòng đang phát → toggle fade-out (dừng).
    if (playing != nullptr && playingIndex == targetIndex)
    {
        if (! allowTransportCommand (TransportCommandKind::stop,
                                     lastBgmTransportKind,
                                     lastBgmTransportCommandMs))
            return;

        if (playing->isFadeOutInProgress())
            return;

        playing->startFadeOut();
        return;
    }

    // Không có transport active → phát dòng đang chọn.
    if (! allowTransportCommand (TransportCommandKind::play,
                                 lastBgmTransportKind,
                                 lastBgmTransportCommandMs))
        return;

    for (auto* other : list->pads)
        if (other != nullptr && other != targetPad)
            other->triggerStop();

    targetPad->triggerPlay();
    syncUiToPlayingPad (targetPad, true);
}

void MainComponent::triggerBgmNext()
{
    if (isPlaybackCommandBlocked())
        return;

    if (activeListIndex < 0 || activeListIndex >= allLists.size())
        return;

    auto* list = allLists[activeListIndex];
    if (list == nullptr || list->isGrid)
        return;

    SoundPad* playing = findPlayingPadInActiveBgmList();
    const int curIdx = (playing != nullptr) ? list->pads.indexOf (playing) : selectedBgmIndex;
    const int nextIdx = findNextBgmTrackIndex (*list, curIdx);
    if (nextIdx < 0)
        return;

    auto* nextPad = list->pads[nextIdx];
    if (nextPad == nullptr)
        return;

    nextPad->prepareForInstantPlay();
    forwardUiSelectionToPad (nextPad, true);

    for (auto* pad : list->pads)
        if (pad != nullptr && pad != nextPad)
            pad->triggerStop();

    nextPad->triggerPlay();
    syncUiToPlayingPad (nextPad, false);
}

void MainComponent::triggerBgmPrev()
{
    if (isPlaybackCommandBlocked())
        return;

    if (activeListIndex < 0 || activeListIndex >= allLists.size())
        return;

    auto* list = allLists[activeListIndex];
    if (list == nullptr || list->isGrid)
        return;

    SoundPad* playing = findPlayingPadInActiveBgmList();
    const int curIdx = (playing != nullptr) ? list->pads.indexOf (playing) : selectedBgmIndex;
    const int prevIdx = findPrevBgmTrackIndex (*list, curIdx);
    if (prevIdx < 0)
        return;

    auto* prevPad = list->pads[prevIdx];
    if (prevPad == nullptr)
        return;

    prevPad->prepareForInstantPlay();
    forwardUiSelectionToPad (prevPad, true);

    for (auto* pad : list->pads)
        if (pad != nullptr && pad != prevPad)
            pad->triggerStop();

    prevPad->triggerPlay();
    syncUiToPlayingPad (prevPad, false);
}

void MainComponent::forwardUiSelectionToPad (SoundPad* pad, bool scrollIntoView)
{
    if (pad == nullptr)
        return;

    const int listIdx = findListIndexForPad (allLists, pad);
    if (listIdx < 0 || listIdx >= allLists.size())
        return;

    auto* list = allLists[listIdx];
    if (list == nullptr)
        return;

    const int padIdx = list->pads.indexOf (pad);
    if (padIdx < 0)
        return;

    uiPlaybackFocusPad = pad;

    if (listIdx == activeListIndex)
    {
        selectedBgmIndex = padIdx;
        selectedPadIndices.clear();
        selectedPadIndices.add (padIdx);
        applyPadSelectionVisualState();

        if (cueListPanel != nullptr && list->isGrid && list->useCueListPanel)
            cueListPanel->setSelectedIndex (padIdx);

        if (scrollIntoView && ! list->isGrid)
        {
            resized();

            const int vHeight = viewScroller.getViewHeight();
            if (vHeight > 0 && pad->isShowing())
            {
                const int padTop = pad->getY();
                const int padBottom = pad->getBottom();
                const int viewTop = viewScroller.getViewPositionY();
                const int padding = 20;

                if (padTop < viewTop + padding)
                    viewScroller.setViewPosition (viewScroller.getViewPositionX(), std::max (0, padTop - padding));
                else if (padBottom > viewTop + vHeight - padding)
                    viewScroller.setViewPosition (viewScroller.getViewPositionX(), padBottom - vHeight + padding);
            }
        }
    }
}

void MainComponent::syncUiToPlayingPad (SoundPad* pad, bool scrollIntoView)
{
    if (pad == nullptr)
        return;

    if (! shouldAcceptPlaybackUiEventFromPad (pad))
        return;

    const int listIdx = findListIndexForPad (allLists, pad);
    if (listIdx < 0 || listIdx >= allLists.size())
        return;

    auto* list = allLists[listIdx];
    if (list == nullptr)
        return;

    const int padIdx = list->pads.indexOf (pad);
    if (padIdx < 0)
        return;

    pad->setThumbnailLoadAllowed (true);
    pad->setNormalizationLoadAllowed (true);
    pad->requestPreloadForPlayback();

    forwardUiSelectionToPad (pad, scrollIntoView);

    inspectorPanel.selectPad (pad);
    masterDeckPanel.setActivePad (pad);
    if (pad != nullptr)
    {
        pad->supplementBpmFromFileIfMissing();
        masterDeckPanel.setTrackMetadata (pad->getMetadata());
    }
    else
    {
        masterDeckPanel.setTrackMetadata ({});
    }
    masterDeckPanel.refreshTransportLabels();
    refreshSidebarPlayingStatus();
    updateCuePlaybackIndicators();
    lastUiSyncedPlayingPad = pad;
    repaint();
}

void MainComponent::importListsFromDroppedFolders (const juce::StringArray& folderPaths, bool targetIsBgm)
{
    if (folderPaths.isEmpty())
        return;

    const bool isGrid = ! targetIsBgm;
    int lastImportedIdx = -1;

    for (const auto& folderPath : folderPaths)
    {
        const juce::File folder (folderPath);
        if (! folder.isDirectory())
            continue;

        juce::String folderName = folder.getFileName();
        if (folderName.isEmpty())
            folderName = targetIsBgm ? showcontrol::localization::defaultBgmListName()
                                   : showcontrol::localization::defaultCueListName();

        const int idx = allLists.size();
        allLists.add (new ListData());

        auto* list = allLists[idx];
        if (list == nullptr)
            continue;

        list->isGrid = isGrid;
        list->useCueListPanel = false;

        juce::Array<juce::File> audioFiles;
        ShowAudioFormats::collectAudioFilesFromFolderShallow (folder, audioFiles);

        int filesToIngest = audioFiles.size();
        const bool cueTruncated = isGrid && filesToIngest > kMaxCuePadsPerList;

        if (cueTruncated)
            filesToIngest = kMaxCuePadsPerList;

        for (int fi = 0; fi < filesToIngest; ++fi)
        {
            auto* p = createSoundPad();
            scrollContent->addChildComponent (p);
            p->setPadIndex (fi);
            applyProjectDefaultsToPad (p);
            p->configurePad (audioFiles.getReference (fi).getFullPathName(),
                              1.0f,
                              isGrid ? list->isLooping : false);
            p->updateTheme (isDarkMode);
            p->setRenderMode (isGrid);
            p->setCueListPlayback (isGrid);
            p->setVisible (true);
            list->pads.add (p);
        }

        if (isGrid)
            syncCueMetadataFromPads (*list);

        sidebarPanel.addSet (folderName, list->pads.size(), isGrid, false, list->isLocked, list->themeColour);
        lastImportedIdx = idx;

        if (cueTruncated)
        {
            ErrorHandler::log (juce::String::fromUTF8 (u8"Đã nạp 48 file nhạc đầu tiên từ thư mục vào ma trận CUE."),
                               ErrorHandler::Severity::Info);
        }
    }

    if (lastImportedIdx < 0)
        return;

    sidebarPanel.setSelectedIndex (lastImportedIdx);
    loadList (lastImportedIdx, sidebarPanel.getListTrackCount (lastImportedIdx), isGrid);

    if (auto* importedList = allLists[lastImportedIdx]; importedList != nullptr && importedList->isGrid)
        updateCueGridUIFromData (*importedList);

    rebuildDefaultHotkeysForList (lastImportedIdx);
    saveProject();
    resized();
    repaint();

    if (isShowing())
        grabKeyboardFocus();
}

void MainComponent::rebuildSidebarFromAllLists()
{
    juce::Array<juce::String> names;
    const int prevCount = sidebarPanel.getListCount();

    for (int i = 0; i < prevCount && i < allLists.size(); ++i)
        names.add (sidebarPanel.getListName (i));

    sidebarPanel.clearAllLists();

    for (int i = 0; i < allLists.size(); ++i)
    {
        juce::String name = (i < names.size())
            ? names[i]
            : (allLists[i]->isGrid ? juce::String::fromUTF8 (u8"Bộ Cue ") : juce::String::fromUTF8 (u8"Bộ BGM ")) + juce::String (i + 1);

        sidebarPanel.addSet (name, allLists[i]->pads.size(), allLists[i]->isGrid,
                             allLists[i]->useCueListPanel, allLists[i]->isLocked, allLists[i]->themeColour);
        sidebarPanel.setListLooping (i, allLists[i]->isLooping);
    }
}

bool MainComponent::isActiveListLocked() const noexcept
{
    if (activeListIndex < 0 || activeListIndex >= allLists.size())
        return false;

    const auto* list = allLists[activeListIndex];
    return list != nullptr && list->isLocked;
}

void MainComponent::openSetInSecondaryWindow (int listIndex)
{
    if (listIndex < 0 || listIndex >= allLists.size())
        return;

    for (auto* window : secondarySetWindows)
    {
        if (window != nullptr && window->getListIndex() == listIndex)
        {
            window->setVisible (true);
            window->toFront (true);
            return;
        }
    }

    auto* list = allLists[listIndex];
    if (list == nullptr)
        return;

    syncCueMetadataFromPads (*list);

    auto* window = new SetSecondaryWindow (
        listIndex,
        sidebarPanel.getListName (listIndex),
        list->cueMeta,
        isDarkMode,
        [this, listIndex] (int cueIndex)
        {
            sidebarPanel.setSelectedIndex (listIndex);
            loadList (listIndex, sidebarPanel.getListTrackCount (listIndex), allLists[listIndex]->isGrid);
            triggerCueGo (cueIndex);
        },
        [this] (SetSecondaryWindow* closing)
        {
            secondarySetWindows.removeObject (closing);
        });

    secondarySetWindows.add (window);
    window->setVisible (true);
}

void MainComponent::duplicateListAtIndex (int listIndex)
{
    if (listIndex < 0 || listIndex >= allLists.size())
        return;

    auto* src = allLists[listIndex];
    if (src == nullptr)
        return;

    auto* dup = new ListData();
    dup->isGrid            = src->isGrid;
    dup->isLooping         = src->isLooping;
    dup->useCueListPanel   = src->useCueListPanel;
    dup->clickPadToTrigger = src->clickPadToTrigger;
    dup->autoArmOnSelect   = src->autoArmOnSelect;
    dup->themeColour       = src->themeColour;
    dup->cueMeta           = src->cueMeta;

    for (int i = 0; i < src->pads.size(); ++i)
    {
        auto* srcPad = src->pads[i];
        auto* p = ensurePadSlotAtIndex (*dup, i);

        if (p == nullptr || srcPad == nullptr)
            continue;

        applyProjectDefaultsToPad (p);

        if (srcPad->hasAudioFile())
            p->configurePad (srcPad->getFilePath(), srcPad->getOutputGain(), srcPad->isLooping());

        p->updateTheme (isDarkMode);
        p->setRenderMode (dup->isGrid);
        p->setVisible (false);
    }

    juce::Array<juce::String> names;
    for (int i = 0; i < sidebarPanel.getListCount(); ++i)
        names.add (sidebarPanel.getListName (i));

    names.insert (listIndex + 1, names[listIndex] + " Copy");
    allLists.insert (listIndex + 1, dup);

    sidebarPanel.clearAllLists();
    for (int i = 0; i < allLists.size(); ++i)
    {
        sidebarPanel.addSet (names[i], allLists[i]->pads.size(), allLists[i]->isGrid,
                             allLists[i]->useCueListPanel, allLists[i]->isLocked, allLists[i]->themeColour);
        sidebarPanel.setListLooping (i, allLists[i]->isLooping);
    }

    rebuildDefaultHotkeysForList (listIndex + 1);
    saveProject();
}

void MainComponent::addSoundsToSet (int listIndex)
{
    if (listIndex < 0 || listIndex >= allLists.size())
        return;

    if (listIndex != activeListIndex)
    {
        sidebarPanel.setSelectedIndex (listIndex);
        loadList (listIndex, sidebarPanel.getListTrackCount (listIndex), allLists[listIndex]->isGrid);
    }

    triggerManualMusicIngestion();
}

void MainComponent::morphSetStructure (int listIndex)
{
    if (listIndex < 0 || listIndex >= allLists.size())
        return;

    auto* list = allLists[listIndex];
    if (list == nullptr)
        return;

    const bool wasBgm    = ! list->isGrid;
    const bool newIsGrid = ! list->isGrid;

    // BGM → CUE: tắt loop danh sách — CUE chỉ loop độc lập từng PAD, không đụng pad->isLooping().
    if (wasBgm && newIsGrid)
    {
        list->isLooping = false;
        sidebarPanel.setListLooping (listIndex, false);
    }

    list->isGrid = newIsGrid;

    // Thoát nhánh QLab CueListPanel — morph chỉ dùng BGM list row hoặc CUE pad grid.
    list->useCueListPanel = false;

    if (cueListPanel != nullptr)
        cueListPanel->setVisible (false);

    for (auto* pad : list->pads)
    {
        if (pad == nullptr)
            continue;

        pad->setRenderMode (newIsGrid);
        pad->setCueListPlayback (newIsGrid);
        pad->setClickToTrigger (newIsGrid && list->clickPadToTrigger);
    }

    syncCueMetadataFromPads (*list);

    juce::Array<juce::String> names;
    for (int i = 0; i < sidebarPanel.getListCount(); ++i)
        names.add (sidebarPanel.getListName (i));

    sidebarPanel.clearAllLists();
    for (int i = 0; i < allLists.size(); ++i)
    {
        sidebarPanel.addSet (names[i], allLists[i]->pads.size(), allLists[i]->isGrid,
                             false, allLists[i]->isLocked, allLists[i]->themeColour);
        sidebarPanel.setListLooping (i, allLists[i]->isLooping);
    }

    sidebarPanel.setSelectedIndex (listIndex);
    loadList (listIndex, sidebarPanel.getListTrackCount (listIndex), newIsGrid);
    rebuildDefaultHotkeysForList (listIndex);
    refreshSidebarPlayingStatus();
    saveProject();
}

void MainComponent::toggleListLock (int listIndex)
{
    if (listIndex < 0 || listIndex >= allLists.size())
        return;

    auto* list = allLists[listIndex];
    if (list == nullptr)
        return;

    list->isLocked = ! list->isLocked;
    sidebarPanel.setListLocked (listIndex, list->isLocked);
    saveProject();
}

void MainComponent::exportSetAtIndex (int listIndex)
{
    if (listIndex < 0 || listIndex >= allLists.size())
        return;

    auto* list = allLists[listIndex];
    if (list == nullptr)
        return;

    exportFileChooser = std::make_unique<juce::FileChooser> (
        juce::String::fromUTF8 (u8"Export Set..."),
        getProjectFile().getParentDirectory(),
        "*.xml");

    exportFileChooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
    [this, listIndex] (const juce::FileChooser& fc)
    {
        auto target = fc.getResult();
        if (! target.existsAsFile() && target.getFileExtension().isEmpty())
            target = target.withFileExtension (".xml");

        if (target.getFullPathName().isEmpty())
            return;

        auto* list = allLists[listIndex];
        if (list == nullptr)
            return;

        auto xml = std::make_unique<juce::XmlElement> ("ShowControlSetExport");
        xml->setAttribute ("version", "1.0");

        auto* listElem = xml->createNewChildElement ("List");
        listElem->setAttribute ("name", sidebarPanel.getListName (listIndex));
        listElem->setAttribute ("isGrid", list->isGrid);
        listElem->setAttribute ("isLooping", list->isLooping);
        listElem->setAttribute ("isLocked", list->isLocked);

        if (! showcontrol::colours::isDefaultTagColour (list->themeColour))
            listElem->setAttribute ("themeColour", (int) list->themeColour.getARGB());

        if (list->isGrid)
        {
            listElem->setAttribute ("useCueListPanel", list->useCueListPanel);
            listElem->setAttribute ("clickPadToTrigger", list->clickPadToTrigger);
            listElem->setAttribute ("autoArmOnSelect", list->autoArmOnSelect);
            syncCueMetadataFromPads (*list);
        }

        for (int pi = 0; pi < list->pads.size(); ++pi)
        {
            auto* pad = list->pads[pi];
            if (pad == nullptr)
                continue;

            auto* padElem = listElem->createNewChildElement ("Pad");
            padElem->setAttribute ("index", pi);

            if (pad->hasAudioFile())
            {
                padElem->setAttribute ("file", pad->getFilePath());
                writePadProjectState (*padElem, *pad);

                if (! list->isGrid && pad->isLooping())
                    padElem->setAttribute ("loopTrack", true);

                if (list->isGrid && pi < list->cueMeta.size())
                    writeCueMetaToPadElem (*padElem, list->cueMeta.getReference (pi));
            }
            else if (list->isGrid && pi < list->cueMeta.size())
            {
                writeCueMetaToPadElem (*padElem, list->cueMeta.getReference (pi));
            }
        }

        xml->writeTo (target);
    });
}

void MainComponent::moveListInProject (int fromIdx, int toIdx)
{
    if (fromIdx < 0 || toIdx < 0 || fromIdx >= allLists.size() || toIdx >= allLists.size())
        return;

    if (fromIdx == toIdx || allLists[fromIdx] == nullptr || allLists[toIdx] == nullptr)
        return;

    if (allLists[fromIdx]->isLocked)
        return;

    if (allLists[fromIdx]->isGrid != allLists[toIdx]->isGrid)
        return;

    juce::Array<juce::String> names;
    for (int i = 0; i < sidebarPanel.getListCount(); ++i)
        names.add (sidebarPanel.getListName (i));

    auto* movedList = allLists.removeAndReturn (fromIdx);
    allLists.insert (toIdx, movedList);

    const juce::String movedName = names[fromIdx];
    names.remove (fromIdx);
    names.insert (toIdx, movedName);

    sidebarPanel.clearAllLists();
    for (int i = 0; i < allLists.size(); ++i)
    {
        sidebarPanel.addSet (names[i], allLists[i]->pads.size(), allLists[i]->isGrid,
                             allLists[i]->useCueListPanel, allLists[i]->isLocked, allLists[i]->themeColour);
        sidebarPanel.setListLooping (i, allLists[i]->isLooping);
    }

    int newActive = activeListIndex;
    if (activeListIndex == fromIdx)
        newActive = toIdx;
    else if (fromIdx < activeListIndex && toIdx >= activeListIndex)
        newActive--;
    else if (fromIdx > activeListIndex && toIdx <= activeListIndex)
        newActive++;

    activeListIndex = juce::jlimit (0, allLists.size() - 1, newActive);
    sidebarPanel.setSelectedIndex (activeListIndex);
    loadList (activeListIndex, sidebarPanel.getListTrackCount (activeListIndex), allLists[activeListIndex]->isGrid);
    saveProject();
}

void MainComponent::movePadInList (int listIdx, int fromPadIdx, int toPadIdx)
{
    if (listIdx < 0 || listIdx >= allLists.size())
        return;

    auto* list = allLists[listIdx];
    if (list == nullptr || list->isLocked)
        return;

    const int n = list->pads.size();
    if (fromPadIdx < 0 || fromPadIdx >= n || toPadIdx < 0 || toPadIdx >= n || fromPadIdx == toPadIdx)
        return;

    if (isPerformingUndoRedo.load (std::memory_order_acquire))
    {
        movePadInListImpl (listIdx, fromPadIdx, toPadIdx);
        return;
    }

    const auto beforeState = std::make_shared<ListOrderUndoState> (captureListOrderSnapshot (listIdx));

    performUndoableMutation (juce::String::fromUTF8 (u8"Di chuyển track"),
                             [this, listIdx, fromPadIdx, toPadIdx]()
                             {
                                 movePadInListImpl (listIdx, fromPadIdx, toPadIdx);
                                 refreshListOrderAfterMutation (listIdx);
                                 triggerSave();
                             },
                             [this, listIdx, beforeState]()
                             {
                                 restoreListOrderSnapshot (listIdx, *beforeState);
                                 refreshListOrderAfterMutation (listIdx);
                                 triggerSave();
                             });
}

void MainComponent::movePadInListImpl (int listIdx, int fromPadIdx, int toPadIdx)
{
    if (listIdx < 0 || listIdx >= allLists.size())
        return;

    auto* list = allLists[listIdx];
    if (list == nullptr || list->isLocked)
        return;

    const int n = list->pads.size();
    if (fromPadIdx < 0 || fromPadIdx >= n || toPadIdx < 0 || toPadIdx >= n || fromPadIdx == toPadIdx)
        return;

    auto* pad = list->pads.removeAndReturn (fromPadIdx);
    list->pads.insert (toPadIdx, pad);

    // Sticky PAD layout: chỉ hoán đổi thứ tự tuyến tính — gridRow/gridCol trên SoundPad giữ nguyên.
    if (juce::isPositiveAndBelow (fromPadIdx, list->cueMeta.size()))
    {
        auto meta = list->cueMeta.removeAndReturn (fromPadIdx);
        const int metaInsert = juce::jlimit (0, list->cueMeta.size(), toPadIdx);
        list->cueMeta.insert (metaInsert, meta);
    }

    for (int i = 0; i < list->pads.size(); ++i)
        if (list->pads[i] != nullptr)
            list->pads[i]->setPadIndex (i);

    if (activeListIndex == listIdx)
    {
        if (selectedBgmIndex == fromPadIdx)
            selectedBgmIndex = toPadIdx;
        else if (fromPadIdx < selectedBgmIndex && toPadIdx >= selectedBgmIndex)
            selectedBgmIndex--;
        else if (fromPadIdx > selectedBgmIndex && toPadIdx <= selectedBgmIndex)
            selectedBgmIndex++;

        selectedPadIndices.clear();
        selectedPadIndices.add (selectedBgmIndex);
    }
}

bool MainComponent::isSearchWindowFocused() noexcept
{
    if (auto* focused = juce::Component::getCurrentlyFocusedComponent())
    {
        if (dynamic_cast<juce::TextEditor*> (focused) != nullptr)
            return true;
    }

    return false;
}

void MainComponent::forceStopActiveAudioForSafety()
{
    for (auto* list : allLists)
    {
        if (list == nullptr)
            continue;

        for (auto* pad : list->pads)
        {
            if (pad != nullptr && pad->isTransportActive())
                pad->triggerStop();
        }
    }
}

void MainComponent::clearAllPanelsSelectionLive()
{
    if (padReorderActive)
        cancelPadReorder();

    endMarqueeSelection();

    selectedPadIndices.clear();
    selectedBgmIndex = -1;

    if (cueListPanel != nullptr)
    {
        cueListPanel->setSelectedIndices ({});
        cueListPanel->setSelectedIndex (-1);
    }

    applyPadSelectionVisualState();
    inspectorPanel.selectPad (nullptr);
    masterDeckPanel.setActivePad (nullptr);
}

void MainComponent::refreshAllPanelsAfterDataMutation (int listIdx)
{
    rebuildSidebarFromAllLists();

    if (juce::isPositiveAndBelow (listIdx, allLists.size()))
        sidebarPanel.setSelectedIndex (listIdx);

    if (listIdx != activeListIndex)
    {
        updateMainDeskDisplay();
        return;
    }

    if (auto* list = getActiveListSafe())
    {
        layoutActiveListPads();

        if (list->isGrid && list->useCueListPanel)
            refreshCueListPanel();
        else if (list->isGrid)
            updateCuePlaybackIndicators();

        applyPadSelectionVisualState();
        refreshSidebarPlayingStatus();
        pushStageMonitorUpdate();
    }

    updateMainDeskDisplay();
    repaint();
}

void MainComponent::refreshGridLayoutAfterMutation (int listIdx)
{
    if (auto* list = allLists[listIdx])
    {
        rebuildHotkeyBindings();
        syncCueMetadataFromPads (*list);
    }

    if (listIdx == activeListIndex)
    {
        layoutActiveListPads();

        if (auto* panel = getPadPanel())
            panel->resyncAndLayout();
    }

    refreshAllPanelsAfterDataMutation (listIdx);
}

void MainComponent::refreshPadGridLayoutFast (int listIdx)
{
    if (listIdx == activeListIndex)
    {
        if (auto* panel = getPadPanel())
            panel->resyncAndLayout();
    }
}

void MainComponent::refreshListOrderAfterMutation (int listIdx)
{
    if (! juce::isPositiveAndBelow (listIdx, allLists.size()))
        return;

    auto* list = allLists[listIdx];
    if (list == nullptr)
        return;

    if (activeListIndex == listIdx)
    {
        applyPadSelectionVisualState();
        resized();

        if (list->isGrid && list->useCueListPanel)
            refreshCueListPanel();
        else if (cueListPanel != nullptr && list->isGrid)
            cueListPanel->setSelectedIndex (selectedBgmIndex);
    }

    rebuildSidebarFromAllLists();
    sidebarPanel.setSelectedIndex (listIdx);
}

void MainComponent::captureSelectionForUndoSnapshot (int listIdx,
                                                     juce::Array<int>& outSelection,
                                                     int& outPrimary) const
{
    outSelection.clear();
    outPrimary = -1;

    if (listIdx == activeListIndex)
    {
        outSelection = selectedPadIndices;
        outPrimary = selectedBgmIndex;
        return;
    }

    if (juce::isPositiveAndBelow (listIdx, allLists.size()))
    {
        if (auto* list = allLists[listIdx])
        {
            outSelection = list->savedPadSelection;
            outPrimary = list->savedPrimaryPadIndex;
        }
    }
}

void MainComponent::applySelectionFromUndoSnapshot (int listIdx,
                                                    const juce::Array<int>& selection,
                                                    int primary)
{
    if (! juce::isPositiveAndBelow (listIdx, allLists.size()))
        return;

    auto* list = allLists[listIdx];
    if (list == nullptr)
        return;

    juce::Array<int> nextSelection;

    for (auto idx : selection)
    {
        if (juce::isPositiveAndBelow (idx, list->pads.size()))
            nextSelection.addIfNotAlreadyThere (idx);
    }

    int nextPrimary = -1;

    if (! nextSelection.isEmpty())
    {
        if (juce::isPositiveAndBelow (primary, list->pads.size())
            && nextSelection.contains (primary))
        {
            nextPrimary = primary;
        }
        else
        {
            nextPrimary = nextSelection.getFirst();
        }
    }

    list->savedPadSelection = nextSelection;
    list->savedPrimaryPadIndex = nextPrimary;

    if (listIdx != activeListIndex)
        return;

    selectedPadIndices = nextSelection;
    selectedBgmIndex = nextPrimary;
    applyPadSelectionVisualState();

    if (list->isGrid && list->useCueListPanel && cueListPanel != nullptr)
    {
        if (selectedPadIndices.size() > 1)
            cueListPanel->setSelectedIndices (selectedPadIndices);
        else
            cueListPanel->setSelectedIndex (selectedBgmIndex);
    }

    if (juce::isPositiveAndBelow (selectedBgmIndex, list->pads.size()))
    {
        if (auto* pad = list->pads[selectedBgmIndex])
            presentPadInInspector (pad);
    }
    else
    {
        inspectorPanel.selectPad (nullptr);
    }
}

bool MainComponent::performApplicationUndo()
{
    if (sidebarPanel.isSearchBarFocused() || isSearchWindowFocused())
        return false;

    if (! undoManager.canUndo())
        return false;

    isPerformingUndoRedo.store (true, std::memory_order_release);
    const bool undone = undoManager.undo();
    isPerformingUndoRedo.store (false, std::memory_order_release);

    if (undone)
    {
        refreshAllPanelsAfterDataMutation (activeListIndex);
        triggerSave();
    }

    return undone;
}

bool MainComponent::performApplicationRedo()
{
    if (sidebarPanel.isSearchBarFocused() || isSearchWindowFocused())
        return false;

    if (! undoManager.canRedo())
        return false;

    isPerformingUndoRedo.store (true, std::memory_order_release);
    const bool redone = undoManager.redo();
    isPerformingUndoRedo.store (false, std::memory_order_release);

    if (redone)
    {
        refreshAllPanelsAfterDataMutation (activeListIndex);
        triggerSave();
    }

    return redone;
}

void MainComponent::performPadAudioCutWithUndo (SoundPad* pad, double cutStart, double cutEnd,
                                                std::function<void (bool success)> onDone)
{
    auto finish = [onDone = std::move (onDone)] (bool ok)
    {
        if (onDone)
            onDone (ok);
    };

    if (pad == nullptr || cutEnd <= cutStart + 0.005)
    {
        finish (false);
        return;
    }

    const int listIdx = findListIndexForPad (allLists, pad);

    if (listIdx < 0)
    {
        finish (false);
        return;
    }

    auto* list = allLists[listIdx];

    if (list == nullptr)
    {
        finish (false);
        return;
    }

    const int padIdx = list->pads.indexOf (pad);

    if (padIdx < 0)
    {
        finish (false);
        return;
    }

    auto beforeXml = capturePadUndoSnapshot (*list, padIdx);

    if (beforeXml == nullptr)
    {
        finish (false);
        return;
    }

    const juce::File sourceFile (pad->getFilePath());

    if (! sourceFile.existsAsFile())
    {
        finish (false);
        return;
    }

    const double oldTotalLen  = pad->getPlaybackLength();
    const double oldTrimStart = pad->getTrimStart();
    const double oldTrimEnd   = pad->getTrimEnd();
    double newTrimStart = 0.0;
    double newTrimEnd   = 0.0;
    showcontrol::audioedit::adjustTrimAfterCut (oldTrimStart, oldTrimEnd, oldTotalLen,
                                                cutStart, cutEnd, newTrimStart, newTrimEnd);

    pad->triggerStopImmediate();

    const auto destFile = showcontrol::audioedit::uniqueEditDestination (sourceFile);
    const auto beforeXmlShared = std::make_shared<juce::XmlElement> (*beforeXml);
    juce::Component::SafePointer<SoundPad> safePad (pad);

    showcontrol::background::enqueue ([this, sourceFile, destFile, cutStart, cutEnd,
                                       safePad, listIdx, padIdx, beforeXmlShared,
                                       newTrimStart, newTrimEnd, finish]() mutable
    {
        auto cutResult = showcontrol::audioedit::cutRegionToWavFile (sourceFile, destFile,
                                                                     cutStart, cutEnd);

        auto payload = cutResult.success
                           ? SoundPad::readPayloadFromFile (cutResult.outputFile)
                           : decltype (SoundPad::readPayloadFromFile (sourceFile)) {};

        if (cutResult.success && payload == nullptr)
        {
            cutResult.success = false;
            cutResult.outputFile.deleteFile();
        }

        auto payloadHolder = std::make_shared<decltype (payload)> (std::move (payload));

        juce::MessageManager::callAsync ([this, cutResult, safePad, listIdx, padIdx, beforeXmlShared,
                                          payloadHolder, newTrimStart, newTrimEnd, finish]() mutable
        {
            if (! cutResult.success)
            {
                finish (false);
                return;
            }

            if (safePad.getComponent() == nullptr)
            {
                finish (false);
                return;
            }

            auto afterXml = std::make_shared<juce::XmlElement> (*beforeXmlShared);
            afterXml->setAttribute ("file", cutResult.outputFile.getFullPathName());

            if (newTrimStart > 0.0)
                afterXml->setAttribute ("trimStart", newTrimStart);
            else
                afterXml->removeAttribute ("trimStart");

            if (newTrimEnd > 0.0)
                afterXml->setAttribute ("trimEnd", newTrimEnd);
            else
                afterXml->removeAttribute ("trimEnd");

            const int undoListIdx = listIdx;
            const int undoPadIdx  = padIdx;

            performUndoableMutation (juce::String::fromUTF8 (u8"Cắt âm thanh"),
                                     [this, undoListIdx, undoPadIdx, afterXml, payloadHolder]()
                                     {
                                         if (! juce::isPositiveAndBelow (undoListIdx, allLists.size()))
                                             return;

                                         auto* list = allLists[undoListIdx];

                                         if (list == nullptr
                                             || ! juce::isPositiveAndBelow (undoPadIdx, list->pads.size()))
                                             return;

                                         auto* pad = list->pads[undoPadIdx];

                                         if (pad == nullptr)
                                             return;

                                         if (payloadHolder != nullptr && *payloadHolder != nullptr)
                                         {
                                             const auto state = readPadProjectState (*afterXml);
                                             pad->setPendingProjectState (state);
                                             pad->setThumbnailLoadAllowed (true, false);
                                             pad->adoptPreloadedAudioPayload (std::move (*payloadHolder));
                                             payloadHolder->reset();
                                         }
                                         else
                                         {
                                             applyPadUndoSnapshot (undoListIdx, undoPadIdx, *afterXml);
                                         }

                                         if (undoListIdx == activeListIndex
                                             && inspectorPanel.getCurrentPad() == pad)
                                         {
                                             inspectorPanel.refreshWaveformFromPad();
                                         }

                                         triggerSave();
                                     },
                                     [this, undoListIdx, undoPadIdx, beforeXmlShared]()
                                     {
                                         applyPadUndoSnapshot (undoListIdx, undoPadIdx, *beforeXmlShared);
                                         triggerSave();
                                     });

            refreshAllPanelsAfterDataMutation (activeListIndex);
            finish (true);
        });
    });
}

void MainComponent::performUndoableMutation (const juce::String& transactionName,
                                           std::function<void()> performMutation,
                                           std::function<void()> undoMutation)
{
    if (isPerformingUndoRedo.load (std::memory_order_acquire))
    {
        if (performMutation)
            performMutation();

        return;
    }

    auto performShared = std::make_shared<std::function<void()>> (std::move (performMutation));
    auto undoShared    = std::make_shared<std::function<void()>> (std::move (undoMutation));

    undoManager.beginNewTransaction (transactionName);
    undoManager.perform (new GlobalStateUndoAction (
        [performShared]
        {
            if (performShared != nullptr && *performShared)
                (*performShared)();
        },
        [undoShared]
        {
            if (undoShared != nullptr && *undoShared)
                (*undoShared)();
        }));
}

MainComponent::ListOrderUndoState MainComponent::captureListOrderSnapshot (int listIdx) const
{
    ListOrderUndoState state;

    if (! juce::isPositiveAndBelow (listIdx, allLists.size()))
        return state;

    auto* list = allLists[listIdx];
    if (list == nullptr)
        return state;

    for (auto* pad : list->pads)
        state.padOrder.add (pad);

    state.cueMeta = list->cueMeta;
    captureSelectionForUndoSnapshot (listIdx, state.padSelection, state.primaryPadIndex);
    return state;
}

void MainComponent::restoreListOrderSnapshot (int listIdx, const ListOrderUndoState& state)
{
    if (! juce::isPositiveAndBelow (listIdx, allLists.size()))
        return;

    auto* list = allLists[listIdx];
    if (list == nullptr)
        return;

    list->pads.clearQuick (false);

    for (auto* pad : state.padOrder)
        list->pads.add (pad);

    list->cueMeta = state.cueMeta;

    for (int i = 0; i < list->pads.size(); ++i)
    {
        if (auto* pad = list->pads[i])
            pad->setPadIndex (i);
    }

    applySelectionFromUndoSnapshot (listIdx, state.padSelection, state.primaryPadIndex);
}

MainComponent::GridPositionsUndoState MainComponent::captureGridPositionsSnapshot (int listIdx) const
{
    GridPositionsUndoState state;

    if (! juce::isPositiveAndBelow (listIdx, allLists.size()))
        return state;

    auto* list = allLists[listIdx];
    if (list == nullptr)
        return state;

    for (auto* pad : list->pads)
    {
        if (pad == nullptr || ! pad->occupiesCueGridSlot())
            continue;

        GridPositionsUndoState::Entry entry;
        entry.pad  = pad;
        entry.row  = pad->getGridRow();
        entry.col  = pad->getGridCol();
        state.entries.add (entry);
    }

    captureSelectionForUndoSnapshot (listIdx, state.padSelection, state.primaryPadIndex);
    return state;
}

void MainComponent::restoreGridPositionsSnapshot (int listIdx, const GridPositionsUndoState& state)
{
    if (! juce::isPositiveAndBelow (listIdx, allLists.size()))
        return;

    for (const auto& entry : state.entries)
    {
        if (entry.pad == nullptr)
            continue;

        entry.pad->setGridPosition (entry.row, entry.col, true);
        entry.pad->refreshHotkeyLabel();
    }

    refreshPadGridLayoutFast (listIdx);
    applySelectionFromUndoSnapshot (listIdx, state.padSelection, state.primaryPadIndex);
}

MainComponent::PlaylistSnapshotUndoState MainComponent::capturePlaylistSnapshot (int listIdx) const
{
    PlaylistSnapshotUndoState state;
    state.listIdx = listIdx;
    state.activeListIndexAtCapture = activeListIndex;
    captureSelectionForUndoSnapshot (activeListIndex,
                                     state.activePadSelection,
                                     state.activePrimaryPadIndex);

    if (! juce::isPositiveAndBelow (listIdx, allLists.size()))
        return state;

    auto* list = allLists[listIdx];
    if (list == nullptr)
        return state;

    state.sidebarName       = sidebarPanel.getListName (listIdx);
    state.isGrid            = list->isGrid;
    state.isLooping         = list->isLooping;
    state.useCueListPanel   = list->useCueListPanel;
    state.clickPadToTrigger = list->clickPadToTrigger;
    state.autoArmOnSelect   = list->autoArmOnSelect;
    state.isLocked          = list->isLocked;
    state.themeColour       = list->themeColour;
    state.cueMeta           = list->cueMeta;

    for (int i = 0; i < list->pads.size(); ++i)
    {
        auto* padXml = new juce::XmlElement ("Pad");

        if (auto* pad = list->pads[i])
        {
            padXml->setAttribute ("file", pad->getFilePath());
            padXml->setAttribute ("index", i);
            writePadProjectState (*padXml, *pad);

            if (juce::isPositiveAndBelow (i, list->cueMeta.size()))
                writeCueMetaToPadElem (*padXml, list->cueMeta.getReference (i));
        }

        state.padXmls.add (padXml);
    }

    return state;
}

void MainComponent::applyUndoXmlToPad (ListData& list, SoundPad* pad, const juce::XmlElement& padElem)
{
    if (pad == nullptr)
        return;

    const juce::String filePath = padElem.getStringAttribute ("file", "");
    CueItem cueMeta;
    readCueMetaFromPadElem (padElem, cueMeta);

    if (filePath.isNotEmpty())
    {
        const auto projectState = readPadProjectState (padElem);
        const bool loopTrack = padElem.getBoolAttribute ("loopTrack", false);
        const bool loop = list.isGrid ? list.isLooping : loopTrack;

        pad->setPendingProjectState (projectState);
        pad->configurePad (filePath, projectState.outputGain, loop);
        pad->updateTheme (isDarkMode);
        pad->setRenderMode (list.isGrid);
        pad->setClickToTrigger (list.clickPadToTrigger);
        pad->setCueListPlayback (list.isGrid);

        if (list.isGrid)
            pad->setLooping (list.isLooping);
        else if (loopTrack)
            pad->setLooping (true);
    }
    else
    {
        pad->setClickToTrigger (list.clickPadToTrigger);
    }

    pad->setTagColour (cueMeta.tagColour);

    if (list.isGrid && padElem.hasAttribute ("gridRow"))
    {
        pad->setGridPosition (padElem.getIntAttribute ("gridRow", 0),
                              padElem.getIntAttribute ("gridCol", 0));
    }
}

SoundPad* MainComponent::insertPadFromUndoXml (ListData& list, int index, const juce::XmlElement& padElem)
{
    auto* pad = createSoundPad();
    scrollContent->addChildComponent (pad);

    const int insertIdx = juce::jlimit (0, list.pads.size(), index);
    list.pads.insert (insertIdx, pad);

    applyUndoXmlToPad (list, pad, padElem);
    wireSoundPad (pad);
    pad->setVisible (true);
    return pad;
}

void MainComponent::hidePadsForAllListsExcept (int visibleListIdx)
{
    for (int i = 0; i < allLists.size(); ++i)
    {
        if (auto* list = allLists[i])
        {
            const bool showPads = (i == visibleListIdx);

            for (auto* p : list->pads)
            {
                if (p != nullptr && ! showPads)
                    p->setVisible (false);
            }
        }
    }
}

std::unique_ptr<juce::XmlElement> MainComponent::capturePadUndoSnapshot (const ListData& list, int padIdx) const
{
    auto xml = std::make_unique<juce::XmlElement> ("Pad");

    if (! juce::isPositiveAndBelow (padIdx, list.pads.size()))
        return xml;

    auto* pad = list.pads[padIdx];

    if (pad == nullptr)
        return xml;

    xml->setAttribute ("file", pad->getFilePath());
    xml->setAttribute ("index", padIdx);
    writePadProjectState (*xml, *pad);

    if (juce::isPositiveAndBelow (padIdx, list.cueMeta.size()))
    {
        writeCueMetaToPadElem (*xml, list.cueMeta.getReference (padIdx));

        const auto& cueName = list.cueMeta.getReference (padIdx).name;

        if (cueName.isNotEmpty())
            xml->setAttribute ("cueName", cueName);
    }

    return xml;
}

void MainComponent::applyPadUndoSnapshot (int listIdx, int padIdx, const juce::XmlElement& xml)
{
    if (! juce::isPositiveAndBelow (listIdx, allLists.size()))
        return;

    auto* list = allLists[listIdx];

    if (list == nullptr || ! juce::isPositiveAndBelow (padIdx, list->pads.size()))
        return;

    auto* pad = list->pads[padIdx];

    if (pad == nullptr)
        return;

    applyUndoXmlToPad (*list, pad, xml);

    CueItem meta;
    readCueMetaFromPadElem (xml, meta);

    if (xml.hasAttribute ("cueName"))
        meta.name = xml.getStringAttribute ("cueName");
    else
        meta.name = pad->getPadName();

    while (list->cueMeta.size() <= padIdx)
        list->cueMeta.add ({});

    list->cueMeta.set (padIdx, meta);
    refreshTagColourLiveUi (*list, padIdx);

    if (listIdx == activeListIndex)
    {
        if (inspectorPanel.getCurrentPad() == pad)
            inspectorPanel.selectPad (pad);

        applyPadSelectionVisualState();
        refreshCueListPanel (false);
        layoutActiveListPads();
    }
}

void MainComponent::restoreListOrderAndDeleteOrphanPads (int listIdx, const ListOrderUndoState& before)
{
    if (! juce::isPositiveAndBelow (listIdx, allLists.size()))
        return;

    auto* list = allLists[listIdx];

    if (list == nullptr)
        return;

    juce::Array<SoundPad*> orphans;

    for (auto* p : list->pads)
    {
        if (p != nullptr && ! before.padOrder.contains (p))
            orphans.add (p);
    }

    restoreListOrderSnapshot (listIdx, before);

    for (auto* p : orphans)
    {
        safelyPreparePadForDeletion (p);

        if (scrollContent != nullptr)
            scrollContent->removeChildComponent (p);

        delete p;
    }
}

void MainComponent::applyTrackRenameAtIndex (int listIdx,
                                             int cueIndex,
                                             const juce::String& newName,
                                             bool saveNow)
{
    if (! juce::isPositiveAndBelow (listIdx, allLists.size()))
        return;

    auto* list = allLists[listIdx];

    if (list == nullptr || ! juce::isPositiveAndBelow (cueIndex, list->pads.size()))
        return;

    if (juce::isPositiveAndBelow (cueIndex, list->cueMeta.size()))
        list->cueMeta.getReference (cueIndex).name = newName;

    if (auto* pad = list->pads[cueIndex])
    {
        pad->setCustomName (newName);
        pad->repaint();

        if (inspectorPanel.getCurrentPad() == pad)
            inspectorPanel.refreshPadDisplayName();
    }

    if (listIdx == activeListIndex)
    {
        if (list->isGrid && list->useCueListPanel && cueListPanel != nullptr)
            cueListPanel->repaintCueRow (cueIndex);

        refreshCueListPanel (false);
    }

    if (saveNow)
        saveApplicationState();
}

void MainComponent::applyTrackRenameWithUndo (int listIdx,
                                              int cueIndex,
                                              const juce::String& newName,
                                              const juce::String& oldName)
{
    if (newName == oldName)
        return;

    performUndoableMutation (juce::String::fromUTF8 (u8"Đổi tên"),
                             [this, listIdx, cueIndex, newName]()
                             {
                                 applyTrackRenameAtIndex (listIdx, cueIndex, newName, true);
                             },
                             [this, listIdx, cueIndex, oldName]()
                             {
                                 applyTrackRenameAtIndex (listIdx, cueIndex, oldName, true);
                             });
}

void MainComponent::performActiveListIngestWithUndo (std::function<void (ListData&)> ingestFn)
{
    if (! juce::isPositiveAndBelow (activeListIndex, allLists.size()) || ingestFn == nullptr)
        return;

    auto* list = allLists[activeListIndex];

    if (list == nullptr || list->isLocked)
        return;

    const int listIdx = activeListIndex;
    const auto beforeOrder = std::make_shared<ListOrderUndoState> (captureListOrderSnapshot (listIdx));

    performUndoableMutation (juce::String::fromUTF8 (u8"Nạp bài hát"),
                             [this, listIdx, ingestFn, beforeOrder]()
                             {
                                 juce::ignoreUnused (beforeOrder);

                                 if (auto* target = allLists[listIdx])
                                     ingestFn (*target);

                                 finalizeAfterFileDropIngest();
                             },
                             [this, listIdx, beforeOrder]()
                             {
                                 restoreListOrderAndDeleteOrphanPads (listIdx, *beforeOrder);
                                 finalizeAfterFileDropIngest();
                                 refreshAllPanelsAfterDataMutation (listIdx);
                                 triggerSave();
                             });
}

void MainComponent::beginInspectorPadUndoSession (SoundPad* pad)
{
    if (isPerformingUndoRedo.load (std::memory_order_acquire) || pad == nullptr)
        return;

    if (inspectorPadUndoSession.beforeXml != nullptr)
        return;

    const int listIdx = findListIndexForPad (allLists, pad);

    if (listIdx < 0)
        return;

    auto* list = allLists[listIdx];

    if (list == nullptr)
        return;

    const int padIdx = list->pads.indexOf (pad);

    if (padIdx < 0)
        return;

    inspectorPadUndoSession.listIdx = listIdx;
    inspectorPadUndoSession.padIdx = padIdx;
    inspectorPadUndoSession.beforeXml = capturePadUndoSnapshot (*list, padIdx);
}

void MainComponent::commitInspectorPadUndoSession (SoundPad* pad)
{
    if (inspectorPadUndoSession.beforeXml == nullptr || pad == nullptr)
        return;

    const int listIdx = findListIndexForPad (allLists, pad);

    if (listIdx != inspectorPadUndoSession.listIdx)
    {
        inspectorPadUndoSession = {};
        return;
    }

    auto* list = allLists[listIdx];

    if (list == nullptr)
    {
        inspectorPadUndoSession = {};
        return;
    }

    const int padIdx = list->pads.indexOf (pad);

    if (padIdx != inspectorPadUndoSession.padIdx)
    {
        inspectorPadUndoSession = {};
        return;
    }

    auto afterXml = capturePadUndoSnapshot (*list, padIdx);

    if (afterXml == nullptr
        || inspectorPadUndoSession.beforeXml->isEquivalentTo (afterXml.get(), true))
    {
        inspectorPadUndoSession = {};
        return;
    }

    const auto beforeXml = std::make_shared<juce::XmlElement> (*inspectorPadUndoSession.beforeXml);
    const auto afterXmlShared = std::make_shared<juce::XmlElement> (*afterXml);
    const int undoListIdx = listIdx;
    const int undoPadIdx = padIdx;
    inspectorPadUndoSession = {};

    performUndoableMutation (juce::String::fromUTF8 (u8"Chỉnh Inspector"),
                             [this, undoListIdx, undoPadIdx, afterXmlShared]()
                             {
                                 applyPadUndoSnapshot (undoListIdx, undoPadIdx, *afterXmlShared);
                                 triggerSave();
                             },
                             [this, undoListIdx, undoPadIdx, beforeXml]()
                             {
                                 applyPadUndoSnapshot (undoListIdx, undoPadIdx, *beforeXml);
                                 triggerSave();
                             });
}

void MainComponent::applyTagColourWithUndo (int listIdx, int index, juce::Colour colour)
{
    if (! juce::isPositiveAndBelow (listIdx, allLists.size()))
        return;

    auto* list = allLists[listIdx];

    if (list == nullptr || ! juce::isPositiveAndBelow (index, list->pads.size()))
        return;

    juce::Colour oldColour = showcontrol::colours::defaultTagColour();

    if (juce::isPositiveAndBelow (index, list->cueMeta.size()))
        oldColour = list->cueMeta.getReference (index).tagColour;
    else if (auto* pad = list->pads[index])
        oldColour = pad->getTagColour();

    const auto snappedNew = showcontrol::colours::snapToPalette (colour);
    const auto snappedOld = showcontrol::colours::snapToPalette (oldColour);

    if (snappedNew == snappedOld)
        return;

    performUndoableMutation (juce::String::fromUTF8 (u8"Đổi màu"),
                             [this, listIdx, index, snappedNew]()
                             {
                                 applyTagColourToPadAndCue (*allLists[listIdx], index, snappedNew);
                             },
                             [this, listIdx, index, snappedOld]()
                             {
                                 applyTagColourToPadAndCue (*allLists[listIdx], index, snappedOld);
                             });
}

void MainComponent::restorePlaylistSnapshot (const PlaylistSnapshotUndoState& state)
{
    const int idx = juce::jlimit (0, allLists.size(), state.listIdx);

    auto* newList = new ListData();
    newList->isGrid            = state.isGrid;
    newList->isLooping         = state.isLooping;
    newList->useCueListPanel   = state.useCueListPanel;
    newList->clickPadToTrigger = state.clickPadToTrigger;
    newList->autoArmOnSelect   = state.autoArmOnSelect;
    newList->isLocked          = state.isLocked;
    newList->themeColour       = state.themeColour;
    newList->cueMeta           = state.cueMeta;

    for (int i = 0; i < state.padXmls.size(); ++i)
    {
        auto* pad = createSoundPad();
        scrollContent->addChildComponent (pad);
        newList->pads.add (pad);

        if (auto* padXml = state.padXmls[i])
            applyUndoXmlToPad (*newList, pad, *padXml);
        else
            applyProjectDefaultsToPad (pad);

        wireSoundPad (pad);
        pad->setPadIndex (i);
        pad->setVisible (false);
    }

    juce::Array<juce::String> names;
    for (int i = 0; i < sidebarPanel.getListCount(); ++i)
        names.add (sidebarPanel.getListName (i));

    names.insert (idx, state.sidebarName);

    // Chèn danh sách làm lệch index — cập nhật TRƯỚC insert để activeListIndex vẫn trỏ đúng list đang xem.
    if (activeListIndex >= idx)
        ++activeListIndex;

    allLists.insert (idx, newList);

    rebuildDefaultHotkeysForList (idx);

    for (int i = idx + 1; i < allLists.size(); ++i)
        rebuildDefaultHotkeysForList (i);

    syncSidebarFromAllLists (names);

    const int viewIdx = juce::jlimit (0, allLists.size() - 1, activeListIndex);

    sidebarPanel.setSelectedIndex (viewIdx);
    loadList (viewIdx,
              allLists[viewIdx]->pads.size(),
              allLists[viewIdx]->isGrid);
    refreshAllPanelsAfterDataMutation (viewIdx);
}

void MainComponent::deleteListAtIndexImpl (int idx)
{
    if (idx < 0 || idx >= allLists.size())
        return;

    if (auto* removed = allLists[idx])
    {
        // Chỉ dừng các pad đang transport-active TRONG list bị xóa — không đụng list khác.
        surgicalStopTransportActivePadsInList (*removed);

        detachInspectorFromPadsInList (removed);
        detachDeckUiReferencesIfPadInList (*removed);

        if (soloPad != nullptr)
        {
            for (auto* p : removed->pads)
            {
                if (p == soloPad)
                {
                    setSoloPad (nullptr, false);
                    break;
                }
            }
        }

        for (auto* p : removed->pads)
            unregisterPadFromMixer (p);
    }

    juce::Array<juce::String> names;
    for (int i = 0; i < sidebarPanel.getListCount(); ++i)
        names.add (sidebarPanel.getListName (i));

    allLists.remove (idx);
    names.remove (idx);

    syncSidebarFromAllLists (names);

    if (allLists.isEmpty())
    {
        sidebarPanel.setSelectedIndex (-1);
        loadList (-1, 0, true);
    }
    else
    {
        const int next = juce::jlimit (0, allLists.size() - 1, idx);
        sidebarPanel.setSelectedIndex (next);
        loadList (next, sidebarPanel.getListTrackCount (next), allLists[next]->isGrid);
    }
}

MainComponent::ListDeletionUndoState MainComponent::captureListDeletionSnapshot (
    int listIdx, const juce::Array<int>& padIndices) const
{
    ListDeletionUndoState state;
    state.listIdx = listIdx;
    captureSelectionForUndoSnapshot (listIdx, state.padSelection, state.primaryPadIndex);

    if (! juce::isPositiveAndBelow (listIdx, allLists.size()))
        return state;

    auto* list = allLists[listIdx];
    if (list == nullptr)
        return state;

    auto sorted = padIndices;
    sorted.sort();

    for (const int idx : sorted)
    {
        if (! juce::isPositiveAndBelow (idx, list->pads.size()))
            continue;

        auto* pad = list->pads[idx];
        if (pad == nullptr)
            continue;

        DeletedPadUndoEntry entry;
        entry.index = idx;
        entry.padXml = std::make_unique<juce::XmlElement> ("Pad");
        entry.padXml->setAttribute ("file", pad->getFilePath());
        entry.padXml->setAttribute ("index", idx);
        writePadProjectState (*entry.padXml, *pad);

        if (juce::isPositiveAndBelow (idx, list->cueMeta.size()))
            writeCueMetaToPadElem (*entry.padXml, list->cueMeta.getReference (idx));

        state.removedPads.add (std::move (entry));
    }

    return state;
}

SoundPad* MainComponent::restorePadFromUndoXml (ListData& list, int index, const juce::XmlElement& padElem)
{
    if (list.isGrid && index >= kMaxCuePadsPerList)
        return nullptr;

    auto* pad = ensurePadSlotAtIndex (list, index);
    if (pad == nullptr)
        return nullptr;

    applyUndoXmlToPad (list, pad, padElem);
    wireSoundPad (pad);
    pad->setVisible (true);
    return pad;
}

void MainComponent::restoreListDeletionSnapshot (const ListDeletionUndoState& state)
{
    if (! juce::isPositiveAndBelow (state.listIdx, allLists.size()))
        return;

    auto* list = allLists[state.listIdx];
    if (list == nullptr || list->isLocked)
        return;

    juce::Array<int> restoreOrder;
    for (int i = 0; i < state.removedPads.size(); ++i)
        restoreOrder.add (i);

    struct RestoreIndexDescending
    {
        const juce::Array<DeletedPadUndoEntry>* entries = nullptr;

        int compareElements (int a, int b) const
        {
            return entries->getReference (b).index - entries->getReference (a).index;
        }
    };

    RestoreIndexDescending restoreSorter;
    restoreSorter.entries = &state.removedPads;
    restoreOrder.sort (restoreSorter);

    for (const int entryIdx : restoreOrder)
    {
        const auto& entry = state.removedPads.getReference (entryIdx);
        if (entry.padXml == nullptr)
            continue;

        auto* restored = insertPadFromUndoXml (*list, entry.index, *entry.padXml);

        if (restored == nullptr)
            continue;

        CueItem meta;
        readCueMetaFromPadElem (*entry.padXml, meta);
        meta.cueNumber = entry.index + 1;

        if (meta.name.isEmpty())
            meta.name = restored->getPadName();

        while (list->cueMeta.size() <= entry.index)
            list->cueMeta.add ({});

        list->cueMeta.set (entry.index, meta);
    }

    for (int i = 0; i < list->pads.size(); ++i)
    {
        if (auto* pad = list->pads[i])
            pad->setPadIndex (i);
    }

    if (list->isGrid)
    {
        syncCueMetadataFromPads (*list);
        rebuildDefaultHotkeysForList (state.listIdx);
    }

    if (state.listIdx == activeListIndex)
    {
        if (list->isGrid)
            updateCueGridUIFromData (*list);
        else
            resized();
    }

    refreshAllPanelsAfterDataMutation (state.listIdx);

    juce::Array<int> restoredSelection;

    for (const auto& entry : state.removedPads)
        restoredSelection.addIfNotAlreadyThere (entry.index);

    if (! restoredSelection.isEmpty())
        applySelectionFromUndoSnapshot (state.listIdx, restoredSelection, restoredSelection.getFirst());
    else
        applySelectionFromUndoSnapshot (state.listIdx, state.padSelection, state.primaryPadIndex);
}

void MainComponent::performPadGridMutationWithUndo (int listIdx,
                                                  const juce::String& actionName,
                                                  std::function<void()> mutation)
{
    if (! juce::isPositiveAndBelow (listIdx, allLists.size()) || mutation == nullptr)
        return;

    auto gridBefore = std::make_shared<GridPositionsUndoState> (captureGridPositionsSnapshot (listIdx));

    performUndoableMutation (actionName,
                           [this, listIdx, mutation, gridBefore]()
                           {
                               mutation();
                               refreshPadGridLayoutFast (listIdx);
                               triggerSave();
                           },
                           [this, listIdx, gridBefore]()
                           {
                               restoreGridPositionsSnapshot (listIdx, *gridBefore);
                               triggerSave();
                           });
}

void MainComponent::applyListSortAscending (int listIdx)
{
    if (! juce::isPositiveAndBelow (listIdx, allLists.size()))
        return;

    auto* list = allLists[listIdx];
    if (list == nullptr || list->isLocked || list->pads.size() <= 1)
        return;

    if (list->isGrid && ! list->useCueListPanel)
        return;

    const auto displayNameForIndex = [list] (int index) -> juce::String
    {
        if (auto* pad = list->pads[index])
        {
            const juce::String padName = pad->getPadName();
            if (padName.isNotEmpty())
                return padName;
        }

        if (juce::isPositiveAndBelow (index, list->cueMeta.size()))
        {
            const auto& metaName = list->cueMeta.getReference (index).name;
            if (metaName.isNotEmpty())
                return metaName;
        }

        return juce::String();
    };

    juce::Array<int> order;

    for (int i = 0; i < list->pads.size(); ++i)
        order.add (i);

    std::sort (order.begin(), order.end(),
               [&displayNameForIndex] (int a, int b)
               {
                   return displayNameForIndex (a).compareNatural (displayNameForIndex (b)) < 0;
               });

    juce::Array<SoundPad*> sortedPtrs;

    for (const int srcIdx : order)
        sortedPtrs.add (list->pads[srcIdx]);

    list->pads.clearQuick (false);

    for (auto* pad : sortedPtrs)
        list->pads.add (pad);

    // Sticky layout: chỉ đổi thứ tự tuyến tính cueMeta — gridRow/gridCol trên SoundPad không đổi.
    if (list->isGrid && list->useCueListPanel)
        syncCueMetadataFromPads (*list);

    for (int i = 0; i < list->pads.size(); ++i)
    {
        if (auto* pad = list->pads[i])
            pad->setPadIndex (i);
    }
}

void MainComponent::sortListTracksAscending (int listIdx)
{
    if (! juce::isPositiveAndBelow (listIdx, allLists.size()))
        return;

    auto* list = allLists[listIdx];
    if (list == nullptr || list->isLocked || list->pads.size() <= 1)
        return;

    if (list->isGrid && ! list->useCueListPanel)
        return;

    const auto displayNameForIndex = [list] (int index) -> juce::String
    {
        if (auto* pad = list->pads[index])
        {
            const juce::String padName = pad->getPadName();
            if (padName.isNotEmpty())
                return padName;
        }

        if (juce::isPositiveAndBelow (index, list->cueMeta.size()))
        {
            const auto& metaName = list->cueMeta.getReference (index).name;
            if (metaName.isNotEmpty())
                return metaName;
        }

        return juce::String();
    };

    juce::Array<int> order;

    for (int i = 0; i < list->pads.size(); ++i)
        order.add (i);

    std::sort (order.begin(), order.end(),
               [&displayNameForIndex] (int a, int b)
               {
                   return displayNameForIndex (a).compareNatural (displayNameForIndex (b)) < 0;
               });

    for (int i = 0; i < order.size(); ++i)
    {
        if (order.getUnchecked (i) != i)
        {
            const auto beforeState = std::make_shared<ListOrderUndoState> (captureListOrderSnapshot (listIdx));

            performUndoableMutation (juce::String::fromUTF8 (u8"Sắp xếp A-Z"),
                                     [this, listIdx]()
                                     {
                                         applyListSortAscending (listIdx);
                                         refreshAllPanelsAfterDataMutation (listIdx);
                                         triggerSave();
                                     },
                                     [this, listIdx, beforeState]()
                                     {
                                         restoreListOrderSnapshot (listIdx, *beforeState);
                                         refreshAllPanelsAfterDataMutation (listIdx);
                                         triggerSave();
                                     });
            return;
        }
    }
}

void MainComponent::surgicalStopPadIfTransportActive (SoundPad* pad) noexcept
{
    if (pad == nullptr || ! pad->isTransportActive())
        return;

    audioEngine.stopCue (pad);
}

void MainComponent::surgicalStopTransportActivePadsInList (const ListData& list) noexcept
{
    for (auto* pad : list.pads)
        surgicalStopPadIfTransportActive (pad);
}

void MainComponent::detachDeckUiReferencesIfPadInList (const ListData& list) noexcept
{
    if (auto* playing = findGloballyPrioritizedPlayingPad())
    {
        for (auto* p : list.pads)
        {
            if (p == playing)
            {
                masterDeckPanel.setActivePad (nullptr);
                break;
            }
        }
    }

    if (lastUiSyncedPlayingPad != nullptr)
    {
        for (auto* p : list.pads)
        {
            if (p == lastUiSyncedPlayingPad)
            {
                lastUiSyncedPlayingPad = nullptr;
                break;
            }
        }
    }
}

void MainComponent::safelyPreparePadForDeletion (SoundPad* pad)
{
    if (pad == nullptr)
        return;

    pad->cancelPendingAsyncWork();

    // Phẫu thuật đích danh: chỉ dừng CHÍNH pad bị xóa nếu đang phát ra loa.
    surgicalStopPadIfTransportActive (pad);

    unregisterPadFromMixer (pad);
    pad->releaseThumbnailResources();
}

void MainComponent::deletePadFromList (SoundPad* pad)
{
    if (pad == nullptr)
        return;

    const int listIdx = findListIndexForPad (allLists, pad);
    if (listIdx < 0)
        return;

    auto* list = allLists[listIdx];
    if (list == nullptr)
        return;

    const int padIdx = list->pads.indexOf (pad);
    if (padIdx < 0)
        return;

    juce::Array<int> target { padIdx };
    deletePadsFromList (listIdx, target);
}

juce::Array<int> MainComponent::collectActiveListDeletionIndices() const
{
    juce::Array<int> indices;

    if (activeListIndex < 0 || activeListIndex >= allLists.size())
        return indices;

    auto* list = allLists[activeListIndex];
    if (list == nullptr)
        return indices;

    if (isCueListViewActive() && cueListPanel != nullptr)
    {
        indices = cueListPanel->getSelectedRowIndices();

        if (! indices.isEmpty())
            return indices;
    }

    indices = selectedPadIndices;

    if (indices.isEmpty() && selectedBgmIndex >= 0 && selectedBgmIndex < list->pads.size())
        indices.add (selectedBgmIndex);

    return indices;
}

bool MainComponent::handleDeleteKeyForActiveSelection()
{
    if (isPlaybackCommandBlocked())
        return false;

    if (allLists.isEmpty() || activeListIndex < 0 || activeListIndex >= allLists.size())
        return false;

    auto* list = allLists[activeListIndex];
    if (list == nullptr || list->isLocked)
        return false;

    if (collectActiveListDeletionIndices().isEmpty())
        return false;

    promptDeleteSelectedPadsConfirmation();
    return true;
}

void MainComponent::promptDeleteSelectedPadsConfirmation()
{
    const auto indices = collectActiveListDeletionIndices();
    if (indices.isEmpty())
        return;

    if (activeListIndex < 0 || activeListIndex >= allLists.size())
        return;

    auto* list = allLists[activeListIndex];
    if (list == nullptr || list->isLocked)
        return;

    juce::String title;
    juce::String subtext;

    if (indices.size() == 1)
    {
        const int idx = indices.getFirst();
        if (idx < 0 || idx >= list->pads.size())
            return;

        if (auto* pad = list->pads[idx])
        {
            title = showcontrol::localization::tr (u8"Xác nhận xóa bài hát khỏi kịch bản?");
            subtext = juce::String::fromUTF8 (u8"\"") + pad->getPadName() + juce::String::fromUTF8 (u8"\" — ")
                    + showcontrol::localization::tr (u8"Hành động này không thể hoàn tác.");
        }
        else
        {
            return;
        }
    }
    else
    {
        title = showcontrol::localization::tr (u8"Xóa nhiều bài hát")
              + juce::String::fromUTF8 (u8" (") + juce::String (indices.size()) + juce::String::fromUTF8 (u8")");
        subtext = showcontrol::localization::tr (u8"Bạn có chắc chắn muốn xóa mục này khỏi kịch bản?")
                + juce::String::fromUTF8 (u8" ")
                + showcontrol::localization::tr (u8"Hành động này không thể hoàn tác.");
    }

    juce::Component::SafePointer<MainComponent> safeThis (this);
    showcontrol::ui::showConfirmDeleteDialog (this,
                                              title,
                                              subtext,
                                              showcontrol::localization::tr (u8"Xoá"),
                                              [safeThis] (bool confirmed)
                                              {
                                                  if (confirmed && safeThis != nullptr)
                                                      safeThis->deleteSelectedPadsFromActiveList();
                                              },
                                              nullptr);
}

bool MainComponent::tryHandleDeleteOrBackspaceKey (const juce::KeyPress& key)
{
    const int keyCode = showcontrol::keyboard::physicalKeyCode (key);

    if (keyCode != juce::KeyPress::deleteKey && keyCode != juce::KeyPress::backspaceKey)
        return false;

    if (! key.getModifiers().isCommandDown())
        return false;

    const juce::uint32 nowMs = juce::Time::getMillisecondCounter();

    if (shouldDebounceDeleteKey (keyCode, nowMs))
        return true;

    return handleDeleteKeyForActiveSelection();
}

void MainComponent::deleteSelectedPadsFromActiveList()
{
    const auto indices = collectActiveListDeletionIndices();
    if (indices.isEmpty())
        return;

    deletePadsFromList (activeListIndex, indices);
}

void MainComponent::compactCueListPads (ListData& list)
{
    if (! list.isGrid)
        return;

    for (int i = list.pads.size() - 1; i >= 0; --i)
    {
        auto* pad = list.pads[i];
        if (pad == nullptr || pad->occupiesCueGridSlot())
            continue;

        safelyPreparePadForDeletion (pad);

        if (scrollContent != nullptr)
            scrollContent->removeChildComponent (pad);

        list.pads.remove (i);
    }

    for (int i = 0; i < list.pads.size(); ++i)
        if (list.pads[i] != nullptr)
            list.pads[i]->setPadIndex (i);

    syncCueMetadataFromPads (list);
}

void MainComponent::updateCueGridUIFromData (ListData& list)
{
    if (! list.isGrid || scrollContent == nullptr)
        return;

    syncCueMetadataFromPads (list);

    for (int i = 0; i < list.pads.size(); ++i)
    {
        auto* p = list.pads[i];
        if (p == nullptr)
            continue;

        scrollContent->addChildComponent (p);
        p->setPadIndex (i);
        p->setCueListPlayback (true);
        p->setRenderMode (true);
        p->setClickToTrigger (false);
        wireSoundPad (p);
    }

    applyPadSelectionVisualState();
    refreshCueListPanel();
    layoutActiveListPads();
    repaint();
}

juce::Array<int> MainComponent::buildBulkDeleteIndicesDescending (const juce::Array<int>& indices, int listSize)
{
    juce::SparseSet<int> uniqueRows;

    for (const int idx : indices)
    {
        if (juce::isPositiveAndBelow (idx, listSize))
            uniqueRows.addRange (juce::Range<int> (idx, idx + 1));
    }

    juce::Array<int> descending;

    for (int rangeIdx = uniqueRows.getNumRanges(); --rangeIdx >= 0;)
    {
        const auto range = uniqueRows.getRange (rangeIdx);

        for (int idx = range.getEnd() - 1; idx >= range.getStart(); --idx)
            descending.add (idx);
    }

    return descending;
}

void MainComponent::deletePadsFromList (int listIdx, const juce::Array<int>& padIndices)
{
    if (listIdx < 0 || listIdx >= allLists.size() || padIndices.isEmpty())
        return;

    if (isPerformingUndoRedo.load (std::memory_order_acquire))
    {
        deletePadsFromListImpl (listIdx, padIndices);
        return;
    }

    const auto deletionSnap = std::make_shared<ListDeletionUndoState> (
        captureListDeletionSnapshot (listIdx, padIndices));
    const auto indicesCopy = padIndices;

    const juce::String undoLabel = indicesCopy.size() > 1
                                       ? juce::String::fromUTF8 (u8"Xóa nhiều track")
                                       : juce::String::fromUTF8 (u8"Xóa track");

    performUndoableMutation (undoLabel,
                             [this, listIdx, indicesCopy]()
                             {
                                 deletePadsFromListImpl (listIdx, indicesCopy);
                                 triggerSave();
                             },
                             [this, deletionSnap]()
                             {
                                 restoreListDeletionSnapshot (*deletionSnap);
                                 refreshAllPanelsAfterDataMutation (deletionSnap->listIdx);
                                 triggerSave();
                             });
}

void MainComponent::deletePadsFromListImpl (int listIdx, const juce::Array<int>& padIndices)
{
    if (listIdx < 0 || listIdx >= allLists.size() || padIndices.isEmpty())
        return;

    auto* list = allLists[listIdx];
    if (list == nullptr || list->isLocked)
        return;

    const auto rowsToRemove = buildBulkDeleteIndicesDescending (padIndices, list->pads.size());

    if (rowsToRemove.isEmpty())
        return;

    juce::SparseSet<int> selectedRows;

    for (const int idx : rowsToRemove)
        selectedRows.addRange (juce::Range<int> (idx, idx + 1));

    if (listIdx == activeListIndex && padReorderActive)
    {
        if (selectedRows.contains (padReorderFromIndex))
            cancelPadReorder();
    }

    if (soloPad != nullptr && list->pads.contains (soloPad))
    {
        const int soloIdx = list->pads.indexOf (soloPad);
        if (soloIdx >= 0 && selectedRows.contains (soloIdx))
            setSoloPad (nullptr, false);
    }

    // Chỉ ngắt Inspector/Deck nếu pad bị xóa đang được UI tham chiếu — không reset khi xóa track khác.
    if (auto* currentInspectorPad = inspectorPanel.getCurrentPad())
    {
        const int inspIdx = list->pads.indexOf (currentInspectorPad);

        if (inspIdx >= 0 && selectedRows.contains (inspIdx))
            inspectorPanel.selectPad (nullptr);
    }

    if (auto* playing = findGloballyPrioritizedPlayingPad())
    {
        const int playingIdx = list->pads.indexOf (playing);

        if (playingIdx >= 0 && selectedRows.contains (playingIdx))
            masterDeckPanel.setActivePad (nullptr);
    }

    if (lastUiSyncedPlayingPad != nullptr)
    {
        const int syncedIdx = list->pads.indexOf (lastUiSyncedPlayingPad);

        if (syncedIdx >= 0 && selectedRows.contains (syncedIdx))
            lastUiSyncedPlayingPad = nullptr;
    }

    const int lowestDeletedRow = rowsToRemove.getLast();
    const int targetRowAfterDelete = juce::jmax (0, lowestDeletedRow - 1);

    // Bulk delete — vòng lặp đảo ngược: xóa từ đáy lên đỉnh, không lệch index.
    for (const int index : rowsToRemove)
    {
        if (! juce::isPositiveAndBelow (index, list->pads.size()))
            continue;

        if (auto* pad = list->pads[index])
        {
            safelyPreparePadForDeletion (pad);

            if (scrollContent != nullptr)
                scrollContent->removeChildComponent (pad);
        }

        list->pads.remove (index);
    }

    for (int i = 0; i < list->pads.size(); ++i)
        if (list->pads[i] != nullptr)
            list->pads[i]->setPadIndex (i);

    if (list->isGrid)
        syncCueMetadataFromPads (*list);

    rebuildDefaultHotkeysForList (listIdx);

    if (activeListIndex == listIdx)
    {
        selectedPadIndices.clear();

        if (! list->pads.isEmpty())
        {
            selectedBgmIndex = juce::jmin (targetRowAfterDelete, list->pads.size() - 1);
            selectedPadIndices.add (selectedBgmIndex);

            if (auto* selectedPad = list->pads[selectedBgmIndex])
                inspectorPanel.selectPad (selectedPad);
        }
        else
        {
            selectedBgmIndex = -1;
        }

        applyPadSelectionVisualState();

        if (list->isGrid && list->useCueListPanel)
        {
            syncCueMetadataFromPads (*list);
            refreshCueListPanel (false);
            layoutActiveListPads();
            repaint();
        }
        else if (list->isGrid)
        {
            updateCueGridUIFromData (*list);
        }
        else
        {
            resized();

            if (juce::isPositiveAndBelow (selectedBgmIndex, list->pads.size()))
                if (auto* selectedPad = list->pads[selectedBgmIndex])
                    forwardUiSelectionToPad (selectedPad, true);
        }

        refreshSidebarPlayingStatus();
        pushStageMonitorUpdate();
    }

    rebuildSidebarFromAllLists();
    sidebarPanel.setSelectedIndex (listIdx);
    saveProject();
}

MainComponent::PadGridLayout MainComponent::getPadGridLayout (int mainViewWidth, int mainViewHeight, int padCount) const
{
    PadGridLayout layout;
    layout.viewWidth = mainViewWidth;
    const int count = juce::jmax (1, padCount);
    const int availableWidth = juce::jmax (80, mainViewWidth - 18);
    const int availableHeight = juce::jmax (80, mainViewHeight - 24);
    const float aspect = 0.77f; // H ~= 0.77 * W
    // Không ép ma trận cứng 8xN; số cột/row chỉ phụ thuộc viewport + số PAD thực tế.
    const int kMaxColsByWidth = juce::jmax (1, availableWidth / juce::jmax (1, 56 + layout.gap));
    const int kMaxCols = juce::jmax (1, juce::jmin (count, kMaxColsByWidth));

    const int preferredPadW = 180;
    int cols = juce::jmax (1, availableWidth / juce::jmax (1, preferredPadW + layout.gap));
    cols = juce::jlimit (1, kMaxCols, cols);

    int rows = juce::jmax (1, (count + cols - 1) / cols);
    int padW = 0;
    int padH = 0;

    // Farrago-like: ưu tiên fill ngang, nhưng nếu thiếu chiều cao thì tăng số cột.
    while (true)
    {
        rows = juce::jmax (1, (count + cols - 1) / cols);
        padW = (availableWidth - (cols - 1) * layout.gap) / cols;
        padW = juce::jlimit (56, 220, padW);
        padH = juce::jmax (44, (int) std::round (padW * aspect));

        const int totalH = rows * padH + (rows - 1) * layout.gap;
        if (totalH <= availableHeight || cols >= kMaxCols)
            break;

        ++cols;
    }

    // Nếu vẫn tràn dọc (trường hợp cực hạn), co theo chiều cao để vừa viewport.
    const int maxPadHByViewport = juce::jmax (44, (availableHeight - (rows - 1) * layout.gap) / rows);
    if (padH > maxPadHByViewport)
    {
        padH = maxPadHByViewport;
        padW = juce::jmax (56, (int) std::round (padH / aspect));
    }

    layout.cols = cols;
    layout.padW = padW;
    layout.padH = padH;
    const int totalGridWidth = (layout.cols * layout.padW) + ((layout.cols - 1) * layout.gap);
    layout.startX = juce::jmax (0, (mainViewWidth - totalGridWidth) / 2);
    return layout;
}

juce::Rectangle<int> MainComponent::PadGridLayout::cellBounds (int slot) const noexcept
{
    const int row = slot / cols;
    const int col = slot % cols;
    return { startX + col * (padW + gap), row * (padH + gap), padW, padH };
}

void MainComponent::resetPadReorderVisualState()
{
    if (activeListIndex < 0 || activeListIndex >= allLists.size())
        return;

    auto* current = allLists[activeListIndex];
    if (current == nullptr)
        return;

    for (auto* p : current->pads)
    {
        if (p == nullptr)
            continue;

        p->setIsCurrentlyDragged (false);
        p->setAlpha (1.0f);
        p->setVisible (true);
    }
}

bool MainComponent::listHasLoadedAudio (const ListData& list) noexcept
{
    for (auto* pad : list.pads)
    {
        if (pad == nullptr)
            continue;

        if (list.isGrid ? pad->occupiesCueGridSlot() : pad->hasAudioFile())
            return true;
    }

    return false;
}

PadPanel* MainComponent::getPadPanel() const noexcept
{
    return dynamic_cast<PadPanel*> (scrollContent.get());
}

bool MainComponent::assignNextFreeGridCell (ListData& list, SoundPad* pad)
{
    if (pad == nullptr)
        return false;

    for (int r = 0; r < showcontrol::padgrid::kRows; ++r)
    {
        for (int c = 0; c < showcontrol::padgrid::kCols; ++c)
        {
            bool taken = false;

            for (auto* p : list.pads)
            {
                if (p == nullptr || p == pad || ! p->occupiesCueGridSlot())
                    continue;

                if (p->getGridRow() == r && p->getGridCol() == c)
                {
                    taken = true;
                    break;
                }
            }

            if (! taken)
            {
                pad->setGridPosition (r, c);
                return true;
            }
        }
    }

    return false;
}

void MainComponent::movePadToGridCell (int listIdx, SoundPad* pad, int row, int col)
{
    if (pad == nullptr)
        return;

    performPadGridMutationWithUndo (listIdx,
                                    juce::String::fromUTF8 (u8"Di chuyển PAD"),
                                    [this, listIdx, pad, row, col]()
                                    {
                                        movePadToGridCellImpl (listIdx, pad, row, col);
                                    });
}

void MainComponent::applyPadGridDropAt (int listIdx, SoundPad* sourcePad, int targetRow, int targetCol)
{
    movePadToGridCell (listIdx, sourcePad, targetRow, targetCol);
}

void MainComponent::movePadToGridCellImpl (int listIdx, SoundPad* pad, int row, int col)
{
    if (pad == nullptr || listIdx < 0 || listIdx >= allLists.size())
        return;

    auto* list = allLists[listIdx];
    if (list == nullptr || list->isLocked || ! list->isGrid)
        return;

    if (! showcontrol::padgrid::isValidGridCell (row, col))
        return;

    SoundPad* occupant = nullptr;

    for (auto* p : list->pads)
    {
        if (p == nullptr || p == pad || ! p->occupiesCueGridSlot())
            continue;

        if (p->getGridRow() == row && p->getGridCol() == col)
        {
            occupant = p;
            break;
        }
    }

    if (occupant != nullptr)
    {
        occupant->setGridPosition (pad->getGridRow(), pad->getGridCol());
        occupant->refreshHotkeyLabel();
    }

    pad->setGridPosition (row, col);
    pad->refreshHotkeyLabel();

    if (auto* panel = getPadPanel())
        panel->resyncAndLayout();
}

void MainComponent::movePadsToGridCell (int listIdx,
                                        const juce::Array<int>& padIndices,
                                        int anchorIndex,
                                        int targetRow,
                                        int targetCol)
{
    performPadGridMutationWithUndo (listIdx,
                                    juce::String::fromUTF8 (u8"Di chuyển PAD"),
                                    [this, listIdx, padIndices, anchorIndex, targetRow, targetCol]()
                                    {
                                        movePadsToGridCellImpl (listIdx, padIndices, anchorIndex, targetRow, targetCol);
                                    });
}

void MainComponent::movePadsToGridCellImpl (int listIdx,
                                        const juce::Array<int>& padIndices,
                                        int anchorIndex,
                                        int targetRow,
                                        int targetCol)
{
    if (listIdx < 0 || listIdx >= allLists.size() || padIndices.isEmpty())
        return;

    auto* list = allLists[listIdx];
    if (list == nullptr || list->isLocked || ! list->isGrid)
        return;

    if (! showcontrol::padgrid::isValidGridCell (targetRow, targetCol))
        return;

    if (anchorIndex < 0 || anchorIndex >= list->pads.size())
        return;

    auto* anchorPad = list->pads[anchorIndex];
    if (anchorPad == nullptr)
        return;

    juce::Array<SoundPad*> movingPads;
    for (const auto idx : padIndices)
    {
        if (idx < 0 || idx >= list->pads.size())
            continue;

        if (auto* pad = list->pads[idx])
            if (! movingPads.contains (pad))
                movingPads.add (pad);
    }

    if (! movingPads.contains (anchorPad))
        movingPads.add (anchorPad);

    if (movingPads.size() <= 1)
    {
        movePadToGridCell (listIdx, anchorPad, targetRow, targetCol);
        return;
    }

    const int deltaRow = targetRow - anchorPad->getGridRow();
    const int deltaCol = targetCol - anchorPad->getGridCol();

    struct PendingMove
    {
        SoundPad* pad = nullptr;
        int fromRow = 0, fromCol = 0, toRow = 0, toCol = 0;
    };

    juce::Array<PendingMove> moves;

    for (auto* pad : movingPads)
    {
        const int toRow = pad->getGridRow() + deltaRow;
        const int toCol = pad->getGridCol() + deltaCol;

        if (! showcontrol::padgrid::isValidGridCell (toRow, toCol))
            return;

        PendingMove move;
        move.pad     = pad;
        move.fromRow = pad->getGridRow();
        move.fromCol = pad->getGridCol();
        move.toRow   = toRow;
        move.toCol   = toCol;
        moves.add (move);
    }

    for (const auto& move : moves)
    {
        SoundPad* blocker = nullptr;

        for (auto* p : list->pads)
        {
            if (p == nullptr || p == move.pad || ! p->occupiesCueGridSlot())
                continue;

            if (p->getGridRow() == move.toRow && p->getGridCol() == move.toCol)
            {
                blocker = p;
                break;
            }
        }

        if (blocker != nullptr && ! movingPads.contains (blocker))
        {
            blocker->setGridPosition (move.fromRow, move.fromCol);
            blocker->refreshHotkeyLabel();
        }
    }

    for (const auto& move : moves)
    {
        move.pad->setGridPosition (move.toRow, move.toCol);
        move.pad->refreshHotkeyLabel();
    }

    if (auto* panel = getPadPanel())
        panel->resyncAndLayout();
}

int MainComponent::findListIndexByName (const juce::String& name) const
{
    if (name.isEmpty())
        return activeListIndex;

    for (int i = 0; i < sidebarPanel.getListCount(); ++i)
    {
        if (sidebarPanel.getListName (i) == name)
            return i;
    }

    return activeListIndex;
}

juce::var MainComponent::buildSidebarListDragPayload (const juce::Array<int>& itemIds) const
{
    juce::String listName;

    if (activeListIndex >= 0 && activeListIndex < sidebarPanel.getListCount())
        listName = sidebarPanel.getListName (activeListIndex);

    juce::Array<showcontrol::crossdrag::TrackCopyRecord> tracks;

    if (activeListIndex >= 0 && activeListIndex < allLists.size())
    {
        if (auto* list = allLists[activeListIndex])
        {
            for (const auto idx : itemIds)
            {
                if (idx < 0 || idx >= list->pads.size())
                    continue;

                auto* pad = list->pads[idx];

                if (pad == nullptr)
                    continue;

                const juce::String path = pad->getConfiguredFilePath().isNotEmpty()
                                              ? pad->getConfiguredFilePath()
                                              : pad->getFilePath();

                if (path.isEmpty())
                    continue;

                showcontrol::crossdrag::TrackCopyRecord rec;
                rec.filePath    = path;
                rec.customName  = pad->getPadName();
                rec.tagColour   = pad->getTagColour();
                rec.outputGain  = pad->getOutputGain();
                rec.looping     = pad->isLooping();
                tracks.add (rec);
            }
        }
    }

    return showcontrol::crossdrag::buildCrossComponentCopyPayload (listName, tracks);
}

juce::var MainComponent::buildPadPanelDragPayload (const juce::Array<int>& padIndices,
                                                     int anchorIndex) const
{
    return showcontrol::crossdrag::buildPadPanelPayload (padIndices, anchorIndex, false);
}

void MainComponent::copyTracksToPadGrid (int targetRow,
                                         int targetCol,
                                         const juce::Array<showcontrol::crossdrag::TrackCopyRecord>& tracks)
{
    const int targetListIdx = activeListIndex;

    if (targetListIdx < 0 || targetListIdx >= allLists.size() || tracks.isEmpty())
        return;

    auto* targetList = allLists[targetListIdx];

    if (targetList == nullptr || targetList->isLocked || ! targetList->isGrid)
        return;

    if (! showcontrol::padgrid::isValidGridCell (targetRow, targetCol))
        return;

    crossComponentDragConsumed = true;

    auto grid = buildGridOccupancyFromList (*targetList);
    int curRow = targetRow;
    int curCol = targetCol;
    juce::Array<SoundPad*> createdPads;
    const bool onActiveList = true;

    for (const auto& track : tracks)
    {
        if (track.filePath.isEmpty())
            continue;

        if (targetList->pads.size() >= kMaxCuePadsPerList)
        {
            showCueGridFullAlert();
            break;
        }

        const auto slot = findProximitySlotInGrid (grid, curRow, curCol);

        if (slot.x < 0)
            break;

        auto* dup = createSoundPad();
        scrollContent->addChildComponent (dup);
        applyProjectDefaultsToPad (dup);
        dup->configurePad (track.filePath, track.outputGain, track.looping);
        dup->setCustomName (track.customName);
        dup->setPadThemeColour (track.tagColour);
        dup->updateTheme (isDarkMode);
        dup->setRenderMode (true);
        dup->setCueListPlayback (true);
        dup->setGridPosition (slot.y, slot.x, true);
        dup->refreshHotkeyLabel();
        dup->setVisible (onActiveList);
        wireSoundPad (dup);

        targetList->pads.add (dup);
        createdPads.add (dup);
        markGridCellOccupied (grid, slot.y, slot.x);
        curRow = slot.y;
        curCol = slot.x + 1;
    }

    if (createdPads.isEmpty())
        return;

    finishBatchDuplicatedPads (createdPads);

    for (int i = 0; i < targetList->pads.size(); ++i)
        if (targetList->pads[i] != nullptr)
            targetList->pads[i]->setPadIndex (i);

    rebuildHotkeyBindings();
    syncCueMetadataFromPads (*targetList);
    layoutActiveListPads();

    if (auto* panel = getPadPanel())
        panel->resyncAndLayout();

    rebuildSidebarFromAllLists();
    persistApplicationStateNow();
}

void MainComponent::copyLegacySidebarItemsToPadGrid (int targetRow,
                                                     int targetCol,
                                                     const juce::Array<int>& itemIds,
                                                     const juce::String& sourceListName)
{
    const int sourceListIdx = findListIndexByName (sourceListName);
    juce::Array<showcontrol::crossdrag::TrackCopyRecord> tracks;

    if (sourceListIdx >= 0 && sourceListIdx < allLists.size())
    {
        if (auto* sourceList = allLists[sourceListIdx])
        {
            for (const auto idx : itemIds)
            {
                if (idx < 0 || idx >= sourceList->pads.size())
                    continue;

                auto* pad = sourceList->pads[idx];

                if (pad == nullptr)
                    continue;

                const juce::String path = pad->getConfiguredFilePath().isNotEmpty()
                                              ? pad->getConfiguredFilePath()
                                              : pad->getFilePath();

                if (path.isEmpty())
                    continue;

                showcontrol::crossdrag::TrackCopyRecord rec;
                rec.filePath    = path;
                rec.customName  = pad->getPadName();
                rec.tagColour   = pad->getTagColour();
                rec.outputGain  = pad->getOutputGain();
                rec.looping     = pad->isLooping();
                tracks.add (rec);
            }
        }
    }

    if (! tracks.isEmpty())
        copyTracksToPadGrid (targetRow, targetCol, tracks);
}

bool MainComponent::canAcceptCrossCopyToPadGrid() const noexcept
{
    if (activeListIndex < 0 || activeListIndex >= allLists.size())
        return false;

    const auto* list = allLists[activeListIndex];
    return list != nullptr && list->isGrid && ! list->isLocked;
}

juce::Point<int> MainComponent::mapDropPointToPadGridCell (juce::Point<int> localInMain) const noexcept
{
    if (auto* panel = getPadPanel())
    {
        const auto inPanel = panel->getLocalPoint (this, localInMain);
        return panel->gridCellAtPoint (inPanel);
    }

    return { 0, 0 };
}

void MainComponent::setPadPanelChildrenMousePassthrough (bool passthrough) noexcept
{
    if (auto* panel = getPadPanel())
        panel->setPadChildrenMousePassthrough (passthrough);
}

void MainComponent::setCrossCopyDropHighlightActive (bool active)
{
    if (crossCopyDropHighlightActive == active)
        return;

    crossCopyDropHighlightActive = active;

    if (activeListIndex >= 0 && activeListIndex < allLists.size())
    {
        if (auto* list = allLists[activeListIndex])
        {
            if (list->isGrid && list->useCueListPanel && cueListPanel != nullptr)
                cueListPanel->setCrossCopyDropHighlight (active);
        }
    }

    repaint();
}

bool MainComponent::handleCrossCopyDropOnPadGrid (const juce::DragAndDropTarget::SourceDetails& dragSourceDetails)
{
    if (! canAcceptCrossCopyToPadGrid())
        return false;

    juce::Array<showcontrol::crossdrag::TrackCopyRecord> copyTracks;
    juce::String sourceListName;

    if (! showcontrol::crossdrag::decodeCrossComponentCopyPayload (dragSourceDetails.description,
                                                                    copyTracks,
                                                                    sourceListName))
    {
        juce::Array<int> legacyItemIds;

        if (! showcontrol::crossdrag::decodeSidebarListPayload (dragSourceDetails.description,
                                                                 legacyItemIds,
                                                                 sourceListName))
            return false;

        const auto cell = mapDropPointToPadGridCell (dragSourceDetails.localPosition);
        copyLegacySidebarItemsToPadGrid (cell.y, cell.x, legacyItemIds, sourceListName);
        crossComponentDragConsumed = true;
    }
    else
    {
        if (copyTracks.isEmpty())
            return false;

        const auto cell = mapDropPointToPadGridCell (dragSourceDetails.localPosition);
        copyTracksToPadGrid (cell.y, cell.x, copyTracks);
    }

    return true;
}

bool MainComponent::isInterestedInDragSource (const juce::DragAndDropTarget::SourceDetails& dragSourceDetails)
{
    if (! showcontrol::crossdrag::isCrossCopyDropInterest (dragSourceDetails.description))
        return false;

    if (sidebarPanel.isVisible() && sidebarPanel.getBounds().contains (dragSourceDetails.localPosition))
        return false;

    if (! canAcceptCrossCopyToPadGrid())
        return false;

    // PadPanel xử lý trực tiếp khi lưới PAD đang hiển thị — MainComponent chỉ bắt khi CueList che vùng đích.
    if (viewScroller.isVisible())
    {
        if (auto* panel = getPadPanel())
        {
            if (panel->isMatrixLayoutEnabled())
                return false;
        }
    }

    return true;
}

void MainComponent::itemDragEnter (const juce::DragAndDropTarget::SourceDetails& dragSourceDetails)
{
    if (! isInterestedInDragSource (dragSourceDetails))
        return;

    setCrossCopyDropHighlightActive (true);
    setPadPanelChildrenMousePassthrough (true);
}

void MainComponent::itemDragMove (const juce::DragAndDropTarget::SourceDetails& dragSourceDetails)
{
    if (crossCopyDropHighlightActive)
        repaint();
    juce::ignoreUnused (dragSourceDetails);
}

void MainComponent::itemDragExit (const juce::DragAndDropTarget::SourceDetails& dragSourceDetails)
{
    juce::ignoreUnused (dragSourceDetails);
    setCrossCopyDropHighlightActive (false);
    setPadPanelChildrenMousePassthrough (false);
}

void MainComponent::itemDropped (const juce::DragAndDropTarget::SourceDetails& dragSourceDetails)
{
    setCrossCopyDropHighlightActive (false);
    setPadPanelChildrenMousePassthrough (false);
    handleCrossCopyDropOnPadGrid (dragSourceDetails);
}

void MainComponent::appendPadsFromGridToList (int targetListIdx,
                                              const juce::Array<int>& padIndices)
{
    const int sourceListIdx = activeListIndex;

    if (targetListIdx < 0 || targetListIdx >= allLists.size()
        || sourceListIdx < 0 || sourceListIdx >= allLists.size()
        || padIndices.isEmpty())
        return;

    auto* sourceList = allLists[sourceListIdx];
    auto* targetList = allLists[targetListIdx];

    if (sourceList == nullptr || targetList == nullptr || targetList->isLocked)
        return;

    juce::Array<SoundPad*> sourcePads;

    for (const auto idx : padIndices)
    {
        if (idx < 0 || idx >= sourceList->pads.size())
            continue;

        if (auto* pad = sourceList->pads[idx])
            if (! sourcePads.contains (pad))
                sourcePads.add (pad);
    }

    if (sourcePads.isEmpty())
        return;

    crossComponentDragConsumed = true;

    const bool onActiveTarget = (targetListIdx == activeListIndex);
    juce::Array<SoundPad*> createdPads;

    for (auto* srcPad : sourcePads)
    {
        if (srcPad == nullptr)
            continue;

        if (! targetList->isGrid && targetList->pads.size() >= kMaxCuePadsPerList)
        {
            showCueListCapacityAlert();
            break;
        }

        if (targetList->isGrid && targetList->pads.size() >= kMaxCuePadsPerList)
        {
            showCueGridFullAlert();
            break;
        }

        auto* dup = createDuplicatePadFromSource (*targetList, srcPad, onActiveTarget, true);

        if (dup == nullptr)
            continue;

        if (targetList->isGrid)
        {
            if (! assignNextFreeGridCell (*targetList, dup))
            {
                delete dup;
                showCueGridFullAlert();
                break;
            }

            dup->refreshHotkeyLabel();
        }

        targetList->pads.add (dup);
        createdPads.add (dup);
    }

    if (createdPads.isEmpty())
        return;

    finishBatchDuplicatedPads (createdPads);

    for (int i = 0; i < targetList->pads.size(); ++i)
        if (targetList->pads[i] != nullptr)
            targetList->pads[i]->setPadIndex (i);

    if (targetList->isGrid)
        syncCueMetadataFromPads (*targetList);

    if (onActiveTarget)
    {
        if (! targetList->isGrid)
        {
            selectedPadIndices.clear();

            for (auto* dup : createdPads)
            {
                const int idx = targetList->pads.indexOf (dup);

                if (idx >= 0)
                    selectedPadIndices.add (idx);
            }

            if (! selectedPadIndices.isEmpty())
                selectedBgmIndex = selectedPadIndices.getLast();

            applyPadSelectionVisualState();
        }

        layoutActiveListPads();
        refreshCueListPanel();

        if (auto* panel = getPadPanel())
            panel->resyncAndLayout();
    }

    rebuildHotkeyBindings();
    rebuildSidebarFromAllLists();
    persistApplicationStateNow();
}

void MainComponent::appendCopyTracksToPlaylist (int targetListIdx,
                                                const juce::Array<showcontrol::crossdrag::TrackCopyRecord>& tracks)
{
    if (targetListIdx < 0 || targetListIdx >= allLists.size() || tracks.isEmpty())
        return;

    auto* targetList = allLists[targetListIdx];

    if (targetList == nullptr || targetList->isLocked)
        return;

    crossComponentDragConsumed = true;

    const bool onActiveTarget = (targetListIdx == activeListIndex);
    juce::Array<SoundPad*> createdPads;

    for (const auto& track : tracks)
    {
        if (track.filePath.isEmpty())
            continue;

        if (targetList->pads.size() >= kMaxCuePadsPerList)
        {
            if (targetList->isGrid)
                showCueGridFullAlert();
            else
                showCueListCapacityAlert();

            break;
        }

        auto* dup = createSoundPad();
        scrollContent->addChildComponent (dup);
        applyProjectDefaultsToPad (dup);
        dup->configurePad (track.filePath, track.outputGain, track.looping);
        dup->setCustomName (track.customName);
        dup->setPadThemeColour (track.tagColour);
        dup->updateTheme (isDarkMode);
        dup->setRenderMode (targetList->isGrid);
        dup->setCueListPlayback (targetList->isGrid);
        dup->setVisible (onActiveTarget);
        wireSoundPad (dup);

        if (targetList->isGrid)
        {
            if (! assignNextFreeGridCell (*targetList, dup))
            {
                delete dup;
                showCueGridFullAlert();
                break;
            }

            dup->refreshHotkeyLabel();
        }

        targetList->pads.add (dup);
        createdPads.add (dup);
    }

    if (createdPads.isEmpty())
        return;

    finishBatchDuplicatedPads (createdPads);

    for (int i = 0; i < targetList->pads.size(); ++i)
        if (targetList->pads[i] != nullptr)
            targetList->pads[i]->setPadIndex (i);

    if (targetList->isGrid)
        syncCueMetadataFromPads (*targetList);

    if (onActiveTarget)
    {
        if (! targetList->isGrid)
        {
            selectedPadIndices.clear();

            for (auto* dup : createdPads)
            {
                const int idx = targetList->pads.indexOf (dup);

                if (idx >= 0)
                    selectedPadIndices.add (idx);
            }

            if (! selectedPadIndices.isEmpty())
                selectedBgmIndex = selectedPadIndices.getLast();

            applyPadSelectionVisualState();
        }

        layoutActiveListPads();
        refreshCueListPanel();

        if (auto* panel = getPadPanel())
            panel->resyncAndLayout();
    }

    rebuildHotkeyBindings();
    sidebarPanel.setListTrackCount (targetListIdx, targetList->pads.size());
    persistApplicationStateNow();
}

void MainComponent::persistApplicationStateNow()
{
    if (isOperatingState())
        return;

    saveApplicationStateInternal();
}

void MainComponent::rebuildHotkeyBindings()
{
    if (activeListIndex >= 0 && activeListIndex < allLists.size())
        rebuildDefaultHotkeysForList (activeListIndex);
}

void MainComponent::layoutActiveListPads()
{
    if (activeListIndex < 0 || activeListIndex >= allLists.size() || scrollContent == nullptr)
        return;

    auto* current = allLists[activeListIndex];
    if (current == nullptr)
        return;

    const int mainViewWidth = getPlaylistViewportContentWidth();
    int totalHeightOfContent = 0;
    const int scrollY = viewScroller.getViewPositionY();
    const int viewH   = viewScroller.getViewHeight();
    constexpr int kPrefetchMarginPx = 220;
    const bool hasLoadedAudio = listHasLoadedAudio (*current);
    auto* scrollContainer = getPadPanel();

    if (scrollContainer != nullptr)
        scrollContainer->setEmptyListHint (PadPanel::EmptyListHint::none);

    if (current->isGrid && current->useCueListPanel && cueListPanel != nullptr)
    {
        for (auto* p : current->pads)
            if (p != nullptr)
                p->setVisible (false);

        refreshCueListPanel (false);

        if (scrollContent != nullptr)
            scrollContent->setSize (mainViewWidth, viewH);

        viewScroller.setViewPosition (0, 0);
        viewScroller.setScrollBarsShown (false, false, false, false);
        return;
    }

    if (cueListPanel != nullptr)
        cueListPanel->setVisible (false);

    if (current->isGrid)
    {
        if (! hasLoadedAudio)
        {
            for (auto* p : current->pads)
                if (p != nullptr)
                    p->setVisible (false);

            if (scrollContainer != nullptr)
                scrollContainer->setEmptyListHint (PadPanel::EmptyListHint::cueGrid);

            scrollContent->setSize (mainViewWidth, viewScroller.getHeight());
            viewScroller.setViewPosition (0, 0);
            return;
        }

        if (scrollContainer != nullptr)
        {
            scrollContainer->setMatrixLayoutEnabled (true);
            scrollContainer->setPadList (&current->pads);
        }

        scrollContent->setSize (mainViewWidth, viewScroller.getHeight());
        viewScroller.setScrollBarsShown (false, false, false, false);

        for (int i = 0; i < current->pads.size(); ++i)
        {
            auto* p = current->pads[i];
            if (p == nullptr)
                continue;

            const bool isDragSource = padReorderActive && i == padReorderFromIndex;

            p->setRenderMode (true);
            p->setIsSelectedRow (isPadSelectedInActiveList (i));

            if (p->isCurrentlyDraggedState())
                p->setAlpha (0.0f);
            else if (isDragSource)
                p->setAlpha (0.0f);
            else
                p->setAlpha (1.0f);
        }

        if (scrollContainer != nullptr)
            scrollContainer->refreshPadGrid();

        PadPanel::BoundedGridLayout gridLayout;

        if (scrollContainer != nullptr)
            gridLayout = scrollContainer->getBoundedGridLayout();

        for (int i = 0; i < current->pads.size(); ++i)
        {
            auto* p = current->pads[i];
            if (p == nullptr)
                continue;

            const auto padCellBounds = showcontrol::padgrid::boundedCellBounds (gridLayout.cellW,
                                                                                  gridLayout.cellH,
                                                                                  p->getGridRow(),
                                                                                  p->getGridCol());
            const bool inPrefetchRange = (padCellBounds.getBottom() >= scrollY - kPrefetchMarginPx)
                                           && (padCellBounds.getY() <= scrollY + viewH + kPrefetchMarginPx);
            const bool selectedOrActive = isPadSelectedInActiveList (i)
                                              || (p->isPlaying() || p->isTransportActive());
            const bool allowThumb = selectedOrActive || inPrefetchRange;
            p->setThumbnailLoadAllowed (allowThumb);
            p->setNormalizationLoadAllowed (allowThumb);

            if (allowThumb && p->getFilePath().isNotEmpty())
                p->reloadWaveformThumbnail();
        }

        return;
    }
    else if (scrollContainer != nullptr)
    {
        scrollContainer->setMatrixLayoutEnabled (false);
    }

    {
        if (! hasLoadedAudio)
        {
            for (auto* p : current->pads)
                if (p != nullptr)
                    p->setVisible (false);

            if (scrollContainer != nullptr)
                scrollContainer->setEmptyListHint (PadPanel::EmptyListHint::bgmRows);

            scrollContent->setSize (mainViewWidth, viewScroller.getHeight());
            viewScroller.setViewPosition (0, 0);
            return;
        }

        const int rowHeight = showcontrol::bgmList::kPlaylistRowHeight;
        int y = 0;

        for (int i = 0; i < current->pads.size(); ++i)
        {
            auto* p = current->pads[i];
            if (p == nullptr)
                continue;

            const bool isDragSource = padReorderActive && i == padReorderFromIndex;

            p->setRenderMode (false);
            p->setIsSelectedRow (isPadSelectedInActiveList (i));
            p->setBounds (0, y, std::max (0, mainViewWidth), rowHeight);
            p->setVisible (true);
            p->setAlpha (isDragSource ? 0.0f : 1.0f);

            const int padTop = y;
            const int padBottom = y + rowHeight;
            const bool inPrefetchRange = (padBottom >= scrollY - kPrefetchMarginPx)
                                           && (padTop <= scrollY + viewH + kPrefetchMarginPx);
            const bool selectedOrPrefetch = isPadSelectedInActiveList (i) || inPrefetchRange;
            p->setThumbnailLoadAllowed (selectedOrPrefetch);
            p->setNormalizationLoadAllowed (selectedOrPrefetch);

            if (selectedOrPrefetch && p->getFilePath().isNotEmpty())
                p->reloadWaveformThumbnail();

            y += rowHeight;
            totalHeightOfContent = y;
        }
    }

    if (current->isGrid)
    {
        scrollContent->setSize (mainViewWidth, viewScroller.getHeight());
        viewScroller.setViewPosition (0, 0);
    }
    else
    {
        scrollContent->setSize (mainViewWidth, std::max (viewScroller.getHeight(), totalHeightOfContent));
    }

    if (! current->isGrid)
        syncBgmListHeaderScrollbar();
}

void MainComponent::autoScrollViewportForPadReorder (juce::Point<int> posInScrollContent)
{
    const auto now = juce::Time::getMillisecondCounterHiRes();
    if (now - padReorderLastAutoScrollMs < 28)
        return;

    const int margin = 52;
    const int step = 18;
    const int viewH = viewScroller.getViewHeight();
    int scrollY = viewScroller.getViewPositionY();
    int nextY = scrollY;

    if (posInScrollContent.y < scrollY + margin)
        nextY = std::max (0, scrollY - step);
    else if (posInScrollContent.y > scrollY + viewH - margin)
        nextY = scrollY + step;

    if (nextY == scrollY)
        return;

    padReorderLastAutoScrollMs = now;
    viewScroller.setViewPosition (viewScroller.getViewPositionX(), nextY);
}

void MainComponent::beginPadReorder (SoundPad* source)
{
    crossComponentDragConsumed = false;

    if (source == nullptr || activeListIndex < 0 || activeListIndex >= allLists.size())
        return;

    auto* list = allLists[activeListIndex];
    if (list == nullptr || list->isLocked)
        return;

    padReorderSource = source;
    padReorderFromIndex = list->pads.indexOf (source);
    padReorderInsertIndex = padReorderFromIndex;
    padReorderActive = padReorderFromIndex >= 0;

    if (! padReorderActive)
        return;

    padReorderIsGridMode = list->isGrid;
    padReorderPointerPos = source->getBounds().getCentre();
    padReorderDragOffset = padReorderPointerPos - source->getBounds().getPosition();
    padReorderLastAutoScrollMs = 0;
    padReorderStackAnimStartMs = juce::Time::getMillisecondCounter();
    padReorderStackAnimActive = selectedPadIndices.size() > 1 && selectedPadIndices.contains (padReorderFromIndex);

    if (list->isGrid)
    {
        if (auto* panel = getPadPanel())
            panel->beginInternalGridDragSession (source);
    }
    else
    {
        padReorderGhostImage = {};
    }

    layoutActiveListPads();

    if (padReorderOverlay != nullptr && ! list->isGrid)
    {
        updatePadReorderOverlayBounds();
        padReorderOverlay->setBufferedToImage (true);
        padReorderOverlay->setVisible (true);
        padReorderOverlay->repaint();
    }

    setMouseCursor (juce::MouseCursor::DraggingHandCursor);
}

void MainComponent::updatePadReorder (juce::Point<int> posInScrollContent)
{
    if (! padReorderActive)
        return;

    const int prevInsertIndex = padReorderInsertIndex;
    const auto prevPointerPos = padReorderPointerPos;
    padReorderPointerPos = posInScrollContent;

    if (padReorderIsGridMode && padReorderSource != nullptr)
    {
        if (auto* panel = getPadPanel())
            panel->applyGridDragHoverAtLocalPoint (posInScrollContent, padReorderSource);
    }

    if (! padReorderIsGridMode)
        padReorderInsertIndex = hitTestPadInsertIndex (posInScrollContent);

    if (padReorderOverlay != nullptr && ! padReorderIsGridMode)
    {
        if (prevInsertIndex != padReorderInsertIndex)
        {
            repaintPadReorderInsertStrip (prevInsertIndex);
            repaintPadReorderInsertStrip (padReorderInsertIndex);
        }

        if (prevPointerPos != padReorderPointerPos)
        {
            repaintPadReorderCapsuleStrip (prevPointerPos);
            repaintPadReorderCapsuleStrip (padReorderPointerPos);
        }
    }
    else if (padReorderOverlay != nullptr)
    {
        padReorderOverlay->repaint();
    }
}

void MainComponent::updatePadReorderOverlayBounds()
{
    if (padReorderOverlay == nullptr)
        return;

    padReorderOverlay->setBounds (viewScroller.getBounds());
    padReorderOverlay->toFront (false);
}

void MainComponent::consumeInternalJucePadDrop() noexcept
{
    crossComponentDragConsumed = true;
    cancelPadReorder (true);
}

void MainComponent::endPadReorder()
{
    if (crossComponentDragConsumed)
    {
        crossComponentDragConsumed = false;
        cancelPadReorder();
        return;
    }

    if (! padReorderActive || padReorderSource == nullptr)
    {
        cancelPadReorder();
        return;
    }

    padReorderInsertIndex = hitTestPadInsertIndex (padReorderPointerPos);

    const int listIdx = activeListIndex;
    const int from = padReorderFromIndex;
    const int target = padReorderInsertIndex;
    auto* draggedPad = padReorderSource;
    const bool gridMode = padReorderIsGridMode;

    if (gridMode && draggedPad != nullptr && draggedPad->hasActiveJuceSystemDrag())
        return;

    if (gridMode && draggedPad != nullptr && listIdx >= 0 && listIdx < allLists.size())
    {
        if (auto* panel = getPadPanel())
        {
            const auto cell = panel->gridCellAtPoint (padReorderPointerPos);
            juce::Array<int> dragSelection = selectedPadIndices;
            dragSelection.sort();

            if (dragSelection.isEmpty() || ! dragSelection.contains (from))
            {
                dragSelection.clear();
                dragSelection.add (from);
            }

            cancelPadReorder();

            if (dragSelection.size() > 1)
                movePadsToGridCell (listIdx, dragSelection, from, cell.y, cell.x);
            else
                movePadToGridCell (listIdx, draggedPad, cell.y, cell.x);

            return;
        }
    }

    cancelPadReorder();

    if (listIdx < 0 || listIdx >= allLists.size() || from < 0 || target < 0)
        return;

    auto* list = allLists[listIdx];
    if (list == nullptr)
        return;

    const int n = list->pads.size();
    const int to = juce::jlimit (0, n, target);

    juce::Array<int> dragSelection = selectedPadIndices;
    dragSelection.sort();

    if (dragSelection.isEmpty() || ! dragSelection.contains (from))
    {
        dragSelection.clear();
        dragSelection.add (from);
    }

    if (dragSelection.size() == 1)
    {
        if (from == to)
            return;

        int destRowIndex = showcontrol::crossdrag::computeFinalRowIndexAfterMoveDown (from, to);
        const int singleTo = juce::jlimit (0, juce::jmax (0, n - 1), destRowIndex >= n ? n - 1 : destRowIndex);

        if (from == singleTo)
            return;

        movePadInList (listIdx, from, singleTo);
        return;
    }

    movePadsBlockInList (listIdx, dragSelection, to);
}

void MainComponent::cancelPadReorder (bool keepPadGridDragVisual)
{
    padReorderSource = nullptr;
    padReorderFromIndex = -1;
    padReorderInsertIndex = -1;
    padReorderActive = false;
    padReorderIsGridMode = false;
    padReorderStackAnimActive = false;
    padReorderStackAnimStartMs = 0;
    setMouseCursor (juce::MouseCursor::NormalCursor);

    padReorderGhostImage = {};
    resetPadReorderVisualState();

    if (auto* panel = getPadPanel())
    {
        if (! keepPadGridDragVisual)
            panel->setDraggingActive (false);

        panel->repaint();
    }

    if (padReorderOverlay != nullptr)
    {
        padReorderOverlay->setBufferedToImage (false);
        padReorderOverlay->setVisible (false);
        padReorderOverlay->repaint();
    }

    layoutActiveListPads();
}

int MainComponent::hitTestPadInsertIndex (juce::Point<int> local) const
{
    if (activeListIndex < 0 || activeListIndex >= allLists.size())
        return -1;

    const auto* list = allLists[activeListIndex];
    if (list == nullptr || list->pads.isEmpty())
        return 0;

    const int n = list->pads.size();

    if (! list->isGrid)
    {
        const int rowHeight = showcontrol::bgmList::kPlaylistRowHeight;

        if (rowHeight <= 0)
            return 0;

        return showcontrol::crossdrag::computeRoundedRowInsertionIndex (local.y, rowHeight, n);
    }

    if (scrollContent == nullptr)
        return 0;

    if (auto* panel = getPadPanel())
    {
        if (activeListIndex >= 0 && activeListIndex < allLists.size())
        {
            if (auto* list = allLists[activeListIndex])
            {
                if (list->isGrid)
                {
                    const auto cell = panel->gridCellAtPoint (local);
                    return showcontrol::padgrid::linearSlotFromGrid (cell.y, cell.x);
                }
            }
        }
    }

    const auto grid = getPadGridLayout (scrollContent->getWidth(), viewScroller.getHeight(), n);
    int bestIdx = 0;
    float bestDist2 = std::numeric_limits<float>::max();

    for (int i = 0; i < n; ++i)
    {
        const auto centre = grid.cellBounds (i).getCentre().toFloat();
        const float d2 = centre.getDistanceSquaredFrom (local.toFloat());

        if (d2 < bestDist2)
        {
            bestDist2 = d2;
            bestIdx = i;
        }
    }

    if (local.y >= grid.cellBounds (n - 1).getBottom() - 2)
        return n;

    return bestIdx;
}

juce::Rectangle<int> MainComponent::getListInsertLineBounds() const
{
    if (activeListIndex < 0 || activeListIndex >= allLists.size())
        return {};

    const auto* list = allLists[activeListIndex];
    if (list == nullptr || list->isGrid)
        return {};

    const int rowHeight = showcontrol::bgmList::kPlaylistRowHeight;

    if (rowHeight <= 0)
        return {};

    const int width = scrollContent != nullptr ? scrollContent->getWidth() : viewScroller.getWidth();
    const int n = list->pads.size();
    const int target = juce::jlimit (0, n, padReorderInsertIndex);
    return { 0, target * rowHeight, width, 0 };
}

void MainComponent::repaintPadReorderInsertStrip (int insertIndex) const
{
    if (padReorderOverlay == nullptr || ! padReorderOverlay->isVisible() || insertIndex < 0)
        return;

    const int rowHeight = showcontrol::bgmList::kPlaylistRowHeight;

    if (rowHeight <= 0)
        return;

    const int scrollY = viewScroller.getViewPositionY();
    const int lineY = insertIndex * rowHeight - scrollY;
    constexpr int margin = 12;
    padReorderOverlay->repaint (0, lineY - margin, padReorderOverlay->getWidth(), margin * 2);
}

void MainComponent::repaintPadReorderCapsuleStrip (juce::Point<int> pointerInScrollContent) const
{
    if (padReorderOverlay == nullptr || ! padReorderOverlay->isVisible())
        return;

    const int scrollY = viewScroller.getViewPositionY();
    const int localY = pointerInScrollContent.y - scrollY;
    constexpr int halfW = 120;
    constexpr int halfH = 19;
    padReorderOverlay->repaint (pointerInScrollContent.x - halfW,
                               localY - halfH,
                               halfW * 2,
                               halfH * 2);
}

juce::Rectangle<int> MainComponent::getGridGapCellBounds() const
{
    if (activeListIndex < 0 || activeListIndex >= allLists.size() || scrollContent == nullptr)
        return {};

    const auto* list = allLists[activeListIndex];
    if (list == nullptr || ! list->isGrid)
        return {};

    const int n = list->pads.size();
    const int target = juce::jlimit (0, showcontrol::padgrid::kMaxCells - 1, padReorderInsertIndex);
    const auto cell = showcontrol::padgrid::gridFromLinearSlot (target);

    if (auto* panel = getPadPanel())
    {
        const auto grid = panel->getBoundedGridLayout();
        return showcontrol::padgrid::boundedCellBounds (grid.cellW, grid.cellH, cell.y, cell.x);
    }

    return {};
}

void MainComponent::paintPadReorderOverlay (juce::Graphics& g) const
{
    if (marqueeSelectionActive)
    {
        const auto pal = ShowTheme::get (isDarkMode);
        auto marquee = getMarqueeRectInScrollContent();
        marquee.setY (marquee.getY() - viewScroller.getViewPositionY());
        g.setColour (pal.accent.withAlpha (0.14f));
        g.fillRect (marquee);
        g.setColour (pal.accent.withAlpha (0.90f));
        g.drawRect (marquee, 2);
    }

    if (! padReorderActive)
        return;

    if (padReorderIsGridMode)
        return;

    if (padReorderInsertIndex < 0)
        return;

    if (activeListIndex < 0 || activeListIndex >= allLists.size())
        return;

    const auto* list = allLists[activeListIndex];
    if (list == nullptr)
        return;

    const int scrollY = viewScroller.getViewPositionY();
    const auto pal = ShowTheme::get (isDarkMode);
    const bool draggingGroup = selectedPadIndices.size() > 1 && selectedPadIndices.contains (padReorderFromIndex);
    const int draggedCount = draggingGroup ? selectedPadIndices.size() : 1;
    const float animDurationMs = 120.0f;
    const float elapsedMs = (float) (juce::Time::getMillisecondCounter() - padReorderStackAnimStartMs);
    const float animT = juce::jlimit (0.0f, 1.0f, elapsedMs / animDurationMs);
    const float easeOut = 1.0f - std::pow (1.0f - animT, 3.0f);
    const bool stackIntroActive = draggingGroup && padReorderStackAnimActive && animT < 1.0f;
    const int pointerX = padReorderPointerPos.x;
    const int pointerY = padReorderPointerPos.y - scrollY;

    if (! padReorderIsGridMode)
    {
        const auto line = getListInsertLineBounds();

        if (line.getWidth() > 0)
        {
            const float lineY = (float) (line.getY() - scrollY);
            showcontrol::crossdrag::paintNeonRoundedCapInsertLine (g, lineY, (float) getWidth());
        }

        if (padReorderSource != nullptr && ! padReorderSource->hasActiveJuceSystemDrag())
        {
            const bool draggingGroup = selectedPadIndices.size() > 1
                                       && selectedPadIndices.contains (padReorderFromIndex);
            const int itemCount = draggingGroup ? selectedPadIndices.size() : 1;

            showcontrol::crossdrag::paintPremiumDragCapsuleProxy (g,
                                                                  (float) pointerX,
                                                                  (float) pointerY,
                                                                  padReorderSource->getPadName(),
                                                                  itemCount);
        }

        return;
    }

    if (padReorderSource == nullptr)
        return;

    if (padReorderIsGridMode)
    {
        const auto srcBounds = padReorderSource->getBounds();
        auto topLeft = padReorderPointerPos - padReorderDragOffset;
        if (stackIntroActive)
            topLeft = padReorderSource->getBounds().getPosition();
        topLeft.y -= scrollY;

        juce::Rectangle<float> ghost ((float) topLeft.x, (float) topLeft.y,
                                      (float) srcBounds.getWidth(),
                                      (float) srcBounds.getHeight());
        const float popScale = stackIntroActive ? (1.0f + 0.08f * std::sin (easeOut * juce::MathConstants<float>::pi)) : 1.04f;
        ghost = ghost.withSizeKeepingCentre (ghost.getWidth() * popScale, ghost.getHeight() * popScale);

        if (draggedCount > 1)
        {
            const int layers = juce::jmin (4, draggedCount - 1);
            for (int i = layers; i >= 1; --i)
            {
                const float introMul = stackIntroActive ? easeOut : 1.0f;
                auto layer = ghost.translated ((float) i * 11.0f * introMul, (float) i * 8.0f * introMul)
                                  .withSizeKeepingCentre (ghost.getWidth() * (1.0f - 0.03f * (float) i),
                                                          ghost.getHeight() * (1.0f - 0.03f * (float) i));
                g.setColour (pal.dragGhostFill.withAlpha (0.44f - (float) (i - 1) * 0.08f));
                g.fillRoundedRectangle (layer, 8.0f);
                g.setColour (pal.accent.withAlpha (0.48f - (float) (i - 1) * 0.09f));
                g.drawRoundedRectangle (layer, 8.0f, 1.6f);
            }
        }

        g.setColour (juce::Colours::black.withAlpha (0.45f));
        g.fillRoundedRectangle (ghost.translated (0.0f, 6.0f), 8.0f);

        if (padReorderGhostImage.isValid())
            g.drawImage (padReorderGhostImage, ghost, juce::RectanglePlacement::stretchToFit);

        g.setColour (pal.accent);
        g.drawRoundedRectangle (ghost.expanded (1.5f), 8.0f, 3.5f);
        g.setColour (pal.accent.withAlpha (0.55f));
        g.drawRoundedRectangle (ghost.expanded (3.0f), 9.0f, 1.5f);

        if (draggedCount > 1)
        {
            const juce::String countText = "x" + juce::String (draggedCount);
            juce::Rectangle<float> badge (ghost.getRight() - 34.0f, ghost.getY() - 12.0f, 30.0f, 18.0f);
            g.setColour (pal.accent);
            g.fillRoundedRectangle (badge, 8.0f);
            g.setColour (juce::Colours::white);
            g.setFont (ShowTheme::fontBold (10.5f));
            g.drawText (countText, badge, juce::Justification::centred);
        }
    }
}

juce::Rectangle<int> MainComponent::getMarqueeRectInScrollContent() const
{
    return juce::Rectangle<int>::leftTopRightBottom (juce::jmin (marqueeStartPos.x, marqueeEndPos.x),
                                                     juce::jmin (marqueeStartPos.y, marqueeEndPos.y),
                                                     juce::jmax (marqueeStartPos.x, marqueeEndPos.x),
                                                     juce::jmax (marqueeStartPos.y, marqueeEndPos.y));
}

void MainComponent::beginMarqueeSelection (juce::Point<int> posInScrollContent, const juce::ModifierKeys& mods)
{
    if (activeListIndex < 0 || activeListIndex >= allLists.size())
        return;

    auto* list = allLists[activeListIndex];
    if (list == nullptr)
        return;

    marqueeStartPos = posInScrollContent;
    marqueeEndPos = posInScrollContent;
    marqueeSelectionPrimed = true;
    marqueeSelectionActive = false;
    marqueeSelectionAdditive = mods.isCommandDown() || mods.isCtrlDown();
    marqueeSelectionRangeSmart = mods.isShiftDown();
    marqueeBaseSelection = selectedPadIndices;
    marqueeAnchorIndex = hitTestPadInsertIndex (posInScrollContent);

    if (marqueeSelectionRangeSmart && marqueeAnchorIndex < 0)
        marqueeAnchorIndex = juce::jlimit (0, juce::jmax (0, list->pads.size() - 1), selectedBgmIndex);
}

void MainComponent::updateMarqueeSelection (juce::Point<int> posInScrollContent)
{
    if (! marqueeSelectionPrimed)
        return;

    marqueeEndPos = posInScrollContent;

    if (! marqueeSelectionActive)
    {
        if (marqueeStartPos.getDistanceFrom (marqueeEndPos) < 5)
            return;
        marqueeSelectionActive = true;
        if (padReorderOverlay != nullptr)
        {
            updatePadReorderOverlayBounds();
            padReorderOverlay->setVisible (true);
            padReorderOverlay->repaint();
        }
    }

    applyMarqueeSelectionToPads();
    if (padReorderOverlay != nullptr)
        padReorderOverlay->repaint();
}

void MainComponent::applyMarqueeSelectionToPads()
{
    if (activeListIndex < 0 || activeListIndex >= allLists.size())
        return;

    auto* list = allLists[activeListIndex];
    if (list == nullptr)
        return;

    juce::Array<int> nextSelection = marqueeSelectionAdditive ? marqueeBaseSelection : juce::Array<int>();
    const auto marquee = getMarqueeRectInScrollContent();

    if (marqueeSelectionRangeSmart && marqueeAnchorIndex >= 0 && marqueeAnchorIndex < list->pads.size())
    {
        const int currentIndex = juce::jlimit (0, list->pads.size() - 1, hitTestPadInsertIndex (marqueeEndPos));

        if (! list->isGrid)
        {
            const int start = juce::jmin (marqueeAnchorIndex, currentIndex);
            const int end = juce::jmax (marqueeAnchorIndex, currentIndex);
            for (int i = start; i <= end; ++i)
                nextSelection.addIfNotAlreadyThere (i);
        }
        else
        {
            const auto grid = getPadGridLayout (scrollContent != nullptr ? scrollContent->getWidth() : viewScroller.getWidth(),
                                                viewScroller.getHeight(),
                                                list->pads.size());
            const int anchorRow = marqueeAnchorIndex / grid.cols;
            const int anchorCol = marqueeAnchorIndex % grid.cols;
            const int currentRow = currentIndex / grid.cols;
            const int currentCol = currentIndex % grid.cols;
            const int rowStart = juce::jmin (anchorRow, currentRow);
            const int rowEnd = juce::jmax (anchorRow, currentRow);
            const int colStart = juce::jmin (anchorCol, currentCol);
            const int colEnd = juce::jmax (anchorCol, currentCol);

            for (int i = 0; i < list->pads.size(); ++i)
            {
                const int row = i / grid.cols;
                const int col = i % grid.cols;
                if (row >= rowStart && row <= rowEnd && col >= colStart && col <= colEnd)
                    nextSelection.addIfNotAlreadyThere (i);
            }
        }
    }
    else
    {
        for (int i = 0; i < list->pads.size(); ++i)
        {
            auto* pad = list->pads[i];
            if (pad == nullptr)
                continue;

            if (marquee.intersects (pad->getBounds()))
                nextSelection.addIfNotAlreadyThere (i);
        }
    }

    nextSelection.sort();
    selectedPadIndices = nextSelection;

    if (! selectedPadIndices.isEmpty())
    {
        selectedBgmIndex = selectedPadIndices.getFirst();
        if (auto* selectedPad = list->pads[selectedBgmIndex])
            inspectorPanel.selectPad (selectedPad);

        // Ưu tiên load waveform + normalize cho toàn bộ selection (giảm cảm giác “thiếu ổn định” khi drag).
        for (auto idx : selectedPadIndices)
        {
            if (idx < 0 || idx >= list->pads.size())
                continue;
            if (auto* p = list->pads[idx])
            {
                p->setThumbnailLoadAllowed (true);
                p->setNormalizationLoadAllowed (true);
            }
        }
    }

    applyPadSelectionVisualState();

    if (! selectedPadIndices.isEmpty() && isShowing())
        grabKeyboardFocus();
}

void MainComponent::endMarqueeSelection()
{
    if (! marqueeSelectionPrimed)
        return;

    if (! marqueeSelectionActive && ! marqueeSelectionAdditive)
    {
        selectedPadIndices.clear();
        applyPadSelectionVisualState();
    }
    else if (! selectedPadIndices.isEmpty() && isShowing())
    {
        grabKeyboardFocus();
    }

    marqueeSelectionPrimed = false;
    marqueeSelectionActive = false;
    marqueeBaseSelection.clear();
    marqueeSelectionRangeSmart = false;
    marqueeAnchorIndex = -1;

    if (padReorderOverlay != nullptr)
    {
        if (! padReorderActive)
            padReorderOverlay->setVisible (false);
        padReorderOverlay->repaint();
    }
}

void MainComponent::moveSelectedPadsInActiveListUp()
{
    if (activeListIndex < 0 || activeListIndex >= allLists.size())
        return;

    auto* list = allLists[activeListIndex];
    if (list == nullptr || list->isLocked || selectedPadIndices.isEmpty())
        return;

    juce::Array<bool> selected;
    selected.insertMultiple (0, false, list->pads.size());
    for (auto idx : selectedPadIndices)
        if (idx >= 0 && idx < selected.size())
            selected.set (idx, true);

    bool moved = false;
    for (int i = 1; i < list->pads.size(); ++i)
    {
        if (selected[i] && ! selected[i - 1])
        {
            list->pads.swap (i, i - 1);
            std::swap (selected.getReference (i), selected.getReference (i - 1));
            moved = true;
        }
    }

    if (! moved)
        return;

    selectedPadIndices.clear();
    for (int i = 0; i < selected.size(); ++i)
        if (selected[i])
            selectedPadIndices.add (i);

    for (int i = 0; i < list->pads.size(); ++i)
        if (auto* pad = list->pads[i])
            pad->setPadIndex (i);

    selectedBgmIndex = selectedPadIndices.getFirst();
    applyPadSelectionVisualState();
    resized();
    rebuildSidebarFromAllLists();
    sidebarPanel.setSelectedIndex (activeListIndex);
    saveProject();
}

void MainComponent::moveSelectedPadsInActiveListDown()
{
    if (activeListIndex < 0 || activeListIndex >= allLists.size())
        return;

    auto* list = allLists[activeListIndex];
    if (list == nullptr || list->isLocked || selectedPadIndices.isEmpty())
        return;

    juce::Array<bool> selected;
    selected.insertMultiple (0, false, list->pads.size());
    for (auto idx : selectedPadIndices)
        if (idx >= 0 && idx < selected.size())
            selected.set (idx, true);

    bool moved = false;
    for (int i = list->pads.size() - 2; i >= 0; --i)
    {
        if (selected[i] && ! selected[i + 1])
        {
            list->pads.swap (i, i + 1);
            std::swap (selected.getReference (i), selected.getReference (i + 1));
            moved = true;
        }
    }

    if (! moved)
        return;

    selectedPadIndices.clear();
    for (int i = 0; i < selected.size(); ++i)
        if (selected[i])
            selectedPadIndices.add (i);

    for (int i = 0; i < list->pads.size(); ++i)
        if (auto* pad = list->pads[i])
            pad->setPadIndex (i);

    selectedBgmIndex = selectedPadIndices.getFirst();
    applyPadSelectionVisualState();
    resized();
    rebuildSidebarFromAllLists();
    sidebarPanel.setSelectedIndex (activeListIndex);
    saveProject();
}

void MainComponent::moveSelectedPadsInActiveListToTop()
{
    if (activeListIndex < 0 || activeListIndex >= allLists.size())
        return;

    auto* list = allLists[activeListIndex];
    if (list == nullptr || list->isLocked || selectedPadIndices.isEmpty())
        return;

    juce::Array<int> sorted = selectedPadIndices;
    sorted.sort();
    if (sorted[0] == 0)
        return;

    juce::OwnedArray<SoundPad> movedPads;
    for (int i = sorted.size() - 1; i >= 0; --i)
        movedPads.insert (0, list->pads.removeAndReturn (sorted[i]));

    for (int i = movedPads.size() - 1; i >= 0; --i)
        list->pads.insert (0, movedPads.removeAndReturn (i));

    selectedPadIndices.clear();
    for (int i = 0; i < sorted.size(); ++i)
        selectedPadIndices.add (i);

    for (int i = 0; i < list->pads.size(); ++i)
        if (auto* pad = list->pads[i])
            pad->setPadIndex (i);

    selectedBgmIndex = 0;
    applyPadSelectionVisualState();
    resized();
    rebuildSidebarFromAllLists();
    sidebarPanel.setSelectedIndex (activeListIndex);
    saveProject();
}

void MainComponent::moveSelectedPadsInActiveListToBottom()
{
    if (activeListIndex < 0 || activeListIndex >= allLists.size())
        return;

    auto* list = allLists[activeListIndex];
    if (list == nullptr || list->isLocked || selectedPadIndices.isEmpty())
        return;

    juce::Array<int> sorted = selectedPadIndices;
    sorted.sort();
    if (sorted.getLast() == list->pads.size() - 1)
        return;

    juce::OwnedArray<SoundPad> movedPads;
    for (int i = sorted.size() - 1; i >= 0; --i)
        movedPads.insert (0, list->pads.removeAndReturn (sorted[i]));

    const int start = list->pads.size();
    while (! movedPads.isEmpty())
        list->pads.add (movedPads.removeAndReturn (0));

    selectedPadIndices.clear();
    for (int i = 0; i < sorted.size(); ++i)
        selectedPadIndices.add (start + i);

    for (int i = 0; i < list->pads.size(); ++i)
        if (auto* pad = list->pads[i])
            pad->setPadIndex (i);

    selectedBgmIndex = selectedPadIndices.getFirst();
    applyPadSelectionVisualState();
    resized();
    rebuildSidebarFromAllLists();
    sidebarPanel.setSelectedIndex (activeListIndex);
    saveProject();
}

void MainComponent::movePadsBlockInList (int listIdx, const juce::Array<int>& sourceIndices, int insertBeforeIndex)
{
    if (listIdx < 0 || listIdx >= allLists.size() || sourceIndices.isEmpty())
        return;

    auto* list = allLists[listIdx];
    if (list == nullptr || list->isLocked)
        return;

    if (isPerformingUndoRedo.load (std::memory_order_acquire))
    {
        movePadsBlockInListImpl (listIdx, sourceIndices, insertBeforeIndex);
        return;
    }

    const auto beforeState = std::make_shared<ListOrderUndoState> (captureListOrderSnapshot (listIdx));
    const auto indicesCopy = sourceIndices;
    const int insertCopy = insertBeforeIndex;

    performUndoableMutation (juce::String::fromUTF8 (u8"Di chuyển track"),
                             [this, listIdx, indicesCopy, insertCopy]()
                             {
                                 movePadsBlockInListImpl (listIdx, indicesCopy, insertCopy);
                                 refreshListOrderAfterMutation (listIdx);
                                 triggerSave();
                             },
                             [this, listIdx, beforeState]()
                             {
                                 restoreListOrderSnapshot (listIdx, *beforeState);
                                 refreshListOrderAfterMutation (listIdx);
                                 triggerSave();
                             });
}

void MainComponent::movePadsBlockInListImpl (int listIdx, const juce::Array<int>& sourceIndices, int insertBeforeIndex)
{
    if (listIdx < 0 || listIdx >= allLists.size() || sourceIndices.isEmpty())
        return;

    auto* list = allLists[listIdx];
    if (list == nullptr || list->isLocked)
        return;

    juce::Array<int> sorted;
    for (auto idx : sourceIndices)
    {
        if (idx >= 0 && idx < list->pads.size() && ! sorted.contains (idx))
            sorted.add (idx);
    }
    sorted.sort();
    if (sorted.isEmpty())
        return;

    const int n = list->pads.size();
    const int clampedInsert = juce::jlimit (0, n, insertBeforeIndex);

    int adjustedInsert = clampedInsert;
    for (auto idx : sorted)
        if (idx < clampedInsert)
            --adjustedInsert;

    adjustedInsert = juce::jlimit (0, n - sorted.size(), adjustedInsert);

    juce::OwnedArray<SoundPad> moving;
    for (int i = sorted.size() - 1; i >= 0; --i)
        moving.insert (0, list->pads.removeAndReturn (sorted[i]));

    juce::Array<CueItem> movingMeta;
    for (int i = sorted.size() - 1; i >= 0; --i)
    {
        if (sorted[i] < list->cueMeta.size())
            movingMeta.insert (0, list->cueMeta.removeAndReturn (sorted[i]));
    }

    int insertOffset = 0;
    while (! moving.isEmpty())
    {
        list->pads.insert (adjustedInsert + insertOffset, moving.removeAndReturn (0));
        ++insertOffset;
    }

    insertOffset = 0;
    while (! movingMeta.isEmpty())
    {
        list->cueMeta.insert (adjustedInsert + insertOffset, movingMeta.removeAndReturn (0));
        ++insertOffset;
    }

    selectedPadIndices.clear();
    for (int i = 0; i < sorted.size(); ++i)
        selectedPadIndices.add (adjustedInsert + i);

    for (int i = 0; i < list->pads.size(); ++i)
        if (auto* pad = list->pads[i])
            pad->setPadIndex (i);

    selectedBgmIndex = selectedPadIndices.getFirst();
}

bool MainComponent::isPadSelectedInActiveList (int padIndex) const
{
    return selectedPadIndices.contains (padIndex);
}

void MainComponent::applyPadSelectionVisualState()
{
    if (activeListIndex < 0 || activeListIndex >= allLists.size())
        return;

    auto* list = allLists[activeListIndex];
    if (list == nullptr)
        return;

    for (int i = 0; i < list->pads.size(); ++i)
        if (auto* pad = list->pads[i])
        {
            const bool sel = selectedPadIndices.contains (i);
            pad->setIsSelectedRow (sel);
        }
}

void MainComponent::applySelectionForPadClick (int clickedIndex, const juce::ModifierKeys& mods)
{
    if (activeListIndex < 0 || activeListIndex >= allLists.size())
        return;

    auto* list = allLists[activeListIndex];
    if (list == nullptr || clickedIndex < 0 || clickedIndex >= list->pads.size())
        return;

    const bool toggle = mods.isCommandDown() || mods.isCtrlDown();
    const bool range = mods.isShiftDown();

    if (range)
    {
        const int anchor = juce::jlimit (0, list->pads.size() - 1, selectedBgmIndex);
        const int start = juce::jmin (anchor, clickedIndex);
        const int end = juce::jmax (anchor, clickedIndex);

        selectedPadIndices.clear();
        for (int i = start; i <= end; ++i)
            selectedPadIndices.add (i);
    }
    else if (toggle)
    {
        if (selectedPadIndices.contains (clickedIndex))
            selectedPadIndices.removeFirstMatchingValue (clickedIndex);
        else
            selectedPadIndices.add (clickedIndex);

        if (selectedPadIndices.isEmpty())
            selectedPadIndices.add (clickedIndex);
    }
    else
    {
        // Giữ nguyên multi-selection khi click vào chính một item đã selected,
        // để drag có thể kéo cả cụm thay vì bị reset về 1 item.
        if (! (selectedPadIndices.size() > 1 && selectedPadIndices.contains (clickedIndex)))
        {
            selectedPadIndices.clear();
            selectedPadIndices.add (clickedIndex);
        }
    }

    selectedPadIndices.sort();
    selectedBgmIndex = clickedIndex;
    applyPadSelectionVisualState();

    const auto deferredIndices = selectedPadIndices;
    const int deferredBgmIndex = selectedBgmIndex;
    const int deferredListIndex = activeListIndex;
    const bool prefetchBgm = ! list->isGrid;
    juce::Component::SafePointer<MainComponent> safeThis (this);

    juce::MessageManager::callAsync ([safeThis, deferredListIndex, deferredIndices, deferredBgmIndex, prefetchBgm]()
    {
        if (safeThis == nullptr || deferredListIndex < 0 || deferredListIndex >= safeThis->allLists.size())
            return;

        auto* deferredList = safeThis->allLists[deferredListIndex];

        if (deferredList == nullptr)
            return;

        for (auto idx : deferredIndices)
        {
            if (idx < 0 || idx >= deferredList->pads.size())
                continue;

            if (auto* p = deferredList->pads[idx])
                p->scheduleDeferredInspectorLoads();
        }

        if (prefetchBgm)
            safeThis->prefetchBgmPadAtIndex (deferredBgmIndex);
    });

    if (isShowing())
        grabKeyboardFocus();

    broadcastSelectionSyncIfPrimary();
}

void MainComponent::crossfadeOtherPadsOnSameBus (SoundPad* starter, int listIndex)
{
    if (starter == nullptr || listIndex < 0 || listIndex >= allLists.size())
        return;

    auto* list = allLists[listIndex];
    if (list == nullptr)
        return;

    const int bus = starter->getOutputBus();

    for (auto* p : list->pads)
    {
        if (p == nullptr || p == starter)
            continue;

        if (p->getOutputBus() == bus && (p->isTransportActive() || p->isFading()))
            p->startFadeOut();
    }
}

void MainComponent::offerFfmpegSetupThenIngestVideo (const juce::File& videoFile, SoundPad* targetPad)
{
    if (targetPad == nullptr || ! videoFile.existsAsFile())
        return;

    juce::Component::SafePointer<MainComponent> safeThis (this);
    juce::Component::SafePointer<SoundPad> safePad (targetPad);

    showcontrol::ui::promptMissingFfmpeg (this,
        [safeThis, safePad, videoFile] (showcontrol::ui::FfmpegPromptChoice choice)
        {
            if (safeThis == nullptr || safePad == nullptr)
                return;

            if (choice == showcontrol::ui::FfmpegPromptChoice::copyInstallCommand)
            {
                ErrorHandler::logAndShow (juce::String::fromUTF8 (u8"Tách audio video"),
                                          juce::String::fromUTF8 (u8"Đã sao chép lệnh «brew install ffmpeg» vào clipboard. "
                                                                  u8"Dán vào Terminal, chạy xong rồi kéo video lại."),
                                          ErrorHandler::Severity::Info);
                return;
            }

            if (choice != showcontrol::ui::FfmpegPromptChoice::installHomebrew)
                return;

            showcontrol::ui::installFfmpegWithProgress (safeThis.getComponent(),
                [safeThis, safePad, videoFile] (bool ok, juce::String err)
                {
                    if (safeThis == nullptr || safePad == nullptr)
                        return;

                    if (ok)
                    {
                        ErrorHandler::logAndShow (juce::String::fromUTF8 (u8"Tách audio video"),
                                                  juce::String::fromUTF8 (u8"Đã cài ffmpeg. Đang tách audio…"),
                                                  ErrorHandler::Severity::Info);
                        safeThis->ingestVideoFileToPad (videoFile, safePad.getComponent());
                        return;
                    }

                    ErrorHandler::logAndShow (juce::String::fromUTF8 (u8"Cài ffmpeg"),
                                              err,
                                              ErrorHandler::Severity::Warning);
                });
        });
}

void MainComponent::ingestVideoFileToPad (const juce::File& videoFile, SoundPad* targetPad)
{
    if (targetPad == nullptr || ! videoFile.existsAsFile())
        return;

    juce::Component::SafePointer<MainComponent> safeThis (this);
    juce::Component::SafePointer<SoundPad> safePad (targetPad);

    if (! VideoAudioExtractor::isFfmpegAvailable())
    {
        offerFfmpegSetupThenIngestVideo (videoFile, targetPad);
        return;
    }

    VideoAudioExtractor::extractAudioToWavAsync (videoFile,
        [safeThis, safePad, videoFile] (bool ok, juce::File wavFile, juce::String error)
        {
            if (safeThis == nullptr || safePad == nullptr)
                return;

            auto* mainComp = safeThis.getComponent();
            auto* pad = safePad.getComponent();

            if (pad == nullptr || MainComponent::findListIndexForPad (mainComp->allLists, pad) < 0)
                return;

            if (! ok)
            {
                if (error == "MISSING_FFMPEG")
                {
                    mainComp->offerFfmpegSetupThenIngestVideo (videoFile, pad);
                    return;
                }

                ErrorHandler::logAndShow (juce::String::fromUTF8 (u8"Tách audio video"),
                                        error.isNotEmpty() ? error
                                                           : juce::String::fromUTF8 (u8"Không tách được audio."),
                                        ErrorHandler::Severity::Warning);
                return;
            }

            const juce::String cleanTrackName = VideoAudioExtractor::displayNameFromVideoFile (videoFile);

            mainComp->applyProjectDefaultsToPad (pad);
            pad->setCustomName (cleanTrackName);
            pad->configurePad (wavFile.getFullPathName(), 1.0f, false);

            const int listIdx = MainComponent::findListIndexForPad (mainComp->allLists, pad);
            if (listIdx >= 0)
            {
                if (auto* list = mainComp->allLists[listIdx])
                {
                    mainComp->syncCueMetadataFromPads (*list);

                    if (listIdx == mainComp->activeListIndex)
                        mainComp->refreshCueListPanel();
                }
            }

            mainComp->saveProject();

            if (mainComp->activeListIndex >= 0)
                mainComp->rebuildDefaultHotkeysForList (mainComp->activeListIndex);

            if (mainComp->inspectorPanel.getCurrentPad() == pad)
                mainComp->inspectorPanel.selectPad (pad);

            mainComp->resized();
            mainComp->repaint();
        });
}

void MainComponent::showHotkeyAssignDialogForPad (SoundPad* pad)
{
    if (pad == nullptr)
        return;

    const int listIdx = findListIndexForPad (allLists, pad);
    if (listIdx < 0)
        return;

    const int padIdx = allLists[listIdx]->pads.indexOf (pad);
    if (padIdx < 0)
        return;

    showcontrol::ui::showHotkeyAssignDialog (this,
                                             isDarkMode,
                                             hotkeyManager,
                                             listIdx,
                                             padIdx,
                                             pad->getPadIndex(),
                                             pad->getPadName(),
                                             hotkeyScopeMode == HotkeyScopeMode::global,
                                             activeListIndex,
                                             [this, pad] (bool applied)
                                             {
                                                 if (! applied || pad == nullptr)
                                                     return;

                                                 saveProject();
                                                 inspectorPanel.selectPad (pad);
                                             });
}

namespace
{
    enum class TrackMenuId : int
    {
        replaceFile  = 1,
        duplicate    = 2,
        trimEditor   = 3,
        revealFile   = 4,
        deleteItem   = 5,
        resetFade    = 6,
        renameTrack  = 7,
        sortAscending = 8
    };
}

void MainComponent::showBgmListBackgroundSortMenu (const juce::MouseEvent& e)
{
    juce::ignoreUnused (e);

    if (activeListIndex < 0 || activeListIndex >= allLists.size())
        return;

    auto* list = allLists[activeListIndex];
    if (list == nullptr || list->isGrid || list->isLocked || list->pads.size() <= 1)
        return;

    juce::PopupMenu menu;
    menu.addItem (1, juce::String::fromUTF8 (u8"Sắp xếp danh sách tự động tăng dần (A-Z) 🔤"));

    const int listIdx = activeListIndex;
    juce::Component::SafePointer<MainComponent> safeThis (this);

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (scrollContent.get()).withMousePosition(),
                        [safeThis, listIdx] (int result)
                        {
                            if (safeThis == nullptr || result != 1)
                                return;

                            safeThis->sortListTracksAscending (listIdx);
                        });
}

void MainComponent::syncContextMenuTargetSelection (int listIdx, int padIdx)
{
    if (listIdx != activeListIndex)
        return;

    if (padIdx < 0)
        return;

    if (auto* list = allLists[listIdx])
    {
        if (padIdx >= list->pads.size())
            return;
    }

    // Click chuột phải ngoài nhóm đang chọn → reset về một dòng/PAD duy nhất.
    if (! selectedPadIndices.contains (padIdx))
    {
        selectedPadIndices.clear();
        selectedPadIndices.add (padIdx);
        selectedBgmIndex = padIdx;
        applyPadSelectionVisualState();

        if (cueListPanel != nullptr)
            cueListPanel->setSelectedIndex (padIdx);
    }
}

void MainComponent::showTrackContextMenu (SoundPad* pad)
{
    if (pad == nullptr)
        return;

    const int listIdx = findListIndexForPad (allLists, pad);
    if (listIdx < 0)
        return;

    auto* list = allLists[listIdx];
    if (list == nullptr)
        return;

    const int padIdx = list->pads.indexOf (pad);
    if (padIdx < 0)
        return;

    syncContextMenuTargetSelection (listIdx, padIdx);

    const int totalTracks = list->pads.size();
    const bool canSort = ! list->isGrid && ! list->isLocked && totalTracks > 1;

    juce::PopupMenu menu;

    menu.addCustomItem (1,
                        showcontrol::colours::makeTagColourMenuRow (
                            pad->getTagColourRef(),
                            [this, listIdx, padIdx] (juce::Colour)
                            {
                                if (listIdx < 0 || listIdx >= allLists.size())
                                    return;

                                auto* colourList = allLists[listIdx];
                                if (colourList == nullptr || ! juce::isPositiveAndBelow (padIdx, colourList->pads.size()))
                                    return;

                                if (auto* targetPad = colourList->pads[padIdx])
                                    applyTagColourWithUndo (listIdx, padIdx, targetPad->getTagColour());
                            }),
                        nullptr,
                        juce::String::fromUTF8 (u8" "));

    if (canSort)
    {
        menu.addItem ((int) TrackMenuId::sortAscending,
                      juce::String::fromUTF8 (u8"Sắp xếp toàn bộ danh sách tăng dần (A-Z) 🔤"));
        menu.addSeparator();
    }

    menu.addItem ((int) TrackMenuId::renameTrack,  showcontrol::localization::tr (u8"Đổi tên bài hát"));
    menu.addItem ((int) TrackMenuId::replaceFile, showcontrol::localization::tr (u8"Thay đổi file nhạc..."), ! list->isLocked);
    menu.addItem ((int) TrackMenuId::duplicate,   showcontrol::localization::tr (u8"Nhân bản"));
    menu.addItem ((int) TrackMenuId::trimEditor,  showcontrol::localization::tr (u8"Chỉnh sửa (Trim Editor)..."));
    menu.addItem ((int) TrackMenuId::revealFile,  showcontrol::localization::tr (u8"Mở vị trí tệp..."));
    menu.addSeparator();
    menu.addItem ((int) TrackMenuId::resetFade,   showcontrol::localization::tr (u8"Reset Fade về mặc định (0 ms)"));
    menu.addItem ((int) TrackMenuId::deleteItem,  showcontrol::localization::tr (u8"Xóa"));

    juce::Component::SafePointer<SoundPad> safePad (pad);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (pad).withMousePosition(),
                        [this, safePad, listIdx, padIdx] (int result)
                        {
                            if (safePad == nullptr)
                                return;

                            if (result == (int) TrackMenuId::sortAscending)
                            {
                                sortListTracksAscending (listIdx);
                                return;
                            }

                            handleTrackMenuResult (safePad.getComponent(), result);
                        });
}

void MainComponent::handleTrackMenuResult (SoundPad* pad, int result)
{
    if (pad == nullptr || result == 0)
        return;

    const int listIdx = findListIndexForPad (allLists, pad);
    if (listIdx < 0)
        return;

    auto* list = allLists[listIdx];
    if (list == nullptr)
        return;

    const int padIdx = list->pads.indexOf (pad);
    if (padIdx < 0)
        return;

    switch ((TrackMenuId) result)
    {
        case TrackMenuId::renameTrack:
            pad->beginTrackNameEdit();
            break;

        case TrackMenuId::replaceFile:
            if (! list->isLocked)
                promptReplaceTrackAudioFile (pad);
            break;

        case TrackMenuId::duplicate:
            if (! list->isLocked)
            {
                if (list->isGrid && listIdx == activeListIndex && selectedPadIndices.size() > 1)
                    duplicateSelectedPads();
                else
                    duplicatePadAtIndex (listIdx, padIdx);
            }
            break;

        case TrackMenuId::trimEditor:
            inspectorPanel.selectPad (pad);
            inspectorPanel.openTrimEditor (this);
            break;

        case TrackMenuId::revealFile:
            revealPadFileInOS (pad);
            break;

        case TrackMenuId::resetFade:
            resetFadeForSelectedPads();
            break;

        case TrackMenuId::deleteItem:
            if (! list->isLocked)
                promptDeleteSelectedPadsConfirmation();
            break;

        default:
            break;
    }
}

void MainComponent::resetFadeForSelectedPads()
{
    if (activeListIndex < 0 || activeListIndex >= allLists.size())
        return;

    auto* list = allLists[activeListIndex];
    if (list == nullptr)
        return;

    juce::Array<int> targets = selectedPadIndices;

    if (targets.isEmpty() && selectedBgmIndex >= 0 && selectedBgmIndex < list->pads.size())
        targets.add (selectedBgmIndex);

    if (targets.isEmpty())
        return;

    for (int i = targets.size() - 1; i >= 0; --i)
    {
        const int idx = targets.getReference (i);

        if (idx < 0 || idx >= list->pads.size())
            continue;

        if (auto* pad = list->pads[idx])
            pad->resetFadeDurations();
    }

    if (auto* current = inspectorPanel.getCurrentPad())
    {
        if (targets.contains (list->pads.indexOf (current)))
            inspectorPanel.selectPad (current);
    }

    if (cueListPanel != nullptr)
        cueListPanel->repaint();

    applyPadSelectionVisualState();
    saveProject();
}

void MainComponent::promptReplaceTrackAudioFile (SoundPad* pad)
{
    if (pad == nullptr)
        return;

    juce::File startDir = juce::File::getSpecialLocation (juce::File::userMusicDirectory);
    const auto currentPath = pad->getFilePath().trim();

    if (currentPath.isNotEmpty())
    {
        const auto parent = juce::File (currentPath).getParentDirectory();
        if (parent.exists())
            startDir = parent;
    }

    juce::Component::SafePointer<SoundPad> safePad (pad);

    trackReplaceFileChooser = std::make_unique<juce::FileChooser> (
        juce::String::fromUTF8 (u8"Chọn file nhạc mới..."),
        startDir,
        ShowAudioFormats::fileChooserWildcard());

    trackReplaceFileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                          [this, safePad] (const juce::FileChooser& fc)
                                          {
                                              const auto file = fc.getResult();
                                              if (safePad == nullptr || ! file.existsAsFile())
                                                  return;

                                              handleAudioFileReplacement (safePad.getComponent(), file);
                                          });
}

void MainComponent::handleAudioFileReplacement (SoundPad* pad, const juce::File& file)
{
    if (pad == nullptr || ! file.existsAsFile())
        return;

    const int listIdx = findListIndexForPad (allLists, pad);
    if (listIdx < 0)
        return;

    auto* list = allLists[listIdx];
    if (list == nullptr || list->isLocked)
        return;

    pad->replaceAudioFileKeepingPadSettings (file);

    if (list->isGrid)
        syncCueMetadataFromPads (*list);

    if (listIdx == activeListIndex)
    {
        inspectorPanel.selectPad (pad);
        refreshCueListPanel();
    }

    saveProject();
}

juce::Point<int> MainComponent::findSmartDuplicateGridSlot (const ListData& list,
                                                            int sourceRow,
                                                            int sourceCol) noexcept
{
    const auto grid = buildGridOccupancyFromList (list);
    return findProximitySlotInGrid (grid, sourceRow, sourceCol);
}

MainComponent::GridOccupancy MainComponent::buildGridOccupancyFromList (const ListData& list) noexcept
{
    GridOccupancy grid {};

    for (auto* pad : list.pads)
    {
        if (pad == nullptr || ! pad->occupiesCueGridSlot())
            continue;

        const int row = juce::jlimit (0, showcontrol::padgrid::kRows - 1, pad->getGridRow());
        const int col = juce::jlimit (0, showcontrol::padgrid::kCols - 1, pad->getGridCol());
        grid[(size_t) row][(size_t) col] = true;
    }

    return grid;
}

void MainComponent::markGridCellOccupied (GridOccupancy& grid, int row, int col) noexcept
{
    row = juce::jlimit (0, showcontrol::padgrid::kRows - 1, row);
    col = juce::jlimit (0, showcontrol::padgrid::kCols - 1, col);
    grid[(size_t) row][(size_t) col] = true;
}

juce::Point<int> MainComponent::findProximitySlotInGrid (const GridOccupancy& grid,
                                                       int sourceRow,
                                                       int sourceCol) noexcept
{
    sourceRow = juce::jlimit (0, showcontrol::padgrid::kRows - 1, sourceRow);
    sourceCol = juce::jlimit (0, showcontrol::padgrid::kCols - 1, sourceCol);

    // Tầng 1: ô sát sườn bên phải cùng hàng
    if ((sourceCol + 1) < showcontrol::padgrid::kCols
        && ! grid[(size_t) sourceRow][(size_t) (sourceCol + 1)])
    {
        return { sourceCol + 1, sourceRow };
    }

    // Tầng 2: quét dạt phải cùng hàng
    for (int c = sourceCol + 2; c < showcontrol::padgrid::kCols; ++c)
    {
        if (! grid[(size_t) sourceRow][(size_t) c])
            return { c, sourceRow };
    }

    // Tầng 3: lấp trống nội bộ hàng bên trái
    for (int c = 0; c < sourceCol; ++c)
    {
        if (! grid[(size_t) sourceRow][(size_t) c])
            return { c, sourceRow };
    }

    // Tầng 4: các hàng phía dưới
    for (int r = sourceRow + 1; r < showcontrol::padgrid::kRows; ++r)
    {
        for (int c = 0; c < showcontrol::padgrid::kCols; ++c)
        {
            if (! grid[(size_t) r][(size_t) c])
                return { c, r };
        }
    }

    // Tầng 5: các hàng phía trên đỉnh
    for (int r = 0; r < sourceRow; ++r)
    {
        for (int c = 0; c < showcontrol::padgrid::kCols; ++c)
        {
            if (! grid[(size_t) r][(size_t) c])
                return { c, r };
        }
    }

    return { -1, -1 };
}

void MainComponent::finishBatchDuplicatedPads (const juce::Array<SoundPad*>& createdPads) noexcept
{
    for (auto* dup : createdPads)
    {
        if (dup == nullptr)
            continue;

        dup->finalizeClonedAudioAttach();
        dup->setThumbnailLoadAllowed (true, ! dup->isThumbnailLoaded());
        dup->setNormalizationLoadAllowed (dup->getAutoNormalize(), false);

        if (dup->hasAudioFile())
            dup->prepareForInstantPlay();
    }
}

SoundPad* MainComponent::createDuplicatePadFromSource (const ListData& list, SoundPad* src,
                                                       bool makeVisible, bool fastRamClone)
{
    if (src == nullptr)
        return nullptr;

    auto* dup = createSoundPad();
    scrollContent->addChildComponent (dup);
    applyProjectDefaultsToPad (dup);

    if (fastRamClone)
    {
        dup->cloneReadyAudioFrom (*src);
    }
    else if (src->hasAudioFile())
    {
        dup->configurePad (src->getFilePath(), src->getOutputGain(), src->isLooping());
    }

    dup->setCustomName (src->getPadName());
    dup->setPadThemeColour (src->getTagColour());
    dup->setTrimStart (src->getTrimStart());
    dup->setTrimEnd (src->getTrimEnd());
    dup->setOutputBus (src->getOutputBus());
    dup->setFadeInMs (src->getFadeInMs());
    dup->setFadeOutMs (src->getFadeOutMs());
    dup->setAutoNormalize (src->getAutoNormalize());
    dup->setNormalizeUseLufs (src->getNormalizeUseLufs());
    dup->setLooping (src->isLooping());
    dup->setOutputGain (src->getOutputGain());
    dup->updateTheme (isDarkMode);
    dup->setRenderMode (list.isGrid);
    dup->setCueListPlayback (list.isGrid);
    dup->setVisible (makeVisible);

    return dup;
}

void MainComponent::duplicatePadImpl (SoundPad* sourcePad)
{
    if (sourcePad == nullptr)
        return;

    const int listIdx = findListIndexForPad (allLists, sourcePad);

    if (listIdx < 0 || listIdx >= allLists.size())
        return;

    auto* list = allLists[listIdx];

    if (list == nullptr || list->isLocked)
        return;

    const int padIdx = list->pads.indexOf (sourcePad);

    if (padIdx < 0)
        return;

    if (list->pads.size() >= kMaxCuePadsPerList && list->isGrid)
    {
        showCueGridFullAlert();
        return;
    }

    const bool onActiveList = (listIdx == activeListIndex);

    if (list->isGrid)
    {
        const auto slot = findSmartDuplicateGridSlot (*list,
                                                        sourcePad->getGridRow(),
                                                        sourcePad->getGridCol());

        if (slot.x < 0)
        {
            showCueGridFullAlert();
            return;
        }

        auto* dup = createDuplicatePadFromSource (*list, sourcePad, onActiveList);

        if (dup == nullptr)
            return;

        list->pads.add (dup);
        dup->setGridPosition (slot.y, slot.x, true);
        dup->refreshHotkeyLabel();

        for (int i = 0; i < list->pads.size(); ++i)
            if (list->pads[i] != nullptr)
                list->pads[i]->setPadIndex (i);

        syncCueMetadataFromPads (*list);

        if (onActiveList)
        {
            const int newIdx = list->pads.indexOf (dup);
            selectedPadIndices.clear();
            selectedPadIndices.add (newIdx);
            selectedBgmIndex = newIdx;
            applyPadSelectionVisualState();
            inspectorPanel.selectPad (dup);
            layoutActiveListPads();

            if (auto* panel = getPadPanel())
                panel->resyncAndLayout();

            refreshCueListPanel();
        }

        rebuildHotkeyBindings();
        rebuildSidebarFromAllLists();
        triggerSave();
        return;
    }

    const int insertAt = padIdx + 1;
    auto* dup = createDuplicatePadFromSource (*list, sourcePad, onActiveList);

    if (dup == nullptr)
        return;

    list->pads.insert (insertAt, dup);

    for (int i = 0; i < list->pads.size(); ++i)
        if (list->pads[i] != nullptr)
            list->pads[i]->setPadIndex (i);

    if (onActiveList)
    {
        selectedPadIndices.clear();
        selectedPadIndices.add (insertAt);
        selectedBgmIndex = insertAt;
        applyPadSelectionVisualState();
        inspectorPanel.selectPad (dup);
        resized();
        refreshCueListPanel();
    }

    rebuildSidebarFromAllLists();
    triggerSave();
}

void MainComponent::duplicateSelectedPadsImpl()
{
    if (activeListIndex < 0 || activeListIndex >= allLists.size())
        return;

    auto* list = allLists[activeListIndex];

    if (list == nullptr || list->isLocked || ! list->isGrid)
        return;

    juce::Array<SoundPad*> selectedPads;

    for (const auto idx : selectedPadIndices)
    {
        if (! juce::isPositiveAndBelow (idx, list->pads.size()))
            continue;

        if (auto* pad = list->pads[idx])
            selectedPads.addIfNotAlreadyThere (pad);
    }

    if (selectedPads.isEmpty())
        return;

    struct PadGridSortOrder
    {
        static int compareElements (SoundPad* a, SoundPad* b)
        {
            const int keyA = a->getGridRow() * showcontrol::padgrid::kCols + a->getGridCol();
            const int keyB = b->getGridRow() * showcontrol::padgrid::kCols + b->getGridCol();
            return keyA - keyB;
        }
    };

    PadGridSortOrder gridOrder;
    selectedPads.sort (gridOrder);

    auto gridOccupied = buildGridOccupancyFromList (*list);

    int successCount = 0;
    int failCount    = 0;
    juce::Array<SoundPad*> createdPads;

    for (auto* sourcePad : selectedPads)
    {
        if (sourcePad == nullptr)
            continue;

        if (list->pads.size() >= kMaxCuePadsPerList)
        {
            ++failCount;
            continue;
        }

        const auto slot = findProximitySlotInGrid (gridOccupied,
                                                   sourcePad->getGridRow(),
                                                   sourcePad->getGridCol());

        if (slot.x < 0)
        {
            ++failCount;
            continue;
        }

        auto* dup = createDuplicatePadFromSource (*list, sourcePad, false, true);

        if (dup == nullptr)
        {
            ++failCount;
            continue;
        }

        list->pads.add (dup);
        dup->assignGridCellSilent (slot.y, slot.x);
        markGridCellOccupied (gridOccupied, slot.y, slot.x);
        createdPads.add (dup);
        ++successCount;
    }

    if (successCount == 0)
    {
        if (failCount > 0)
            showCueGridFullAlert();

        return;
    }

    for (int i = 0; i < list->pads.size(); ++i)
        if (list->pads[i] != nullptr)
            list->pads[i]->setPadIndex (i);

    syncCueMetadataFromPads (*list);
    finishBatchDuplicatedPads (createdPads);

    for (auto* dup : createdPads)
        if (dup != nullptr)
            dup->setVisible (true);

    selectedPadIndices.clear();

    for (auto* dup : createdPads)
    {
        const int newIdx = list->pads.indexOf (dup);

        if (newIdx >= 0)
            selectedPadIndices.addIfNotAlreadyThere (newIdx);
    }

    selectedPadIndices.sort();

    if (! selectedPadIndices.isEmpty())
    {
        selectedBgmIndex = selectedPadIndices.getFirst();
        applyPadSelectionVisualState();
        inspectorPanel.selectPad (list->pads[selectedBgmIndex]);
    }

    layoutActiveListPads();

    if (auto* panel = getPadPanel())
        panel->resyncAndLayout();

    refreshCueListPanel();
    rebuildHotkeyBindings();
    rebuildSidebarFromAllLists();
    triggerSave();

    if (failCount > 0)
        showCueBatchDuplicatePartialAlert (successCount, failCount);
}

void MainComponent::duplicatePad (SoundPad* sourcePad)
{
    if (sourcePad == nullptr)
        return;

    const int listIdx = findListIndexForPad (allLists, sourcePad);

    if (listIdx < 0)
        return;

    if (isPerformingUndoRedo.load (std::memory_order_acquire))
    {
        duplicatePadImpl (sourcePad);
        return;
    }

    const auto beforeOrder = std::make_shared<ListOrderUndoState> (captureListOrderSnapshot (listIdx));

    performUndoableMutation (juce::String::fromUTF8 (u8"Nhân bản track"),
                             [this, sourcePad]()
                             {
                                 duplicatePadImpl (sourcePad);
                             },
                             [this, listIdx, beforeOrder]()
                             {
                                 restoreListOrderAndDeleteOrphanPads (listIdx, *beforeOrder);
                                 refreshAllPanelsAfterDataMutation (listIdx);
                                 triggerSave();
                             });
}

void MainComponent::duplicateSelectedPads()
{
    if (activeListIndex < 0 || activeListIndex >= allLists.size())
        return;

    if (isPerformingUndoRedo.load (std::memory_order_acquire))
    {
        duplicateSelectedPadsImpl();
        return;
    }

    const int listIdx = activeListIndex;
    const auto beforeOrder = std::make_shared<ListOrderUndoState> (captureListOrderSnapshot (listIdx));

    performUndoableMutation (juce::String::fromUTF8 (u8"Nhân bản track"),
                             [this]()
                             {
                                 duplicateSelectedPadsImpl();
                             },
                             [this, listIdx, beforeOrder]()
                             {
                                 restoreListOrderAndDeleteOrphanPads (listIdx, *beforeOrder);
                                 refreshAllPanelsAfterDataMutation (listIdx);
                                 triggerSave();
                             });
}

void MainComponent::duplicatePadAtIndex (int listIdx, int padIdx)
{
    if (listIdx < 0 || listIdx >= allLists.size())
        return;

    auto* list = allLists[listIdx];

    if (list == nullptr || padIdx < 0 || padIdx >= list->pads.size())
        return;

    duplicatePad (list->pads[padIdx]);
}

void MainComponent::revealPadFileInOS (SoundPad* pad)
{
    if (pad == nullptr)
        return;

    const juce::String path = pad->getFilePath().trim();
    if (path.isEmpty())
        return;

    juce::File file (path);

    if (file.exists())
    {
        file.revealToUser();
        return;
    }

    const auto parent = file.getParentDirectory();
    if (parent.exists())
        parent.revealToUser();
}

void MainComponent::handleTrackFinished (SoundPad* finishedPad)
{
    advanceBgmPlaylistOnNaturalEnd (finishedPad);
}

void MainComponent::advanceBgmPlaylistOnNaturalEnd (SoundPad* finishedPad)
{
    if (finishedPad == nullptr)
        return;

    // Natural end: SoundPad::timerCallback() phát hiện trackFinishedGeneration tăng
    // (PadRealtimeSource::finishTrackNaturally) — không đi qua fade-out Spacebar.
    if (finishedPad->isFadeOutInProgress() || finishedPad->isStopping())
        return;

    if (finishedPad->getCueState() != PadCueState::ready)
        return;

    const int listIndex = findListIndexForPad (allLists, finishedPad);
    if (listIndex < 0 || listIndex >= allLists.size())
        return;

    auto* bgmList = allLists[listIndex];
    if (bgmList == nullptr || bgmList->isGrid)
        return;

    // Chỉ xử lý BGM đang active — không đụng CUE/PAD list khác.
    if (listIndex != activeListIndex)
        return;

    const int finishedIdx = bgmList->pads.indexOf (finishedPad);
    if (finishedIdx < 0 || finishedIdx >= bgmList->pads.size())
        return;

    // Luôn tìm bài kế tiếp; isLooping chỉ ảnh hưởng wrap ở cuối playlist (findNextBgmTrackIndex).
    const int nextIdx = findNextBgmTrackIndex (*bgmList, finishedIdx);

    if (nextIdx < 0)
    {
        // Hết danh sách + loop tắt: dừng hẳn, giữ selection ở bài cuối vừa phát xong.
        for (auto* pad : bgmList->pads)
            if (pad != nullptr && pad->isTransportActive())
                pad->triggerStop();

        selectedBgmIndex = finishedIdx;
        selectedPadIndices.clear();
        selectedPadIndices.add (finishedIdx);
        applyPadSelectionVisualState();
        inspectorPanel.selectPad (finishedPad);
        masterDeckPanel.setActivePad (finishedPad);
        finishedPad->supplementBpmFromFileIfMissing();
        masterDeckPanel.setTrackMetadata (finishedPad->getMetadata());
        masterDeckPanel.refreshTransportLabels();
        lastUiSyncedPlayingPad = nullptr;
        refreshSidebarPlayingStatus();
        repaint();
        return;
    }

    if (nextIdx >= bgmList->pads.size())
        return;

    auto* nextPad = bgmList->pads[nextIdx];
    if (nextPad == nullptr || ! nextPad->hasAudioFile())
        return;

    nextPad->prepareForInstantPlay();
    prefetchBgmPadAtIndex (nextIdx);

    for (auto* pad : bgmList->pads)
    {
        if (pad != nullptr && pad != nextPad)
            pad->triggerStop();
    }

    nextPad->triggerPlay();
    syncUiToPlayingPad (nextPad, true);
}

//==============================================================================
MainComponent::MainComponent()
{
    setOpaque (true);
    formatManager.registerBasicFormats();
    showcontrol::audio::bindActiveFormatManager (formatManager);
    updateChecker = std::make_unique<showcontrol::update::ShowUpdateChecker>();

    // Áp dụng Global LookAndFeel ngay đầu, trước khi thêm bất kỳ widget nào
    juce::LookAndFeel::setDefaultLookAndFeel (&appLookAndFeel);
    tooltipWindow.setMillisecondsBeforeTipAppears (1000);

    addAndMakeVisible (masterDeckPanel);
    addAndMakeVisible (busMixerPanel);

    masterDeckPanel.getMasterLevelLeft  = [this] { return multiOutputCallback.getLevelLeft(); };
    masterDeckPanel.getMasterLevelRight = [this] { return multiOutputCallback.getLevelRight(); };
    masterDeckPanel.resolveLiveTransportPad = [this]() -> SoundPad*
    {
        return findGloballyPrioritizedPlayingPad();
    };

    masterDeckPanel.onVolumeChanged = [this] (float volume) {
        multiOutputCallback.setMasterGain (volume);
        busMixerPanel.setBusGain (0, volume, juce::dontSendNotification);
    };

    busMixerPanel.onBusGainChanged = [this] (int bus, float gain)
    {
        multiOutputCallback.setBusGain (bus, gain);
        if (bus == 0)
            masterDeckPanel.setMasterVolumeValue (gain, juce::dontSendNotification);
        saveProject();
    };
    busMixerPanel.getBusPeak = [this] (int bus, bool isLeft)
    {
        return isLeft ? multiOutputCallback.getBusPeakL (bus) : multiOutputCallback.getBusPeakR (bus);
    };
    busMixerPanel.getBusName = [this] (int bus) { return multiOutputCallback.getBusName (bus); };
    for (int b = 0; b < BusMixerPanel::kNumBuses; ++b)
        busMixerPanel.setBusGain (b, multiOutputCallback.getBusGain (b), juce::dontSendNotification);

    masterDeckPanel.onPauseAll = [this] { triggerGlobalPauseAll(); };

    masterDeckPanel.onStopAll = [this] { triggerGlobalStopAll(); };

    masterDeckPanel.onFadeAll = [this] { triggerGlobalPanicFadeAll(); };

    masterDeckPanel.onBgmPrev      = [this] { triggerBgmPrev(); };
    masterDeckPanel.onBgmPlayPause = [this] { triggerBgmPlayPause(); };
    masterDeckPanel.onBgmNext      = [this] { triggerBgmNext(); };

    addAndMakeVisible (sidebarPanel);
    addAndMakeVisible (inspectorPanel);
    addAndMakeVisible (showSidebarBtn);
    addAndMakeVisible (showInspectorBtn);
    splitterButtonLaf = std::make_unique<SplitterButtonLookAndFeel>();
    showSidebarBtn.setLookAndFeel (splitterButtonLaf.get());
    showInspectorBtn.setLookAndFeel (splitterButtonLaf.get());

    leftSplitter = std::make_unique<SplitterHandle> (true);
    rightSplitter = std::make_unique<SplitterHandle> (false);
    addAndMakeVisible (*leftSplitter);
    addAndMakeVisible (*rightSplitter);
    leftSplitter->onDragDelta = [this] (int delta)
    {
        // Chỉ resize trong min/max, không auto-hide khi kéo quá min.
        constexpr int minW = 220;
        constexpr int maxW = 380;
        sidebarWidth = juce::jlimit (minW, maxW, sidebarWidth + delta);
        if (! sidebarVisible)
            setSidebarVisible (true);
        else
            resized();
    };
    leftSplitter->onDragFinished = [this] { saveProject(); };

    rightSplitter->onDragDelta = [this] (int delta)
    {
        // Chỉ resize trong min/max, không auto-hide khi kéo quá min.
        constexpr int minW = 300;
        constexpr int maxW = 520;
        inspectorWidth = juce::jlimit (minW, maxW, inspectorWidth + delta);
        if (! inspectorVisible)
            setInspectorVisible (true);
        else
            resized();
    };
    rightSplitter->onDragFinished = [this] { saveProject(); };

    showSidebarBtn.setButtonText (juce::String::fromUTF8 (u8"◂"));
    showSidebarBtn.onClick = [this]
    {
        setSidebarVisible (! sidebarVisible);
        saveProject();
    };
    showInspectorBtn.setButtonText (juce::String::fromUTF8 (u8"▸"));
    showInspectorBtn.onClick = [this]
    {
        setInspectorVisible (! inspectorVisible);
        saveProject();
    };
    showSidebarBtn.setTooltip (showcontrol::localization::tr (u8"Ẩn/Hiện Sidebar"));
    showInspectorBtn.setTooltip (showcontrol::localization::tr (u8"Ẩn/Hiện Inspector"));

    listHeaderComponent = std::make_unique<ListHeaderComponent>();
    addAndMakeVisible (*listHeaderComponent);

    emptyStatePanel = std::make_unique<EmptyProjectPlaceholder>();
    emptyStatePanel->setDarkMode (isDarkMode);
    addChildComponent (*emptyStatePanel);
    emptyStatePanel->setVisible (false);

    addAndMakeVisible (viewScroller);
    viewScroller.setWantsKeyboardFocus (false);
    
    auto* container = new PadPanel();
    scrollContent.reset (container);
    scrollContent->setWantsKeyboardFocus (false);

    container->onChainedKeyPressed = [this] (const juce::KeyPress& key)
    {
        if (showcontrol::keyboard::isUndoKeyPress (key))
            return performApplicationUndo();

        if (showcontrol::keyboard::isRedoKeyPress (key))
            return performApplicationRedo();

        return false;
    };

    container->onBackgroundRightClick = [this] (const juce::MouseEvent& e)
    {
        if (activeListIndex >= 0 && activeListIndex < allLists.size())
        {
            auto* list = allLists[activeListIndex];
            if (list != nullptr && ! list->isGrid)
            {
                showBgmListBackgroundSortMenu (e);
                return;
            }
        }

        triggerManualMusicIngestion();
    };
    container->onBackgroundMouseDown = [this] (const juce::MouseEvent& e)
    {
        if (! e.mods.isRightButtonDown())
            beginMarqueeSelection (e.getPosition(), e.mods);
    };
    container->onBackgroundMouseDrag = [this] (const juce::MouseEvent& e)
    {
        updateMarqueeSelection (e.getPosition());
    };
    container->onBackgroundMouseUp = [this] (const juce::MouseEvent&)
    {
        endMarqueeSelection();
    };
    container->onPadDroppedAtCell = [this] (SoundPad* pad, int row, int col)
    {
        if (pad == nullptr || activeListIndex < 0 || activeListIndex >= allLists.size())
            return;

        movePadToGridCell (activeListIndex, pad, row, col);
    };
    container->onGridFocusPadChanged = [this] (SoundPad* pad)
    {
        forwardUiSelectionToPad (pad, false);

        if (pad == nullptr || activeListIndex < 0 || activeListIndex >= allLists.size())
            return;

        auto* list = allLists[activeListIndex];

        if (list == nullptr || ! list->isGrid)
            return;

        const int deferredIdx = list->pads.indexOf (pad);

        if (deferredIdx < 0)
            return;

        const bool autoArm = list->autoArmOnSelect;
        juce::Component::SafePointer<MainComponent> safeThis (this);

        juce::MessageManager::callAsync ([safeThis, deferredIdx, autoArm]()
        {
            if (safeThis == nullptr)
                return;

            safeThis->prefetchBgmPadAtIndex (deferredIdx);

            if (safeThis->activeListIndex < 0 || safeThis->activeListIndex >= safeThis->allLists.size())
                return;

            auto* activeList = safeThis->allLists[safeThis->activeListIndex];

            if (activeList == nullptr || deferredIdx < 0 || deferredIdx >= activeList->pads.size())
                return;

            if (auto* focusedPad = activeList->pads[deferredIdx])
            {
                if (autoArm)
                    safeThis->armPad (focusedPad);
                else
                    focusedPad->prepareForInstantPlay();
            }
        });
    };

    viewScroller.setViewedComponent (scrollContent.get(), false);
    viewScroller.setScrollBarsShown (true, false, true, false);
    viewScroller.setScrollBarThickness (7);
    viewScroller.setOpaque (true);
    container->setOpaque (true);

    padReorderOverlay = std::make_unique<PadReorderOverlay> (*this);
    addChildComponent (*padReorderOverlay);
    padReorderOverlay->setVisible (false);

    addAndMakeVisible (addMusicFloatingBtn);
    addMusicFloatingBtn.setButtonText (juce::String::fromUTF8 (u8"+ THÊM BÀI HÁT VÀO SẢNH"));
    addMusicFloatingBtn.onClick = [this] { triggerManualMusicIngestion(); };
    addMusicFloatingBtn.setVisible (false); // theo yêu cầu: bỏ nút khỏi UI

    playoutModeButtonLaf = std::make_unique<PlayoutModeButtonLookAndFeel>();
    playoutModeButtonLaf->setDarkMode (isDarkMode);

    playoutModeBar = std::make_unique<PlayoutModeBar>();
    playoutModeBar->setLookAndFeelPtr (playoutModeButtonLaf.get());
    playoutModeBar->setDarkMode (isDarkMode);
    playoutModeBar->onPadModeSelected = [this] (bool isPadMode) { setPlayoutMode (isPadMode); };
    addAndMakeVisible (*playoutModeBar);
    playoutModeBar->setVisible (false);

    cueListPanel = std::make_unique<CueListPanel>();
    addAndMakeVisible (*cueListPanel);
    cueListPanel->setVisible (false);

    cueListPanel->onChainedKeyPressed = [this] (const juce::KeyPress& key)
    {
        if (showcontrol::keyboard::isUndoKeyPress (key))
            return performApplicationUndo();

        if (showcontrol::keyboard::isRedoKeyPress (key))
            return performApplicationRedo();

        return false;
    };

    cueListPanel->onCueSelected = [this] (int idx)
    {
        if (activeListIndex < 0 || activeListIndex >= allLists.size())
            return;

        auto* list = allLists[activeListIndex];
        if (list == nullptr || idx < 0 || idx >= list->pads.size())
            return;

        selectedBgmIndex = idx;
        selectedPadIndices.clear();
        selectedPadIndices.add (idx);

        if (auto* pad = list->pads[idx])
        {
            if (list->autoArmOnSelect)
                armPad (pad);

            syncPadTagColourFromCueMeta (*list, pad);
            presentPadInInspector (pad);
        }

        applyPadSelectionVisualState();

        juce::Component::SafePointer<MainComponent> safeThis (this);
        juce::MessageManager::callAsync ([safeThis, idx]()
        {
            if (safeThis == nullptr)
                return;

            safeThis->prefetchBgmPadAtIndex (idx);

            if (safeThis->activeListIndex < 0 || safeThis->activeListIndex >= safeThis->allLists.size())
                return;

            auto* activeList = safeThis->allLists[safeThis->activeListIndex];

            if (activeList == nullptr || idx < 0 || idx >= activeList->pads.size())
                return;

            if (auto* deferredPad = activeList->pads[idx])
            {
                if (! activeList->autoArmOnSelect)
                    deferredPad->prepareForInstantPlay();
            }
        });

        broadcastSelectionSyncIfPrimary();
    };

    cueListPanel->onDeleteKeyPressed = [this] { handleDeleteKeyForActiveSelection(); };

    cueListPanel->onCueRightClick = [this] (int idx)
    {
        if (activeListIndex >= 0)
            syncContextMenuTargetSelection (activeListIndex, idx);
    };

    cueListPanel->onCueTriggered = [this] (int idx) { triggerCueListPlay (idx); };

    cueListPanel->onCueListPlay  = [this] (int idx) { triggerCueListPlay (idx); };
    cueListPanel->onCueListPause = [this] (int idx) { triggerCueListPause (idx); };
    cueListPanel->onCueListStop  = [this] (int idx) { triggerCueListStop (idx); };

    cueListPanel->setPadAccessor ([this] (int index) -> SoundPad*
    {
        if (activeListIndex < 0 || activeListIndex >= allLists.size())
            return nullptr;

        auto* list = allLists[activeListIndex];
        if (list == nullptr || ! juce::isPositiveAndBelow (index, list->pads.size()))
            return nullptr;

        return list->pads[index];
    });

    cueListPanel->onCueSelectionChanged = [this] (const juce::Array<int>& indices)
    {
        if (isOperatingState())
            return;

        selectedPadIndices = indices;

        if (indices.isEmpty())
            return;

        selectedBgmIndex = indices.getLast();
        applyPadSelectionVisualState();

        if (activeListIndex < 0 || activeListIndex >= allLists.size())
            return;

        auto* list = allLists[activeListIndex];
        if (list == nullptr || ! juce::isPositiveAndBelow (selectedBgmIndex, list->pads.size()))
            return;

        if (auto* pad = list->pads[selectedBgmIndex])
            inspectorPanel.selectPad (pad);

        broadcastSelectionSyncIfPrimary();
    };

    cueListPanel->onCuesBlockReordered = [this] (const juce::Array<int>& sourceIndices, int insertBeforeIndex)
    {
        if (activeListIndex < 0)
            return;

        movePadsBlockInList (activeListIndex, sourceIndices, insertBeforeIndex);

        if (auto* list = getActiveListSafe())
            syncCueMetadataFromPads (*list);

        refreshCueListPanel();

        if (cueListPanel != nullptr)
            cueListPanel->setSelectedIndices (selectedPadIndices);
    };

    cueListPanel->onCueReordered = [this] (int fromIdx, int toIdx)
    {
        if (activeListIndex >= 0)
            movePadInList (activeListIndex, fromIdx, toIdx);
    };

    cueListPanel->onSortRowsAscending = [this]()
    {
        if (activeListIndex >= 0)
            sortListTracksAscending (activeListIndex);
    };

    cueListPanel->canSortRows = [this]() -> bool
    {
        if (auto* list = getActiveListSafe())
            return ! list->isLocked && list->pads.size() > 1;

        return false;
    };

    cueListPanel->onTrackMenuResult = [this] (int cueIndex, int result)
    {
        if (activeListIndex < 0 || activeListIndex >= allLists.size())
            return;

        auto* list = allLists[activeListIndex];
        if (list == nullptr || cueIndex < 0 || cueIndex >= list->pads.size())
            return;

        if (auto* pad = list->pads[cueIndex])
            handleTrackMenuResult (pad, result);
    };

    cueListPanel->onCueColorChanged = [this] (int cueIndex, juce::Colour colour)
    {
        if (isOperatingState())
            return;

        if (activeListIndex < 0 || activeListIndex >= allLists.size())
            return;

        auto* list = allLists[activeListIndex];
        if (list == nullptr)
            return;

        applyTagColourWithUndo (activeListIndex, cueIndex, colour);
    };

    cueListPanel->onTrackRenamed = [this] (int cueIndex, const juce::String& newName, const juce::String& oldName)
    {
        if (isOperatingState())
            return;

        if (activeListIndex < 0 || activeListIndex >= allLists.size())
            return;

        auto* list = allLists[activeListIndex];
        if (list == nullptr || cueIndex < 0 || cueIndex >= list->pads.size())
            return;

        applyTrackRenameWithUndo (activeListIndex, cueIndex, newName, oldName);
    };

    sidebarPanel.onListSelected = [this] (int idx, int count, bool /*isGridHint*/)
    {
        if (idx < 0 || idx >= allLists.size() || allLists[idx] == nullptr)
            return;

        loadList (idx, count, allLists[idx]->isGrid);
        updateMainDeskDisplay();
        broadcastSelectionSyncIfPrimary();
    };
    sidebarPanel.onAddList = [this] (int idx, juce::String name, int count, bool isGrid)
    {
        while (allLists.size() <= idx)
            allLists.add (new ListData());

        auto* list = allLists[idx];
        list->isGrid = isGrid;
        list->useCueListPanel = false;

        sidebarPanel.addSet (name, list->pads.size(), isGrid, false, list->isLocked, list->themeColour);
        loadList (idx, count, isGrid);
    };
    sidebarPanel.onFoldersSmartImport = [this] (const juce::StringArray& folderPaths, bool targetIsBgm)
    {
        importListsFromDroppedFolders (folderPaths, targetIsBgm);
    };
    sidebarPanel.onCrossCopyDroppedToPlaylist = [this] (int listIdx,
                                                        const juce::Array<showcontrol::crossdrag::TrackCopyRecord>& tracks)
    {
        appendCopyTracksToPlaylist (listIdx, tracks);
    };
    sidebarPanel.onModeChanged = [this] (int idx, bool isGrid)
    {
        if (idx < 0 || idx >= allLists.size() || allLists[idx] == nullptr)
            return;

        allLists[idx]->isGrid = isGrid;
        allLists[idx]->useCueListPanel = false;
        loadList (idx, 0, isGrid);
        saveProject();
    };

    sidebarPanel.onDeleteList = [this] (int idx)
    {
        if (idx < 0 || idx >= allLists.size())
            return;

        if (isPerformingUndoRedo.load (std::memory_order_acquire))
        {
            deleteListAtIndexImpl (idx);
            return;
        }

        const auto playlistSnap = std::make_shared<PlaylistSnapshotUndoState> (capturePlaylistSnapshot (idx));

        performUndoableMutation (juce::String::fromUTF8 (u8"Xóa danh sách"),
                                 [this, idx]()
                                 {
                                     deleteListAtIndexImpl (idx);
                                     clearAllPanelsSelectionLive();
                                     triggerSave();
                                 },
                                 [this, playlistSnap]()
                                 {
                                     restorePlaylistSnapshot (*playlistSnap);
                                     triggerSave();
                                 });
    };

    sidebarPanel.onMoveList = [this] (int fromIdx, int toIdx) { moveListInProject (fromIdx, toIdx); };

    sidebarPanel.onRenameList = [this] (int idx, juce::String) { juce::ignoreUnused (idx); saveProject(); };

    sidebarPanel.onDuplicateSet   = [this] (int idx) { duplicateListAtIndex (idx); };
    sidebarPanel.onAddSounds      = [this] (int idx) { addSoundsToSet (idx); };
    sidebarPanel.onMorphSetStructure = [this] (int idx) { morphSetStructure (idx); };

    sidebarPanel.onListThemeColourChanged = [this] (int idx, juce::Colour colour)
    {
        updateListThemeColour (idx, colour);
    };

    // BGM: isLooping = lặp lại cả danh sách (playlist wrap). CUE: loop từng pad.
    sidebarPanel.onLoopListToggled = [this] (int idx, bool isLooping)
    {
        if (idx < 0 || idx >= allLists.size())
            return;

        auto* list = allLists[idx];
        if (list == nullptr)
            return;

        list->isLooping = isLooping;
        sidebarPanel.setListLooping (idx, isLooping);

        if (list->isGrid)
        {
            for (auto* pad : list->pads)
                if (pad != nullptr)
                    pad->setLooping (isLooping);
        }

        saveProject();
        repaint();
    };

    sidebarPanel.onSearchChanged = [this] (const juce::String& query) {
        juce::String cleanQuery = cleanVietnameseString (query).trim();
        juce::StringArray searchWords; searchWords.addTokens (cleanQuery, " ", ""); searchWords.removeEmptyStrings();

        for (int i = 0; i < allLists.size(); ++i) {
            auto* current = allLists[i]; bool listHasMatch = false;
            juce::String cleanListName = cleanVietnameseString (sidebarPanel.getListName (i));
            bool nameMatches = true;
            for (auto word : searchWords) { if (! cleanListName.contains (word)) { nameMatches = false; break; } }
            if (searchWords.size() > 0 && nameMatches) listHasMatch = true;

            if (current != nullptr) {
                for (auto* pad : current->pads) {
                    if (pad != nullptr) {
                        juce::String targetTokens = cleanVietnameseString (pad->getSearchableTokens());
                        bool allWordsMatch = true;
                        for (auto word : searchWords) { if (! targetTokens.contains (word)) { allWordsMatch = false; break; } }
                        if (allWordsMatch) listHasMatch = true;
                        if (i == activeListIndex) pad->setVisible (searchWords.size() == 0 || allWordsMatch);
                        else pad->setVisible (false);
                    }
                }
            }
            sidebarPanel.setListSearchMatch (i, searchWords.size() == 0 || listHasMatch);
        }
        resized();
    };

    inspectorPanel.onHotkeyScopeChanged = [this] (int scopeId)
    {
        hotkeyScopeMode = (scopeId == 2)
            ? HotkeyScopeMode::global
            : HotkeyScopeMode::activeList;
        saveProject();
    };

    inspectorPanel.isPlaybackCommandBlocked = [this] { return isPlaybackCommandBlocked(); };

    inspectorPanel.onPlayPadRequested = [this] (SoundPad* pad)
    {
        if (pad == nullptr)
            return;

        const int listIdx = findListIndexForPad (allLists, pad);
        if (listIdx >= 0 && listIdx < allLists.size() && listIdx != activeListIndex)
        {
            auto* list = allLists[listIdx];
            if (list != nullptr)
            {
                sidebarPanel.setSelectedIndex (listIdx);
                loadList (listIdx, sidebarPanel.getListTrackCount (listIdx), list->isGrid);
            }
        }

        syncUiToPlayingPad (pad, true);
    };

    inspectorPanel.onFadePadRequested = [this] (SoundPad* pad)
    {
        if (pad != nullptr)
            syncUiToPlayingPad (pad, false);
    };

    inspectorPanel.onProjectEdited = [this]
    {
        if (isOperatingState())
            return;

        saveProject();
    };

    inspectorPanel.onTagColourChanged = [this] (SoundPad* pad, juce::Colour colour)
    {
        if (isOperatingState() || pad == nullptr)
            return;

        const int listIdx = findListIndexForPad (allLists, pad);
        if (listIdx < 0)
            return;

        auto* list = allLists[listIdx];
        if (list == nullptr)
            return;

        const int padIdx = list->pads.indexOf (pad);
        if (padIdx < 0)
            return;

        applyTagColourWithUndo (listIdx, padIdx, colour);
    };

    inspectorPanel.onInspectorGestureBegan = [this]
    {
        beginInspectorPadUndoSession (inspectorPanel.getCurrentPad());
    };

    inspectorPanel.onInspectorGestureEnded = [this]
    {
        commitInspectorPadUndoSession (inspectorPanel.getCurrentPad());
    };

    inspectorPanel.onPadAudioCutRequested = [this] (SoundPad* pad, double cutStart, double cutEnd,
                                                    std::function<void (bool)> onDone)
    {
        performPadAudioCutWithUndo (pad, cutStart, cutEnd, std::move (onDone));
    };

    inspectorPanel.onApplicationUndoRequested = [this] { return performApplicationUndo(); };
    inspectorPanel.onApplicationRedoRequested = [this] { return performApplicationRedo(); };
    inspectorPanel.onApplicationCanUndo = [this] { return undoManager.canUndo(); };
    inspectorPanel.onApplicationCanRedo = [this] { return undoManager.canRedo(); };

    inspectorPanel.onActivePadChanged = [this] { refreshGlobalTrackAccent(); };

    inspectorPanel.onPadLoopChanged = [this] (SoundPad* pad)
    {
        if (cueListPanel == nullptr || pad == nullptr)
            return;

        if (auto* list = getActiveListSafe())
        {
            const int row = list->pads.indexOf (pad);

            if (row >= 0)
                cueListPanel->repaintCueRow (row);
        }
    };

    inspectorPanel.onTrackNameChanged = [this]
    {
        if (isOperatingState())
            return;

        if (cueListPanel != nullptr && cueListPanel->isVisible())
        {
            if (auto* list = getActiveListSafe())
            {
                syncCueMetadataFromPads (*list);
                cueListPanel->setCues (list->cueMeta);
            }
            else
            {
                cueListPanel->refreshListBoxData (false);
            }
        }
        else if (auto* pad = inspectorPanel.getCurrentPad())
        {
            pad->repaint();
        }
    };

    inspectorPanel.onOutputBusChanged = [this] (int /*bus*/) { saveProject(); };

    inspectorPanel.onNormalizeActiveListRequested = [this] (const showcontrol::loudness::LoudnessSettings& settings)
    {
        normalizeActiveListWithSettings (settings);
    };

    inspectorPanel.onFetchLoudnessListPreview = [this] (const showcontrol::loudness::LoudnessSettings& settings)
    {
        return buildLoudnessPreviewForActiveList (settings);
    };

    inspectorPanel.onDefaultNormalizeModeChanged = [this] (bool useLufs)
    {
        projectDefaultNormalizeLufs = useLufs;
        saveProject();
    };

    masterDeckPanel.onAudioSettingsRequested = [this] { showPreferencesDialog (0); };
    masterDeckPanel.onStageMonitorToggleRequested = [this] { toggleStageMonitorWindow(); };

    inspectorPanel.setDefaultNormalizeMode (projectDefaultNormalizeLufs);
    inspectorPanel.setHotkeyScopeSelectionId (static_cast<int> (hotkeyScopeMode));
    juce::Desktop::getInstance().addDarkModeSettingListener (this);

    setWantsKeyboardFocus (true);

    setSize (1280, 800);
    setSidebarVisible (true);
    setInspectorVisible (true);
    resized();

    loadApplicationState();
    setAppLanguage (languagePreferenceIndex);
    applyThemePreference (themePreferenceId);
    resized();
    repaint();

    juce::MessageManager::callAsync ([safeThis = juce::Component::SafePointer<MainComponent> (this)]
    {
        if (safeThis != nullptr)
            safeThis->finishDeferredStartup();
    });
}

void MainComponent::finishDeferredStartup()
{
    if (deferredStartupComplete)
        return;

    timeSliceThread.startThread (juce::Thread::Priority::high);
    showcontrol::waveform::waveformCacheDirectory().createDirectory();

    deviceManager.initialiseWithDefaultDevices (0, 2);
    {
        juce::AudioDeviceManager::AudioDeviceSetup setup;
        deviceManager.getAudioDeviceSetup (setup);

        bool changed = false;

        if (setup.bufferSize > 256)
        {
            setup.bufferSize = 256;
            changed = true;
        }

        juce::BigInteger desiredOutputs;
        desiredOutputs.setRange (0, 16, true);

        if (setup.outputChannels != desiredOutputs)
        {
            setup.outputChannels = desiredOutputs;
            changed = true;
        }

        if (changed)
        {
            const auto err = deviceManager.setAudioDeviceSetup (setup, true);

            if (err.isNotEmpty())
                ErrorHandler::log ("Audio setup info: " + err, ErrorHandler::Severity::Warning);
        }
    }

    for (int b = 0; b < showcontrol::routing::kInspectorBusCount; ++b)
        multiOutputCallback.setBusName (b, showcontrol::routing::getBusDisplayName (b));

    for (int b = showcontrol::routing::kInspectorBusCount; b < MultiOutputAudioCallback::kMaxBuses; ++b)
        multiOutputCallback.setBusName (b, "AUX " + juce::String (b));

    deviceManager.addAudioCallback (&multiOutputCallback);

    registerAllPadsWithMixer();
    forceAllPadsIdleAtStartup();

    deferredIdlePadsTimer = std::make_unique<OneShotApplicationTimer>();
    deferredIdlePadsTimer->onFire = [safeThis = juce::Component::SafePointer<MainComponent> (this)]
    {
        if (safeThis != nullptr)
            safeThis->forceAllPadsIdleAtStartup();
    };
    deferredIdlePadsTimer->startMs (150);

    inspectorPanel.setBusNames (multiOutputCallback.getAllBusNames());
    attachReadAheadToAllPads();
    refreshSidebarPlayingStatus();
    resized();
    repaint();
    startTimer (100);
    deferredStartupComplete = true;

    if (updateChecker != nullptr)
        updateChecker->checkForUpdatesAsync (false);

    restartBackupSync();
}

void MainComponent::triggerManualMusicIngestion()
{
    if (activeListIndex < 0 || activeListIndex >= allLists.size())
        return;

    mainFileChooser = std::make_unique<juce::FileChooser> (
        juce::String::fromUTF8 (u8"Nạp bài hát kịch bản..."),
        juce::File::getSpecialLocation (juce::File::userHomeDirectory),
        ShowAudioFormats::mediaFileChooserWildcard());

    mainFileChooser->launchAsync (ShowAudioFormats::fileChooserOpenMultipleFlags(),
    [this] (const juce::FileChooser& fc)
    {
        const auto results = fc.getResults();

        if (results.isEmpty())
            return;

        auto* current = allLists[activeListIndex];

        if (current == nullptr)
            return;

        juce::StringArray audioPaths;
        juce::StringArray videoPaths;

        for (const auto& file : results)
        {
            if (VideoAudioExtractor::isVideoFile (file))
                videoPaths.addIfNotAlreadyThere (file.getFullPathName());
            else if (ShowAudioFormats::isSupportedAudioFile (file))
                audioPaths.addIfNotAlreadyThere (file.getFullPathName());
        }

        const int totalValid = audioPaths.size() + videoPaths.size();

        if (totalValid == 0)
            return;

        if (current->isGrid)
        {
            performActiveListIngestWithUndo ([&] (ListData& list)
            {
                ingestDroppedFilesToActiveCuePads (list, audioPaths, videoPaths);
            });
        }
        else
        {
            performActiveListIngestWithUndo ([&] (ListData& list)
            {
                ingestDroppedFilesToActiveBgmList (list, audioPaths, videoPaths,
                                                   0, std::numeric_limits<int>::max());
            });
        }
    });
}

bool MainComponent::isInterestedInFileDrag (const juce::StringArray& files) { juce::ignoreUnused (files); return true; }
void MainComponent::fileDragEnter (const juce::StringArray& files, int x, int y) { juce::ignoreUnused (files, x, y); isCurrentlyDragging = true; repaint(); }
void MainComponent::fileDragExit (const juce::StringArray& files) { juce::ignoreUnused (files); isCurrentlyDragging = false; repaint(); }

juce::Rectangle<int> MainComponent::getCenterContentDropBounds() const
{
    const int splitW = 6;
    const int leftOffset = (sidebarVisible ? sidebarWidth + splitW : 0);
    const int rightOffset = (inspectorVisible ? inspectorWidth + splitW : 0);
    const int centerTop = masterDeckPanel.getBottom();

    return { leftOffset, centerTop,
             getWidth() - leftOffset - rightOffset,
             getHeight() - centerTop };
}

void MainComponent::finalizeAfterFileDropIngest()
{
    juce::StringArray listNames;
    for (int i = 0; i < allLists.size(); ++i)
        listNames.add (sidebarPanel.getListName (i));

    sidebarPanel.clearAllLists();
    for (int i = 0; i < allLists.size(); ++i)
    {
        sidebarPanel.addSet (listNames[i], allLists[i]->pads.size(), allLists[i]->isGrid,
                             allLists[i]->useCueListPanel, allLists[i]->isLocked, allLists[i]->themeColour);
    }
    sidebarPanel.setSelectedIndex (activeListIndex);

    if (emptyStatePanel != nullptr)
        emptyStatePanel->setVisible (false);

    saveApplicationState();
    refreshCueListPanel();
    rebuildDefaultHotkeysForList (activeListIndex);
    resized();
    repaint();

    if (isShowing())
        grabKeyboardFocus();
}

void MainComponent::ingestDroppedFilesToActiveCuePads (ListData& list,
                                                       const juce::StringArray& validAudioFiles,
                                                       const juce::StringArray& validVideoFiles)
{
    const int totalIncoming = validAudioFiles.size() + validVideoFiles.size();

    if (cueListCannotAcceptAnyFile (list.pads) && totalIncoming > 0)
    {
        showCueListCapacityAlert();
        return;
    }

    int ingestBudget = maxCueIngestSlots (list.pads);
    bool truncated = false;

    int fileIdx = 0;
    while (fileIdx < validAudioFiles.size() && ingestBudget > 0 && list.pads.size() < kMaxCuePadsPerList)
    {
        auto* p = createSoundPad();
        scrollContent->addChildComponent (p);
        p->setPadIndex (list.pads.size());
        assignNextFreeGridCell (list, p);
        applyProjectDefaultsToPad (p);
        p->configurePad (validAudioFiles[fileIdx], 1.0f, false);
        p->updateTheme (isDarkMode);
        p->setRenderMode (true);
        p->setCueListPlayback (true);
        p->setVisible (true);
        wireSoundPad (p);
        list.pads.add (p);
        ++fileIdx;
        --ingestBudget;
    }

    if (fileIdx < validAudioFiles.size())
        truncated = true;

    int videoIdx = 0;
    while (videoIdx < validVideoFiles.size() && ingestBudget > 0 && list.pads.size() < kMaxCuePadsPerList)
    {
        auto* p = createSoundPad();
        scrollContent->addChildComponent (p);
        p->setPadIndex (list.pads.size());
        assignNextFreeGridCell (list, p);
        applyProjectDefaultsToPad (p);
        p->updateTheme (isDarkMode);
        p->setRenderMode (true);
        p->setCueListPlayback (true);
        p->setVisible (true);
        wireSoundPad (p);
        list.pads.add (p);
        ingestVideoFileToPad (juce::File (validVideoFiles[videoIdx]), p);
        ++videoIdx;
        --ingestBudget;
    }

    if (videoIdx < validVideoFiles.size())
        truncated = true;

    if (truncated)
        showCueListCapacityAlert();

    syncCueMetadataFromPads (list);
}

void MainComponent::ingestDroppedFilesToActiveBgmList (ListData& list,
                                                       const juce::StringArray& validAudioFiles,
                                                       const juce::StringArray& validVideoFiles,
                                                       int dropLocalX,
                                                       int dropLocalY)
{
    int targetRowIdx = -1;

    for (int i = 0; i < list.pads.size(); ++i)
    {
        if (list.pads[i]->getBounds().contains (dropLocalX, dropLocalY))
        {
            targetRowIdx = i;
            break;
        }
    }

    int insertPos = (targetRowIdx != -1) ? targetRowIdx : list.pads.size();

    for (const auto& file : validAudioFiles)
    {
        auto* p = createSoundPad();
        scrollContent->addChildComponent (p);
        applyProjectDefaultsToPad (p);
        p->configurePad (file, 1.0f, false);
        p->updateTheme (isDarkMode);
        p->setRenderMode (false);
        p->setVisible (true);
        wireSoundPad (p);
        list.pads.insert (insertPos, p);
        ++insertPos;
    }

    for (const auto& videoPath : validVideoFiles)
    {
        auto* p = createSoundPad();
        scrollContent->addChildComponent (p);
        applyProjectDefaultsToPad (p);
        p->updateTheme (isDarkMode);
        p->setRenderMode (false);
        p->setVisible (true);
        wireSoundPad (p);
        list.pads.insert (insertPos, p);
        ingestVideoFileToPad (juce::File (videoPath), p);
        ++insertPos;
    }

    for (int i = 0; i < list.pads.size(); ++i)
        list.pads[i]->setPadIndex (i);
}

void MainComponent::filesDropped (const juce::StringArray& files, int x, int y)
{
    isCurrentlyDragging = false;

    const auto dropPoint = juce::Point<int> (x, y);

    // ── VÙNG 1: SIDEBAR — thư mục → Smart Folder (list mới theo tên folder) ──
    if (sidebarVisible && sidebarPanel.getBounds().contains (dropPoint))
    {
        juce::StringArray folderPaths;

        for (const auto& path : files)
        {
            const juce::File item (path);

            if (item.isDirectory())
                folderPaths.add (path);
        }

        if (! folderPaths.isEmpty())
        {
            const int sidebarLocalY = y - sidebarPanel.getY();
            const bool targetIsBgm = sidebarPanel.isSmartImportTargetBgm (sidebarLocalY);
            importListsFromDroppedFolders (folderPaths, targetIsBgm);
        }

        return;
    }

    juce::StringArray validAudioFiles;
    juce::StringArray validVideoFiles;
    ShowAudioFormats::collectAudioFilesFromDrop (files, validAudioFiles);
    VideoAudioExtractor::collectVideoFilesFromDrop (files, validVideoFiles);

    if (validAudioFiles.isEmpty() && validVideoFiles.isEmpty())
        return;

    const auto centerBounds = getCenterContentDropBounds();

    if (! centerBounds.contains (dropPoint))
        return;

    if (allLists.isEmpty() || activeListIndex < 0 || activeListIndex >= allLists.size())
        return;

    auto* current = allLists[activeListIndex];

    if (current == nullptr || current->isLocked)
        return;

    const int localX = x - viewScroller.getX() + viewScroller.getViewPositionX();
    const int localY = y - viewScroller.getY() + viewScroller.getViewPositionY();

    const bool cueListViewActive = isCueListViewActive();
    const bool inCueDropZone = current->isGrid
        && ((cueListViewActive && cueListPanel != nullptr && cueListPanel->getBounds().contains (dropPoint))
            || viewScroller.getBounds().contains (dropPoint));

    // ── VÙNG 2: BGM LIST — quét thư mục/file, nối tiếp vào danh sách BGM ──
    if (! current->isGrid && viewScroller.getBounds().contains (dropPoint))
    {
        performActiveListIngestWithUndo ([&, localX, localY] (ListData& list)
        {
            ingestDroppedFilesToActiveBgmList (list, validAudioFiles, validVideoFiles, localX, localY);
        });
        return;
    }

    // ── VÙNG 3: CUE (PAD grid hoặc Cue List) — đổ nhạc theo view đang hiển thị ──
    if (inCueDropZone)
    {
        performActiveListIngestWithUndo ([&] (ListData& list)
        {
            ingestDroppedFilesToActiveCuePads (list, validAudioFiles, validVideoFiles);
        });

        if (cueListViewActive && cueListPanel != nullptr)
            cueListPanel->grabKeyboardFocus();
    }
}

void MainComponent::paint (juce::Graphics& g)
{
    const auto cols = showcontrol::ui::ThemePaintColours::read (*this);
    g.fillAll (cols.windowBg);

    const int splitW = 6;
    const int leftOffset = (sidebarVisible ? sidebarWidth + splitW : 0);
    const int rightOffset = (inspectorVisible ? inspectorWidth + splitW : 0);
    const int centerTopY = kMacUnifiedTitleBarInset + 200;
    auto centerFrame = juce::Rectangle<int> (leftOffset, centerTopY,
                                             getWidth() - leftOffset - rightOffset,
                                             getHeight() - centerTopY);

    g.setColour (cols.centerBg);
    g.fillRect (centerFrame);
    g.setColour (cols.borderSubtle);
    g.drawRect (centerFrame, 1.0f);
}

void MainComponent::paintOverChildren (juce::Graphics& g)
{
    if (! crossCopyDropHighlightActive)
        return;

    const auto pal = ShowTheme::get (isDarkMode);
    const int splitW = 6;
    const int leftOffset = (sidebarVisible ? sidebarWidth + splitW : 0);
    const int rightOffset = (inspectorVisible ? inspectorWidth + splitW : 0);
    const int centerTopY = kMacUnifiedTitleBarInset + 200;
    auto centerFrame = juce::Rectangle<int> (leftOffset, centerTopY,
                                             getWidth() - leftOffset - rightOffset,
                                             getHeight() - centerTopY).toFloat().reduced (3.0f);

    showcontrol::crossdrag::paintNeonDropTargetGlow (g, centerFrame, pal.accent);
}

void MainComponent::setSidebarVisible (bool shouldShow)
{
    if (sidebarVisible == shouldShow)
        return;

    constexpr int animMs = 140;
    if (shouldShow)
    {
        sidebarVisible = true;
        sidebarPanel.setVisible (true);
        if (leftSplitter != nullptr) leftSplitter->setVisible (true);
        showSidebarBtn.setButtonText (juce::String::fromUTF8 (u8"◂"));
        resized(); // lấy target layout

        const auto targetSidebarBounds = sidebarPanel.getBounds();
        const auto targetSplitterBounds = (leftSplitter != nullptr ? leftSplitter->getBounds() : juce::Rectangle<int>());
        const auto targetBtnBounds = showSidebarBtn.getBounds();

        auto startSidebarBounds = targetSidebarBounds.withX (-targetSidebarBounds.getWidth() - 8);
        auto startSplitterBounds = targetSplitterBounds.withX (startSidebarBounds.getRight());
        auto startBtnBounds = targetBtnBounds.withX (startSplitterBounds.getX() - targetBtnBounds.getWidth() + 1);

        sidebarPanel.setBounds (startSidebarBounds);
        if (leftSplitter != nullptr) leftSplitter->setBounds (startSplitterBounds);
        showSidebarBtn.setBounds (startBtnBounds);

        layoutAnimator.animateComponent (&sidebarPanel, targetSidebarBounds, 1.0f, animMs, false, 1.0, 1.0);
        if (leftSplitter != nullptr)
            layoutAnimator.animateComponent (leftSplitter.get(), targetSplitterBounds, 1.0f, animMs, false, 1.0, 1.0);
        layoutAnimator.animateComponent (&showSidebarBtn, targetBtnBounds, 1.0f, animMs, false, 1.0, 1.0);
        return;
    }

    // Hide: khu giữa bung full ngay, panel trượt ngang sang trái
    const auto oldSidebarBounds = sidebarPanel.getBounds();
    const auto oldSplitterBounds = (leftSplitter != nullptr ? leftSplitter->getBounds() : juce::Rectangle<int>());
    const auto oldBtnBounds = showSidebarBtn.getBounds();

    sidebarVisible = false;
    showSidebarBtn.setButtonText (juce::String::fromUTF8 (u8"▸"));
    resized(); // áp layout mới cho center view và vị trí nút bung ra
    const auto targetBtnBounds = showSidebarBtn.getBounds();

    sidebarPanel.setVisible (true);
    if (leftSplitter != nullptr) leftSplitter->setVisible (true);
    sidebarPanel.setBounds (oldSidebarBounds);
    if (leftSplitter != nullptr) leftSplitter->setBounds (oldSplitterBounds);
    showSidebarBtn.setBounds (oldBtnBounds);

    const auto hideSidebarBounds = oldSidebarBounds.withX (-oldSidebarBounds.getWidth() - 8);
    const auto hideSplitterBounds = oldSplitterBounds.withX (hideSidebarBounds.getRight());

    layoutAnimator.animateComponent (&sidebarPanel, hideSidebarBounds, 1.0f, animMs, false, 1.0, 1.0);
    if (leftSplitter != nullptr)
        layoutAnimator.animateComponent (leftSplitter.get(), hideSplitterBounds, 1.0f, animMs, false, 1.0, 1.0);
    layoutAnimator.animateComponent (&showSidebarBtn, targetBtnBounds, 1.0f, animMs, false, 1.0, 1.0);

    juce::Timer::callAfterDelay (animMs + 10, [safe = juce::Component::SafePointer<MainComponent> (this)] {
        if (safe == nullptr) return;
        safe->sidebarPanel.setVisible (false);
        if (safe->leftSplitter != nullptr) safe->leftSplitter->setVisible (false);
    });
}

void MainComponent::setInspectorVisible (bool shouldShow)
{
    if (inspectorVisible == shouldShow)
        return;

    constexpr int animMs = 140;
    if (shouldShow)
    {
        inspectorVisible = true;
        inspectorPanel.setVisible (true);
        if (rightSplitter != nullptr) rightSplitter->setVisible (true);
        showInspectorBtn.setButtonText (juce::String::fromUTF8 (u8"▸"));
        resized(); // target

        const auto targetInspectorBounds = inspectorPanel.getBounds();
        const auto targetSplitterBounds = (rightSplitter != nullptr ? rightSplitter->getBounds() : juce::Rectangle<int>());
        const auto targetBtnBounds = showInspectorBtn.getBounds();

        auto startInspectorBounds = targetInspectorBounds.withX (getWidth() + 8);
        auto startSplitterBounds = targetSplitterBounds.withX (startInspectorBounds.getX() - targetSplitterBounds.getWidth());
        auto startBtnBounds = targetBtnBounds.withX (startSplitterBounds.getRight() - 1);

        inspectorPanel.setBounds (startInspectorBounds);
        if (rightSplitter != nullptr) rightSplitter->setBounds (startSplitterBounds);
        showInspectorBtn.setBounds (startBtnBounds);

        layoutAnimator.animateComponent (&inspectorPanel, targetInspectorBounds, 1.0f, animMs, false, 1.0, 1.0);
        if (rightSplitter != nullptr)
            layoutAnimator.animateComponent (rightSplitter.get(), targetSplitterBounds, 1.0f, animMs, false, 1.0, 1.0);
        layoutAnimator.animateComponent (&showInspectorBtn, targetBtnBounds, 1.0f, animMs, false, 1.0, 1.0);
        return;
    }

    // Hide: khu giữa bung full ngay, panel trượt ngang sang phải
    const auto oldInspectorBounds = inspectorPanel.getBounds();
    const auto oldSplitterBounds = (rightSplitter != nullptr ? rightSplitter->getBounds() : juce::Rectangle<int>());
    const auto oldBtnBounds = showInspectorBtn.getBounds();

    inspectorVisible = false;
    showInspectorBtn.setButtonText (juce::String::fromUTF8 (u8"◂"));
    resized(); // layout mới
    const auto targetBtnBounds = showInspectorBtn.getBounds();

    inspectorPanel.setVisible (true);
    if (rightSplitter != nullptr) rightSplitter->setVisible (true);
    inspectorPanel.setBounds (oldInspectorBounds);
    if (rightSplitter != nullptr) rightSplitter->setBounds (oldSplitterBounds);
    showInspectorBtn.setBounds (oldBtnBounds);

    const auto hideInspectorBounds = oldInspectorBounds.withX (getWidth() + 8);
    const auto hideSplitterBounds = oldSplitterBounds.withX (hideInspectorBounds.getX() - oldSplitterBounds.getWidth());

    layoutAnimator.animateComponent (&inspectorPanel, hideInspectorBounds, 1.0f, animMs, false, 1.0, 1.0);
    if (rightSplitter != nullptr)
        layoutAnimator.animateComponent (rightSplitter.get(), hideSplitterBounds, 1.0f, animMs, false, 1.0, 1.0);
    layoutAnimator.animateComponent (&showInspectorBtn, targetBtnBounds, 1.0f, animMs, false, 1.0, 1.0);

    juce::Timer::callAfterDelay (animMs + 10, [safe = juce::Component::SafePointer<MainComponent> (this)] {
        if (safe == nullptr) return;
        safe->inspectorPanel.setVisible (false);
        if (safe->rightSplitter != nullptr) safe->rightSplitter->setVisible (false);
    });
}

void MainComponent::mouseDown (const juce::MouseEvent& e)
{
    constexpr int kHeaderDragHeight = 45;

    if (e.y < kHeaderDragHeight && e.mods.isLeftButtonDown())
    {
        if (! showcontrol::mac::isMouseOverInteractiveDescendant (*this, e.getPosition()))
        {
            if (auto* topLevel = getTopLevelComponent())
            {
                if (auto* peer = topLevel->getPeer())
                {
                   #if JUCE_MAC
                    if (showcontrol::mac::startDraggingWindow (*peer, e))
                        return;
                   #endif

                    windowDragger.startDraggingComponent (topLevel, e.getEventRelativeTo (topLevel));
                    windowDragActive = true;
                    return;
                }
            }
        }
    }
}

void MainComponent::mouseDrag (const juce::MouseEvent& e)
{
    if (! windowDragActive)
        return;

    if (auto* topLevel = getTopLevelComponent())
        windowDragger.dragComponent (topLevel, e.getEventRelativeTo (topLevel), nullptr);
}

void MainComponent::mouseUp (const juce::MouseEvent& e)
{
    juce::ignoreUnused (e);
    windowDragActive = false;
}

void MainComponent::resized()
{
    constexpr int kSplitterW        = 6;
    constexpr int kTopDeckH         = 186;
    constexpr int kBottomBarH       = 36;
    constexpr int kPanelToggleBtnW  = 24;
    constexpr int kPanelToggleBtnH  = 22;

    // ── Layer 0: toàn bộ client area ─────────────────────────────────────────
    auto totalArea = getLocalBounds();

    if (kMacUnifiedTitleBarInset > 0)
        totalArea.removeFromTop (kMacUnifiedTitleBarInset);

    // ── Layer 1: dải đỉnh (master deck / timer / waveform) ───────────────────
    auto topArea = totalArea.removeFromTop (kTopDeckH).reduced (8, 4);
    masterDeckPanel.setBounds (topArea);
    busMixerPanel.setBounds ({});
    busMixerPanel.setVisible (false);

    // ── Layer 2: sidebar + inspector kịch sàn (y = đáy top deck → đáy cửa sổ) ─
    // Tuyệt đối KHÔNG removeFromBottom trên totalArea tại bước này.
    if (sidebarVisible && sidebarPanel.isVisible())
    {
        sidebarPanel.setBounds (totalArea.removeFromLeft (sidebarWidth));

        if (leftSplitter != nullptr)
            leftSplitter->setBounds (totalArea.removeFromLeft (kSplitterW));
    }
    else
    {
        sidebarPanel.setBounds ({});
        if (leftSplitter != nullptr)
            leftSplitter->setBounds ({});
    }

    if (inspectorVisible && inspectorPanel.isVisible())
    {
        inspectorPanel.setBounds (totalArea.removeFromRight (inspectorWidth));

        if (rightSplitter != nullptr)
            rightSplitter->setBounds (totalArea.removeFromRight (kSplitterW));
    }
    else
    {
        inspectorPanel.setBounds ({});
        if (rightSplitter != nullptr)
            rightSplitter->setBounds ({});
    }

    // Nút ẩn/hiện panel — overlay cố định theo splitter, không cuộn theo content.
    const int panelToggleY = getHeight() - 86;
    const int leftToggleX = sidebarVisible && leftSplitter != nullptr && ! leftSplitter->getBounds().isEmpty()
                              ? leftSplitter->getX() - kPanelToggleBtnW + 6
                              : 6; // sidebar ẩn: ghim sát mép trái cửa sổ (4–8px)
    const int rightToggleX = inspectorVisible && rightSplitter != nullptr && ! rightSplitter->getBounds().isEmpty()
                               ? rightSplitter->getRight() - 6
                               : getWidth() - kPanelToggleBtnW - 8;

    showSidebarBtn.setBounds (leftToggleX, panelToggleY, kPanelToggleBtnW, kPanelToggleBtnH);
    showInspectorBtn.setBounds (rightToggleX, panelToggleY, kPanelToggleBtnW, kPanelToggleBtnH);

    // ── Layer 3: phân khu trung tâm (phần còn lại sau 2 sườn) ────────────────
    auto centerZone = totalArea;

    const bool hasActiveList = ! allLists.isEmpty()
                               && activeListIndex >= 0
                               && activeListIndex < allLists.size()
                               && allLists[activeListIndex] != nullptr;

    if (! hasActiveList)
    {
        if (emptyStatePanel != nullptr)
        {
            emptyStatePanel->setBounds (centerZone);
            emptyStatePanel->setVisible (true);
            emptyStatePanel->toFront (false);
        }

        viewScroller.setBounds ({});
        viewScroller.setVisible (false);
        listHeaderComponent->setVisible (false);
        addMusicFloatingBtn.setBounds ({});

        if (playoutModeBar != nullptr)
            playoutModeBar->setVisible (false);

        if (cueListPanel != nullptr)
            cueListPanel->setVisible (false);

        showSidebarBtn.toFront (false);
        showInspectorBtn.toFront (false);
        return;
    }

    if (emptyStatePanel != nullptr)
        emptyStatePanel->setVisible (false);

    auto* current = allLists[activeListIndex];
    const bool isBgmListMode   = ! current->isGrid;
    const bool isCueGridMode   = current->isGrid;
    const bool padGridActive   = isCueGridMode && ! current->useCueListPanel;
    const bool cueListActive   = isCueGridMode && current->useCueListPanel && cueListPanel != nullptr;
    const bool hasLoadedAudio  = listHasLoadedAudio (*current);

    // ── Layer 3a: thanh đáy CHỈ trong centerZone (36px) ──────────────────────
    juce::Rectangle<int> bottomBarBounds;

    if (isCueGridMode)
        bottomBarBounds = centerZone.removeFromBottom (kBottomBarH);

    if (playoutModeBar != nullptr)
    {
        if (isCueGridMode)
        {
            playoutModeBar->setVisible (true);
            syncPlayoutModeBarFromActiveList();
            playoutModeBar->setBounds (bottomBarBounds);
            playoutModeBar->setViewMode (padGridActive);
        }
        else
        {
            playoutModeBar->setVisible (false);
        }
    }

    addMusicFloatingBtn.setBounds ({});

    // ── Layer 4: ruột trung tâm phía trên thanh đáy ──────────────────────────
    if (cueListActive)
    {
        listHeaderComponent->setVisible (false);
        viewScroller.setBounds ({});
        viewScroller.setVisible (false);
        viewScroller.setScrollBarsShown (false, false, false, false);

        cueListPanel->setBounds (centerZone);
        cueListPanel->setVisible (true);
        cueListPanel->toFront (false);
    }
    else
    {
        if (cueListPanel != nullptr)
            cueListPanel->setVisible (false);

        viewScroller.setVisible (true);

        if (isBgmListMode)
        {
            const bool showBgmHeader = current->pads.size() > 0;
            listHeaderComponent->setVisible (showBgmHeader);

            if (showBgmHeader)
            {
                listHeaderComponent->setBounds (centerZone.removeFromTop (showcontrol::bgmList::kPlaylistHeaderHeight));
                centerZone.removeFromTop (showcontrol::bgmList::kPlaylistHeaderGap);
                listHeaderComponent->toFront (false);
            }

            viewScroller.setBounds (centerZone);
            viewScroller.setScrollBarsShown (showBgmHeader, false, showBgmHeader, false);
            syncBgmListHeaderScrollbar();
        }
        else
        {
            listHeaderComponent->setVisible (false);
            viewScroller.setBounds (centerZone);
            viewScroller.setScrollBarsShown (false, false, false, false);
        }
    }

    if (playoutModeBar != nullptr && playoutModeBar->isVisible())
        playoutModeBar->toFront (false);

    layoutActiveListPads();
    updatePadReorderOverlayBounds();

    // Z-order: cụm nút ẩn/hiện panel sườn luôn nổi trên CueList / scroller / bottom bar.
    showSidebarBtn.toFront (false);
    showInspectorBtn.toFront (false);
}

bool MainComponent::trySwitchListByShortcut (const juce::KeyPress& key)
{
    const auto mods = key.getModifiers();

    if (! showcontrol::keyboard::playlistModifierTargetsGrid (mods)
        && ! showcontrol::keyboard::playlistModifierTargetsCueList (mods))
        return false;

    const bool targetIsGrid = showcontrol::keyboard::playlistModifierTargetsGrid (mods);
    const int hotkeyIndex = showcontrol::keyboard::hotkeyIndexForKeyPress (key);

    if (hotkeyIndex < 0)
        return false;

    int count = 0;
    for (int i = 0; i < allLists.size(); ++i)
    {
        const auto* list = allLists[i];
        if (list == nullptr || list->isGrid != targetIsGrid)
            continue;

        if (count == hotkeyIndex)
        {
            sidebarPanel.setSelectedIndex (i);
            loadList (i, sidebarPanel.getListTrackCount (i), list->isGrid);
            return true;
        }

        ++count;
    }

    return false;
}

bool MainComponent::isShowControlManagedHotkey (const juce::KeyPress& key) const noexcept
{
    const int code = showcontrol::keyboard::physicalKeyCode (key);

    if (HotkeyManager::isArrowNavigationKeyCode (code))
    {
        if (hotkeyManager.findByKeyPress (key) != nullptr)
            return true;

        if (hotkeyScopeMode == HotkeyScopeMode::global)
        {
            if (hotkeyManager.findByKeyPressPreferList (key, activeListIndex) != nullptr)
                return true;
        }
        else if (activeListIndex >= 0)
        {
            if (hotkeyManager.findByKeyPressInList (key, activeListIndex) != nullptr)
                return true;
        }

        return false;
    }

    if (HotkeyManager::isManagedApplicationKeyCode (code))
        return true;

    if (hotkeyManager.findByKeyPress (key) != nullptr)
        return true;

    if (hotkeyScopeMode == HotkeyScopeMode::global)
    {
        if (hotkeyManager.findByKeyPressPreferList (key, activeListIndex) != nullptr)
            return true;
    }
    else if (activeListIndex >= 0)
    {
        if (hotkeyManager.findByKeyPressInList (key, activeListIndex) != nullptr)
            return true;
    }

    return false;
}

void MainComponent::routePhysicalHotkeyFromKeyCode (int keyCode)
{
    const juce::uint32 nowMs = juce::Time::getMillisecondCounter();

    if (isSpacebarKey (juce::KeyPress (keyCode)))
    {
        executeSpacebarTransportKey (juce::KeyPress (keyCode));
        return;
    }

    if (keyCode == juce::KeyPress::escapeKey)
    {
        triggerGlobalPanicFadeAll();
        return;
    }

    if (HotkeyManager::isArrowNavigationKeyCode (keyCode))
    {
        const juce::KeyPress key (keyCode, juce::ModifierKeys::getCurrentModifiers(), 0);
        const auto normalized = showcontrol::keyboard::normalizedForHotkeyMatch (key);

        if (! matchesHotkeyBindingForKey (normalized))
            handleArrowNavigationKey (key);

        return;
    }

    if (activeListIndex >= 0 && activeListIndex < allLists.size())
    {
        if (auto* currentList = getActiveListSafe())
        {
            if (keyCode == juce::KeyPress::returnKey)
            {
                if (currentList->isGrid)
                    triggerCueGo (selectedBgmIndex);

                return;
            }

            if (activeList == 1 && cueListPanel != nullptr && cueListPanel->isVisible())
            {
                if (keyCode == (int) 'P' || keyCode == (int) 'p')
                {
                    cueListPanel->handleTransportKey (juce::KeyPress (keyCode));
                    return;
                }

                if (keyCode == (int) 'S' || keyCode == (int) 's')
                {
                    cueListPanel->handleTransportKey (juce::KeyPress (keyCode));
                    return;
                }
            }
            else if (keyCode == (int) 'S' || keyCode == (int) 's')
            {
                for (auto* pad : currentList->pads)
                    if (pad != nullptr)
                        pad->triggerStop();

                return;
            }

            if (! currentList->isGrid && (keyCode == (int) 'N' || keyCode == (int) 'n'))
            {
                triggerBgmNext();
                return;
            }
        }
    }

    tryTriggerPadByPhysicalKeyCode (keyCode, nowMs);
}

bool MainComponent::triggerPadByKeyCode (int keyCode, juce::ModifierKeys modifiers)
{
    if (isPlaybackCommandBlocked())
        return false;

    if (! juce::MessageManager::getInstance()->isThisTheMessageThread())
        return false;

    if (showcontrol::keyboard::isKeyboardFocusInTextInput())
        return false;

    const bool optionOnly = modifiers.isAltDown()
                         && ! modifiers.isCommandDown()
                         && ! modifiers.isCtrlDown();
    const bool plainMatrix = ! modifiers.isCommandDown()
                          && ! modifiers.isCtrlDown()
                          && ! modifiers.isAltDown();

    if (! plainMatrix && ! optionOnly)
        return false;

    if (! showcontrol::padgrid::isMatrixPhysicalKeyCode (keyCode))
        return false;

    auto* list = getActiveListSafe();
    if (list == nullptr || ! list->isGrid)
        return false;

    const auto cell = showcontrol::padgrid::gridCellForPhysicalKey (keyCode, optionOnly);

    if (! showcontrol::padgrid::isValidGridCell (cell.y, cell.x))
        return false;

    SoundPad* pad = nullptr;

    if (auto* panel = getPadPanel())
        pad = panel->getPadAtGrid (cell.y, cell.x);

    if (pad == nullptr)
    {
        for (auto* p : list->pads)
        {
            if (p == nullptr || ! p->occupiesCueGridSlot())
                continue;

            if (p->getGridRow() == cell.y && p->getGridCol() == cell.x)
            {
                pad = p;
                break;
            }
        }
    }

    if (pad == nullptr || ! pad->hasAudioFile())
        return false;

    const int padIndex = list->pads.indexOf (pad);
    if (padIndex < 0)
        return false;

    return triggerCueGo (padIndex);
}

bool MainComponent::triggerPadByKeyCode (const juce::KeyPress& key)
{
    const int telexResolved = showcontrol::keyboard::resolveTelexAwareTopRowKeyCode (key);
    const int keyCode = showcontrol::keyboard::physicalKeyCode (key);
    const int triggerCode = telexResolved != 0 ? telexResolved : keyCode;
    return triggerPadByKeyCode (triggerCode, key.getModifiers());
}

bool MainComponent::tryTriggerPadByTelexAwareKeyPress (const juce::KeyPress& key, juce::uint32 nowMs)
{
    const int resolved = showcontrol::keyboard::resolveTelexAwareTopRowKeyCode (key);

    if (resolved == 0)
        return false;

    tryTriggerPadByPhysicalKeyCode (resolved, nowMs);
    return true;
}

bool MainComponent::tryTriggerPadByPhysicalKeyCode (int keyCode, juce::uint32 nowMs)
{
    const bool swallowMatrixKey = HotkeyManager::isManagedApplicationKeyCode (keyCode)
                               || showcontrol::keyboard::isFarragoTopRowMatrixKeyCode (keyCode);

    if (showcontrol::keyboard::shouldBlockPlaybackHotkey())
        return swallowMatrixKey;

    if (HotkeyManager::isArrowNavigationKeyCode (keyCode))
        return false;

    if (activeList == 1 && cueListPanel != nullptr && cueListPanel->isVisible())
    {
        if (keyCode == (int) 'P' || keyCode == (int) 'p'
            || keyCode == (int) 'S' || keyCode == (int) 's')
            return true;
    }

    if (! swallowMatrixKey)
        return false;

    const juce::KeyPress key (keyCode, juce::ModifierKeys::getCurrentModifiers(), 0);
    const auto normalized = showcontrol::keyboard::normalizedForHotkeyMatch (key);

    const auto gate = hotkeyManager.evaluateKeyPressGate (keyCode, nowMs);

    if (gate.isDuplicate)
    {
        logHotkeyTrace ("swallow duplicate keyCode=" + juce::String (keyCode));
        return true;
    }

    if (! gate.shouldExecute)
        return true;

    logHotkeyTrace ("physical edge keyCode=" + juce::String (keyCode)
                    + " activeList=" + juce::String (activeListIndex));

    if (hotkeyScopeMode == HotkeyScopeMode::global)
    {
        if (const auto* binding = hotkeyManager.findByKeyPressPreferList (normalized, activeListIndex))
        {
            if (triggerPadFromHotkey (*binding))
            {
                lastHotkeyKeyCode   = keyCode;
                lastHotkeyTriggerMs = nowMs;
                logHotkeyTrace ("trigger success");
            }

            return true;
        }
    }
    else if (activeListIndex >= 0 && activeListIndex < allLists.size())
    {
        if (const auto* binding = hotkeyManager.findByKeyPressInList (normalized, activeListIndex))
        {
            if (triggerPadFromHotkey (*binding))
            {
                lastHotkeyKeyCode   = keyCode;
                lastHotkeyTriggerMs = nowMs;
                logHotkeyTrace ("trigger success");
            }

            return true;
        }
    }

    logHotkeyTrace ("no binding match keyCode=" + juce::String (keyCode));
    return true;
}

bool MainComponent::matchesHotkeyBindingForKey (const juce::KeyPress& key) const noexcept
{
    const auto normalized = showcontrol::keyboard::normalizedForHotkeyMatch (key);

    if (hotkeyScopeMode == HotkeyScopeMode::global)
        return hotkeyManager.findByKeyPressPreferList (normalized, activeListIndex) != nullptr;

    if (activeListIndex >= 0)
        return hotkeyManager.findByKeyPressInList (normalized, activeListIndex) != nullptr;

    return false;
}

bool MainComponent::handleArrowNavigationKey (const juce::KeyPress& key)
{
    auto* currentList = getActiveListSafe();

    if (currentList == nullptr || currentList->pads.size() == 0)
        return false;

    if (currentList->isGrid)
    {
        if (auto* panel = getPadPanel())
        {
            if (panel->keyPressed (key))
                return true;
        }

        return false;
    }

    // Nếu selection đang rỗng/ngoài range (vừa chuyển list), kéo về index hợp lệ
    if (! juce::isPositiveAndBelow (selectedBgmIndex, currentList->pads.size()))
        selectedBgmIndex = 0;

    const int arrowCode = HotkeyManager::normalizeArrowKeyCode (key.getKeyCode());

    if (arrowCode == juce::KeyPress::upKey || arrowCode == juce::KeyPress::leftKey)
        selectedBgmIndex = juce::jlimit (0, currentList->pads.size() - 1, selectedBgmIndex - 1);
    else if (arrowCode == juce::KeyPress::downKey || arrowCode == juce::KeyPress::rightKey)
        selectedBgmIndex = juce::jlimit (0, currentList->pads.size() - 1, selectedBgmIndex + 1);
    else
        return false;

    if (selectedBgmIndex < 0 || selectedBgmIndex >= currentList->pads.size())
        return false;

    if (auto* targetPad = currentList->pads[selectedBgmIndex])
    {
        inspectorPanel.selectPad (targetPad);
        selectedPadIndices.clear();
        selectedPadIndices.add (selectedBgmIndex);
        applyPadSelectionVisualState();

        if (! currentList->isGrid)
        {
            int vHeight = viewScroller.getViewHeight();

            if (vHeight > 0)
            {
                const int padTop = targetPad->getY();
                const int padBottom = targetPad->getBottom();
                const int viewTop = viewScroller.getViewPositionY();
                const int viewBottom = viewTop + vHeight;
                constexpr int padding = 20;

                if (padTop < viewTop + padding)
                    viewScroller.setViewPosition (viewScroller.getViewPositionX(), std::max (0, padTop - padding));
                else if (padBottom > viewBottom - padding)
                    viewScroller.setViewPosition (viewScroller.getViewPositionX(), padBottom - vHeight + padding);
            }
        }

        const int deferredIdx = selectedBgmIndex;
        const bool autoArm = currentList->autoArmOnSelect;
        juce::Component::SafePointer<MainComponent> safeThis (this);

        juce::MessageManager::callAsync ([safeThis, deferredIdx, autoArm]()
        {
            if (safeThis == nullptr)
                return;

            safeThis->prefetchBgmPadAtIndex (deferredIdx);

            if (safeThis->activeListIndex < 0 || safeThis->activeListIndex >= safeThis->allLists.size())
                return;

            auto* list = safeThis->allLists[safeThis->activeListIndex];

            if (list == nullptr || deferredIdx < 0 || deferredIdx >= list->pads.size())
                return;

            if (auto* pad = list->pads[deferredIdx])
            {
                if (autoArm)
                    safeThis->armPad (pad);
                else
                    pad->prepareForInstantPlay();
            }
        });

        if (cueListPanel != nullptr)
            cueListPanel->setSelectedIndex (selectedBgmIndex);
    }

    return true;
}

bool MainComponent::isSpacebarKey (const juce::KeyPress& key) noexcept
{
    const int code = showcontrol::keyboard::physicalKeyCode (key);
    return code == juce::KeyPress::spaceKey || code == 32;
}

bool MainComponent::executeSpacebarTransportKey (const juce::KeyPress& key)
{
    if (! isSpacebarKey (key))
        return false;

    if (swallowLikelyOsKeyRepeat (key))
        return true;

    const juce::uint32 nowMs = juce::Time::getMillisecondCounter();

    const juce::uint32 quietUntil = g_gateQuietUntilMs.load (std::memory_order_acquire);
    if (quietUntil != 0 && nowMs < quietUntil)
    {
        logHotkeyTrace ("swallow duplicate keyCode=32 (GLOBAL)");
        return true;
    }

    const juce::uint32 previousMs = g_lastGlobalSpacebarPressTime.exchange (nowMs, std::memory_order_acq_rel);
    if (previousMs != 0)
    {
        const juce::uint32 delta = nowMs - previousMs;
        if (delta < kGlobalSpacebarDebounceMs)
        {
            g_gateQuietUntilMs.store (nowMs + kGlobalSpacebarDebounceMs, std::memory_order_release);
            logHotkeyTrace ("swallow duplicate keyCode=32 (GLOBAL)");
            return true;
        }
    }

    g_gateQuietUntilMs.store (nowMs + kGlobalSpacebarDebounceMs, std::memory_order_release);

    if (isPlaybackCommandBlocked() || nowMs < startupInputGuardUntilMs)
    {
        logHotkeyTrace ("ignore startup keyCode=32");
        return true;
    }

    if (allLists.isEmpty())
        return true;

    if (activeList == 1 && cueListPanel != nullptr && cueListPanel->isVisible())
    {
        if (cueListPanel->handleTransportKey (key))
        {
            lastHotkeyKeyCode   = key.getKeyCode();
            lastHotkeyTriggerMs = nowMs;
        }

        return true;
    }

    logHotkeyTrace ("keyCode=" + juce::String (key.getKeyCode())
                    + " listIdx=" + juce::String (activeListIndex)
                    + " route=" + juce::String (activeList));

    if (activeListIndex >= 0 && activeListIndex < allLists.size())
    {
        if (auto* list = getActiveListSafe())
        {
            if (list->isGrid)
            {
                if (selectedBgmIndex >= 0 && selectedBgmIndex < list->pads.size())
                    triggerCueGo (selectedBgmIndex);
            }
            else
            {
                triggerBgmPlayPause();
            }
        }
    }

    lastHotkeyKeyCode   = key.getKeyCode();
    lastHotkeyTriggerMs = nowMs;
    return true;
}

void MainComponent::triggerGlobalPanicFadeAll()
{
    if (! juce::MessageManager::getInstance()->isThisTheMessageThread())
    {
        juce::Component::SafePointer<MainComponent> safeThis (this);
        juce::MessageManager::callAsync ([safeThis]
        {
            if (safeThis != nullptr)
                safeThis->triggerGlobalPanicFadeAll();
        });
        return;
    }

    if (shouldBlockLocalPlaybackCommand())
        return;

    const juce::uint32 nowMs = juce::Time::getMillisecondCounter();

    if (nowMs - lastPanicFadeRequestMs < 120)
        return;

    lastPanicFadeRequestMs = nowMs;

    if (panicFadeDispatchScheduled.exchange (true, std::memory_order_acq_rel))
        return;

    juce::Component::SafePointer<MainComponent> safeThis (this);
    juce::MessageManager::callAsync ([safeThis]
    {
        if (safeThis == nullptr)
            return;

        safeThis->panicFadeDispatchScheduled.store (false, std::memory_order_release);
        safeThis->executePanicFadeAllLocked();
    });
}

void MainComponent::executePanicFadeAllLocked()
{
    static constexpr double kPanicFadeMs = 1500.0;

    if (! syncApplying.load())
    {
        broadcastSyncIfPrimary ([] (showcontrol::backup::ShowBackupSyncBroadcaster& b)
        {
            b.sendPanic();
        });
    }

    if (! juce::MessageManager::getInstance()->isThisTheMessageThread())
    {
        juce::Component::SafePointer<MainComponent> safeThis (this);
        juce::MessageManager::callAsync ([safeThis]
        {
            if (safeThis != nullptr)
                safeThis->executePanicFadeAllLocked();
        });
        return;
    }

    juce::Array<juce::Component::SafePointer<SoundPad>> fadeTargets;
    fadeTargets.ensureStorageAllocated (32);

    {
        const juce::ScopedLock audioDeviceLock (deviceManager.getAudioCallbackLock());

        for (auto* list : allLists)
        {
            if (list == nullptr)
                continue;

            for (auto* pad : list->pads)
            {
                if (pad == nullptr)
                    continue;

                if (! pad->isTransportActive())
                    continue;

                if (pad->isFadeOutInProgress())
                    continue;

                fadeTargets.add (pad);
            }
        }
    }

    int fadedCount = 0;

    for (auto& safePad : fadeTargets)
    {
        if (auto* pad = safePad.getComponent())
        {
            pad->startFadeOut (kPanicFadeMs, true);
            ++fadedCount;
        }
    }

    juce::Component::SafePointer<MainComponent> safeThis (this);
    juce::MessageManager::callAsync ([safeThis, fadedCount]
    {
        if (safeThis != nullptr)
            safeThis->applyPanicFadeUiAftermath (fadedCount);
    });
}

void MainComponent::applyPanicFadeUiAftermath (int fadedCount)
{
    if (! juce::MessageManager::getInstance()->isThisTheMessageThread())
        return;

    refreshSidebarPlayingStatus();
    updateCuePlaybackIndicators();
    pushStageMonitorUpdate();
    logHotkeyTrace ("panic fade all pads=" + juce::String (fadedCount));

    if (! isInitialLoading.load (std::memory_order_relaxed))
        saveApplicationState();

    repaint();
}

void MainComponent::reloadPadWaveformFromConfig (SoundPad* pad)
{
    if (pad == nullptr)
        return;

    const auto path = pad->getFilePath();

    if (path.isEmpty())
        return;

    const juce::File trackFile (path);

    if (! trackFile.existsAsFile())
        return;

    pad->setThumbnailLoadAllowed (true);
    pad->reloadWaveformThumbnail();
    pad->repaint();
}

void MainComponent::reloadAllPadWaveformsFromConfig()
{
    for (auto* list : allLists)
    {
        if (list == nullptr)
            continue;

        for (auto* pad : list->pads)
            reloadPadWaveformFromConfig (pad);
    }
}

void MainComponent::reloadAllPadWaveformsStaggered (int batchSize, int batchDelayMs)
{
    juce::Array<SoundPad*> pads;
    pads.ensureStorageAllocated (64);

    for (auto* list : allLists)
    {
        if (list == nullptr)
            continue;

        for (auto* pad : list->pads)
            if (pad != nullptr)
                pads.add (pad);
    }

    if (pads.isEmpty())
        return;

    batchSize  = juce::jmax (1, batchSize);
    batchDelayMs = juce::jmax (0, batchDelayMs);

    struct BatchState
    {
        juce::Component::SafePointer<MainComponent> owner;
        juce::Array<SoundPad*> pads;
        int nextIndex = 0;
        int batchSize = 6;
        int batchDelayMs = 16;
    };

    auto state = std::make_shared<BatchState>();
    state->owner = this;
    state->pads = std::move (pads);
    state->batchSize = batchSize;
    state->batchDelayMs = batchDelayMs;

    std::function<void()> processBatch;
    processBatch = [state, processBatch]()
    {
        if (state->owner == nullptr)
            return;

        const int end = juce::jmin (state->nextIndex + state->batchSize, state->pads.size());

        for (int i = state->nextIndex; i < end; ++i)
            state->owner->reloadPadWaveformFromConfig (state->pads.getReference (i));

        state->nextIndex = end;

        if (state->nextIndex < state->pads.size())
            juce::Timer::callAfterDelay (state->batchDelayMs, processBatch);
    };

    processBatch();
}

void MainComponent::refreshStartupPlaylistDisplay()
{
    for (auto* list : allLists)
    {
        if (list == nullptr)
            continue;

        if (list->isGrid)
            syncCueMetadataFromPads (*list);

        for (auto* pad : list->pads)
            if (pad != nullptr)
                pad->repaint();
    }

    refreshCueListPanel();

    if (cueListPanel != nullptr)
    {
        cueListPanel->refreshListBoxData();
        cueListPanel->repaint();
    }

    layoutActiveListPads();
    sidebarPanel.repaint();
    repaint();
}

void MainComponent::finalizeStartupPlaylistUi()
{
    if (activeListIndex >= 0 && activeListIndex < allLists.size())
    {
        auto* list = allLists[activeListIndex];
        if (list != nullptr)
            loadList (activeListIndex, list->pads.size(), list->isGrid);
    }
    else
    {
        loadList (-1, 0, true);
    }

    reloadAllPadWaveformsStaggered();
    layoutActiveListPads();
    refreshSidebarPlayingStatus();
    updateCuePlaybackIndicators();

    if (cueListPanel != nullptr)
    {
        cueListPanel->refreshListBoxData();
        cueListPanel->repaint();
    }

    sidebarPanel.repaint();
    resized();
    repaint();
}

bool MainComponent::loadPlaylistFromJson (const juce::var& playlistVar)
{
    if (! playlistVar.isArray())
        return false;

    const auto* playlistArray = playlistVar.getArray();
    if (playlistArray == nullptr || playlistArray->isEmpty())
        return false;

    resetPlaybackDisplayCaches();
    allLists.clear();
    sidebarPanel.clearAllLists();

    for (const auto& listVar : *playlistArray)
    {
        if (! listVar.isObject())
            continue;

        auto* newList = new ListData();
        newList->isGrid = static_cast<bool> (listVar.getProperty ("isGrid", true));
        const auto listName = listVar.getProperty ("name",
            newList->isGrid ? showcontrol::localization::defaultCueListName()
                            : showcontrol::localization::defaultBgmListName()).toString();
        const auto themeHex = listVar.getProperty ("themeColour", {}).toString().trim();
        if (themeHex.isNotEmpty())
            newList->themeColour = showcontrol::colours::snapToPalette (
                showcontrol::colours::colourFromHexString (themeHex));
        const auto tracks   = listVar.getProperty ("tracks", juce::var());

        if (tracks.isArray())
        {
            int padIdx = 0;

            for (const auto& trackVar : *tracks.getArray())
            {
                juce::String filePath;
                juce::String displayTitle;
                juce::String displayFormat;

                if (trackVar.isObject())
                {
                    filePath      = trackVar.getProperty ("file", {}).toString().trim();
                    displayTitle  = trackVar.getProperty ("title", {}).toString().trim();
                    displayFormat = trackVar.getProperty ("format", {}).toString().trim();
                }
                else
                {
                    filePath = trackVar.toString().trim();
                }

                if (filePath.isEmpty())
                    continue;

                auto* p = createSoundPad();
                scrollContent->addChildComponent (p);
                p->setPadIndex (padIdx);

                if (displayTitle.isNotEmpty())
                    p->applyInstantDisplayTitle (displayTitle);

                if (displayFormat.isNotEmpty())
                    p->applyInstantDisplayFormat (displayFormat);

                p->configurePad (filePath, 1.0f, newList->isGrid ? newList->isLooping : false);
                p->updateTheme (isDarkMode);
                p->setRenderMode (newList->isGrid);

                if (newList->isGrid)
                    p->setLooping (newList->isLooping);

                if (newList->isGrid && trackVar.isObject())
                {
                    if (auto* trackObj = trackVar.getDynamicObject())
                    {
                        if (trackObj->hasProperty ("gridRow") && trackObj->hasProperty ("gridCol"))
                        {
                            p->setGridPosition ((int) trackObj->getProperty ("gridRow"),
                                                (int) trackObj->getProperty ("gridCol"));
                        }
                    }
                }

                newList->pads.add (p);
                ++padIdx;
            }
        }

        allLists.add (newList);
        sidebarPanel.addSet (listName, newList->pads.size(), newList->isGrid,
                             newList->useCueListPanel, newList->isLocked, newList->themeColour);
        sidebarPanel.setListLooping (allLists.size() - 1, newList->isLooping);
    }

    return ! allLists.isEmpty();
}

juce::var MainComponent::buildPlaylistJson() const
{
    juce::Array<juce::var> playlistLists;

    for (int i = 0; i < allLists.size(); ++i)
    {
        auto* list = allLists[i];
        if (list == nullptr)
            continue;

        juce::Array<juce::var> tracks;

        for (auto* pad : list->pads)
        {
            if (pad == nullptr)
                continue;

            const auto path = pad->getConfiguredFilePath();

            if (path.isEmpty())
                continue;

            const auto title = pad->getPadName();

            juce::DynamicObject::Ptr trackObj (new juce::DynamicObject());
            trackObj->setProperty ("file", path);

            if (title.isNotEmpty() && title != juce::String::fromUTF8 (u8"Trống"))
                trackObj->setProperty ("title", title);

            const auto formatInfo = pad->getCachedFormatInfoString();

            if (formatInfo.isNotEmpty())
                trackObj->setProperty ("format", formatInfo);

            if (list->isGrid && pad->occupiesCueGridSlot())
            {
                trackObj->setProperty ("gridRow", pad->getGridRow());
                trackObj->setProperty ("gridCol", pad->getGridCol());
            }

            tracks.add (juce::var (trackObj.get()));
        }

        juce::DynamicObject::Ptr listObj (new juce::DynamicObject());
        listObj->setProperty ("name", sidebarPanel.getListName (i));
        listObj->setProperty ("isGrid", list->isGrid);
        if (! showcontrol::colours::isDefaultTagColour (list->themeColour))
            listObj->setProperty ("themeColour", showcontrol::colours::colourToHexString (list->themeColour));
        listObj->setProperty ("tracks", tracks);
        playlistLists.add (juce::var (listObj.get()));
    }

    return playlistLists;
}

bool MainComponent::keyPressed (const juce::KeyPress& key)
{
    if (sidebarPanel.isSearchBarFocused() || isSearchWindowFocused())
        return false;

    if (showcontrol::keyboard::isUndoKeyPress (key))
        return performApplicationUndo();

    if (showcontrol::keyboard::isRedoKeyPress (key))
        return performApplicationRedo();

    if (auto* focusedComponent = juce::Component::getCurrentlyFocusedComponent())
    {
        if (dynamic_cast<juce::TextEditor*> (focusedComponent) != nullptr
            || dynamic_cast<juce::ComboBox*> (focusedComponent) != nullptr)
            return false;
    }

    const int keyCode = showcontrol::keyboard::physicalKeyCode (key);
    const int telexResolved = showcontrol::keyboard::resolveTelexAwareTopRowKeyCode (key);
    const int triggerCode = telexResolved != 0 ? telexResolved : keyCode;
    const bool matrixKey = showcontrol::padgrid::isMatrixPhysicalKeyCode (triggerCode);
    const bool optionMatrixKey = matrixKey
                              && key.getModifiers().isAltDown()
                              && ! key.getModifiers().isCommandDown()
                              && ! key.getModifiers().isCtrlDown();
    const bool globalSwallowHotkey = telexResolved != 0
                                  || showcontrol::keyboard::isFarragoTopRowMatrixKeyCode (keyCode)
                                  || matrixKey
                                  || optionMatrixKey;

    if (globalSwallowHotkey
        && juce::Component::getCurrentlyModalComponent() == nullptr)
    {
        handleApplicationHotkey (key);
        return true;
    }

    if (handleApplicationHotkey (key))
        return true;

    if (showcontrol::keyboard::isFarragoTopRowMatrixKeyCode (keyCode)
        || telexResolved != 0
        || HotkeyManager::isManagedApplicationKeyCode (keyCode))
        return true;

    return false;
}

bool MainComponent::handleApplicationHotkey (const juce::KeyPress& key)
{
    if (! juce::MessageManager::getInstance()->isThisTheMessageThread())
        return false;

    if (juce::Component::getCurrentlyModalComponent() != nullptr)
        return false;

    const int keyCode = showcontrol::keyboard::physicalKeyCode (key);

    if ((keyCode == juce::KeyPress::deleteKey || keyCode == juce::KeyPress::backspaceKey)
        && key.getModifiers().isCommandDown())
        return tryHandleDeleteOrBackspaceKey (key);

    const juce::uint32 nowMs = juce::Time::getMillisecondCounter();
    const int telexResolved = showcontrol::keyboard::resolveTelexAwareTopRowKeyCode (key);
    const int triggerCode = telexResolved != 0 ? telexResolved : keyCode;
    const bool matrixKey = showcontrol::padgrid::isMatrixPhysicalKeyCode (triggerCode);
    const bool optionOnly = key.getModifiers().isAltDown()
                         && ! key.getModifiers().isCommandDown()
                         && ! key.getModifiers().isCtrlDown();
    const bool plainMatrix = matrixKey
                          && ! key.getModifiers().isCommandDown()
                          && ! key.getModifiers().isCtrlDown()
                          && ! key.getModifiers().isAltDown();

    if (matrixKey && (plainMatrix || optionOnly))
    {
        if (triggerPadByKeyCode (key))
        {
            lastHotkeyKeyCode   = triggerCode;
            lastHotkeyTriggerMs = nowMs;
        }

        return true;
    }

    if (key.getModifiers().isCommandDown()
        || key.getModifiers().isCtrlDown())
    {
        if (trySwitchListByShortcut (key))
            return true;

        return false;
    }

    if (key.getModifiers().isAltDown())
    {
        if (trySwitchListByShortcut (key))
            return true;

        return false;
    }

    const bool topRowMatrixKey = telexResolved != 0
                              || showcontrol::keyboard::isFarragoTopRowMatrixKeyCode (keyCode);
    const bool swallowMatrixKey = topRowMatrixKey
                               || HotkeyManager::isManagedApplicationKeyCode (keyCode);

    const bool canTriggerPlayback = ! isPlaybackCommandBlocked()
                                 && nowMs >= startupInputGuardUntilMs
                                 && ! allLists.isEmpty();

    if (topRowMatrixKey)
    {
        const int triggerCode = telexResolved != 0 ? telexResolved : keyCode;
        tryTriggerPadByPhysicalKeyCode (triggerCode, nowMs);
        return true;
    }

    if (! canTriggerPlayback)
        return swallowMatrixKey;

    if (isSpacebarKey (key)
        || keyCode == juce::KeyPress::escapeKey
        || keyCode == juce::KeyPress::returnKey
        || HotkeyManager::isArrowNavigationKeyCode (keyCode)
        || HotkeyManager::isManagedApplicationKeyCode (keyCode)
        || keyCode == (int) 'P' || keyCode == (int) 'p'
        || keyCode == (int) 'S' || keyCode == (int) 's'
        || keyCode == (int) 'N' || keyCode == (int) 'n')
    {
        routePhysicalHotkeyFromKeyCode (keyCode);
        return true;
    }

    return swallowMatrixKey;
}

void MainComponent::saveActiveListSelection()
{
    if (! juce::isPositiveAndBelow (activeListIndex, allLists.size()))
        return;

    if (auto* list = allLists[activeListIndex])
    {
        list->savedPadSelection = selectedPadIndices;
        list->savedPrimaryPadIndex = selectedBgmIndex;
    }
}

void MainComponent::restoreListSelection (ListData& list)
{
    selectedPadIndices.clear();
    selectedBgmIndex = -1;

    for (auto idx : list.savedPadSelection)
    {
        if (juce::isPositiveAndBelow (idx, list.pads.size()))
            selectedPadIndices.addIfNotAlreadyThere (idx);
    }

    if (! selectedPadIndices.isEmpty())
    {
        if (juce::isPositiveAndBelow (list.savedPrimaryPadIndex, list.pads.size())
            && selectedPadIndices.contains (list.savedPrimaryPadIndex))
        {
            selectedBgmIndex = list.savedPrimaryPadIndex;
        }
        else
        {
            selectedBgmIndex = selectedPadIndices.getFirst();
        }

        return;
    }

    if (list.pads.size() > 0 && listHasLoadedAudio (list))
    {
        for (int i = 0; i < list.pads.size(); ++i)
        {
            if (auto* p = list.pads[i]; p != nullptr && p->hasAudioFile())
            {
                selectedPadIndices.add (i);
                selectedBgmIndex = i;
                break;
            }
        }
    }
}

void MainComponent::loadList (int listIndex, int trackCount, bool isGridHint)
{
    juce::ignoreUnused (isGridHint);

    if (allLists.isEmpty() || listIndex < 0 || listIndex >= allLists.size())
    {
        enterEmptyProjectState();
        return;
    }

    if (soloPad != nullptr)
        setSoloPad (nullptr, false);

    saveActiveListSelection();

    if (activeListIndex >= 0 && activeListIndex < allLists.size() && allLists[activeListIndex] != nullptr)
    {
        for (auto* p : allLists[activeListIndex]->pads)
            if (p != nullptr)
                p->setVisible (false);
    }

    if (cueListPanel != nullptr)
        cueListPanel->setVisible (false);

    if (emptyStatePanel != nullptr)
        emptyStatePanel->setVisible (false);

    viewScroller.setVisible (true);

    activeListIndex = listIndex;

    auto* target = allLists[listIndex];

    if (target == nullptr)
    {
        enterEmptyProjectState();
        return;
    }

    // Phân loại CUE/BGM lấy từ ListData (không ghi đè bằng hint sidebar — tránh nhầm view).
    const bool isGrid = target->isGrid;

    if (isGrid)
        compactCueListPads (*target);

    restoreListSelection (*target);
    syncCueMetadataFromPads (*target);

    for (auto* p : target->pads)
        if (p != nullptr)
            scrollContent->addChildComponent (p);

    for (int i = 0; i < target->pads.size(); ++i)
    {
        auto* p = target->pads[i];

        if (p != nullptr)
        {
            p->setPadIndex (i);
            p->setCueListPlayback (isGrid);
            p->setRenderMode (isGrid);
            p->setClickToTrigger (false);
            p->setVisible (true);
            wireSoundPad (p);
        }
    }

    masterDeckPanel.setListMode (! isGrid);
    sidebarPanel.setListLooping (listIndex, target->isLooping);

    if (isGrid)
        refreshCueListPanel();

    rebuildDefaultHotkeysForList (listIndex);

    applyPadSelectionVisualState();

    if (juce::isPositiveAndBelow (selectedBgmIndex, target->pads.size()))
    {
        if (auto* pad = target->pads[selectedBgmIndex])
        {
            syncPadTagColourFromCueMeta (*target, pad);
            presentPadInInspector (pad);
        }
    }
    else
    {
        inspectorPanel.selectPad (nullptr);
    }

    resized();

    activeList = (isGrid && target->useCueListPanel) ? 1 : 0;

    if (isShowing())
    {
        if (activeList == 1 && cueListPanel != nullptr && cueListPanel->isShowing())
            cueListPanel->grabKeyboardFocus();
        else if (viewScroller.isVisible())
            viewScroller.grabKeyboardFocus();
        else
            grabKeyboardFocus();
    }

    updateMainDeskDisplay();
    hidePadsForAllListsExcept (listIndex);
}

void MainComponent::syncCueMetadataFromPads (ListData& list)
{
    while (list.cueMeta.size() < list.pads.size())
        list.cueMeta.add ({});

    while (list.cueMeta.size() > list.pads.size())
        list.cueMeta.removeLast();

    for (int i = 0; i < list.pads.size(); ++i)
    {
        auto& meta = list.cueMeta.getReference (i);
        meta.cueNumber = i + 1;

        if (auto* pad = list.pads[i])
        {
            meta.name      = pad->getPadName();
            meta.filePath  = pad->getFilePath();
            meta.tagColour = pad->getTagColour();
        }
        else
        {
            meta.name.clear();
            meta.filePath.clear();
        }
    }
}

void MainComponent::refreshTagColourLiveUi (ListData& list, int changedIndex)
{
    for (auto* pad : list.pads)
    {
        if (pad != nullptr)
            pad->repaint();
    }

    if (scrollContent != nullptr)
        scrollContent->repaint();

    viewScroller.repaint();

    if (cueListPanel != nullptr)
    {
        if (juce::isPositiveAndBelow (changedIndex, list.cueMeta.size()))
            cueListPanel->syncCueTagColourAt (changedIndex, list.cueMeta.getReference (changedIndex).tagColour);

        cueListPanel->repaint();
    }

    refreshGlobalTrackAccent();
    repaint();
}

void MainComponent::updateListThemeColour (int listIndex, juce::Colour colour)
{
    if (! juce::isPositiveAndBelow (listIndex, allLists.size()))
        return;

    auto* list = allLists[listIndex];
    if (list == nullptr)
        return;

    list->themeColour = showcontrol::colours::snapToPalette (colour);
    sidebarPanel.setListThemeColour (listIndex, list->themeColour);
    sidebarPanel.repaint();
    triggerSave();
}

void MainComponent::refreshGlobalTrackAccent()
{
    SoundPad* accentPad = findGloballyPrioritizedPlayingPad();

    if (accentPad == nullptr)
        accentPad = inspectorPanel.getCurrentPad();

    masterDeckPanel.setTrackAccentFromPad (accentPad);
}

void MainComponent::syncPadTagColourFromCueMeta (ListData& list, SoundPad* pad) noexcept
{
    if (pad == nullptr)
        return;

    const int idx = list.pads.indexOf (pad);

    if (! juce::isPositiveAndBelow (idx, list.cueMeta.size()))
        return;

    const auto snapped = showcontrol::colours::snapToPalette (list.cueMeta.getReference (idx).tagColour);

    if (pad->getTagColour().getARGB() != snapped.getARGB())
        pad->setPadThemeColour (snapped);
}

void MainComponent::presentPadInInspector (SoundPad* pad)
{
    inspectorPanel.selectPad (pad);
    refreshGlobalTrackAccent();
}

void MainComponent::syncBgmListHeaderScrollbar()
{
    if (listHeaderComponent == nullptr || ! listHeaderComponent->isVisible())
        return;

    const int contentW = getPlaylistViewportContentWidth();
    auto headerBounds = listHeaderComponent->getBounds();
    listHeaderComponent->setBounds (headerBounds.getX(), headerBounds.getY(),
                                    contentW,
                                    headerBounds.getHeight());
}

int MainComponent::getPlaylistViewportContentWidth() noexcept
{
    if (! viewScroller.isVisible())
        return 0;

    const int visibleW = viewScroller.getMaximumVisibleWidth();
    if (visibleW > 0)
        return visibleW;

    int scrollbarTrim = 0;

    if (const auto& vBar = viewScroller.getVerticalScrollBar(); vBar.isVisible())
        scrollbarTrim = vBar.getWidth();

    return juce::jmax (0, viewScroller.getWidth() - scrollbarTrim);
}

void MainComponent::applyTagColourToPadAndCue (ListData& list, int index, juce::Colour colour)
{
    if (! juce::isPositiveAndBelow (index, list.pads.size()))
        return;

    const auto snapped = showcontrol::colours::snapToPalette (colour);

    if (auto* pad = list.pads[index])
        pad->setPadThemeColour (snapped);

    if (index < list.cueMeta.size())
        list.cueMeta.getReference (index).tagColour = snapped;

    refreshTagColourLiveUi (list, index);

    if (auto* pad = list.pads[index])
    {
        if (inspectorPanel.getCurrentPad() == pad)
            inspectorPanel.refreshTagColourUi();
    }

    triggerSave();
}

void MainComponent::synchronizePadGridWithEngineState()
{
    auto* list = getActiveListSafe();

    if (list == nullptr || ! list->isGrid)
        return;

    syncCueMetadataFromPads (*list);

    for (auto* p : list->pads)
    {
        if (p == nullptr)
            continue;

        p->clearHotkeyTriggerGuard();
        p->setCueListPlayback (true);
        p->setRenderMode (true);
        p->setClickToTrigger (false);
    }

    SoundPad* focusPad = nullptr;

    if (juce::isPositiveAndBelow (selectedBgmIndex, list->pads.size()))
        focusPad = list->pads[selectedBgmIndex];

    if (focusPad == nullptr)
    {
        for (auto* p : list->pads)
        {
            if (p != nullptr && (p->isPlaying() || p->isPaused()))
            {
                focusPad = p;
                break;
            }
        }
    }

    if (focusPad != nullptr)
    {
        forwardUiSelectionToPad (focusPad, false);
        inspectorPanel.selectPad (focusPad);
    }
    else
    {
        applyPadSelectionVisualState();
    }

    updateCuePlaybackIndicators();
    refreshSidebarPlayingStatus();

    if (auto* panel = getPadPanel())
    {
        panel->setPadList (&list->pads);
        panel->refreshPadGrid();
    }

    viewScroller.resized();

    for (auto* p : list->pads)
        if (p != nullptr && p->isVisible())
            p->repaint();

    viewScroller.repaint();
}

void MainComponent::finishPlayoutViewHeavySync (bool isPadMode)
{
    if (auto* activeList = getActiveListSafe())
    {
        if (isPadMode)
            synchronizePadGridWithEngineState();
        else
        {
            refreshCueListPanel (false);

            if (cueListPanel != nullptr)
                cueListPanel->repaint();
        }

        juce::ignoreUnused (activeList);
    }

    repaint();
}

void MainComponent::releaseUiFocusForViewSwitch()
{
    if (padReorderActive)
        cancelPadReorder();

    if (cueListPanel != nullptr)
        cueListPanel->haltActiveTimers();

    endMarqueeSelection();

    juce::Component::unfocusAllComponents();
}

void MainComponent::applyPlayoutViewFocus (bool isPadMode)
{
    if (! isShowing())
        return;

    if (! isPadMode && cueListPanel != nullptr && cueListPanel->isShowing())
    {
        cueListPanel->grabKeyboardFocus();
        return;
    }

    if (auto* panel = getPadPanel())
    {
        if (isPadMode && viewScroller.isVisible())
        {
            panel->grabKeyboardFocus();
            return;
        }
    }

    if (viewScroller.isVisible())
        viewScroller.grabKeyboardFocus();
    else if (isShowing())
        grabKeyboardFocus();
}

void MainComponent::flushPlayoutViewGraphics (bool isPadMode)
{
    // Ép bounds swap đồng bộ (0ms) — tránh viewScroller 0×0.
    resized();

    const auto heavy = [this, isPadMode]
    {
        finishPlayoutViewHeavySync (isPadMode);
        applyPlayoutViewFocus (isPadMode);
    };

    if (juce::MessageManager::getInstance()->isThisTheMessageThread())
        juce::MessageManager::callAsync (heavy);
    else
        heavy();
}

void MainComponent::setPlayoutMode (bool isPadMode)
{
    setPlayoutModeInternal (isPadMode, true);
}

void MainComponent::setPlayoutModeInternal (bool isPadMode, bool persistToDisk)
{
    if (isOperatingState())
        return;

    auto* list = getActiveListSafe();

    if (list == nullptr || ! list->isGrid)
        return;

    releaseUiFocusForViewSwitch();

    list->useCueListPanel = ! isPadMode;
    activeList = isPadMode ? 0 : 1;

    if (activeListIndex >= 0)
        sidebarPanel.setListViewMode (activeListIndex, ! isPadMode);

    if (playoutModeBar != nullptr)
    {
        playoutModeBar->setPadModeActive (isPadMode);
        playoutModeBar->setViewMode (isPadMode);
        playoutModeBar->toFront (false);
    }

    if (cueListPanel != nullptr)
        cueListPanel->setVisible (! isPadMode);

    resized();
    finishPlayoutViewHeavySync (isPadMode);
    applyPlayoutViewFocus (isPadMode);

    if (persistToDisk && ! isOperatingState())
        saveProject();

    broadcastSelectionSyncIfPrimary();
}

void MainComponent::syncPlayoutModeBarFromActiveList()
{
    if (playoutModeBar == nullptr)
        return;

    if (auto* list = getActiveListSafe())
    {
        if (list->isGrid)
            playoutModeBar->setPadModeActive (! list->useCueListPanel);
    }
}

void MainComponent::refreshCueListPanel (bool resetScrollToTop)
{
    if (cueListPanel == nullptr || activeListIndex < 0 || activeListIndex >= allLists.size())
        return;

    auto* list = allLists[activeListIndex];

    if (list == nullptr || ! list->isGrid || ! list->useCueListPanel)
        return;

    syncCueMetadataFromPads (*list);
    cueListPanel->setCues (list->cueMeta);

    if (selectedPadIndices.size() > 1)
        cueListPanel->setSelectedIndices (selectedPadIndices);
    else
        cueListPanel->setSelectedIndex (selectedBgmIndex);

    if (resetScrollToTop)
        cueListPanel->resetListScrollToTop();

    updateCuePlaybackIndicators();
    cueListPanel->notifyPlaybackActivity();
}

void MainComponent::updateCuePlaybackIndicators()
{
    if (isOperatingState())
        return;

    if (cueListPanel == nullptr || activeListIndex < 0 || activeListIndex >= allLists.size())
        return;

    auto* list = allLists[activeListIndex];

    if (list == nullptr)
        return;

    int playingIdx = -1;
    int armedIdx   = -1;
    int activeCount = 0;

    for (int i = 0; i < list->pads.size(); ++i)
    {
        auto* pad = list->pads[i];

        if (pad == nullptr)
            continue;

        if (pad->isTransportActive())
        {
            if (uiPlaybackFocusPad != nullptr && pad != uiPlaybackFocusPad && pad->isFadeOutInProgress())
                continue;

            playingIdx = i;
            ++activeCount;
        }

        if (pad->isArmed())
            armedIdx = i;
    }

    if (uiPlaybackFocusPad != nullptr && list->pads.contains (uiPlaybackFocusPad))
    {
        const int focusIdx = list->pads.indexOf (uiPlaybackFocusPad);

        if (uiPlaybackFocusPad->isPlaying() || uiPlaybackFocusPad->isPaused())
            playingIdx = focusIdx;
        else if (playingIdx < 0 && juce::isPositiveAndBelow (focusIdx, list->pads.size()))
            playingIdx = focusIdx;
    }
    else if (activeCount != 1)
    {
        playingIdx = -1;

        for (int i = 0; i < list->pads.size(); ++i)
        {
            auto* pad = list->pads[i];

            if (pad != nullptr && (pad->isPlaying() || pad->isPaused()))
            {
                if (uiPlaybackFocusPad != nullptr && pad != uiPlaybackFocusPad && pad->isFadeOutInProgress())
                    continue;

                playingIdx = i;
                break;
            }
        }

        if (playingIdx < 0 && juce::isPositiveAndBelow (selectedBgmIndex, list->pads.size()))
            playingIdx = selectedBgmIndex;
    }

    if (playingIdx == lastCuePlaybackPlayingIdx
        && armedIdx == lastCuePlaybackArmedIdx
        && activeListIndex == lastCuePlaybackListIndex)
        return;

    lastCuePlaybackPlayingIdx = playingIdx;
    lastCuePlaybackArmedIdx   = armedIdx;
    lastCuePlaybackListIndex  = activeListIndex;

    cueListPanel->setPlayingIndex (playingIdx);
    cueListPanel->setArmedIndex (armedIdx);
    cueListPanel->notifyPlaybackActivity();
}

void MainComponent::armPad (SoundPad* pad)
{
    if (pad == nullptr)
        return;

    if (activeListIndex >= 0 && activeListIndex < allLists.size())
    {
        auto* list = allLists[activeListIndex];

        if (list != nullptr)
        {
            for (auto* p : list->pads)
                if (p != nullptr)
                    p->setArmed (false);
        }
    }

    pad->prepareForInstantPlay();
    pad->setArmed (true);
    updateCuePlaybackIndicators();
}

bool MainComponent::isCueListViewActive() const noexcept
{
    if (cueListPanel == nullptr || ! cueListPanel->isVisible())
        return false;

    if (activeListIndex < 0 || activeListIndex >= allLists.size())
        return false;

    const auto* list = allLists[activeListIndex];
    return list != nullptr && list->isGrid && list->useCueListPanel;
}

bool MainComponent::triggerCueListPlay (int padIndex)
{
    if (isPlaybackCommandBlocked())
        return false;

    if (activeListIndex < 0 || activeListIndex >= allLists.size())
        return false;

    auto* list = allLists[activeListIndex];
    if (list == nullptr || ! list->isGrid || ! juce::isPositiveAndBelow (padIndex, list->pads.size()))
        return false;

    auto* pad = list->pads[padIndex];
    if (pad == nullptr || ! pad->hasAudioFile())
        return false;

    if (! pad->tryClaimPadHotkeyTrigger())
        return false;

    double preWaitMs = 0.0;
    if (padIndex < list->cueMeta.size())
        preWaitMs = list->cueMeta.getReference (padIndex).preWaitMs;

    armPad (pad);
    forwardUiSelectionToPad (pad, false);
    inspectorPanel.selectPad (pad);

    pendingGoTimer.reset();

    if (preWaitMs > 1.0)
    {
        pendingGoPadIndex = padIndex;
        auto* timer = new PendingCueGoTimer();
        pendingGoTimer.reset (timer);
        timer->onFire = [this, padIndex]
        {
            pendingGoPadIndex = -1;

            if (activeListIndex < 0 || activeListIndex >= allLists.size())
                return;

            auto* activeList = allLists[activeListIndex];
            if (activeList == nullptr || ! juce::isPositiveAndBelow (padIndex, activeList->pads.size()))
                return;

            if (auto* goPad = activeList->pads[padIndex])
            {
                audioEngine.playCue (goPad);
                updateCuePlaybackIndicators();
                refreshSidebarPlayingStatus();
            }
        };
        timer->startMs (preWaitMs);
        return true;
    }

    const bool ok = audioEngine.playCue (pad);
    updateCuePlaybackIndicators();
    refreshSidebarPlayingStatus();
    return ok;
}

bool MainComponent::triggerCueListPause (int padIndex)
{
    if (shouldBlockLocalPlaybackCommand())
        return false;

    if (activeListIndex < 0 || activeListIndex >= allLists.size())
        return false;

    auto* list = allLists[activeListIndex];
    if (list == nullptr || ! juce::isPositiveAndBelow (padIndex, list->pads.size()))
        return false;

    const bool ok = audioEngine.toggleCuePauseResume (list->pads[padIndex]);
    updateCuePlaybackIndicators();
    refreshSidebarPlayingStatus();

    if (ok)
    {
        broadcastSyncIfPrimary ([this, padIndex] (showcontrol::backup::ShowBackupSyncBroadcaster& b)
        {
            b.sendPauseCue (activeListIndex, padIndex);
        });
    }

    return ok;
}

bool MainComponent::triggerCueListStop (int padIndex)
{
    if (shouldBlockLocalPlaybackCommand())
        return false;

    if (activeListIndex < 0 || activeListIndex >= allLists.size())
        return false;

    auto* list = allLists[activeListIndex];
    if (list == nullptr)
        return false;

    bool ok = false;

    if (juce::isPositiveAndBelow (padIndex, list->pads.size()))
        ok = audioEngine.stopCue (list->pads[padIndex]) || ok;
    else
    {
        for (auto* pad : list->pads)
            ok = audioEngine.stopCue (pad) || ok;
    }

    updateCuePlaybackIndicators();
    refreshSidebarPlayingStatus();

    if (ok)
    {
        broadcastSyncIfPrimary ([this, padIndex] (showcontrol::backup::ShowBackupSyncBroadcaster& b)
        {
            b.sendStopCue (activeListIndex, padIndex);
        });
    }

    return ok;
}

bool MainComponent::triggerCueGo (int padIndex, bool fromSync)
{
    if (! fromSync && shouldBlockLocalPlaybackCommand())
        return false;

    if (isPlaybackCommandBlocked())
        return false;

    if (activeListIndex < 0 || activeListIndex >= allLists.size())
        return false;

    auto* list = allLists[activeListIndex];

    if (list == nullptr)
        return false;

    if (padIndex < 0 || padIndex >= list->pads.size())
        return false;

    auto* pad = list->pads[padIndex];

    if (pad == nullptr || ! pad->hasAudioFile())
        return false;

    if (! pad->tryClaimPadHotkeyTrigger())
    {
        logHotkeyTrace ("trigger blocked pad guard cue pad=" + juce::String (padIndex));
        return false;
    }

    if (pad->isFadeOutInProgress())
    {
        logHotkeyTrace ("trigger blocked fadeOutArm cue pad=" + juce::String (padIndex));
        return false;
    }

    double preWaitMs = 0.0;

    if (padIndex < list->cueMeta.size())
        preWaitMs = list->cueMeta.getReference (padIndex).preWaitMs;

    armPad (pad);
    forwardUiSelectionToPad (pad, false);
    inspectorPanel.selectPad (pad);

    pendingGoTimer.reset();
    auto runCueGoPad = [this] (SoundPad* goPad)
    {
        if (goPad == nullptr)
            return;

        if (goPad->isPaused())
        {
            goPad->triggerStop();
            goPad->triggerPlay();
        }
        else if (goPad->isPlaying() || goPad->isFading() || goPad->isStopping())
        {
            // Đang phát / fade-out stop → chỉ dừng (fade), không triggerPlay ngay sau.
            if (goPad->isStopping() || goPad->isFading() || goPad->isFadeOutArmed())
            {
                updateCuePlaybackIndicators();
                refreshSidebarPlayingStatus();
                return;
            }

            goPad->stopTransportWithConfiguredFade();

            updateCuePlaybackIndicators();
            refreshSidebarPlayingStatus();
            return;
        }

        goPad->triggerPlay();
        updateCuePlaybackIndicators();
        refreshSidebarPlayingStatus();
    };

    if (preWaitMs > 1.0)
    {
        pendingGoPadIndex = padIndex;

        if (! fromSync)
        {
            broadcastSyncIfPrimary ([this, padIndex, preWaitMs] (showcontrol::backup::ShowBackupSyncBroadcaster& b)
            {
                b.sendGo (activeListIndex, padIndex, (float) preWaitMs);
            });
        }

        auto* timer = new PendingCueGoTimer();
        pendingGoTimer.reset (timer);
        timer->onFire = [this, padIndex, runCueGoPad]
        {
            pendingGoPadIndex = -1;

            if (activeListIndex < 0 || activeListIndex >= allLists.size())
                return;

            auto* activeList = allLists[activeListIndex];

            if (activeList == nullptr || padIndex < 0 || padIndex >= activeList->pads.size())
                return;

            runCueGoPad (activeList->pads[padIndex]);
        };
        timer->startMs (preWaitMs);
        return true;
    }

    runCueGoPad (pad);

    if (! fromSync)
    {
        broadcastSyncIfPrimary ([this, padIndex] (showcontrol::backup::ShowBackupSyncBroadcaster& b)
        {
            b.sendGo (activeListIndex, padIndex, 0.0f);
        });
    }

    return true;
}

void MainComponent::setSoloPad (SoundPad* pad, bool enable)
{
    if (! enable)
    {
        for (const auto& entry : soloGainBackups)
        {
            if (entry.pad != nullptr)
                entry.pad->setOutputGain (entry.gain);
        }

        soloGainBackups.clear();
        soloPad = nullptr;
        return;
    }

    if (pad == nullptr || activeListIndex < 0 || activeListIndex >= allLists.size())
        return;

    auto* list = allLists[activeListIndex];

    if (list == nullptr)
        return;

    setSoloPad (nullptr, false);
    soloPad = pad;
    soloGainBackups.clear();

    for (auto* p : list->pads)
    {
        if (p == nullptr || p == pad)
            continue;

        SoloGainBackup backup;
        backup.pad  = p;
        backup.gain = p->getOutputGain();
        soloGainBackups.add (backup);
        p->setOutputGain (0.0f);
    }
}

bool MainComponent::triggerPadFromHotkey (const HotkeyBinding& binding, bool fromSync)
{
    if (! fromSync && shouldBlockLocalPlaybackCommand())
        return false;

    if (isPlaybackCommandBlocked())
        return false;

    if (! juce::MessageManager::getInstance()->isThisTheMessageThread())
        return false;

    if (binding.padListIndex < 0 || binding.padListIndex >= allLists.size())
    {
        logHotkeyTrace ("fail: invalid list index " + juce::String (binding.padListIndex));
        return false;
    }

    auto* list = allLists[binding.padListIndex];

    if (list == nullptr || binding.padIndex < 0 || binding.padIndex >= list->pads.size())
    {
        logHotkeyTrace ("fail: invalid pad index " + juce::String (binding.padIndex));
        return false;
    }

    juce::Component::SafePointer<SoundPad> safePad (list->pads[binding.padIndex]);
    auto* pad = safePad.getComponent();

    if (pad == nullptr || ! pad->hasAudioFile())
    {
        logHotkeyTrace ("fail: pad empty/unloaded at list=" + juce::String (binding.padListIndex)
                        + " pad=" + juce::String (binding.padIndex));
        return false;
    }

    if (binding.padListIndex != activeListIndex)
    {
        const bool targetIsGrid = list->isGrid;
        sidebarPanel.setSelectedIndex (binding.padListIndex);
        loadList (binding.padListIndex, sidebarPanel.getListTrackCount (binding.padListIndex), targetIsGrid);
        list = allLists[binding.padListIndex];
    }

    if (list == nullptr || binding.padIndex < 0 || binding.padIndex >= list->pads.size())
        return false;

    if (list->isGrid)
    {
        // Guard duy nhất nằm trong triggerCueGo (tryClaimPadHotkeyTrigger) — không claim 2 lần.
        if (! triggerCueGo (binding.padIndex, fromSync))
            return false;

        logHotkeyTrace ("go cue list=" + juce::String (binding.padListIndex)
                        + " pad=" + juce::String (binding.padIndex));
        return true;
    }

    logHotkeyTrace ("bgm hotkey disabled list=" + juce::String (binding.padListIndex)
                    + " pad=" + juce::String (binding.padIndex));
    return false;
}

void MainComponent::rebuildDefaultHotkeysForList (int listIndex)
{
    if (listIndex < 0 || listIndex >= allLists.size())
        return;

    auto* list = allLists[listIndex];

    if (list == nullptr)
        return;

    hotkeyManager.removeBindingsForPad (listIndex, -1);

    for (int i = 0; i < list->pads.size(); ++i)
    {
        auto* pad = list->pads[i];

        if (pad == nullptr || ! pad->hasAudioFile())
            continue;

        HotkeyBinding binding;
        binding.padListIndex = listIndex;
        binding.padIndex     = i;
        binding.keyPress     = showcontrol::padgrid::hotkeyForCell (pad->getGridRow(), pad->getGridCol()).keyPress;
        binding.description  = pad->getPadName();
        hotkeyManager.addBinding (binding);
    }
}

void MainComponent::ensureDefaultHotkeysForList (int listIndex)
{
    if (listIndex < 0 || listIndex >= allLists.size())
        return;

    auto* list = allLists[listIndex];
    if (list == nullptr)
        return;

    for (int i = 0; i < list->pads.size(); ++i)
    {
        auto* pad = list->pads[i];
        if (pad == nullptr || ! pad->hasAudioFile())
            continue;

        const auto kp = showcontrol::padgrid::hotkeyForCell (pad->getGridRow(), pad->getGridCol()).keyPress;

        if (hotkeyManager.hasKeyboardBindingForPad (listIndex, i))
            continue;

        HotkeyBinding binding;
        binding.padListIndex = listIndex;
        binding.padIndex     = i;
        binding.keyPress     = kp;
        binding.description  = pad->getPadName();
        hotkeyManager.addBinding (binding);
    }
}

juce::File MainComponent::getProjectFile()
{
    const auto home = juce::File::getSpecialLocation (juce::File::userHomeDirectory);
    const auto newFile    = home.getChildFile ("ShowCue_Project.dat");
    const auto legacyFile = home.getChildFile ("Show_Control_Project.dat");

    if (legacyFile.existsAsFile() && ! newFile.existsAsFile())
        return legacyFile;

    return newFile;
}

SoundPad* MainComponent::ensurePadSlotAtIndex (ListData& list, int index)
{
    jassert (index >= 0);
    if (list.isGrid && index >= kMaxCuePadsPerList)
        return nullptr;

    while (list.pads.size() <= index)
    {
        if (list.isGrid && list.pads.size() >= kMaxCuePadsPerList)
            break;
        auto* p = createSoundPad();
        scrollContent->addChildComponent (p);
        const int idx = list.pads.size();
        p->setPadIndex (idx);
        p->configurePad ("", 1.0f, false);
        p->updateTheme (isDarkMode);
        p->setRenderMode (list.isGrid);
        list.pads.add (p);
    }

    if (index >= list.pads.size())
        return nullptr;
    return list.pads[index];
}

void MainComponent::showAudioSettingsDialog()
{
    showPreferencesDialog (0);
}

void MainComponent::showPreferencesDialog (int initialTabIndex)
{
    GlobalPreferencesDialog::Callbacks callbacks;

    callbacks.onBusNameLiveChanged = [this] (int busIndex, const juce::String& text)
    {
        auto name = text.trim();
        if (name.isEmpty())
            name = showcontrol::routing::getBusDisplayName (busIndex);

        multiOutputCallback.setBusName (busIndex, name);

        if (busIndex < showcontrol::routing::kInspectorBusCount)
            inspectorPanel.setBusNames (multiOutputCallback.getAllBusNames());
    };

    callbacks.onAudioSettingsApplied = [this] (const AudioDeviceSettingsPanel::ApplyResult& result)
    {
        for (int b = 0; b < AudioDeviceSettingsPanel::kNumBuses && b < result.busNames.size(); ++b)
            multiOutputCallback.setBusName (b, result.busNames[b]);

        inspectorPanel.setBusNames (multiOutputCallback.getAllBusNames());
        busMixerPanel.repaint();
        saveProject();
    };

    callbacks.onThemeChanged = [this] (int themeId)
    {
        applyThemePreference (themeId);
        saveProject();
    };

    callbacks.onLanguageChanged = [this] (int languageIndex)
    {
        setAppLanguage (languageIndex);
        saveProject();
    };

    callbacks.onCheckForUpdatesRequested = [this]
    {
        checkForUpdates();
    };

    callbacks.onBackupSettingsChanged = [this]
    {
        restartBackupSync();
        updateBackupStatusLabel();
    };

    callbacks.getBackupTakeoverActive = [this] { return backupTakeoverActive; };

    callbacks.onBackupTakeoverChanged = [this] (bool active)
    {
        setBackupTakeoverActive (active);
    };

    callbacks.onScanLanPeers = [this] (int wantRole,
                                       std::function<void (const juce::Array<showcontrol::backup::LanPeerInfo>&)> onDone)
    {
        scanLanPeersAsync (wantRole, std::move (onDone));
    };

    auto* dialog = new GlobalPreferencesDialog (deviceManager,
                                                isDarkMode,
                                                multiOutputCallback.getAllBusNames(),
                                                themePreferenceId,
                                                languagePreferenceIndex,
                                                std::move (callbacks));
    dialog->setInitialTabIndex (initialTabIndex);

    juce::DialogWindow::LaunchOptions opts;
    opts.content.setOwned (dialog);
    opts.dialogTitle             = showcontrol::localization::tr (u8"Cài đặt");
    opts.dialogBackgroundColour  = appLookAndFeel.findColour (juce::ResizableWindow::backgroundColourId);
    opts.escapeKeyTriggersCloseButton = true;
    opts.useNativeTitleBar       = true;
    opts.resizable               = true;
    opts.componentToCentreAround = this;

    if (auto* dw = opts.launchAsync())
    {
        dw->centreWithSize (680, 560);
        dw->grabKeyboardFocus();
        dialog->grabKeyboardFocus();

       #if JUCE_MAC
        showcontrol::mac::applyFarragoFullSizeContentView (*dw);

        juce::Component::SafePointer<juce::DialogWindow> safePrefsWindow (dw);
        juce::MessageManager::callAsync ([safePrefsWindow]
        {
            if (safePrefsWindow != nullptr)
                showcontrol::mac::applyFarragoFullSizeContentView (*safePrefsWindow);
        });
       #endif
    }
}

juce::ApplicationCommandTarget* MainComponent::getNextCommandTarget()
{
    return nullptr;
}

void MainComponent::getAllCommands (juce::Array<juce::CommandID>& commands)
{
    commands.add (juce::StandardApplicationCommandIDs::undo);
    commands.add (juce::StandardApplicationCommandIDs::redo);
    commands.add (ShowControlCommandIDs::showAboutDialog);
    commands.add (ShowControlCommandIDs::checkForUpdates);
    commands.add (ShowControlCommandIDs::openPreferences);
    commands.add (ShowControlCommandIDs::importShowcuePackage);
    commands.add (ShowControlCommandIDs::exportShowcuePackage);
}

void MainComponent::getCommandInfo (juce::CommandID commandID, juce::ApplicationCommandInfo& result)
{
    switch (commandID)
    {
        case juce::StandardApplicationCommandIDs::undo:
        {
            auto undoName = showcontrol::localization::tr (u8"Hoàn tác");

            if (undoManager.canUndo())
                undoName += " " + undoManager.getUndoDescription();

            result.setInfo (undoName,
                            showcontrol::localization::tr (u8"Hoàn tác thao tác gần nhất"),
                            showcontrol::localization::tr (u8"Chỉnh sửa"),
                            0);
            result.defaultKeypresses.add (juce::KeyPress ('z', juce::ModifierKeys::commandModifier, 0));
            result.setActive (undoManager.canUndo());
            break;
        }

        case juce::StandardApplicationCommandIDs::redo:
        {
            auto redoName = showcontrol::localization::tr (u8"Làm lại");

            if (undoManager.canRedo())
                redoName += " " + undoManager.getRedoDescription();

            result.setInfo (redoName,
                            showcontrol::localization::tr (u8"Làm lại thao tác vừa hoàn tác"),
                            showcontrol::localization::tr (u8"Chỉnh sửa"),
                            0);
            result.defaultKeypresses.add (juce::KeyPress ('z', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier, 0));
            result.setActive (undoManager.canRedo());
            break;
        }

        case ShowControlCommandIDs::showAboutDialog:
            result.setInfo (showcontrol::localization::tr (u8"About ShowCue"),
                            showcontrol::localization::tr (u8"Giới thiệu ứng dụng"),
                            showcontrol::localization::tr (u8"Application"),
                            0);
            break;

        case ShowControlCommandIDs::checkForUpdates:
            result.setInfo (showcontrol::localization::tr (u8"Kiểm tra cập nhật..."),
                            showcontrol::localization::tr (u8"Kiểm tra phiên bản mới trên máy chủ"),
                            showcontrol::localization::tr (u8"Application"),
                            0);
            break;

        case ShowControlCommandIDs::openPreferences:
            result.setInfo (showcontrol::localization::tr (u8"Cài đặt..."),
                            showcontrol::localization::tr (u8"Mở hộp thoại cấu hình hệ thống"),
                            showcontrol::localization::tr (u8"General"),
                            0);
            result.addDefaultKeypress (',', juce::ModifierKeys::commandModifier);
            break;

        case ShowControlCommandIDs::importShowcuePackage:
            result.setInfo (showcontrol::localization::tr (u8"Nhập file cấu hình..."),
                            showcontrol::localization::tr (u8"Khôi phục project từ gói .showcue"),
                            showcontrol::localization::tr (u8"Tệp"),
                            0);
            break;

        case ShowControlCommandIDs::exportShowcuePackage:
            result.setInfo (showcontrol::localization::tr (u8"Xuất file cấu hình..."),
                            showcontrol::localization::tr (u8"Đóng gói project để copy sang máy Backup"),
                            showcontrol::localization::tr (u8"Tệp"),
                            0);
            break;

        default:
            break;
    }
}

bool MainComponent::perform (const juce::ApplicationCommandTarget::InvocationInfo& info)
{
    switch (info.commandID)
    {
        case juce::StandardApplicationCommandIDs::undo:
            return performApplicationUndo();

        case juce::StandardApplicationCommandIDs::redo:
            return performApplicationRedo();

        case ShowControlCommandIDs::showAboutDialog:
            showAboutDialog();
            return true;

        case ShowControlCommandIDs::checkForUpdates:
            checkForUpdates();
            return true;

        case ShowControlCommandIDs::openPreferences:
            showPreferencesDialog (0);
            return true;

        case ShowControlCommandIDs::importShowcuePackage:
            importProjectShowcuePackage();
            return true;

        case ShowControlCommandIDs::exportShowcuePackage:
            exportProjectShowcuePackage();
            return true;

        default:
            break;
    }

    return false;
}

void MainComponent::showAboutDialog()
{
    showcontrol::about::showAboutDialog (this, appLookAndFeel);
}

void MainComponent::checkForUpdates()
{
    if (updateChecker != nullptr)
        updateChecker->checkForUpdatesAsync (true);
}

void MainComponent::darkModeSettingChanged()
{
    if (themePreferenceId == 3)
        applyThemePreference (3);
}

void MainComponent::lookAndFeelChanged()
{
    juce::Component::lookAndFeelChanged();

    if (auto* showLaf = dynamic_cast<ShowControlLookAndFeel*> (&getLookAndFeel()))
    {
        const bool dark = showLaf->isDarkMode();

        if (isDarkMode != dark)
        {
            isDarkMode = dark;
            refreshAllPanelThemes (dark);
        }
    }

    repaint();
}

void MainComponent::refreshAllPanelThemes (bool shouldBeDark)
{
    masterDeckPanel.updateThemeColors (shouldBeDark);
    sidebarPanel.updateThemeColors (shouldBeDark);
    inspectorPanel.updateThemeColors (shouldBeDark);
    busMixerPanel.updateTheme (shouldBeDark);

    if (cueListPanel != nullptr)
        cueListPanel->updateTheme (shouldBeDark);

    if (auto* header = dynamic_cast<ListHeaderComponent*> (listHeaderComponent.get()))
        header->setDarkMode (shouldBeDark);

    if (emptyStatePanel != nullptr)
        emptyStatePanel->setDarkMode (shouldBeDark);

    if (auto* scroll = getPadPanel())
        scroll->setDarkMode (shouldBeDark);

    const auto pal = ShowTheme::get (shouldBeDark);
    addMusicFloatingBtn.setColour (juce::TextButton::buttonColourId, pal.panelElevated);
    addMusicFloatingBtn.setColour (juce::TextButton::textColourOffId, pal.textPrimary);
    showSidebarBtn.setColour (juce::TextButton::buttonColourId, pal.panelElevated);
    showSidebarBtn.setColour (juce::TextButton::textColourOffId, pal.textPrimary);
    showInspectorBtn.setColour (juce::TextButton::buttonColourId, pal.panelElevated);
    showInspectorBtn.setColour (juce::TextButton::textColourOffId, pal.textPrimary);

    if (splitterButtonLaf != nullptr)
        splitterButtonLaf->setDarkMode (shouldBeDark);

    if (playoutModeButtonLaf != nullptr)
        playoutModeButtonLaf->setDarkMode (shouldBeDark);

    if (playoutModeBar != nullptr)
        playoutModeBar->setDarkMode (shouldBeDark);

    if (leftSplitter != nullptr)
        leftSplitter->setDarkMode (shouldBeDark);

    if (rightSplitter != nullptr)
        rightSplitter->setDarkMode (shouldBeDark);

    for (auto* list : allLists)
    {
        if (list == nullptr)
            continue;

        for (auto* pad : list->pads)
            if (pad != nullptr)
                pad->updateTheme (shouldBeDark);
    }

    sidebarPanel.repaint();
    inspectorPanel.repaint();
    masterDeckPanel.repaint();
    busMixerPanel.repaint();

    if (cueListPanel != nullptr)
        cueListPanel->repaint();
}

void MainComponent::setAppLanguage (int languageIndex)
{
    languagePreferenceIndex = juce::jlimit (0, 2, languageIndex);
    showcontrol::localization::setAppLanguage (languagePreferenceIndex);
    broadcastLookAndFeelToAllWindows();
    refreshLocalizedUi();

    if (! isInitialLoading.load (std::memory_order_relaxed))
        saveApplicationState();

    if (auto* top = getTopLevelComponent())
        top->repaint();

    repaint();
}

void MainComponent::refreshLocalizedUi()
{
    refreshLocalizedBusNames();
    sidebarPanel.refreshLocalizedText();
    masterDeckPanel.refreshLocalizedText();
    inspectorPanel.refreshLocalizedText();

    if (cueListPanel != nullptr)
        cueListPanel->refreshLocalizedText();

    if (stageMonitorWindow != nullptr)
        if (auto* monitor = stageMonitorWindow->getMonitorComponent())
            monitor->repaint();

    showSidebarBtn.setTooltip (showcontrol::localization::tr (u8"Ẩn/Hiện Sidebar"));
    showInspectorBtn.setTooltip (showcontrol::localization::tr (u8"Ẩn/Hiện Inspector"));
    busMixerPanel.repaint();

    if (listHeaderComponent != nullptr)
        listHeaderComponent->repaint();

    if (emptyStatePanel != nullptr)
        emptyStatePanel->repaint();

   #if JUCE_MAC
    showcontrol::mac::refreshNativeMenuBar();
   #endif
}

void MainComponent::refreshLocalizedBusNames()
{
    for (int b = 0; b < MultiOutputAudioCallback::kMaxBuses; ++b)
    {
        const auto current = multiOutputCallback.getBusName (b).trim();

        if (showcontrol::routing::isDefaultBusName (b, current))
            multiOutputCallback.setBusName (b, showcontrol::routing::getBusDisplayName (b));
    }

    inspectorPanel.setBusNames (multiOutputCallback.getAllBusNames());
}

void MainComponent::applyThemePreference (int themeId)
{
    themePreferenceId = themeId;
    const bool shouldBeDark = ShowTheme::resolveIsDarkFromThemeId (themeId);
    isDarkMode = shouldBeDark;

    appLookAndFeel.setDarkMode (shouldBeDark);
    juce::LookAndFeel::setDefaultLookAndFeel (&appLookAndFeel);

    // Một lần broadcast — sendLookAndFeelChange đệ quy xuống mọi child (không cần forceLookAndFeelRefreshRecursively).
    broadcastLookAndFeelToAllWindows();

    refreshAllPanelThemes (shouldBeDark);

    if (auto* top = getTopLevelComponent())
    {
        if (auto* dw = dynamic_cast<juce::DocumentWindow*> (top))
        {
            dw->setBackgroundColour (appLookAndFeel.findColour (juce::ResizableWindow::backgroundColourId));
            dw->repaint();
        }
        else
        {
            top->repaint();
        }
    }

    if (! isInitialLoading.load (std::memory_order_relaxed))
        saveApplicationState();

    repaint();
}

void MainComponent::refreshSidebarPlayingStatus()
{
    if (cachedSidebarListPlayingActive.size() != allLists.size())
        cachedSidebarListPlayingActive.resize (allLists.size());

    for (int i = 0; i < allLists.size(); ++i)
    {
        bool listActive = false;
        auto* list = allLists[i];

        if (list != nullptr)
        {
            for (auto* pad : list->pads)
            {
                if (pad != nullptr && pad->isTransportActive())
                {
                    listActive = true;
                    break;
                }
            }
        }

        if (cachedSidebarListPlayingActive.getReference (i) != listActive)
        {
            cachedSidebarListPlayingActive.set (i, listActive);
            sidebarPanel.updatePlayingStatus (i, listActive);
        }
    }
}

static SoundPad::PadProjectState readPadProjectState (const juce::XmlElement& padElem)
{
    SoundPad::PadProjectState state;
    state.customName    = padElem.getStringAttribute ("customName", "");
    state.trimStart     = padElem.getDoubleAttribute ("trimStart", 0.0);
    state.trimEnd       = padElem.getDoubleAttribute ("trimEnd", 0.0);
    state.outputGain    = (float) padElem.getDoubleAttribute ("gain", 1.0);
    state.autoNormalize    = padElem.getBoolAttribute ("autoNormalize", true);
    state.normalizeUseLufs = padElem.getBoolAttribute ("normalizeLufs", false);
    state.normalizePreset = padElem.getIntAttribute ("normalizePreset",
                                                     (int) showcontrol::loudness::Preset::liveShow);
    state.normalizeProfile = padElem.getIntAttribute ("normalizeProfile",
                                                      (int) showcontrol::loudness::ContentProfile::general);
    state.normalizeSafeMode = padElem.getBoolAttribute ("normalizeSafeMode", true);
    state.normalizeCustomTargetLufs = padElem.getDoubleAttribute ("normalizeCustomTargetLufs", -16.0);
    state.outputBus        = juce::jlimit (0, showcontrol::routing::kInspectorBusCount - 1,
                                           padElem.getIntAttribute ("outputBus", 0));

    // Cached metadata — dùng để hiển thị ngay trước khi file được đọc lại
    state.cachedMeta.title  = padElem.getStringAttribute ("metaTitle", "");
    state.cachedMeta.artist = padElem.getStringAttribute ("metaArtist", "");
    state.cachedMeta.album  = padElem.getStringAttribute ("metaAlbum", "");
    const juce::String bpmStr = padElem.getStringAttribute ("metaBpm", "");
    if (bpmStr.isNotEmpty()) state.cachedMeta.bpm = bpmStr.getDoubleValue();
    state.cachedMeta.formatInfoString = padElem.getStringAttribute ("metaFormat", "");
    state.cachedMeta.sampleRate   = padElem.getIntAttribute ("metaSampleRate", 0);
    state.cachedMeta.bitDepth     = padElem.getIntAttribute ("metaBitDepth", 0);
    state.cachedMeta.numChannels  = padElem.getIntAttribute ("metaChannels", 0);

    if (state.cachedMeta.formatInfoString.isEmpty()
        && (state.cachedMeta.sampleRate > 0 || state.cachedMeta.bitDepth > 0 || state.cachedMeta.numChannels > 0))
    {
        state.cachedMeta.formatInfoString = state.cachedMeta.buildFormatInfoUncached();
    }

    state.fadeInMs  = padElem.getDoubleAttribute ("fadeInMs", 0.0);
    state.fadeOutMs = padElem.getDoubleAttribute ("fadeOutMs", 0.0);

    state.dspEqEnabled = padElem.getBoolAttribute ("dspEq", false);
    state.dspEqLowDb   = (float) padElem.getDoubleAttribute ("dspEqLow", 0.0);
    state.dspEqMidDb   = (float) padElem.getDoubleAttribute ("dspEqMid", 0.0);
    state.dspEqHighDb  = (float) padElem.getDoubleAttribute ("dspEqHigh", 0.0);
    for (int b = 0; b < PadParametricEq6::kNumBands; ++b)
        state.dspEqBandDb[(size_t) b] = (float) padElem.getDoubleAttribute ("dspEqBand" + juce::String (b), 0.0);

    return state;
}

static void writeCueMetaToPadElem (juce::XmlElement& padElem, const CueItem& cue)
{
    if (cue.preWaitMs > 0.0)
        padElem.setAttribute ("cuePreWaitMs", cue.preWaitMs);

    if (cue.postWaitMs > 0.0)
        padElem.setAttribute ("cuePostWaitMs", cue.postWaitMs);

    if (cue.autoFollow)
        padElem.setAttribute ("cueAutoFollow", true);

    if (! cue.isEnabled)
        padElem.setAttribute ("cueEnabled", false);

    const auto defaultTag = showcontrol::colours::defaultTagColour();

    if (! showcontrol::colours::isDefaultTagColour (cue.tagColour))
        padElem.setAttribute ("tagColour", (int) cue.tagColour.getARGB());
}

static void readCueMetaFromPadElem (const juce::XmlElement& padElem, CueItem& cue)
{
    cue.preWaitMs  = padElem.getDoubleAttribute ("cuePreWaitMs", 0.0);
    cue.postWaitMs = padElem.getDoubleAttribute ("cuePostWaitMs", 0.0);
    cue.autoFollow = padElem.getBoolAttribute ("cueAutoFollow", false);
    cue.isEnabled  = padElem.getBoolAttribute ("cueEnabled", true);

    if (padElem.hasAttribute ("tagColour"))
        cue.tagColour = showcontrol::colours::snapToPalette (
            juce::Colour ((juce::uint32) padElem.getIntAttribute ("tagColour")));
}

static void writePadProjectState (juce::XmlElement& padElem, const SoundPad& pad)
{
    const auto custom = pad.getPadName();

    if (pad.getFilePath().isNotEmpty())
    {
        juce::File f (pad.getFilePath());
        const auto fileStem = VideoAudioExtractor::displayNameFromAudioPath (f);
        if (custom != fileStem)
            padElem.setAttribute ("customName", custom);
    }
    else if (custom != juce::String::fromUTF8 (u8"Trống"))
    {
        padElem.setAttribute ("customName", custom);
    }

    if (pad.getTrimStart() > 0.0)
        padElem.setAttribute ("trimStart", pad.getTrimStart());

    if (pad.getTrimEnd() > 0.0)
        padElem.setAttribute ("trimEnd", pad.getTrimEnd());

    const float gain = pad.getOutputGain();
    if (std::abs (gain - 1.0f) > 0.001f)
        padElem.setAttribute ("gain", (double) gain);

    if (! pad.getAutoNormalize())
        padElem.setAttribute ("autoNormalize", false);

    if (pad.getNormalizeUseLufs())
        padElem.setAttribute ("normalizeLufs", true);

    const auto& loudnessSettings = pad.getLoudnessSettings();
    if ((int) loudnessSettings.preset != (int) showcontrol::loudness::Preset::liveShow)
        padElem.setAttribute ("normalizePreset", (int) loudnessSettings.preset);
    if ((int) loudnessSettings.profile != (int) showcontrol::loudness::ContentProfile::general)
        padElem.setAttribute ("normalizeProfile", (int) loudnessSettings.profile);
    if (! loudnessSettings.safeMode)
        padElem.setAttribute ("normalizeSafeMode", false);
    if (std::abs (loudnessSettings.customTargetLufs + 16.0) > 0.01)
        padElem.setAttribute ("normalizeCustomTargetLufs", loudnessSettings.customTargetLufs);

    // Lưu bus routing nếu khác mặc định (bus 0 = Main FOH)
    if (pad.getOutputBus() != 0)
        padElem.setAttribute ("outputBus", pad.getOutputBus());

    // Cache title hiển thị — load project vẽ tên ngay, không đợi async file I/O
    const auto displayTitle = pad.getPadName();

    if (displayTitle.isNotEmpty()
        && displayTitle != juce::String::fromUTF8 (u8"Trống"))
    {
        padElem.setAttribute ("metaTitle", displayTitle);
    }

    const auto& meta = pad.getMetadata();

    if (meta.artist.isNotEmpty()) padElem.setAttribute ("metaArtist", meta.artist);
    if (meta.album.isNotEmpty())  padElem.setAttribute ("metaAlbum",  meta.album);
    if (meta.bpm > 0.0)          padElem.setAttribute ("metaBpm",    meta.bpm);

    if (meta.formatInfoString.isNotEmpty())
        padElem.setAttribute ("metaFormat", meta.formatInfoString);
    else if (meta.sampleRate > 0 || meta.bitDepth > 0 || meta.numChannels > 0)
    {
        if (meta.sampleRate > 0)  padElem.setAttribute ("metaSampleRate", meta.sampleRate);
        if (meta.bitDepth > 0)    padElem.setAttribute ("metaBitDepth", meta.bitDepth);
        if (meta.numChannels > 0) padElem.setAttribute ("metaChannels", meta.numChannels);
    }

    // Per-pad fade duration
    if (pad.getFadeInMs() > 0.0)
        padElem.setAttribute ("fadeInMs", pad.getFadeInMs());
    if (std::abs (pad.getFadeOutMs() - 1500.0) > 10.0)
        padElem.setAttribute ("fadeOutMs", pad.getFadeOutMs());

    if (pad.getDspEqEnabled())
    {
        padElem.setAttribute ("dspEq", true);
        for (int b = 0; b < PadParametricEq6::kNumBands; ++b)
        {
            const float g = pad.getDspEqBandGainDb (b);
            if (std::abs (g) > 0.05f)
                padElem.setAttribute ("dspEqBand" + juce::String (b), (double) g);
        }
    }

    if (! showcontrol::colours::isDefaultTagColour (pad.getTagColour()))
        padElem.setAttribute ("tagColour", (int) pad.getTagColour().getARGB());

    if (pad.occupiesCueGridSlot())
    {
        padElem.setAttribute ("gridRow", pad.getGridRow());
        padElem.setAttribute ("gridCol", pad.getGridCol());
    }
}

namespace
{
constexpr auto kMissingAudioFormatFallback = u8"-- kHz / -- bit";

juce::String probeAudioFormatOnBackgroundThread (const juce::File& file,
                                                 int& outSampleRate,
                                                 int& outBitDepth,
                                                 int& outNumChannels)
{
    outSampleRate  = 0;
    outBitDepth    = 0;
    outNumChannels = 0;

    if (! file.existsAsFile())
        return juce::String::fromUTF8 (kMissingAudioFormatFallback);

    juce::AudioFormatManager localFormatManager;
    localFormatManager.registerBasicFormats();

    if (auto reader = std::unique_ptr<juce::AudioFormatReader> (localFormatManager.createReaderFor (file)))
    {
        outSampleRate  = (int) reader->sampleRate;
        outBitDepth    = (int) reader->bitsPerSample;
        outNumChannels = (int) reader->numChannels;

        AudioMetadata meta;
        meta.sampleRate  = outSampleRate;
        meta.bitDepth    = outBitDepth;
        meta.numChannels = outNumChannels;
        return meta.buildFormatInfoUncached();
    }

    return juce::String::fromUTF8 (kMissingAudioFormatFallback);
}
} // namespace

bool MainComponent::collectPadsNeedingFormatMigration (juce::Array<AudioFormatMigrationEntry>& out) const
{
    out.clear();
    bool needsMigration = false;

    for (int listIndex = 0; listIndex < allLists.size(); ++listIndex)
    {
        auto* list = allLists[listIndex];

        if (list == nullptr)
            continue;

        for (int padIndex = 0; padIndex < list->pads.size(); ++padIndex)
        {
            auto* pad = list->pads[padIndex];

            if (pad == nullptr)
                continue;

            const auto path = pad->getConfiguredFilePath();

            if (path.isEmpty())
                continue;

            if (pad->getCachedFormatInfoString().isNotEmpty())
                continue;

            AudioFormatMigrationEntry entry;
            entry.listIndex = listIndex;
            entry.padIndex  = padIndex;
            entry.filePath  = path;
            out.add (entry);
            needsMigration = true;
        }
    }

    return needsMigration;
}

void MainComponent::applyAudioFormatMigrationResults (const juce::Array<AudioFormatMigrationResult>& results)
{
    for (const auto& result : results)
    {
        if (result.listIndex < 0 || result.listIndex >= allLists.size())
            continue;

        auto* list = allLists[result.listIndex];

        if (list == nullptr || result.padIndex < 0 || result.padIndex >= list->pads.size())
            continue;

        if (auto* pad = list->pads[result.padIndex])
        {
            pad->applyMigratedAudioFormat (result.formatString,
                                           result.sampleRate,
                                           result.bitDepth,
                                           result.numChannels);
        }
    }
}

void MainComponent::refreshUiAfterFormatMigration()
{
    for (auto* list : allLists)
    {
        if (list == nullptr)
            continue;

        for (auto* pad : list->pads)
            if (pad != nullptr)
                pad->repaint();
    }

    refreshCueListPanel();

    if (cueListPanel != nullptr)
    {
        cueListPanel->refreshListBoxData();
        cueListPanel->repaint();
    }

    if (auto* selectedPad = inspectorPanel.getCurrentPad())
        inspectorPanel.refreshMetadata();

    sidebarPanel.repaint();
    repaint();
}

void MainComponent::startBackgroundAudioFormatMigration (const juce::Array<AudioFormatMigrationEntry>& entries)
{
    if (entries.isEmpty())
        return;

    bool expected = false;

    if (! audioFormatMigrationRunning.compare_exchange_strong (expected, true, std::memory_order_acq_rel))
        return;

    juce::Component::SafePointer<MainComponent> safeThis (this);
    const auto entriesCopy = entries;

    showcontrol::background::enqueue ([safeThis, entriesCopy]()
    {
        juce::Array<AudioFormatMigrationResult> results;
        results.ensureStorageAllocated (entriesCopy.size());

        for (const auto& entry : entriesCopy)
        {
            AudioFormatMigrationResult result;
            result.listIndex = entry.listIndex;
            result.padIndex  = entry.padIndex;

            const juce::File trackFile (entry.filePath);
            result.formatString = probeAudioFormatOnBackgroundThread (trackFile,
                                                                      result.sampleRate,
                                                                      result.bitDepth,
                                                                      result.numChannels);
            results.add (result);
        }

        juce::MessageManager::callAsync ([safeThis, results]()
        {
            if (safeThis == nullptr)
                return;

            safeThis->applyAudioFormatMigrationResults (results);
            safeThis->saveApplicationState();
            safeThis->refreshUiAfterFormatMigration();
            safeThis->audioFormatMigrationRunning.store (false, std::memory_order_release);
        });
    });
}

namespace
{
    bool jsonConfigHasPersistedProject (const juce::var& jsonRoot) noexcept
    {
        if (auto* obj = jsonRoot.getDynamicObject())
        {
            const auto embeddedXml = obj->getProperty ("projectXml").toString().trim();

            if (embeddedXml.isNotEmpty())
            {
                if (auto parsed = juce::parseXML (embeddedXml))
                    if (parsed->hasTagName ("ShowControlProject"))
                        return true;
            }

            const auto playlist = obj->getProperty ("playlist");

            if (playlist.isArray())
                if (const auto* arr = playlist.getArray())
                    if (! arr->isEmpty())
                        return true;
        }

        return false;
    }
}

void MainComponent::applyFactoryDefaultApplicationState()
{
    themePreferenceId       = 1; // Dark
    languagePreferenceIndex = 1; // Tiếng Việt

    projectDefaultNormalizeLufs = false;
    hotkeyScopeMode             = HotkeyScopeMode::activeList;
    sidebarWidth   = 250;
    inspectorWidth = 360;
    sidebarVisible   = true;
    inspectorVisible = true;

    inspectorPanel.selectPad (nullptr);
    masterDeckPanel.setActivePad (nullptr);
    lastUiSyncedPlayingPad = nullptr;
    resetPlaybackDisplayCaches();

    for (auto* list : allLists)
    {
        if (list == nullptr)
            continue;

        for (auto* pad : list->pads)
        {
            if (pad == nullptr)
                continue;

            safelyPreparePadForDeletion (pad);

            if (scrollContent != nullptr)
                scrollContent->removeChildComponent (pad);
        }
    }

    allLists.clear();
    sidebarPanel.clearAllLists();

    activeListIndex = -1;
    selectedPadIndices.clear();
    selectedBgmIndex = 0;

    for (int b = 0; b < MultiOutputAudioCallback::kMaxBuses; ++b)
    {
        multiOutputCallback.setBusGain (b, 1.0f);
        busMixerPanel.setBusGain (b, 1.0f, juce::dontSendNotification);
    }

    masterDeckPanel.setMasterVolumeValue (1.0f, juce::dontSendNotification);
    multiOutputCallback.setMasterLimiterEnabled (true);
    multiOutputCallback.setMasterLimiterThresholdDb (-1.0f);
    multiOutputCallback.setMasterLimiterReleaseMs (80.0f);

    for (int b = 0; b < showcontrol::routing::kInspectorBusCount; ++b)
        multiOutputCallback.setBusName (b, showcontrol::routing::getBusDisplayName (b));

    for (int b = showcontrol::routing::kInspectorBusCount; b < MultiOutputAudioCallback::kMaxBuses; ++b)
        multiOutputCallback.setBusName (b, "AUX " + juce::String (b));

    inspectorPanel.setBusNames (multiOutputCallback.getAllBusNames());
    sidebarPanel.setSelectedIndex (-1);
}

void MainComponent::loadApplicationState()
{
    StateOperationScope stateGuard (isPerformingStateOperation);

    if (! stateGuard.enteredSuccessfully())
        return;

    if (! stateIoLock.enter (3000))
    {
        std::cout << "[CONFIG] [WARN] Cannot acquire state lock for load." << std::endl;
        return;
    }
    InterProcessUnlockScope ioUnlock (stateIoLock);

    const auto configPath = showcontrol::state::getCanonicalConfigFile().getFullPathName();
    std::cout << "[CONFIG] loadApplicationState() — target: " << configPath.toStdString() << std::endl;

    isInitialLoading.store (true, std::memory_order_relaxed);
    startupReassertTimer.reset();
    startupGuardTimer.reset();

    showcontrol::state::ensureDefaultConfigExists();

    std::unique_ptr<juce::XmlElement> xml;

    const auto jsonRoot    = showcontrol::state::readJsonConfig();
    const bool configIsFresh = ! jsonConfigHasPersistedProject (jsonRoot);

    if (! configIsFresh)
    {
        if (auto* jsonObj = jsonRoot.getDynamicObject())
        {
            const int jsonTheme = static_cast<int> (jsonObj->getProperty ("appTheme"));
            const int jsonLang  = static_cast<int> (jsonObj->getProperty ("appLanguage"));

            if (jsonTheme >= 1 && jsonTheme <= 3)
                themePreferenceId = jsonTheme;

            if (jsonLang >= 0 && jsonLang <= 2)
                languagePreferenceIndex = jsonLang;

            const auto embeddedXml = jsonObj->getProperty ("projectXml").toString();

            if (embeddedXml.isNotEmpty())
                xml = juce::parseXML (embeddedXml);
        }

        if (xml == nullptr || ! xml->hasTagName ("ShowControlProject"))
        {
            const auto projectFile = getProjectFile();

            if (projectFile.existsAsFile())
                xml = juce::parseXML (projectFile);
        }
    }
    else
    {
        std::cout << "[CONFIG] [INFO] Fresh install — factory defaults (no persisted project)." << std::endl;
    }

    bool projectLoaded = false;

    if (xml != nullptr && xml->hasTagName ("ShowControlProject"))
    {
            const auto version = xml->getStringAttribute ("version", "1.0");
            const bool isV2    = (version == "2.0");

            projectDefaultNormalizeLufs = xml->getBoolAttribute ("defaultNormalizeLufs", false);
            hotkeyScopeMode = (xml->getIntAttribute ("hotkeyScopeMode", 1) == 2)
                ? HotkeyScopeMode::global
                : HotkeyScopeMode::activeList;

            sidebarWidth   = xml->getIntAttribute ("sidebarWidth", sidebarWidth);
            inspectorWidth = xml->getIntAttribute ("inspectorWidth", inspectorWidth);
            sidebarVisible   = xml->getBoolAttribute ("sidebarVisible", sidebarVisible);
            inspectorVisible = xml->getBoolAttribute ("inspectorVisible", inspectorVisible);

            // Load cấu hình bus routing (nếu có)
            if (auto* routingElem = xml->getChildByName ("AudioRouting"))
            {
                for (auto* busElem : routingElem->getChildIterator())
                {
                    if (! busElem->hasTagName ("Bus")) continue;
                    const int b = busElem->getIntAttribute ("index", -1);
                    if (b >= 0 && b < MultiOutputAudioCallback::kMaxBuses)
                    {
                        multiOutputCallback.setBusName (b, busElem->getStringAttribute ("name"));
                        const float gain = (float) busElem->getDoubleAttribute ("gain", 1.0);
                        multiOutputCallback.setBusGain (b, gain);
                        busMixerPanel.setBusGain (b, gain, juce::dontSendNotification);
                        if (b == 0)
                            masterDeckPanel.setMasterVolumeValue (gain, juce::dontSendNotification);

                        if (b == 0)
                        {
                            multiOutputCallback.setMasterLimiterEnabled (busElem->getBoolAttribute ("limiter", true));
                            multiOutputCallback.setMasterLimiterThresholdDb (
                                (float) busElem->getDoubleAttribute ("limiterThreshold", -1.0));
                            multiOutputCallback.setMasterLimiterReleaseMs (
                                (float) busElem->getDoubleAttribute ("limiterRelease", 80.0));
                        }
                    }
                }
            }

            resetPlaybackDisplayCaches();
            allLists.clear();
            sidebarPanel.clearAllLists();

            for (auto* listElem : xml->getChildIterator())
            {
                if (! listElem->hasTagName ("List"))
                    continue;

                auto* newList = new ListData();
                newList->isGrid            = listElem->getBoolAttribute ("isGrid", true);
                newList->isLooping         = listElem->getBoolAttribute ("isLooping", false);
                newList->isLocked          = listElem->getBoolAttribute ("isLocked", false);
                newList->useCueListPanel   = listElem->getBoolAttribute ("useCueListPanel", false);
                newList->clickPadToTrigger = listElem->getBoolAttribute ("clickPadToTrigger", false);
                newList->autoArmOnSelect   = listElem->getBoolAttribute ("autoArmOnSelect", true);
                if (listElem->hasAttribute ("themeColour"))
                {
                    newList->themeColour = showcontrol::colours::snapToPalette (
                        juce::Colour ((juce::uint32) listElem->getIntAttribute ("themeColour")));
                }
                const juce::String listName = listElem->getStringAttribute ("name",
                    newList->isGrid ? showcontrol::localization::defaultCueListName()
                                    : showcontrol::localization::defaultBgmListName());

                for (auto* padElem : listElem->getChildIterator())
                {
                    if (! padElem->hasTagName ("Pad"))
                        continue;

                    const juce::String filePath = padElem->getStringAttribute ("file", "");

                    if (isV2)
                    {
                        const int index = padElem->getIntAttribute ("index", (int) newList->pads.size());
                        if (newList->isGrid && index >= kMaxCuePadsPerList)
                            continue;
                        auto* p = ensurePadSlotAtIndex (*newList, index);
                        if (p == nullptr)
                            continue;
                        p->setPadIndex (index);

                        CueItem cueMeta;
                        readCueMetaFromPadElem (*padElem, cueMeta);

                        if (filePath.isNotEmpty())
                        {
                            const auto state = readPadProjectState (*padElem);
                            const bool loopTrack = padElem->getBoolAttribute ("loopTrack", false);
                            const bool loop = newList->isGrid ? newList->isLooping : loopTrack;

                            p->setPendingProjectState (state);
                            p->configurePad (filePath, state.outputGain, loop);
                            p->updateTheme (isDarkMode);
                            p->setRenderMode (newList->isGrid);
                            p->setClickToTrigger (newList->clickPadToTrigger);

                            if (newList->isGrid)
                                p->setLooping (newList->isLooping);
                            else if (loopTrack)
                                p->setLooping (true);
                        }
                        else
                        {
                            p->setClickToTrigger (newList->clickPadToTrigger);
                        }

                        p->setTagColour (cueMeta.tagColour);

                        if (newList->isGrid && padElem->hasAttribute ("gridRow"))
                        {
                            p->setGridPosition (padElem->getIntAttribute ("gridRow", 0),
                                                padElem->getIntAttribute ("gridCol", 0));
                        }

                        if (newList->isGrid)
                        {
                            cueMeta.cueNumber = index + 1;

                            if (cueMeta.name.isEmpty() && p != nullptr)
                            {
                                const auto displayName = p->getPadName();
                                if (displayName != juce::String::fromUTF8 (u8"Trống"))
                                    cueMeta.name = displayName;
                            }

                            newList->cueMeta.add (cueMeta);
                        }
                    }
                    else if (filePath.isNotEmpty())
                    {
                        if (newList->isGrid && newList->pads.size() >= kMaxCuePadsPerList)
                            continue;
                        auto* p = createSoundPad();
                        scrollContent->addChildComponent (p);
                        const int padIdx = newList->pads.size();
                        p->setPadIndex (padIdx);

                        const bool loopTrack = padElem->getBoolAttribute ("loopTrack", false);
                        const auto state = readPadProjectState (*padElem);
                        p->setPendingProjectState (state);
                        p->configurePad (filePath, 1.0f, newList->isGrid ? newList->isLooping : loopTrack);
                        p->updateTheme (isDarkMode);
                        p->setRenderMode (newList->isGrid);

                        if (newList->isGrid)
                            p->setLooping (newList->isLooping);
                        else if (loopTrack)
                            p->setLooping (true);

                        CueItem legacyMeta;
                        readCueMetaFromPadElem (*padElem, legacyMeta);
                        p->setTagColour (legacyMeta.tagColour);

                        if (newList->isGrid && padElem->hasAttribute ("gridRow"))
                        {
                            p->setGridPosition (padElem->getIntAttribute ("gridRow", 0),
                                                padElem->getIntAttribute ("gridCol", 0));
                        }

                        newList->pads.add (p);
                    }
                }

                allLists.add (newList);
                sidebarPanel.addSet (listName, newList->pads.size(), newList->isGrid,
                                     newList->useCueListPanel, newList->isLocked, newList->themeColour);
                sidebarPanel.setListLooping (allLists.size() - 1, newList->isLooping);
            }

            hotkeyManager.loadFromXml (*xml);
            projectLoaded = true;
    }
    else if (! configIsFresh)
    {
        if (auto* jsonObj = jsonRoot.getDynamicObject())
            projectLoaded = loadPlaylistFromJson (jsonObj->getProperty ("playlist"));
    }

    if (! projectLoaded)
    {
        applyFactoryDefaultApplicationState();
        loadList (-1, 0, true);
        refreshStartupPlaylistDisplay();
        isInitialLoading.store (false, std::memory_order_relaxed);
        saveApplicationState();
        std::cout << "[CONFIG] [INFO] Factory default state saved to disk." << std::endl;
        return;
    }

    for (int i = 0; i < allLists.size(); ++i)
    {
        if (auto* list = allLists[i]; list != nullptr && list->isGrid)
            compactCueListPads (*list);
    }

    int initialListIndex = -1;

    if (! allLists.isEmpty())
    {
        initialListIndex = 0;
        for (int i = 0; i < allLists.size(); ++i)
        {
            if (auto* list = allLists[i]; list != nullptr && ! list->isGrid)
            {
                initialListIndex = i;
                break;
            }
        }

        sidebarPanel.setSelectedIndex (initialListIndex);
        loadList (initialListIndex, allLists[initialListIndex]->pads.size(), allLists[initialListIndex]->isGrid);
    }
    else
    {
        sidebarPanel.setSelectedIndex (-1);
        loadList (-1, 0, true);
    }

    refreshStartupPlaylistDisplay();

    juce::Array<AudioFormatMigrationEntry> formatMigrationEntries;

    if (collectPadsNeedingFormatMigration (formatMigrationEntries))
        startBackgroundAudioFormatMigration (formatMigrationEntries);

    pendingGoTimer.reset();
    pendingGoPadIndex = -1;

    const bool selectedListIsBgm = (initialListIndex >= 0
                                    && initialListIndex < allLists.size()
                                    && allLists[initialListIndex] != nullptr
                                    && ! allLists[initialListIndex]->isGrid);
    const int selectedPadAtStartup = selectedPadIndices.isEmpty() ? -1 : selectedPadIndices.getFirst();
    logHotkeyTrace ("startup state list=" + juce::String (initialListIndex)
                    + " isBgm=" + juce::String (selectedListIsBgm ? 1 : 0)
                    + " pad=" + juce::String (selectedPadAtStartup)
                    + " play=0");

    constexpr int kStartupReassertDelayMs  = 900;
    constexpr int kStartupPlaybackGuardMs = 2200;
    startupInputGuardUntilMs = juce::Time::getMillisecondCounter() + kStartupPlaybackGuardMs;

    // Async load callback có thể ghi đè selection muộn; chốt lại sau startup (không phát nhạc).
    startupReassertTimer = std::make_unique<OneShotApplicationTimer>();
    startupReassertTimer->onFire = [safeThis = juce::Component::SafePointer<MainComponent> (this), initialListIndex]
    {
        if (safeThis == nullptr)
            return;

        if (initialListIndex < 0 || initialListIndex >= safeThis->allLists.size())
            return;

        auto* initialList = safeThis->allLists[initialListIndex];
        if (initialList == nullptr || initialList->pads.isEmpty())
            return;

        safeThis->activeListIndex = initialListIndex;
        safeThis->sidebarPanel.setSelectedIndex (initialListIndex);
        safeThis->selectedBgmIndex = 0;
        safeThis->selectedPadIndices.clear();
        safeThis->selectedPadIndices.add (0);
        safeThis->applyPadSelectionVisualState();

        if (auto* firstPad = initialList->pads[0])
            safeThis->inspectorPanel.selectPad (firstPad);

        safeThis->resized();
        logHotkeyTrace ("startup reassert selection list=" + juce::String (initialListIndex) + " pad=0");
    };
    startupReassertTimer->startMs (kStartupReassertDelayMs);

    startupGuardTimer = std::make_unique<OneShotApplicationTimer>();
    startupGuardTimer->onFire = [safeThis = juce::Component::SafePointer<MainComponent> (this)]
    {
        if (safeThis == nullptr)
            return;

        safeThis->isInitialLoading.store (false, std::memory_order_relaxed);
        safeThis->startupInputGuardUntilMs = 0;
        logHotkeyTrace ("startup playback guard ended");
        safeThis->finalizeStartupPlaylistUi();
        safeThis->saveApplicationState();
    };
    startupGuardTimer->startMs (kStartupPlaybackGuardMs);
}

void MainComponent::loadProject()
{
    loadApplicationState();
}

std::unique_ptr<juce::XmlElement> MainComponent::buildProjectXml()
{
    auto xml = std::make_unique<juce::XmlElement> ("ShowControlProject");
    xml->setAttribute ("version", "2.0");
    xml->setAttribute ("themeId", themePreferenceId);
    xml->setAttribute ("languageId", languagePreferenceIndex);
    xml->setAttribute ("languageScheme", "3way");
    xml->setAttribute ("hotkeyScopeMode", static_cast<int> (hotkeyScopeMode));
    if (projectDefaultNormalizeLufs)
        xml->setAttribute ("defaultNormalizeLufs", true);

    xml->setAttribute ("sidebarWidth", sidebarWidth);
    xml->setAttribute ("inspectorWidth", inspectorWidth);
    xml->setAttribute ("sidebarVisible", sidebarVisible);
    xml->setAttribute ("inspectorVisible", inspectorVisible);

    for (int i = 0; i < allLists.size(); ++i)
    {
        auto* list = allLists[i];

        if (list == nullptr)
            continue;

        auto* listElem = xml->createNewChildElement ("List");
        listElem->setAttribute ("name", sidebarPanel.getListName (i));
        listElem->setAttribute ("isGrid", list->isGrid);
        listElem->setAttribute ("isLooping", list->isLooping);

        if (list->isLocked)
            listElem->setAttribute ("isLocked", true);

        if (! showcontrol::colours::isDefaultTagColour (list->themeColour))
            listElem->setAttribute ("themeColour", (int) list->themeColour.getARGB());

        if (list->isGrid)
        {
            listElem->setAttribute ("useCueListPanel", list->useCueListPanel);
            listElem->setAttribute ("clickPadToTrigger", list->clickPadToTrigger);
            listElem->setAttribute ("autoArmOnSelect", list->autoArmOnSelect);
            syncCueMetadataFromPads (*list);
        }

        for (int pi = 0; pi < list->pads.size(); ++pi)
        {
            auto* pad = list->pads[pi];

            if (pad == nullptr)
                continue;

            auto* padElem = listElem->createNewChildElement ("Pad");
            padElem->setAttribute ("index", pi);

            if (pad->hasAudioFile())
            {
                padElem->setAttribute ("file", pad->getFilePath());

                if (! list->isGrid && pad->isLooping())
                    padElem->setAttribute ("loopTrack", true);

                writePadProjectState (*padElem, *pad);

                if (list->isGrid && pi < list->cueMeta.size())
                    writeCueMetaToPadElem (*padElem, list->cueMeta.getReference (pi));
            }
            else if (list->isGrid && pi < list->cueMeta.size())
            {
                writeCueMetaToPadElem (*padElem, list->cueMeta.getReference (pi));
            }
        }
    }

    // Lưu cấu hình bus routing
    auto* routingElem = xml->createNewChildElement ("AudioRouting");
    for (int b = 0; b < MultiOutputAudioCallback::kMaxBuses; ++b)
    {
        auto* busElem = routingElem->createNewChildElement ("Bus");
        busElem->setAttribute ("index", b);
        busElem->setAttribute ("name", multiOutputCallback.getBusName (b));
        busElem->setAttribute ("gain", (double) multiOutputCallback.getBusGain (b));
        if (b == 0)
        {
            if (! multiOutputCallback.getMasterLimiterEnabled())
                busElem->setAttribute ("limiter", false);
            const float thr = multiOutputCallback.getMasterLimiterThresholdDb();
            if (std::abs (thr + 1.0f) > 0.05f)
                busElem->setAttribute ("limiterThreshold", (double) thr);
            const float rel = multiOutputCallback.getMasterLimiterReleaseMs();
            if (std::abs (rel - 80.0f) > 0.5f)
                busElem->setAttribute ("limiterRelease", (double) rel);
        }
    }

    hotkeyManager.saveToXml (*xml);
    return xml;
}

void MainComponent::handleAsyncUpdate()
{
    executeActualDiskWriteJSON();
}

void MainComponent::executeActualDiskWriteJSON()
{
    StateOperationScope stateGuard (isPerformingStateOperation);

    if (! stateGuard.enteredSuccessfully())
        return;

    saveApplicationStateInternal();
}

void MainComponent::saveApplicationState()
{
    if (isOperatingState())
        return;

    triggerAsyncUpdate();
}

void MainComponent::saveApplicationStateInternal()
{
    if (! stateIoLock.enter (3000))
    {
        std::cout << "[CONFIG] [WARN] Cannot acquire state lock for save." << std::endl;
        return;
    }
    InterProcessUnlockScope ioUnlock (stateIoLock);

    const auto configFile = showcontrol::state::getCanonicalConfigFile();

    if (! showcontrol::state::ensureConfigParentDirectory (configFile))
        return;

    auto xml = buildProjectXml();

    if (xml == nullptr)
        return;

    if (! writeXmlAtomically (getProjectFile(), *xml))
    {
        std::cout << "[CONFIG] [ERROR] Atomic XML write failed." << std::endl;
        return;
    }

    if (const char* failpoint = std::getenv ("SHOWCUE_FAIL_STATE_SAVE_AFTER_XML"))
    {
        juce::ignoreUnused (failpoint);
        std::cout << "[CONFIG] [TEST] Save aborted by failpoint SHOWCUE_FAIL_STATE_SAVE_AFTER_XML." << std::endl;
        return;
    }

    juce::DynamicObject::Ptr root (new juce::DynamicObject());
    root->setProperty ("projectSchema", showcontrol::persistence::kProjectSchemaVersion);
    root->setProperty ("appTheme", themePreferenceId);
    root->setProperty ("appLanguage", languagePreferenceIndex);
    root->setProperty ("projectXml", xml->toString());
    root->setProperty ("savedAtMs", juce::Time::getMillisecondCounterHiRes());
    root->setProperty ("configPath", configFile.getFullPathName());
    root->setProperty ("playlist", buildPlaylistJson());

    if (! showcontrol::state::hardFlushJsonConfig (juce::var (root.get())))
    {
        std::cout << "[CONFIG] [ERROR] Atomic JSON write failed." << std::endl;
        return;
    }

    showcontrol::persistence::snapshotProjectAfterSave (configFile, getProjectFile());
}

void MainComponent::maybeRunAutosave()
{
    if (isOperatingState() || ! deferredStartupComplete)
        return;

    const auto now = juce::Time::getMillisecondCounter();

    if (lastAutosaveAtMs != 0
        && now - lastAutosaveAtMs < (juce::uint32) showcontrol::persistence::kAutosaveIntervalMs)
        return;

    lastAutosaveAtMs = now;
    saveApplicationStateInternal();
}

bool MainComponent::triggerExternalGo (int listIndex, int padIndex)
{
    return triggerExternalSyncGo (listIndex, padIndex, 0.0f);
}

bool MainComponent::triggerExternalSyncGo (int listIndex, int padIndex, float preWaitMs)
{
    const bool wasSyncing = syncApplying.exchange (true);

    struct SyncScope
    {
        std::atomic<bool>& flag;
        bool restore;
        ~SyncScope() { if (restore) flag.store (false); }
    } scope { syncApplying, ! wasSyncing };

    if (listIndex >= 0 && listIndex < allLists.size() && listIndex != activeListIndex)
    {
        auto* list = allLists[listIndex];

        if (list != nullptr)
        {
            sidebarPanel.setSelectedIndex (listIndex);
            loadList (listIndex, sidebarPanel.getListTrackCount (listIndex), list->isGrid);
        }
    }

    if (preWaitMs > 1.0f
        && listIndex >= 0 && listIndex < allLists.size()
        && listIndex == activeListIndex)
    {
        return triggerCueGo (padIndex, true);
    }

    HotkeyBinding binding;
    binding.padListIndex = listIndex;
    binding.padIndex     = padIndex;
    return triggerPadFromHotkey (binding, true);
}

void MainComponent::triggerGlobalStopAll()
{
    if (shouldBlockLocalPlaybackCommand())
        return;

    for (auto* list : allLists)
    {
        if (list == nullptr)
            continue;

        for (auto* pad : list->pads)
        {
            if (pad != nullptr)
                pad->triggerStop();
        }
    }

    broadcastSyncIfPrimary ([] (showcontrol::backup::ShowBackupSyncBroadcaster& b)
    {
        b.sendStopAll();
    });
}

void MainComponent::triggerGlobalPauseAll()
{
    if (shouldBlockLocalPlaybackCommand())
        return;

    for (auto* list : allLists)
    {
        if (list == nullptr || ! list->isGrid)
            continue;

        for (auto* pad : list->pads)
        {
            if (pad != nullptr && pad->isPlaying())
                pad->triggerPause();
        }
    }

    broadcastSyncIfPrimary ([] (showcontrol::backup::ShowBackupSyncBroadcaster& b)
    {
        b.sendPauseAll();
    });
}

bool MainComponent::shouldBlockLocalPlaybackCommand() const noexcept
{
    if (syncApplying.load (std::memory_order_acquire))
        return false;

    if (showcontrol::prefs::loadBackupRole() != (int) showcontrol::backup::Role::backup)
        return false;

    if (backupTakeoverActive)
        return false;

    return showcontrol::prefs::loadBackupFollowerLock();
}

void MainComponent::broadcastSyncIfPrimary (
    const std::function<void (showcontrol::backup::ShowBackupSyncBroadcaster&)>& action)
{
    if (syncApplying.load (std::memory_order_acquire))
        return;

    if (showcontrol::prefs::loadBackupRole() != (int) showcontrol::backup::Role::primary)
        return;

    if (backupBroadcaster == nullptr || ! backupBroadcaster->isConnected())
        return;

    action (*backupBroadcaster);
}

void MainComponent::setBackupTakeoverActive (bool active)
{
    backupTakeoverActive = active;
    updateBackupStatusLabel();

    if (showcontrol::prefs::loadBackupRole() == (int) showcontrol::backup::Role::backup
        && backupBroadcaster != nullptr
        && backupBroadcaster->isConnected())
    {
        backupBroadcaster->sendTakeover (active);
    }
}

void MainComponent::handleSyncPanic()
{
    if (showcontrol::prefs::loadBackupRole() != (int) showcontrol::backup::Role::backup)
        return;

    const bool wasSyncing = syncApplying.exchange (true);
    struct SyncScope
    {
        std::atomic<bool>& flag;
        bool restore;
        ~SyncScope() { if (restore) flag.store (false); }
    } scope { syncApplying, ! wasSyncing };

    executePanicFadeAllLocked();
}

void MainComponent::handleSyncGo (int listIndex, int padIndex, float preWaitMs)
{
    if (showcontrol::prefs::loadBackupRole() != (int) showcontrol::backup::Role::backup)
        return;

    triggerExternalSyncGo (listIndex, padIndex, preWaitMs);
}

void MainComponent::handleSyncStopAll()
{
    if (showcontrol::prefs::loadBackupRole() != (int) showcontrol::backup::Role::backup)
        return;

    const bool wasSyncing = syncApplying.exchange (true);
    struct SyncScope
    {
        std::atomic<bool>& flag;
        bool restore;
        ~SyncScope() { if (restore) flag.store (false); }
    } scope { syncApplying, ! wasSyncing };

    for (auto* list : allLists)
    {
        if (list == nullptr)
            continue;

        for (auto* pad : list->pads)
        {
            if (pad != nullptr)
                pad->triggerStop();
        }
    }
}

void MainComponent::handleSyncPauseAll()
{
    if (showcontrol::prefs::loadBackupRole() != (int) showcontrol::backup::Role::backup)
        return;

    const bool wasSyncing = syncApplying.exchange (true);
    struct SyncScope
    {
        std::atomic<bool>& flag;
        bool restore;
        ~SyncScope() { if (restore) flag.store (false); }
    } scope { syncApplying, ! wasSyncing };

    for (auto* list : allLists)
    {
        if (list == nullptr || ! list->isGrid)
            continue;

        for (auto* pad : list->pads)
        {
            if (pad != nullptr && pad->isPlaying())
                pad->triggerPause();
        }
    }
}

void MainComponent::handleSyncStopCue (int listIndex, int padIndex)
{
    if (showcontrol::prefs::loadBackupRole() != (int) showcontrol::backup::Role::backup)
        return;

    const bool wasSyncing = syncApplying.exchange (true);
    struct SyncScope
    {
        std::atomic<bool>& flag;
        bool restore;
        ~SyncScope() { if (restore) flag.store (false); }
    } scope { syncApplying, ! wasSyncing };

    if (listIndex >= 0 && listIndex < allLists.size() && listIndex != activeListIndex)
    {
        auto* list = allLists[listIndex];

        if (list != nullptr)
        {
            sidebarPanel.setSelectedIndex (listIndex);
            loadList (listIndex, sidebarPanel.getListTrackCount (listIndex), list->isGrid);
        }
    }

    if (listIndex == activeListIndex)
        triggerCueListStop (padIndex);
}

void MainComponent::handleSyncPauseCue (int listIndex, int padIndex)
{
    if (showcontrol::prefs::loadBackupRole() != (int) showcontrol::backup::Role::backup)
        return;

    const bool wasSyncing = syncApplying.exchange (true);
    struct SyncScope
    {
        std::atomic<bool>& flag;
        bool restore;
        ~SyncScope() { if (restore) flag.store (false); }
    } scope { syncApplying, ! wasSyncing };

    if (listIndex >= 0 && listIndex < allLists.size() && listIndex != activeListIndex)
    {
        auto* list = allLists[listIndex];

        if (list != nullptr)
        {
            sidebarPanel.setSelectedIndex (listIndex);
            loadList (listIndex, sidebarPanel.getListTrackCount (listIndex), list->isGrid);
        }
    }

    if (listIndex == activeListIndex)
        triggerCueListPause (padIndex);
}

void MainComponent::handleSyncHeartbeat (juce::uint32 /*sequence*/)
{
    if (showcontrol::prefs::loadBackupRole() == (int) showcontrol::backup::Role::backup)
    {
        lastPrimaryHeartbeatRxMs = juce::Time::getMillisecondCounter();
        updateBackupStatusLabel();
    }
}

void MainComponent::handleSyncTakeover (bool active)
{
    if (showcontrol::prefs::loadBackupRole() == (int) showcontrol::backup::Role::primary)
    {
        std::cout << "[BACKUP] Backup takeover " << (active ? "armed" : "released") << std::endl;
        updateBackupStatusLabel();
    }
}

int MainComponent::currentSyncViewMode() const noexcept
{
    const auto* list = getActiveListSafe();

    if (list == nullptr || ! list->isGrid)
        return -1;

    return list->useCueListPanel ? 1 : 0;
}

void MainComponent::broadcastSelectionSyncIfPrimary()
{
    if (syncApplying.load (std::memory_order_acquire))
        return;

    if (showcontrol::prefs::loadBackupRole() != (int) showcontrol::backup::Role::primary)
        return;

    if (backupBroadcaster == nullptr || ! backupBroadcaster->isConnected())
        return;

    if (activeListIndex < 0)
        return;

    const int padIndex  = selectedBgmIndex;
    const int viewMode  = currentSyncViewMode();
    juce::Array<int> multi = selectedPadIndices;

    if (multi.isEmpty() && padIndex >= 0)
        multi.add (padIndex);

    if (lastBroadcastSelectionList == activeListIndex
        && lastBroadcastSelectionPad == padIndex
        && lastBroadcastSelectionView == viewMode
        && lastBroadcastSelectionMulti == multi)
    {
        return;
    }

    lastBroadcastSelectionList = activeListIndex;
    lastBroadcastSelectionPad  = padIndex;
    lastBroadcastSelectionView = viewMode;
    lastBroadcastSelectionMulti = multi;

    backupBroadcaster->sendSelection (activeListIndex, padIndex, viewMode, multi);
}

void MainComponent::applySyncedSelection (int listIndex,
                                          int padIndex,
                                          int viewMode,
                                          const juce::Array<int>& multiIndices)
{
    if (listIndex < 0 || listIndex >= allLists.size())
        return;

    const bool wasSyncing = syncApplying.exchange (true);
    struct SyncScope
    {
        std::atomic<bool>& flag;
        bool restore;
        ~SyncScope() { if (restore) flag.store (false); }
    } scope { syncApplying, ! wasSyncing };

    auto* listMeta = allLists[listIndex];

    if (listMeta == nullptr)
        return;

    if (listIndex != activeListIndex)
    {
        sidebarPanel.setSelectedIndex (listIndex);
        loadList (listIndex, sidebarPanel.getListTrackCount (listIndex), listMeta->isGrid);
    }

    if (viewMode >= 0 && listMeta->isGrid)
    {
        const bool wantPadMode = (viewMode == 0);
        const bool hasPadMode  = ! listMeta->useCueListPanel;

        if (wantPadMode != hasPadMode)
            setPlayoutModeInternal (wantPadMode, false);
    }

    juce::Array<int> indices = multiIndices;

    if (indices.isEmpty() && padIndex >= 0)
        indices.add (padIndex);

    indices.sort();

    for (int i = indices.size(); --i > 0;)
    {
        if (indices[i] == indices[i - 1])
            indices.remove (i);
    }

    selectedPadIndices = indices;

    if (padIndex >= 0)
        selectedBgmIndex = padIndex;
    else if (! indices.isEmpty())
        selectedBgmIndex = indices.getLast();
    else
        selectedBgmIndex = -1;

    applyPadSelectionVisualState();

    if (cueListPanel != nullptr && listMeta->isGrid && listMeta->useCueListPanel)
    {
        if (indices.size() > 1)
            cueListPanel->setSelectedIndices (indices);
        else
            cueListPanel->setSelectedIndex (selectedBgmIndex);
    }

    if (activeListIndex == listIndex
        && juce::isPositiveAndBelow (selectedBgmIndex, listMeta->pads.size()))
    {
        if (auto* pad = listMeta->pads[selectedBgmIndex])
            presentPadInInspector (pad);
    }

    updateMainDeskDisplay();
}

void MainComponent::handleSyncSelection (int listIndex,
                                         int padIndex,
                                         int viewMode,
                                         const juce::Array<int>& multiIndices)
{
    if (showcontrol::prefs::loadBackupRole() != (int) showcontrol::backup::Role::backup)
        return;

    if (backupTakeoverActive)
        return;

    applySyncedSelection (listIndex, padIndex, viewMode, multiIndices);
}

void MainComponent::updateBackupStatusLabel()
{
    const int role = showcontrol::prefs::loadBackupRole();

    if (role == (int) showcontrol::backup::Role::standalone)
    {
        masterDeckPanel.setBackupRoleStatusText ({});
        return;
    }

    juce::String text;

    if (role == (int) showcontrol::backup::Role::primary)
    {
        text = showcontrol::localization::tr (u8"Máy chính");
        const auto peers = showcontrol::prefs::loadBackupPeerHosts();

        if (peers.size() == 1)
            text += " \u2192 " + peers[0];
        else if (peers.size() > 1)
            text += " \u2192 " + juce::String (peers.size()) + " "
                   + showcontrol::localization::tr (u8"máy phụ");
    }
    else
    {
        text = backupTakeoverActive
             ? showcontrol::localization::tr (u8"Máy phụ — TAKEOVER")
             : showcontrol::localization::tr (u8"Máy phụ — Follower");

        const auto now = juce::Time::getMillisecondCounter();

        if (lastPrimaryHeartbeatRxMs > 0
            && now - lastPrimaryHeartbeatRxMs > (juce::uint32) showcontrol::backup::kHeartbeatStaleThresholdMs)
        {
            text += " · " + showcontrol::localization::tr (u8"Mất kết nối Primary");
        }
    }

    masterDeckPanel.setBackupRoleStatusText (text);
}

void MainComponent::scanLanPeersAsync (
    int wantRole,
    std::function<void (const juce::Array<showcontrol::backup::LanPeerInfo>&)> onDone)
{
    const int syncPort = showcontrol::prefs::loadBackupSyncPort();

    std::thread ([wantRole, syncPort, onDone = std::move (onDone)]() mutable
    {
        const auto peers = showcontrol::backup::scanLanPeers (wantRole, syncPort);

        juce::MessageManager::callAsync ([peers, onDone = std::move (onDone)]() mutable
        {
            if (onDone)
                onDone (peers);
        });
    }).detach();
}

void MainComponent::startBackupDiscoveryResponder()
{
    stopBackupDiscoveryResponder();

    const int role = showcontrol::prefs::loadBackupRole();

    if (role == (int) showcontrol::backup::Role::standalone)
        return;

    const int discoveryPort = showcontrol::backup::discoveryPortForSyncPort (
        showcontrol::prefs::loadBackupSyncPort());

    backupDiscoverySocket = std::make_unique<juce::DatagramSocket> (true);
    backupDiscoverySocket->setEnablePortReuse (true);

    if (backupDiscoverySocket->bindToPort (discoveryPort) <= 0)
    {
        backupDiscoverySocket.reset();
        std::cout << "[BACKUP] [WARN] Cannot bind discovery UDP " << discoveryPort << std::endl;
    }
}

void MainComponent::stopBackupDiscoveryResponder()
{
    backupDiscoverySocket.reset();
}

void MainComponent::pollBackupDiscoverySocket()
{
    if (backupDiscoverySocket == nullptr)
        return;

    const int ourRole = showcontrol::prefs::loadBackupRole();

    if (ourRole == (int) showcontrol::backup::Role::standalone)
        return;

    for (int safety = 0; safety < 8; ++safety)
    {
        if (backupDiscoverySocket->waitUntilReady (true, 0) <= 0)
            break;

        char buffer[512] = {};
        juce::String senderHost;
        int senderPort = 0;
        const int bytes = backupDiscoverySocket->read (buffer, (int) sizeof (buffer) - 1, false,
                                                       senderHost, senderPort);

        if (bytes <= 0)
            continue;

        buffer[bytes] = '\0';

        int wantRole  = 0;
        int replyPort = 0;

        if (! showcontrol::backup::parseDiscoverProbe (juce::String::fromUTF8 (buffer),
                                                       wantRole, replyPort))
            continue;

        if (! showcontrol::backup::roleMatchesDiscoverRequest (ourRole, wantRole))
            continue;

        const auto announce = showcontrol::backup::makeDiscoverAnnounce (
            ourRole,
            juce::SystemStats::getComputerName(),
            showcontrol::prefs::loadBackupSyncPort());

        backupDiscoverySocket->write (senderHost, replyPort, announce.toRawUTF8(),
                                       (int) announce.getNumBytesAsUTF8());
    }
}

void MainComponent::tickBackupHeartbeat()
{
    const int role = showcontrol::prefs::loadBackupRole();
    const auto now = juce::Time::getMillisecondCounter();

    if (role == (int) showcontrol::backup::Role::backup)
    {
        if (lastPrimaryHeartbeatRxMs > 0
            && now - lastPrimaryHeartbeatRxMs > (juce::uint32) showcontrol::backup::kHeartbeatStaleThresholdMs)
        {
            updateBackupStatusLabel();
        }

        return;
    }

    if (role != (int) showcontrol::backup::Role::primary)
        return;

    if (lastHeartbeatTickMs != 0
        && now - lastHeartbeatTickMs < (juce::uint32) showcontrol::backup::kHeartbeatIntervalMs)
        return;

    lastHeartbeatTickMs = now;

    if (backupBroadcaster != nullptr && backupBroadcaster->isConnected())
        backupBroadcaster->sendHeartbeat (++heartbeatSendSeq);
}

void MainComponent::restartBackupSync()
{
    oscListener.reset();
    backupBroadcaster.reset();
    stopBackupDiscoveryResponder();

    const int role     = showcontrol::prefs::loadBackupRole();
    const int port     = showcontrol::prefs::loadBackupSyncPort();
    const auto peers   = showcontrol::prefs::loadBackupPeerHosts();
    bool listenEnabled = showcontrol::prefs::loadOscEnabled();

    if (const char* env = std::getenv ("SHOWCUE_OSC_ENABLE"))
        listenEnabled = (env[0] != '0' && env[0] != '\0');

    if (role != (int) showcontrol::backup::Role::standalone)
        listenEnabled = true;

    if (role == (int) showcontrol::backup::Role::primary)
    {
        backupBroadcaster = std::make_unique<showcontrol::backup::ShowBackupSyncBroadcaster>();

        if (! backupBroadcaster->configure (peers, port))
            std::cout << "[BACKUP] [WARN] Primary: no backup peer IP configured." << std::endl;
        else
            std::cout << "[BACKUP] Primary broadcasting to " << peers.size()
                      << " peer(s) on port " << port << std::endl;
    }

    if (role == (int) showcontrol::backup::Role::backup)
    {
        lastPrimaryHeartbeatRxMs = 0;

        if (peers.size() > 0)
        {
            backupBroadcaster = std::make_unique<showcontrol::backup::ShowBackupSyncBroadcaster>();
            backupBroadcaster->configure (peers[0], port);
        }
    }

    startBackupDiscoveryResponder();

    if (! listenEnabled)
    {
        updateBackupStatusLabel();
        return;
    }

    showcontrol::osc::ShowOscCallbacks callbacks;
    const bool isBackupRole = (role == (int) showcontrol::backup::Role::backup);

    callbacks.onPanic = [this, isBackupRole]
    {
        if (isBackupRole)
            handleSyncPanic();
        else
            triggerGlobalPanicFadeAll();
    };
    callbacks.onGo = [this, isBackupRole] (int listIndex, int padIndex, float preWaitMs)
    {
        if (isBackupRole)
            handleSyncGo (listIndex, padIndex, preWaitMs);
        else
            triggerExternalSyncGo (listIndex, padIndex, preWaitMs);
    };
    callbacks.onStopAll = [this, isBackupRole]
    {
        if (isBackupRole)
            handleSyncStopAll();
        else
            triggerGlobalStopAll();
    };
    callbacks.onPauseAll = [this, isBackupRole]
    {
        if (isBackupRole)
            handleSyncPauseAll();
        else
            triggerGlobalPauseAll();
    };
    callbacks.onStopCue = [this, isBackupRole] (int listIndex, int padIndex)
    {
        if (isBackupRole)
            handleSyncStopCue (listIndex, padIndex);
        else if (listIndex == activeListIndex)
            triggerCueListStop (padIndex);
    };
    callbacks.onPauseCue = [this, isBackupRole] (int listIndex, int padIndex)
    {
        if (isBackupRole)
            handleSyncPauseCue (listIndex, padIndex);
        else if (listIndex == activeListIndex)
            triggerCueListPause (padIndex);
    };
    callbacks.onHeartbeat = [this] (juce::uint32 sequence)
    {
        handleSyncHeartbeat (sequence);
    };
    callbacks.onTakeover = [this] (bool active)
    {
        handleSyncTakeover (active);
    };
    callbacks.onSelection = [this] (int listIndex, int padIndex, int viewMode, const juce::Array<int>& multiIndices)
    {
        handleSyncSelection (listIndex, padIndex, viewMode, multiIndices);
    };

    auto listener = std::make_unique<showcontrol::osc::ShowOscListener> (callbacks);

    if (! listener->start (port))
    {
        std::cout << "[BACKUP] [WARN] Cannot bind UDP port " << port << std::endl;
        updateBackupStatusLabel();
        return;
    }

    std::cout << "[BACKUP] Listening on UDP port " << port << std::endl;
    oscListener = std::move (listener);
    updateBackupStatusLabel();
}


void MainComponent::exportProjectShowcuePackage()
{
    auto xml = buildProjectXml();

    if (xml == nullptr)
        return;

    juce::DynamicObject::Ptr root (new juce::DynamicObject());
    root->setProperty ("projectSchema", showcontrol::persistence::kProjectSchemaVersion);
    root->setProperty ("appTheme", themePreferenceId);
    root->setProperty ("appLanguage", languagePreferenceIndex);
    root->setProperty ("projectXml", xml->toString());
    root->setProperty ("savedAtMs", juce::Time::getMillisecondCounterHiRes());
    root->setProperty ("playlist", buildPlaylistJson());

    const juce::String jsonText = juce::JSON::toString (juce::var (root.get()), true);

    mainFileChooser = std::make_unique<juce::FileChooser> (
        showcontrol::localization::tr (u8"Xuất file cấu hình"),
        juce::File::getSpecialLocation (juce::File::userDocumentsDirectory),
        "*.showcue");

    const int chooserFlags = juce::FileBrowserComponent::saveMode
                           | juce::FileBrowserComponent::canSelectFiles;

    mainFileChooser->launchAsync (chooserFlags, [this, jsonText, xmlText = xml->toString()] (const juce::FileChooser& fc)
    {
        auto dest = fc.getResult();

        if (dest == juce::File())
            return;

        if (! dest.hasFileExtension ("showcue"))
            dest = dest.withFileExtension ("showcue");

        if (! showcontrol::persistence::exportShowcuePackage (dest, jsonText, xmlText))
        {
            juce::AlertWindow::showMessageBoxAsync (juce::AlertWindow::WarningIcon,
                showcontrol::localization::tr (u8"Xuất file cấu hình"),
                showcontrol::localization::tr (u8"Không ghi được file cấu hình."),
                showcontrol::localization::tr (u8"Đóng"));
            return;
        }

        juce::AlertWindow::showMessageBoxAsync (juce::AlertWindow::InfoIcon,
            showcontrol::localization::tr (u8"Xuất file cấu hình"),
            showcontrol::localization::tr (u8"Đã lưu: ") + dest.getFullPathName(),
            showcontrol::localization::tr (u8"Đóng"));
    });
}

void MainComponent::importProjectShowcuePackage()
{
    if (isOperatingState())
    {
        juce::AlertWindow::showMessageBoxAsync (juce::AlertWindow::WarningIcon,
            showcontrol::localization::tr (u8"Nhập file cấu hình"),
            showcontrol::localization::tr (u8"Không thể nhập khi đang phát cue. Dừng phát trước."),
            showcontrol::localization::tr (u8"Đóng"));
        return;
    }

    mainFileChooser = std::make_unique<juce::FileChooser> (
        showcontrol::localization::tr (u8"Nhập file cấu hình"),
        juce::File::getSpecialLocation (juce::File::userDocumentsDirectory),
        "*.showcue");

    const int chooserFlags = juce::FileBrowserComponent::openMode
                           | juce::FileBrowserComponent::canSelectFiles;

    mainFileChooser->launchAsync (chooserFlags, [this] (const juce::FileChooser& fc)
    {
        const auto source = fc.getResult();

        if (source == juce::File())
            return;

        const auto package = showcontrol::persistence::readShowcuePackage (source);

        if (! package.success)
        {
            juce::AlertWindow::showMessageBoxAsync (juce::AlertWindow::WarningIcon,
                showcontrol::localization::tr (u8"Nhập file cấu hình"),
                importPackageErrorMessage (package.error),
                showcontrol::localization::tr (u8"Đóng"));
            return;
        }

        const juce::String confirmMessage =
            showcontrol::localization::tr (u8"Ghi đè cấu hình hiện tại bằng file:\n")
            + source.getFileName()
            + "\n\n"
            + showcontrol::localization::tr (u8"Bản hiện tại sẽ được sao lưu vào thư mục backups/.");

        juce::AlertWindow::showAsync (
            juce::MessageBoxOptions()
                .withIconType (juce::MessageBoxIconType::WarningIcon)
                .withTitle (showcontrol::localization::tr (u8"Nhập file cấu hình"))
                .withMessage (confirmMessage)
                .withButton (showcontrol::localization::tr (u8"Nhập"))
                .withButton (showcontrol::localization::tr (u8"Hủy")),
            [safeThis = juce::Component::SafePointer<MainComponent> (this), package] (int result)
            {
                if (safeThis == nullptr || result != 0)
                    return;

                if (! showcontrol::persistence::installImportedConfiguration (package.configJson,
                                                                              package.projectXml))
                {
                    juce::AlertWindow::showMessageBoxAsync (juce::AlertWindow::WarningIcon,
                        showcontrol::localization::tr (u8"Nhập file cấu hình"),
                        showcontrol::localization::tr (u8"Không ghi được cấu hình."),
                        showcontrol::localization::tr (u8"Đóng"));
                    return;
                }

                safeThis->forceStopActiveAudioForSafety();
                safeThis->loadApplicationState();

                juce::AlertWindow::showMessageBoxAsync (juce::AlertWindow::InfoIcon,
                    showcontrol::localization::tr (u8"Nhập file cấu hình"),
                    showcontrol::localization::tr (u8"Đã nhập cấu hình. Kiểm tra media nếu đường dẫn file âm thanh khác máy nguồn."),
                    showcontrol::localization::tr (u8"Đóng"));
            });
    });
}

void MainComponent::triggerSave()
{
    if (isOperatingState())
        return;

    triggerAsyncUpdate();
}

void MainComponent::saveProject()
{
    triggerSave();
}