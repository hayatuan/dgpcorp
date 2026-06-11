#include "MainComponent.h"
#include <functional>
#include <memory>
#include "GlobalPreferencesDialog.h"
#include "ShowLocalization.h"
#include "ShowAboutDialog.h"
#include "ShowUpdateChecker.h"
#include "ShowWaveformCache.h"
#include "ShowControlMacWindow.h"
#include "ShowAppPreferences.h"
#include "ShowApplicationState.h"

namespace
{
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

void forceLookAndFeelRefreshRecursively (juce::Component& root)
{
    root.lookAndFeelChanged();
    root.repaint();

    for (int i = 0; i < root.getNumChildComponents(); ++i)
        if (auto* child = root.getChildComponent (i))
            forceLookAndFeelRefreshRecursively (*child);
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

// ── Spacebar global debounce (file-scope, không nằm trong header)
// Msg thread only: chỉ dùng atomic timestamp + không ảnh hưởng RT audio thread.
static std::atomic<juce::uint32> g_lastGlobalSpacebarPressTime { 0 };
static std::atomic<juce::uint32> g_gateQuietUntilMs { 0 };
static constexpr juce::uint32 kGlobalSpacebarDebounceMs = 350;

/** JUCE 8 không expose isKeyRepeat — lọc burst KeyDown <45ms cùng keyCode (OS auto-repeat). */
bool swallowLikelyOsKeyRepeat (const juce::KeyPress& key) noexcept
{
    static int lastCode = 0;
    static juce::uint32 lastMs = 0;
    const int code = key.getKeyCode();
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
    constexpr int kMaxCuePadsPerList = 48;

    void showCueListCapacityAlert()
    {
        juce::AlertWindow::showMessageBoxAsync (
            juce::AlertWindow::WarningIcon,
            "Giới hạn danh sách CUE",
            "Mỗi bộ Nhạc CUE chỉ cho phép chứa tối đa 48 ô PAD biểu diễn để đảm bảo hiệu năng và bố cục hiển thị.",
            "Đã hiểu");
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
class ScrollableContainer : public juce::Component
{
public:
    enum class EmptyListHint
    {
        none,
        cueGrid,
        bgmRows
    };

    ScrollableContainer() {}

    std::function<void(const juce::MouseEvent&)> onBackgroundRightClick;
    std::function<void(const juce::MouseEvent&)> onBackgroundMouseDown;
    std::function<void(const juce::MouseEvent&)> onBackgroundMouseDrag;
    std::function<void(const juce::MouseEvent&)> onBackgroundMouseUp;

    void mouseDown (const juce::MouseEvent& e) override {
        if (e.mods.isRightButtonDown() && onBackgroundRightClick)
            onBackgroundRightClick(e);
        else if (onBackgroundMouseDown)
            onBackgroundMouseDown (e);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (onBackgroundMouseDrag)
            onBackgroundMouseDrag (e);
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (onBackgroundMouseUp)
            onBackgroundMouseUp (e);
    }

    void setDarkMode (bool dark)
    {
        isDarkMode = dark;
        repaint();
    }

    void setEmptyListHint (EmptyListHint hint)
    {
        if (emptyListHint != hint)
        {
            emptyListHint = hint;
            repaint();
        }
    }

    void paint (juce::Graphics& g) override
    {
        const auto pal = ShowTheme::get (isDarkMode);
        g.fillAll (pal.centerBg);

        if (emptyListHint == EmptyListHint::none)
            return;

        g.setColour (pal.textMuted);
        g.setFont (ShowTheme::font (15.0f));

        const juce::String message = (emptyListHint == EmptyListHint::cueGrid)
            ? showcontrol::localization::tr (u8"Danh sách CUE trống. Hãy kéo thả file âm thanh vào đây để tự động cấu hình các ô PAD biểu diễn.")
            : showcontrol::localization::tr (u8"Danh sách BGM trống. Hãy kéo thả file nhạc nền vào phân vùng này để thiết lập danh sách phát.");

        g.drawFittedText (message,
                          getLocalBounds().reduced (32),
                          juce::Justification::centred,
                          4);
    }

private:
    bool isDarkMode = true;
    EmptyListHint emptyListHint = EmptyListHint::none;
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
        g.setFont (ShowTheme::font (15.0f));
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
    ListHeaderComponent() : isDarkMode(true) {}

    void setDarkMode(bool dark) { isDarkMode = dark; repaint(); }

    void paint (juce::Graphics& g) override {
        const auto pal = ShowTheme::get (isDarkMode);
        g.setColour (pal.panelElevated);
        g.fillRect (getLocalBounds());

        g.setColour (pal.border);
        g.drawHorizontalLine (getHeight() - 1, 0.0f, (float)getWidth());
        g.setColour (pal.textSecondary);

        g.setFont (ShowTheme::fontBold (13.0f));
        
        const auto titleRect     = showcontrol::bgmList::titleBounds (getWidth(), getHeight());
        const auto remainingRect = showcontrol::bgmList::timeRemainingBounds (getWidth(), getHeight());
        const auto totalRect     = showcontrol::bgmList::totalDurationBounds (getWidth(), getHeight());

        g.drawText (showcontrol::localization::tr (u8"TÊN BÀI HÁT BGM"), titleRect, juce::Justification::centredLeft);
        g.drawText (showcontrol::localization::tr (u8"CÒN LẠI"), remainingRect, juce::Justification::centred);
        g.drawText (showcontrol::localization::tr (u8"THỜI LƯỢNG"), totalRect, juce::Justification::centred);
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
        auto area = getLocalBounds().reduced (8, 6);
        constexpr int btnW = 52;
        constexpr int gap = 8;
        const int clusterW = btnW * 2 + gap;
        auto cluster = area.withSizeKeepingCentre (clusterW, area.getHeight());
        padModeBtn.setBounds (cluster.removeFromLeft (btnW));
        cluster.removeFromLeft (gap);
        cueListModeBtn.setBounds (cluster);
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

    for (int i = 0; i < kMaxPadSlots; ++i)
    {
        PadRealtimeSource* expected = nullptr;

        if (slots[(size_t) i].compare_exchange_strong (expected, src, std::memory_order_acq_rel))
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
    constexpr float kBoost = 1.60f;
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
                                 list->useCueListPanel, list->isLocked);
            sidebarPanel.setListLooping (i, list->isLooping);
        }
    }
}

void MainComponent::enterEmptyProjectState()
{
    if (soloPad != nullptr)
        setSoloPad (nullptr, false);

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
        cueListPanel->setVisible (false);

    if (scrollContent != nullptr)
    {
        if (auto* scrollContainer = dynamic_cast<ScrollableContainer*> (scrollContent.get()))
            scrollContainer->setEmptyListHint (ScrollableContainer::EmptyListHint::none);

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

    gridSizeSlider.setVisible (false);
    viewScroller.setVisible (false);

    if (playoutModeBar != nullptr)
        playoutModeBar->setVisible (false);

    if (emptyStatePanel != nullptr)
        emptyStatePanel->setVisible (true);

    resized();
    repaint();
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

    saveApplicationState();
}


MainComponent::~MainComponent()
{
    shutdownActiveTimers();
    updateChecker.reset();

    juce::Desktop::getInstance().removeDarkModeSettingListener (this);

    removeKeyListener (this);

    if (topLevelKeyListenerHost != nullptr)
    {
        topLevelKeyListenerHost->removeKeyListener (this);
        topLevelKeyListenerHost = nullptr;
    }

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
        inspectorPanel.selectPad (selected);

        if (activeListIndex >= 0 && activeListIndex < allLists.size())
        {
            auto* list = allLists[activeListIndex];

            if (list != nullptr && list->isGrid)
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

        if (isShowing())
            grabKeyboardFocus();
    };

    pad->isPlaybackCommandBlocked = [this] { return isPlaybackCommandBlocked(); };

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
            if (auto* p = safePad.getComponent())
            {
                if (p->isTransportActive())
                    syncUiToPlayingPad (p, false);
            }

            masterDeckPanel.refreshTransportLabels();
            masterDeckPanel.repaint();
            inspectorPanel.refreshTransportUi();
            refreshSidebarPlayingStatus();
            updateCuePlaybackIndicators();
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
            // Polyphony: không fade/stop pad khác khi một pad bắt đầu phát.
            syncUiToPlayingPad (starter, false);
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

    pad->onTrackNameChanged = [this] (SoundPad* p)
    {
        const int listIdx = findListIndexForPad (allLists, p);
        if (listIdx < 0 || listIdx >= allLists.size())
            return;

        auto* list = allLists[listIdx];
        if (list == nullptr)
            return;

        const int padIdx = list->pads.indexOf (p);
        if (padIdx < 0)
            return;

        if (padIdx < list->cueMeta.size())
            list->cueMeta.getReference (padIdx).name = p->getPadName();

        if (listIdx == activeListIndex)
        {
            if (inspectorPanel.getCurrentPad() == p)
                inspectorPanel.selectPad (p);

            refreshCueListPanel();
        }

        saveProject();
    };

    pad->onPadReorderBegin = [this] (SoundPad* p) { beginPadReorder (p); };
    pad->onPadReorderMove = [this] (SoundPad* p, juce::Point<int> pos) { juce::ignoreUnused (p); updatePadReorder (pos); };
    pad->onPadReorderEnd = [this] (SoundPad* p) { juce::ignoreUnused (p); endPadReorder(); };

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

            // Cập nhật InspectorPanel nếu pad đang được chọn — hiện metadata mới đọc xong
            if (inspectorPanel.getCurrentPad() == loadedPad)
            {
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

void MainComponent::normalizeActiveList (bool useLufs)
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

        p->setAutoNormalize (true);
        p->setNormalizeUseLufs (useLufs);
        p->requestNormalization();
    }

    saveProject();
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
    SoundPad* playingPad = findPlayingPadInActiveBgmList();

    if (playingPad == nullptr)
    {
        for (int i = 0; i < allLists.size(); ++i)
        {
            auto* current = allLists[i];
            if (current == nullptr)
                continue;

            for (auto* pad : current->pads)
            {
                if (pad != nullptr && pad->isTransportActive())
                {
                    playingPad = pad;
                    break;
                }
            }

            if (playingPad != nullptr)
                break;
        }
    }

    if (playingPad != nullptr && playingPad != lastUiSyncedPlayingPad)
        syncUiToPlayingPad (playingPad, true);
    else if (playingPad == nullptr)
        lastUiSyncedPlayingPad = nullptr;

    masterDeckPanel.setActivePad (playingPad);
    if (playingPad != nullptr)
    {
        playingPad->supplementBpmFromFileIfMissing();
        masterDeckPanel.setTrackMetadata (playingPad->getMetadata());
    }
    else
    {
        masterDeckPanel.setTrackMetadata ({});
    }
    refreshSidebarPlayingStatus();
    updateCuePlaybackIndicators();

    // Cập nhật trạng thái BGM transport buttons (play/stop icon, prev/next enabled).
    if (activeListIndex >= 0 && activeListIndex < allLists.size())
    {
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

    if (! padReorderActive)
        repaint();

    pushStageMonitorUpdate();
}

SoundPad* MainComponent::findAnyActivePlayingPad() const
{
    if (auto* pad = findPlayingPadInActiveBgmList())
        return pad;

    for (int i = 0; i < allLists.size(); ++i)
    {
        auto* list = allLists[i];
        if (list == nullptr)
            continue;

        for (auto* pad : list->pads)
        {
            if (pad != nullptr && pad->isTransportActive())
                return pad;
        }
    }

    return nullptr;
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
        stageMonitorWindow->removeKeyListener (this);
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
            {
                safeThis->stageMonitorWindow->removeKeyListener (safeThis.getComponent());
                safeThis->stageMonitorWindow.reset();
            }
        });
    });

    if (stageMonitorWindow == nullptr)
        return;

    stageMonitorWindow->addKeyListener (this);
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

        if (! playing->isFadeOutInProgress())
            playing->startFadeOut();

        targetPad->triggerPlay();
        syncUiToPlayingPad (targetPad, true);
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

    for (auto* pad : list->pads)
        if (pad != nullptr && pad != nextPad)
            pad->triggerStop();

    nextPad->triggerPlay();
    syncUiToPlayingPad (nextPad, true);
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

    for (auto* pad : list->pads)
        if (pad != nullptr && pad != prevPad)
            pad->triggerStop();

    prevPad->triggerPlay();
    syncUiToPlayingPad (prevPad, true);
}

void MainComponent::syncUiToPlayingPad (SoundPad* pad, bool scrollIntoView)
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

    pad->setThumbnailLoadAllowed (true);
    pad->setNormalizationLoadAllowed (true);
    pad->requestPreloadForPlayback();

    if (listIdx == activeListIndex && ! list->isGrid)
    {
        selectedBgmIndex = padIdx;
        selectedPadIndices.clear();
        selectedPadIndices.add (padIdx);
        applyPadSelectionVisualState();

        if (scrollIntoView)
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

        sidebarPanel.addSet (folderName, list->pads.size(), isGrid, false, list->isLocked);
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
                             allLists[i]->useCueListPanel, allLists[i]->isLocked);
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
                             allLists[i]->useCueListPanel, allLists[i]->isLocked);
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
                             false, allLists[i]->isLocked);
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
                             allLists[i]->useCueListPanel, allLists[i]->isLocked);
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

    auto* pad = list->pads.removeAndReturn (fromPadIdx);
    list->pads.insert (toPadIdx, pad);

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
        applyPadSelectionVisualState();
        resized();
    }

    rebuildSidebarFromAllLists();
    sidebarPanel.setSelectedIndex (listIdx);
    saveProject();
}

void MainComponent::safelyPreparePadForDeletion (SoundPad* pad)
{
    if (pad == nullptr)
        return;

    pad->cancelPendingAsyncWork();

    if (pad->isTransportActive())
        pad->triggerStop();

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
                                              this);
}

bool MainComponent::tryHandleDeleteOrBackspaceKey (const juce::KeyPress& key)
{
    const int keyCode = key.getKeyCode();

    if (keyCode != juce::KeyPress::deleteKey && keyCode != juce::KeyPress::backspaceKey)
        return false;

    if (auto* focused = juce::Component::getCurrentlyFocusedComponent())
    {
        if (dynamic_cast<juce::TextEditor*> (focused) != nullptr)
            return false;
    }

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

void MainComponent::deletePadsFromList (int listIdx, const juce::Array<int>& padIndices)
{
    if (listIdx < 0 || listIdx >= allLists.size() || padIndices.isEmpty())
        return;

    auto* list = allLists[listIdx];
    if (list == nullptr || list->isLocked)
        return;

    juce::SparseSet<int> selectedRows;
    for (auto idx : padIndices)
    {
        if (idx >= 0 && idx < list->pads.size())
            selectedRows.addRange (juce::Range<int> (idx, idx + 1));
    }

    if (selectedRows.isEmpty())
        return;

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

    // Chặt đứt mọi con trỏ UI trước khi remove — tránh heap-use-after-free trên waveform.
    inspectorPanel.selectPad (nullptr);
    masterDeckPanel.setActivePad (nullptr);
    lastUiSyncedPlayingPad = nullptr;

    const int selectionAnchor = selectedRows.getTotalRange().getStart();

    // Duyệt ngược từ dải cuối về đầu — xóa không làm lệch index còn lại.
    for (int rangeIdx = selectedRows.getNumRanges(); --rangeIdx >= 0;)
    {
        const auto range = selectedRows.getRange (rangeIdx);

        for (int index = range.getEnd() - 1; index >= range.getStart(); --index)
        {
            if (index < 0 || index >= list->pads.size())
                continue;

            if (auto* pad = list->pads[index])
            {
                safelyPreparePadForDeletion (pad);

                if (scrollContent != nullptr)
                    scrollContent->removeChildComponent (pad);
            }

            list->pads.remove (index);
        }
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
            selectedBgmIndex = juce::jlimit (0, list->pads.size() - 1, selectionAnchor);
            selectedPadIndices.add (selectedBgmIndex);

            if (auto* selectedPad = list->pads[selectedBgmIndex])
                inspectorPanel.selectPad (selectedPad);
        }
        else
        {
            selectedBgmIndex = 0;
        }

        applyPadSelectionVisualState();

        if (list->isGrid)
            updateCueGridUIFromData (*list);
        else
            resized();

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

    const int preferredPadW = juce::jlimit (56, 220, (int) std::round (gridSizeSlider.getValue()));
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

void MainComponent::layoutActiveListPads()
{
    if (activeListIndex < 0 || activeListIndex >= allLists.size() || scrollContent == nullptr)
        return;

    auto* current = allLists[activeListIndex];
    if (current == nullptr)
        return;

    const int mainViewWidth = viewScroller.getWidth();
    int totalHeightOfContent = 0;
    const int scrollY = viewScroller.getViewPositionY();
    const int viewH   = viewScroller.getViewHeight();
    constexpr int kPrefetchMarginPx = 220;
    const bool hasLoadedAudio = listHasLoadedAudio (*current);
    auto* scrollContainer = dynamic_cast<ScrollableContainer*> (scrollContent.get());

    if (scrollContainer != nullptr)
        scrollContainer->setEmptyListHint (ScrollableContainer::EmptyListHint::none);

    if (current->isGrid && current->useCueListPanel && cueListPanel != nullptr)
    {
        for (auto* p : current->pads)
            if (p != nullptr)
                p->setVisible (false);

        syncCueMetadataFromPads (*current);
        cueListPanel->setCues (current->cueMeta);
        cueListPanel->setSelectedIndex (selectedBgmIndex);
        cueListPanel->resetListScrollToTop();
        updateCuePlaybackIndicators();

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
                scrollContainer->setEmptyListHint (ScrollableContainer::EmptyListHint::cueGrid);

            scrollContent->setSize (mainViewWidth, viewScroller.getHeight());
            viewScroller.setViewPosition (0, 0);
            return;
        }

        const auto grid = getPadGridLayout (mainViewWidth, viewScroller.getHeight(), current->pads.size());

        for (int i = 0; i < current->pads.size(); ++i)
        {
            auto* p = current->pads[i];
            if (p == nullptr)
                continue;

            const bool isDragSource = padReorderActive && i == padReorderFromIndex;

            p->setRenderMode (true);
            p->setIsSelectedRow (isPadSelectedInActiveList (i));
            const auto cellBounds = grid.cellBounds (i);
            p->setBounds (cellBounds);
            p->setVisible (true);
            p->setAlpha (isDragSource ? 0.34f : 1.0f);

            const bool inPrefetchRange = (cellBounds.getBottom() >= scrollY - kPrefetchMarginPx)
                                           && (cellBounds.getY() <= scrollY + viewH + kPrefetchMarginPx);
            const bool selectedOrActive = isPadSelectedInActiveList (i)
                                              || (p->isPlaying() || p->isTransportActive());
            const bool allowThumb = selectedOrActive || inPrefetchRange;
            p->setThumbnailLoadAllowed (allowThumb);
            p->setNormalizationLoadAllowed (allowThumb);

            if (allowThumb && p->getFilePath().isNotEmpty())
                p->reloadWaveformThumbnail();
            totalHeightOfContent = std::max (totalHeightOfContent, p->getBottom() + 12);
        }
    }
    else
    {
        if (! hasLoadedAudio)
        {
            for (auto* p : current->pads)
                if (p != nullptr)
                    p->setVisible (false);

            if (scrollContainer != nullptr)
                scrollContainer->setEmptyListHint (ScrollableContainer::EmptyListHint::bgmRows);

            scrollContent->setSize (mainViewWidth, viewScroller.getHeight());
            viewScroller.setViewPosition (0, 0);
            return;
        }

        constexpr int rowHeight = 44;
        int y = 0;

        for (int i = 0; i < current->pads.size(); ++i)
        {
            auto* p = current->pads[i];
            if (p == nullptr)
                continue;

            const bool isDragSource = padReorderActive && i == padReorderFromIndex;

            p->setRenderMode (false);
            p->setIsSelectedRow (isPadSelectedInActiveList (i));
            p->setBounds (0, y, mainViewWidth, rowHeight);
            p->setVisible (true);
            p->setAlpha (isDragSource ? 0.30f : 1.0f);

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
        padReorderGhostImage = source->createComponentSnapshot (source->getLocalBounds(), true, 2.0f);
    else
        padReorderGhostImage = {};

    layoutActiveListPads();

    if (padReorderOverlay != nullptr)
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

    padReorderPointerPos = posInScrollContent;

    padReorderInsertIndex = hitTestPadInsertIndex (posInScrollContent);
}

void MainComponent::updatePadReorderOverlayBounds()
{
    if (padReorderOverlay == nullptr)
        return;

    padReorderOverlay->setBounds (viewScroller.getBounds());
    padReorderOverlay->toFront (false);
}

void MainComponent::endPadReorder()
{
    if (! padReorderActive || padReorderSource == nullptr)
    {
        cancelPadReorder();
        return;
    }

    padReorderInsertIndex = hitTestPadInsertIndex (padReorderPointerPos);

    const int listIdx = activeListIndex;
    const int from = padReorderFromIndex;
    const int target = padReorderInsertIndex;

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
        const int singleTo = juce::jlimit (0, juce::jmax (0, n - 1), to == n ? n - 1 : to);
        if (from == singleTo)
            return;

        movePadInList (listIdx, from, singleTo);
        return;
    }

    movePadsBlockInList (listIdx, dragSelection, to);
}

void MainComponent::cancelPadReorder()
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
        int lastBottom = 0;
        for (int i = 0; i < n; ++i)
            if (const auto* p = list->pads[i])
                lastBottom = juce::jmax (lastBottom, p->getBottom());

        if (lastBottom > 0 && local.y >= lastBottom - 2)
            return n;

        int bestIdx = 0;
        float bestDist2 = std::numeric_limits<float>::max();

        for (int i = 0; i < n; ++i)
        {
            const auto* p = list->pads[i];
            if (p == nullptr)
                continue;

            const auto centre = p->getBounds().getCentre().toFloat();
            const float d2 = centre.getDistanceSquaredFrom (local.toFloat());

            if (d2 < bestDist2)
            {
                bestDist2 = d2;
                bestIdx = i;
            }
        }

        return bestIdx;
    }

    if (scrollContent == nullptr)
        return 0;

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

    const int width = scrollContent != nullptr ? scrollContent->getWidth() : viewScroller.getWidth();
    const int n = list->pads.size();
    const int target = juce::jlimit (0, n, padReorderInsertIndex);

    if (target == n)
    {
        if (n > 0)
        {
            if (auto* last = list->pads[n - 1])
                return { 0, last->getBottom(), width, 0 };
        }
        return {};
    }

    if (auto* p = list->pads[target])
        return { 0, p->getY(), width, 0 };

    return {};
}

juce::Rectangle<int> MainComponent::getGridGapCellBounds() const
{
    if (activeListIndex < 0 || activeListIndex >= allLists.size() || scrollContent == nullptr)
        return {};

    const auto* list = allLists[activeListIndex];
    if (list == nullptr || ! list->isGrid)
        return {};

    const int n = list->pads.size();
    const auto grid = getPadGridLayout (scrollContent->getWidth(), viewScroller.getHeight(), n);
    const int target = juce::jlimit (0, n, padReorderInsertIndex);

    if (target == n && n > 0)
    {
        const auto lastCell = grid.cellBounds (n - 1);
        return lastCell.translated (grid.padW + grid.gap, 0);
    }

    return grid.cellBounds (juce::jlimit (0, juce::jmax (0, n - 1), target));
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

    if (! padReorderActive || padReorderInsertIndex < 0)
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

    if (! padReorderIsGridMode)
    {
        const auto line = getListInsertLineBounds();
        if (line.getWidth() > 0)
        {
            const int lineY = line.getY() - scrollY;
            g.setColour (pal.accent.withAlpha (0.55f));
            g.fillEllipse (18.0f, (float) lineY - 4.0f, 8.0f, 8.0f);
            g.fillEllipse ((float) getWidth() - 26.0f, (float) lineY - 4.0f, 8.0f, 8.0f);
            g.setColour (pal.accent);
            g.fillRect (22, lineY - 1, getWidth() - 44, 2);
        }
    }
    else
    {
        auto targetCell = getGridGapCellBounds();
        targetCell.setY (targetCell.getY() - scrollY);
        const auto targetF = targetCell.toFloat().expanded (6.0f, 6.0f);

        const float pulse = 0.5f + 0.5f * std::sin ((float) juce::Time::getMillisecondCounter() * 0.009f);
        g.setColour (pal.dragTargetGlow.withAlpha (0.06f + 0.14f * pulse));
        g.fillRoundedRectangle (targetF.expanded (4.0f), 12.0f);
        g.setColour (pal.dragTargetGlow.withAlpha (0.16f + 0.12f * pulse));
        g.fillRoundedRectangle (targetF, 10.0f);
        g.setColour (pal.accent.withAlpha (0.90f));
        g.drawRoundedRectangle (targetF, 10.0f, 2.5f);
    }

    if (padReorderSource == nullptr)
        return;

    const int pointerX = padReorderPointerPos.x;
    const int pointerY = padReorderPointerPos.y - scrollY;

    if (padReorderIsGridMode)
    {
        const auto srcBounds = padReorderSource->getBounds();
        auto topLeft = padReorderPointerPos - padReorderDragOffset;
        if (stackIntroActive && padReorderSource != nullptr)
            topLeft = padReorderSource->getBounds().getPosition();
        topLeft.y -= scrollY;

        juce::Rectangle<float> ghost ((float) topLeft.x, (float) topLeft.y,
                                      (float) srcBounds.getWidth(),
                                      (float) srcBounds.getHeight());
        const float popScale = stackIntroActive ? (1.0f + 0.08f * std::sin (easeOut * juce::MathConstants<float>::pi)) : 1.04f;
        ghost = ghost.withSizeKeepingCentre (ghost.getWidth() * popScale, ghost.getHeight() * popScale);

        auto targetCell = getGridGapCellBounds();
        targetCell.setY (targetCell.getY() - scrollY);
        const auto targetCentre = targetCell.getCentre().toFloat();
        const auto ghostCentre = ghost.getCentre();
        const float snap = 0.22f;
        ghost.setCentre (ghostCentre.x + (targetCentre.x - ghostCentre.x) * snap,
                         ghostCentre.y + (targetCentre.y - ghostCentre.y) * snap);

        g.setColour (pal.dragTargetGlow.withAlpha (0.20f));
        g.drawLine ((float) pointerX, (float) pointerY, targetCentre.x, targetCentre.y, 1.5f);

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
    else
    {
        const juce::String title = padReorderSource->getPadName();
        const auto titleFont = ShowTheme::fontBold (12.0f);
        g.setFont (titleFont);
        juce::GlyphArrangement glyphs;
        glyphs.addLineOfText (titleFont, title, 0.0f, 0.0f);
        const float textW = glyphs.getBoundingBox (0, -1, true).getWidth();
        const float pillW = juce::jmin (420.0f, textW + 28.0f);
        const float pillH = 32.0f;

        juce::Rectangle<float> ghost ((float) pointerX - pillW * 0.5f,
                                      (float) pointerY - pillH * 0.5f,
                                      pillW, pillH);
        if (stackIntroActive && padReorderSource != nullptr)
        {
            const auto src = padReorderSource->getBounds();
            ghost.setCentre ((float) src.getCentreX(), (float) (src.getCentreY() - scrollY));
        }
        if (stackIntroActive)
            ghost = ghost.withSizeKeepingCentre (ghost.getWidth() * (1.0f + 0.04f * std::sin (easeOut * juce::MathConstants<float>::pi)),
                                                 ghost.getHeight() * (1.0f + 0.04f * std::sin (easeOut * juce::MathConstants<float>::pi)));

        if (draggedCount > 1)
        {
            const int layers = juce::jmin (4, draggedCount - 1);
            for (int i = layers; i >= 1; --i)
            {
                const float introMul = stackIntroActive ? easeOut : 1.0f;
                auto layer = ghost.translated ((float) i * 12.0f * introMul, (float) i * 7.0f * introMul)
                                  .withSizeKeepingCentre (ghost.getWidth() * (1.0f - 0.04f * (float) i),
                                                          ghost.getHeight() * (1.0f - 0.03f * (float) i));
                g.setColour (pal.dragGhostFill.withAlpha (0.42f - (float) (i - 1) * 0.08f));
                g.fillRoundedRectangle (layer, pillH * 0.5f);
                g.setColour (pal.accent.withAlpha (0.42f - (float) (i - 1) * 0.08f));
                g.drawRoundedRectangle (layer, pillH * 0.5f, 1.5f);
            }
        }

        g.setColour (juce::Colours::black.withAlpha (0.28f));
        g.fillRoundedRectangle (ghost.translated (0.0f, 2.0f), pillH * 0.5f);

        g.setColour (pal.dragGhostFill);
        g.fillRoundedRectangle (ghost, pillH * 0.5f);

        g.setColour (pal.dragGhostText);
        g.drawText (title, ghost.reduced (12.0f, 4.0f), juce::Justification::centredLeft, true);

        g.setColour (pal.accent.withAlpha (0.75f));
        g.drawRoundedRectangle (ghost, pillH * 0.5f, 1.5f);

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
    applyPadSelectionVisualState();
    resized();
    rebuildSidebarFromAllLists();
    sidebarPanel.setSelectedIndex (listIdx);
    saveProject();
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

    showcontrol::ui::promptMissingFfmpeg (this,
        [this, videoFile, targetPad] (showcontrol::ui::FfmpegPromptChoice choice)
        {
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

            showcontrol::ui::installFfmpegWithProgress (this,
                [this, videoFile, targetPad] (bool ok, juce::String err)
                {
                    if (ok)
                    {
                        ErrorHandler::logAndShow (juce::String::fromUTF8 (u8"Tách audio video"),
                                                  juce::String::fromUTF8 (u8"Đã cài ffmpeg. Đang tách audio…"),
                                                  ErrorHandler::Severity::Info);
                        ingestVideoFileToPad (videoFile, targetPad);
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

    if (! VideoAudioExtractor::isFfmpegAvailable())
    {
        offerFfmpegSetupThenIngestVideo (videoFile, targetPad);
        return;
    }

    VideoAudioExtractor::extractAudioToWavAsync (videoFile,
        [this, targetPad, videoFile] (bool ok, juce::File wavFile, juce::String error)
        {
            if (! ok)
            {
                if (error == "MISSING_FFMPEG")
                {
                    offerFfmpegSetupThenIngestVideo (videoFile, targetPad);
                    return;
                }

                ErrorHandler::logAndShow (juce::String::fromUTF8 (u8"Tách audio video"),
                                        error.isNotEmpty() ? error
                                                           : juce::String::fromUTF8 (u8"Không tách được audio."),
                                        ErrorHandler::Severity::Warning);
                return;
            }

            juce::File audioPath = wavFile;
            const juce::File sidecar = videoFile.getSiblingFile (videoFile.getFileNameWithoutExtension()
                                                                  + ".showcontrol.wav");
            if (sidecar.getFullPathName() != wavFile.getFullPathName())
            {
                sidecar.deleteFile();
                if (wavFile.copyFileTo (sidecar))
                    audioPath = sidecar;
            }

            applyProjectDefaultsToPad (targetPad);
            targetPad->configurePad (audioPath.getFullPathName(), 1.0f, false);
            saveProject();

            if (activeListIndex >= 0)
                rebuildDefaultHotkeysForList (activeListIndex);

            if (inspectorPanel.getCurrentPad() == targetPad)
                inspectorPanel.selectPad (targetPad);

            resized();
            repaint();
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
        replaceFile = 1,
        duplicate   = 2,
        trimEditor  = 3,
        revealFile  = 4,
        deleteItem  = 5,
        resetFade   = 6,
        renameTrack = 7
    };
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

    juce::PopupMenu menu;
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
                        [this, safePad] (int result)
                        {
                            if (safePad == nullptr)
                                return;

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
                duplicatePadAtIndex (listIdx, padIdx);
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

void MainComponent::duplicatePadAtIndex (int listIdx, int padIdx)
{
    if (listIdx < 0 || listIdx >= allLists.size())
        return;

    auto* list = allLists[listIdx];
    if (list == nullptr || list->isLocked)
        return;

    if (padIdx < 0 || padIdx >= list->pads.size())
        return;

    if (list->isGrid && list->pads.size() >= kMaxCuePadsPerList)
    {
        showCueListCapacityAlert();
        return;
    }

    auto* src = list->pads[padIdx];
    if (src == nullptr)
        return;

    const int insertAt = padIdx + 1;
    auto* dup = createSoundPad();
    scrollContent->addChildComponent (dup);
    applyProjectDefaultsToPad (dup);

    if (src->hasAudioFile())
        dup->configurePad (src->getFilePath(), src->getOutputGain(), src->isLooping());

    dup->setCustomName (src->getPadName());
    dup->setTrimStart (src->getTrimStart());
    dup->setTrimEnd (src->getTrimEnd());
    dup->setOutputBus (src->getOutputBus());
    dup->updateTheme (isDarkMode);
    dup->setRenderMode (list->isGrid);
    dup->setCueListPlayback (list->isGrid);
    dup->setVisible (listIdx == activeListIndex);

    list->pads.insert (insertAt, dup);

    for (int i = 0; i < list->pads.size(); ++i)
        if (list->pads[i] != nullptr)
            list->pads[i]->setPadIndex (i);

    if (list->isGrid)
        syncCueMetadataFromPads (*list);

    if (listIdx == activeListIndex)
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
    saveProject();
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

    masterDeckPanel.onPauseAll = [this] {
        for (auto* list : allLists) {
            if (list == nullptr || ! list->isGrid)
                continue;

            for (auto* pad : list->pads)
            {
                if (pad != nullptr && pad->isPlaying())
                    pad->triggerPause();
            }
        }
    };

    masterDeckPanel.onStopAll = [this] {
        for (auto* list : allLists) {
            if (list != nullptr) {
                for (auto* pad : list->pads) {
                    if (pad != nullptr)
                        pad->triggerStop();
                }
            }
        }
    };

    masterDeckPanel.onFadeAll = [this] { triggerGlobalPanicFadeAll(); };

    sidebarPanel.setPanicKeyListener (this);

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
    
    auto* container = new ScrollableContainer();
    scrollContent.reset (container);
    scrollContent->setWantsKeyboardFocus (false);

    container->onBackgroundRightClick = [this] (const juce::MouseEvent&) {
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

    viewScroller.setViewedComponent (scrollContent.get(), false);
    viewScroller.setScrollBarsShown (true, false, true, false);
    viewScroller.setScrollBarThickness (7);
    viewScroller.setOpaque (false);
    container->setOpaque (true);

    padReorderOverlay = std::make_unique<PadReorderOverlay> (*this);
    addChildComponent (*padReorderOverlay);
    padReorderOverlay->setVisible (false);

    addAndMakeVisible (gridSizeSlider);
    gridSizeSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    gridSizeSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    gridSizeSlider.setRange (56.0, 180.0, 1.0);
    gridSizeSlider.setValue (180.0); // mặc định ưu tiên khoảng 4 pad/hàng ở layout chuẩn
    gridSizeSlider.onValueChange = [this]
    {
        resized();
        saveProject();
    };

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

            inspectorPanel.selectPad (pad);
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

    cueListPanel->onTrackRenamed = [this] (int cueIndex, const juce::String& newName)
    {
        if (activeListIndex < 0 || activeListIndex >= allLists.size())
            return;

        auto* list = allLists[activeListIndex];
        if (list == nullptr || cueIndex < 0 || cueIndex >= list->pads.size())
            return;

        if (cueIndex < list->cueMeta.size())
            list->cueMeta.getReference (cueIndex).name = newName;

        if (auto* pad = list->pads[cueIndex])
        {
            pad->repaint();

            if (inspectorPanel.getCurrentPad() == pad)
                inspectorPanel.selectPad (pad);
        }

        saveApplicationState();
    };

    sidebarPanel.onListSelected = [this] (int idx, int count, bool /*isGridHint*/)
    {
        if (idx < 0 || idx >= allLists.size() || allLists[idx] == nullptr)
            return;

        loadList (idx, count, allLists[idx]->isGrid);
    };
    sidebarPanel.onAddList = [this] (int idx, juce::String name, int count, bool isGrid)
    {
        while (allLists.size() <= idx)
            allLists.add (new ListData());

        auto* list = allLists[idx];
        list->isGrid = isGrid;
        list->useCueListPanel = false;

        sidebarPanel.addSet (name, list->pads.size(), isGrid, false, list->isLocked);
        loadList (idx, count, isGrid);
    };
    sidebarPanel.onFoldersSmartImport = [this] (const juce::StringArray& folderPaths, bool targetIsBgm)
    {
        importListsFromDroppedFolders (folderPaths, targetIsBgm);
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

        if (auto* removed = allLists[idx])
        {
            // CHỐT CHẶN: ngắt Inspector khỏi thumbnail TRƯỚC khi OwnedArray hủy SoundPad.
            detachInspectorFromPadsInList (removed);

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

        saveProject();
    };

    sidebarPanel.onMoveList = [this] (int fromIdx, int toIdx) { moveListInProject (fromIdx, toIdx); };

    sidebarPanel.onRenameList = [this] (int idx, juce::String) { juce::ignoreUnused (idx); saveProject(); };

    sidebarPanel.onDuplicateSet   = [this] (int idx) { duplicateListAtIndex (idx); };
    sidebarPanel.onAddSounds      = [this] (int idx) { addSoundsToSet (idx); };
    sidebarPanel.onMorphSetStructure = [this] (int idx) { morphSetStructure (idx); };

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

    inspectorPanel.onProjectEdited = [this] { saveProject(); };

    inspectorPanel.onTrackNameChanged = [this]
    {
        if (cueListPanel != nullptr && cueListPanel->isVisible())
        {
            if (auto* list = getActiveListSafe())
            {
                syncCueMetadataFromPads (*list);
                cueListPanel->setCues (list->cueMeta);
            }
            else
            {
                cueListPanel->refreshListBoxData();
            }
        }
        else if (auto* pad = inspectorPanel.getCurrentPad())
        {
            pad->repaint();
        }
    };

    inspectorPanel.onOutputBusChanged = [this] (int /*bus*/) { saveProject(); };

    inspectorPanel.onNormalizeActiveListRequested = [this] (bool useLufs)
    {
        normalizeActiveList (useLufs);
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
    addKeyListener (this);

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
}

void MainComponent::parentHierarchyChanged()
{
    juce::Component::parentHierarchyChanged();

    if (topLevelKeyListenerHost == nullptr)
    {
        if (auto* top = getTopLevelComponent())
        {
            topLevelKeyListenerHost = top;
            top->addKeyListener (this);
        }
    }
}

void MainComponent::triggerManualMusicIngestion()
{
    if (activeListIndex < 0 || activeListIndex >= allLists.size()) return;

    mainFileChooser = std::make_unique<juce::FileChooser> (
        juce::String::fromUTF8 (u8"Nạp bài hát kịch bản..."),
        juce::File::getSpecialLocation (juce::File::userHomeDirectory),
        ShowAudioFormats::fileChooserWildcard());

    mainFileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::canSelectMultipleItems,
    [this] (const juce::FileChooser& fc) {
        auto results = fc.getResults(); if (results.size() == 0) return;
        auto* current = allLists[activeListIndex];
        if (current == nullptr) return;

        const bool isBgm = ! current->isGrid;
        int filesToAdd = results.size();

        if (! isBgm)
        {
            const int slotsLeft = juce::jmax (0, kMaxCuePadsPerList - current->pads.size());
            if (slotsLeft <= 0)
            {
                showCueListCapacityAlert();
                return;
            }

            filesToAdd = juce::jmin (slotsLeft, results.size());
            if (filesToAdd < results.size())
                showCueListCapacityAlert();
        }

        for (int ri = 0; ri < filesToAdd; ++ri) {
            auto file = results.getReference (ri);
            int newIdx = current->pads.size(); auto* p = createSoundPad(); scrollContent->addChildComponent (p);
            p->setPadIndex (newIdx);
            applyProjectDefaultsToPad (p);
            p->configurePad (file.getFullPathName(), 1.0f, false); p->updateTheme (isDarkMode); p->setRenderMode (current->isGrid);
            p->setVisible (true);
            current->pads.add (p);
        }
        
        juce::Array<juce::String> listNames;
        for (int i = 0; i < allLists.size(); ++i) { listNames.add (sidebarPanel.getListName (i)); }
        sidebarPanel.clearAllLists();
        for (int i = 0; i < allLists.size(); ++i) {
            sidebarPanel.addSet (listNames[i], allLists[i]->pads.size(), allLists[i]->isGrid,
                                 allLists[i]->useCueListPanel, allLists[i]->isLocked);
        }
        sidebarPanel.setSelectedIndex (activeListIndex);

        saveProject();
        refreshCueListPanel();
        rebuildDefaultHotkeysForList (activeListIndex);
        resized();
        repaint();

        if (isShowing())
            grabKeyboardFocus();
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
                             allLists[i]->useCueListPanel, allLists[i]->isLocked);
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
        ingestDroppedFilesToActiveBgmList (*current, validAudioFiles, validVideoFiles, localX, localY);
        finalizeAfterFileDropIngest();
        saveApplicationState();
        return;
    }

    // ── VÙNG 3: CUE (PAD grid hoặc Cue List) — đổ nhạc theo view đang hiển thị ──
    if (inCueDropZone)
    {
        ingestDroppedFilesToActiveCuePads (*current, validAudioFiles, validVideoFiles);
        finalizeAfterFileDropIngest();
        saveApplicationState();

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
    auto b = getLocalBounds();

    if (kMacUnifiedTitleBarInset > 0)
        b.removeFromTop (kMacUnifiedTitleBarInset);

    auto topDeck = b.removeFromTop (186).reduced (8, 4);
    masterDeckPanel.setBounds (topDeck);
    busMixerPanel.setBounds ({});
    busMixerPanel.setVisible (false);

    const int splitW = 6;
    const int visibleSidebarW = sidebarVisible ? sidebarWidth : 0;
    const int visibleInspectorW = inspectorVisible ? inspectorWidth : 0;

    if (sidebarVisible)
    {
        sidebarPanel.setBounds (b.removeFromLeft (visibleSidebarW));
        if (leftSplitter != nullptr)
            leftSplitter->setBounds (b.removeFromLeft (splitW));
    }
    else if (leftSplitter != nullptr)
    {
        sidebarPanel.setBounds ({});
        leftSplitter->setBounds ({});
    }

    if (inspectorVisible)
    {
        // Đặt inspector ở mép phải, splitter nằm giữa center và inspector (đối xứng với sidebar).
        inspectorPanel.setBounds (b.removeFromRight (visibleInspectorW));
        if (rightSplitter != nullptr)
            rightSplitter->setBounds (b.removeFromRight (splitW));
    }
    else if (rightSplitter != nullptr)
    {
        rightSplitter->setBounds ({});
        inspectorPanel.setBounds ({});
    }

    const int centerTop = masterDeckPanel.getBottom();
    const int buttonY = getHeight() - 86; // nâng lên tránh đè nút xóa list
    const int btnW = 24;
    const int btnH = 22;

    // Nút dính theo mép splitter như Farrago (không trôi theo content scroll).
    const int leftBtnX = sidebarVisible && leftSplitter != nullptr && ! leftSplitter->getBounds().isEmpty()
                           ? leftSplitter->getX() - btnW + 6
                           : 8;
    const int rightBtnX = inspectorVisible && rightSplitter != nullptr && ! rightSplitter->getBounds().isEmpty()
                            ? rightSplitter->getRight() - 6
                            : getWidth() - btnW - 8;

    showSidebarBtn.setBounds (leftBtnX, buttonY, btnW, btnH);
    showInspectorBtn.setBounds (rightBtnX, buttonY, btnW, btnH);
    showSidebarBtn.toFront (false);
    showInspectorBtn.toFront (false);

    const int leftOffset = (sidebarVisible ? sidebarWidth + splitW : 0);
    const int rightOffset = (inspectorVisible ? inspectorWidth + splitW : 0);
    auto centerFrame = juce::Rectangle<int> (leftOffset, centerTop,
                                             getWidth() - leftOffset - rightOffset,
                                             getHeight() - centerTop);

    if (allLists.isEmpty() || activeListIndex < 0 || activeListIndex >= allLists.size())
    {
        if (emptyStatePanel != nullptr)
        {
            emptyStatePanel->setBounds (centerFrame);
            emptyStatePanel->setVisible (true);
            emptyStatePanel->toFront (false);
        }

        viewScroller.setBounds ({});
        viewScroller.setVisible (false);
        listHeaderComponent->setVisible (false);
        gridSizeSlider.setVisible (false);
        addMusicFloatingBtn.setBounds ({});
        return;
    }

    if (emptyStatePanel != nullptr)
        emptyStatePanel->setVisible (false);

    viewScroller.setVisible (true);

    if (auto* current = allLists[activeListIndex])
    {
        auto innerBounds = centerFrame;
        const bool isListMode = ! current->isGrid;
        const bool hasLoadedAudio = listHasLoadedAudio (*current);

        constexpr int kBottomBarH = 42;
        auto bottomBarBounds = innerBounds.removeFromBottom (kBottomBarH);

        const bool padGridActive = current->isGrid && ! current->useCueListPanel;
        constexpr int kPadSliderStripW = 168;

        if (padGridActive)
        {
            auto sliderStrip = bottomBarBounds.removeFromLeft (kPadSliderStripW);
            gridSizeSlider.setVisible (true);
            gridSizeSlider.setBounds (sliderStrip.reduced (10, 9));
            gridSizeSlider.toFront (false);
        }
        else
        {
            gridSizeSlider.setVisible (false);
            gridSizeSlider.setBounds ({});
        }

        if (playoutModeBar != nullptr)
        {
            const bool showModeBar = current->isGrid;
            playoutModeBar->setVisible (showModeBar);

            if (showModeBar)
            {
                playoutModeBar->setBounds (bottomBarBounds);
                playoutModeBar->toFront (false);
                syncPlayoutModeBarFromActiveList();
            }
        }

        if (padGridActive)
            gridSizeSlider.toFront (false);

        addMusicFloatingBtn.setBounds ({});

        const bool cueListViewActive = current->isGrid && current->useCueListPanel && cueListPanel != nullptr;

        if (cueListViewActive)
        {
            listHeaderComponent->setVisible (false);
            viewScroller.setBounds ({});
            viewScroller.setVisible (false);
            viewScroller.setScrollBarsShown (false, false, false, false);
            cueListPanel->setBounds (innerBounds);
            cueListPanel->setVisible (true);
            cueListPanel->toFront (false);
        }
        else
        {
            if (cueListPanel != nullptr)
                cueListPanel->setVisible (false);

            viewScroller.setVisible (true);

            if (isListMode)
            {
                listHeaderComponent->setVisible (hasLoadedAudio);

                if (hasLoadedAudio)
                    listHeaderComponent->setBounds (innerBounds.removeFromTop (28));

                viewScroller.setBounds (innerBounds);
                viewScroller.setScrollBarsShown (hasLoadedAudio, false, hasLoadedAudio, false);
            }
            else
            {
                listHeaderComponent->setVisible (false);
                viewScroller.setBounds (innerBounds);
                viewScroller.setScrollBarsShown (false, false, false, false);
            }
        }

        layoutActiveListPads();
        updatePadReorderOverlayBounds();
    }
}

bool MainComponent::trySwitchListByShortcut (const juce::KeyPress& key)
{
    const auto mods = key.getModifiers();
    const bool ctrlOnly = mods.isCtrlDown() && ! mods.isCommandDown() && ! mods.isAltDown();
    const bool cmdOnly  = mods.isCommandDown() && ! mods.isCtrlDown() && ! mods.isAltDown();

    if (! ctrlOnly && ! cmdOnly)
        return false;

    const bool targetIsGrid = cmdOnly;

    const int code = key.getKeyCode();
    int slot = 0;

    if (code >= '1' && code <= '9')
        slot = code - '0';
    else if (code >= juce::KeyPress::numberPad0 && code <= juce::KeyPress::numberPad9)
        slot = code - juce::KeyPress::numberPad0;
    else
        return false;

    if (slot < 1)
        return false;

    int count = 0;
    for (int i = 0; i < allLists.size(); ++i)
    {
        const auto* list = allLists[i];
        if (list == nullptr || list->isGrid != targetIsGrid)
            continue;

        ++count;
        if (count == slot)
        {
            sidebarPanel.setSelectedIndex (i);
            loadList (i, sidebarPanel.getListTrackCount (i), list->isGrid);
            return true;
        }
    }

    return false;
}

bool MainComponent::isShowControlManagedHotkey (const juce::KeyPress& key) const noexcept
{
    const int code = key.getKeyCode();

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

bool MainComponent::keyStateChanged (bool isKeyDown)
{
    if (! isKeyDown)
        hotkeyManager.notifyKeyReleased (0);

    return false;
}

bool MainComponent::matchesHotkeyBindingForKey (const juce::KeyPress& key) const noexcept
{
    if (hotkeyScopeMode == HotkeyScopeMode::global)
        return hotkeyManager.findByKeyPressPreferList (key, activeListIndex) != nullptr;

    if (activeListIndex >= 0)
        return hotkeyManager.findByKeyPressInList (key, activeListIndex) != nullptr;

    return false;
}

bool MainComponent::handleArrowNavigationKey (const juce::KeyPress& key)
{
    auto* currentList = getActiveListSafe();

    if (currentList == nullptr || currentList->pads.size() == 0)
        return false;

    const int arrowCode = HotkeyManager::normalizeArrowKeyCode (key.getKeyCode());
    const int mainViewWidth = viewScroller.getWidth();
    const int mainViewHeight = viewScroller.getHeight();
    const int cols = getPadGridLayout (mainViewWidth, mainViewHeight, currentList->pads.size()).cols;

    if (currentList->isGrid)
    {
        if (arrowCode == juce::KeyPress::leftKey)
            selectedBgmIndex = juce::jlimit (0, currentList->pads.size() - 1, selectedBgmIndex - 1);
        else if (arrowCode == juce::KeyPress::rightKey)
            selectedBgmIndex = juce::jlimit (0, currentList->pads.size() - 1, selectedBgmIndex + 1);
        else if (arrowCode == juce::KeyPress::upKey)
            selectedBgmIndex = juce::jlimit (0, currentList->pads.size() - 1, selectedBgmIndex - cols);
        else if (arrowCode == juce::KeyPress::downKey)
            selectedBgmIndex = juce::jlimit (0, currentList->pads.size() - 1, selectedBgmIndex + cols);
        else
            return false;
    }
    else
    {
        if (arrowCode == juce::KeyPress::upKey || arrowCode == juce::KeyPress::leftKey)
            selectedBgmIndex = juce::jlimit (0, currentList->pads.size() - 1, selectedBgmIndex - 1);
        else if (arrowCode == juce::KeyPress::downKey || arrowCode == juce::KeyPress::rightKey)
            selectedBgmIndex = juce::jlimit (0, currentList->pads.size() - 1, selectedBgmIndex + 1);
        else
            return false;
    }

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
    const int code = key.getKeyCode();
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

bool MainComponent::keyPressed (const juce::KeyPress& key, juce::Component* originatingComponent)
{
    juce::ignoreUnused (originatingComponent);
    return handleApplicationHotkey (key);
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
        cueListPanel->resetListScrollToTop();
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

                newList->pads.add (p);
                ++padIdx;
            }
        }

        allLists.add (newList);
        sidebarPanel.addSet (listName, newList->pads.size(), newList->isGrid,
                             newList->useCueListPanel, newList->isLocked);
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

            tracks.add (juce::var (trackObj.get()));
        }

        juce::DynamicObject::Ptr listObj (new juce::DynamicObject());
        listObj->setProperty ("name", sidebarPanel.getListName (i));
        listObj->setProperty ("isGrid", list->isGrid);
        listObj->setProperty ("tracks", tracks);
        playlistLists.add (juce::var (listObj.get()));
    }

    return playlistLists;
}

bool MainComponent::keyPressed (const juce::KeyPress& key)
{
    return handleApplicationHotkey (key);
}

bool MainComponent::handleApplicationHotkey (const juce::KeyPress& key)
{
    if (! juce::MessageManager::getInstance()->isThisTheMessageThread())
        return false;

    if (juce::Component::getCurrentlyModalComponent() != nullptr)
        return false;

    if (isSpacebarKey (key))
        return executeSpacebarTransportKey (key);

    if (key == juce::KeyPress::escapeKey)
    {
        if (swallowLikelyOsKeyRepeat (key))
            return true;

        triggerGlobalPanicFadeAll();
        return true;
    }

    const int pressedKeyCode = key.getKeyCode();
    const juce::uint32 nowMs = juce::Time::getMillisecondCounter();
    const bool managedHotkey = isShowControlManagedHotkey (key);

    // Lớp 1 — luôn tiêu thụ (không rò sang Slider/Button con) khi đang xử lý chuỗi key lồng nhau.
    if (inExclusiveKeyHandler)
        return true;

    const juce::ScopedValueSetter<bool> keyHandlerScope (inExclusiveKeyHandler, true);

    if (isPlaybackCommandBlocked() || nowMs < startupInputGuardUntilMs)
    {
        logHotkeyTrace ("ignore startup keyCode=" + juce::String (pressedKeyCode));
        return true;
    }

    if (allLists.isEmpty())
        return true;

    // Delete/Backspace — ưu tiên trước matrix hotkey (KeyListener + focus con đều đi qua đây).
    if (tryHandleDeleteOrBackspaceKey (key))
    {
        lastHotkeyKeyCode   = key.getKeyCode();
        lastHotkeyTriggerMs = nowMs;
        return true;
    }

    if (key.getModifiers().isShiftDown()
        && (key.getModifiers().isCommandDown() || key.getModifiers().isCtrlDown())
        && (key.getTextCharacter() == 'd' || key.getTextCharacter() == 'D'))
    {
        toggleStageMonitorWindow();
        return true;
    }

    // Mũi tên không có macro binding — bubble (return false), không chạy matrix/transport phía dưới.
    if (HotkeyManager::isArrowNavigationKeyCode (pressedKeyCode)
        && ! matchesHotkeyBindingForKey (key))
    {
        if (handleArrowNavigationKey (key))
            return true;

        return false;
    }

    // Cue List view — Space/P/S biệt lập, không dùng GO toggle của PAD grid.
    if (activeList == 1 && cueListPanel != nullptr && cueListPanel->isVisible())
    {
        if (cueListPanel->handleTransportKey (key))
        {
            lastHotkeyKeyCode   = pressedKeyCode;
            lastHotkeyTriggerMs = nowMs;
            return true;
        }
    }

    if (managedHotkey)
    {
        const auto gate = hotkeyManager.evaluateKeyPressGate (pressedKeyCode, nowMs);

        if (gate.isDuplicate)
        {
            logHotkeyTrace ("swallow duplicate keyCode=" + juce::String (pressedKeyCode));
            return true;
        }

        if (! gate.shouldExecute)
            return true;
    }

    logHotkeyTrace ("keyCode=" + juce::String (pressedKeyCode)
                    + " activeList=" + juce::String (activeListIndex));

    if (trySwitchListByShortcut (key))
        return true;

    if (hotkeyScopeMode == HotkeyScopeMode::global)
    {
        if (const auto* binding = hotkeyManager.findByKeyPressPreferList (key, activeListIndex))
        {
            logHotkeyTrace ("match key -> list=" + juce::String (binding->padListIndex)
                            + " pad=" + juce::String (binding->padIndex) + " scope=global");
            if (triggerPadFromHotkey (*binding))
            {
                lastHotkeyKeyCode = pressedKeyCode;
                lastHotkeyTriggerMs = nowMs;
                logHotkeyTrace ("trigger success");
            }
            else
                logHotkeyTrace ("trigger failed");

            return true;
        }

        if (managedHotkey && ! HotkeyManager::isArrowNavigationKeyCode (pressedKeyCode))
        {
            logHotkeyTrace ("no binding match in global scope");
            return true;
        }
    }
    else if (activeListIndex >= 0 && activeListIndex < allLists.size())
    {
        if (const auto* binding = hotkeyManager.findByKeyPressInList (key, activeListIndex))
        {
            logHotkeyTrace ("match key -> list=" + juce::String (binding->padListIndex)
                            + " pad=" + juce::String (binding->padIndex) + " scope=active");
            if (triggerPadFromHotkey (*binding))
            {
                lastHotkeyKeyCode = pressedKeyCode;
                lastHotkeyTriggerMs = nowMs;
                logHotkeyTrace ("trigger success");
            }
            else
                logHotkeyTrace ("trigger failed");

            return true;
        }

        if (managedHotkey && ! HotkeyManager::isArrowNavigationKeyCode (pressedKeyCode))
        {
            logHotkeyTrace ("no binding match in active scope");
            return true;
        }
    }

    if (activeListIndex < 0 || activeListIndex >= allLists.size())
        return managedHotkey;

    auto* currentList = getActiveListSafe();

    if (currentList == nullptr || currentList->pads.size() == 0)
        return managedHotkey;

    if (pressedKeyCode == juce::KeyPress::returnKey)
    {
        if (currentList->isGrid)
        {
            triggerCueGo (selectedBgmIndex);
            lastHotkeyKeyCode  = pressedKeyCode;
            lastHotkeyTriggerMs = nowMs;
        }
        return true;
    }

    if (pressedKeyCode == 'S' || pressedKeyCode == 's')
    {
        if (isCueListViewActive())
        {
            triggerCueListStop (selectedBgmIndex);
        }
        else
        {
            for (auto* pad : currentList->pads)
                if (pad != nullptr)
                    pad->triggerStop();
        }

        lastHotkeyKeyCode  = pressedKeyCode;
        lastHotkeyTriggerMs = nowMs;
        return true;
    }

    if ((pressedKeyCode == 'P' || pressedKeyCode == 'p') && isCueListViewActive())
    {
        triggerCueListPause (selectedBgmIndex);
        lastHotkeyKeyCode  = pressedKeyCode;
        lastHotkeyTriggerMs = nowMs;
        return true;
    }
    
    if (! currentList->isGrid && (pressedKeyCode == 'N' || pressedKeyCode == 'n'))
    {
        triggerBgmNext();
        lastHotkeyKeyCode  = pressedKeyCode;
        lastHotkeyTriggerMs = nowMs;
        return true;
    }

    return managedHotkey;
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
    selectedPadIndices.clear();

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

    selectedBgmIndex = 0;
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

    if (target->pads.size() > 0 && listHasLoadedAudio (*target))
    {
        for (int i = 0; i < target->pads.size(); ++i)
        {
            if (auto* p = target->pads[i]; p != nullptr && p->hasAudioFile())
            {
                selectedPadIndices.add (i);
                inspectorPanel.selectPad (p);
                break;
            }
        }
    }
    else
    {
        inspectorPanel.selectPad (nullptr);
    }

    lastUiSyncedPlayingPad = nullptr;

    resized();

    if (! isGrid)
    {
        for (auto* p : target->pads)
        {
            if (p != nullptr && p->isTransportActive())
            {
                syncUiToPlayingPad (p, true);
                break;
            }
        }
    }

    activeList = (isGrid && target->useCueListPanel) ? 1 : 0;

    if (isShowing())
    {
        if (activeList == 1 && cueListPanel != nullptr)
            cueListPanel->grabKeyboardFocus();
        else if (viewScroller.isVisible())
            viewScroller.grabKeyboardFocus();
        else
            grabKeyboardFocus();
    }
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
            meta.name     = pad->getPadName();
            meta.filePath = pad->getFilePath();
        }
        else
        {
            meta.name.clear();
            meta.filePath.clear();
        }
    }
}

void MainComponent::setPlayoutMode (bool isPadMode)
{
    auto* list = getActiveListSafe();

    if (list == nullptr || ! list->isGrid)
        return;

    list->useCueListPanel = ! isPadMode;
    activeList = isPadMode ? 0 : 1; // 1 = Danh sách CUE QLab-style

    if (activeListIndex >= 0)
        sidebarPanel.setListViewMode (activeListIndex, ! isPadMode);

    if (playoutModeBar != nullptr)
        playoutModeBar->setPadModeActive (isPadMode);

    layoutActiveListPads();
    viewScroller.resized();
    repaint();

    if (! isPadMode && cueListPanel != nullptr)
    {
        refreshCueListPanel();

        if (isShowing())
            cueListPanel->grabKeyboardFocus();
    }
    else if (isShowing())
    {
        if (viewScroller.isVisible())
            viewScroller.grabKeyboardFocus();
        else
            grabKeyboardFocus();
    }

    saveProject();
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

void MainComponent::refreshCueListPanel()
{
    if (cueListPanel == nullptr || activeListIndex < 0 || activeListIndex >= allLists.size())
        return;

    auto* list = allLists[activeListIndex];

    if (list == nullptr || ! list->isGrid || ! list->useCueListPanel)
        return;

    syncCueMetadataFromPads (*list);
    cueListPanel->setCues (list->cueMeta);
    cueListPanel->setSelectedIndex (selectedBgmIndex);
    cueListPanel->resetListScrollToTop();
    updateCuePlaybackIndicators();
    cueListPanel->notifyPlaybackActivity();
}

void MainComponent::updateCuePlaybackIndicators()
{
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
            playingIdx = i;
            ++activeCount;
        }

        if (pad->isArmed())
            armedIdx = i;
    }

    if (activeCount != 1)
        playingIdx = -1;

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
    selectedBgmIndex = padIndex;
    inspectorPanel.selectPad (pad);

    if (cueListPanel != nullptr)
        cueListPanel->setSelectedIndex (padIndex);

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
    if (activeListIndex < 0 || activeListIndex >= allLists.size())
        return false;

    auto* list = allLists[activeListIndex];
    if (list == nullptr || ! juce::isPositiveAndBelow (padIndex, list->pads.size()))
        return false;

    const bool ok = audioEngine.toggleCuePauseResume (list->pads[padIndex]);
    updateCuePlaybackIndicators();
    refreshSidebarPlayingStatus();
    return ok;
}

bool MainComponent::triggerCueListStop (int padIndex)
{
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
    return ok;
}

bool MainComponent::triggerCueGo (int padIndex)
{
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
    selectedBgmIndex = padIndex;
    inspectorPanel.selectPad (pad);

    if (cueListPanel != nullptr)
        cueListPanel->setSelectedIndex (padIndex);

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

            goPad->startFadeOut();

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

bool MainComponent::triggerPadFromHotkey (const HotkeyBinding& binding)
{
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
        if (! triggerCueGo (binding.padIndex))
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

    // 48-key matrix:
    // 1..0, Q..P, A..;, Z..,. (40 keys) + F1..F8 (8 keys)
    const juce::String keyMatrix = "1234567890QWERTYUIOPASDFGHJKL;ZXCVBNM,.";
    const int maxMappedPads = juce::jmin (list->pads.size(), keyMatrix.length() + 8);

    for (int i = 0; i < maxMappedPads; ++i)
    {
        if (list->pads[i] == nullptr || ! list->pads[i]->hasAudioFile())
            continue;

        HotkeyBinding binding;
        binding.padListIndex = listIndex;
        binding.padIndex     = i;
        if (i < keyMatrix.length())
            binding.keyPress = juce::KeyPress (keyMatrix[i]);
        else
            binding.keyPress = juce::KeyPress (juce::KeyPress::F1Key + (i - keyMatrix.length()));
        binding.description  = list->pads[i]->getPadName();
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

    // 48-key matrix:
    // 1..0, Q..P, A..;, Z..,. (40 keys) + F1..F8 (8 keys)
    const juce::String keyMatrix = "1234567890QWERTYUIOPASDFGHJKL;ZXCVBNM,.";
    const int maxMappedPads = juce::jmin (list->pads.size(), keyMatrix.length() + 8);

    for (int i = 0; i < maxMappedPads; ++i)
    {
        auto* pad = list->pads[i];
        if (pad == nullptr || ! pad->hasAudioFile())
            continue;

        juce::KeyPress kp;
        if (i < keyMatrix.length())
            kp = juce::KeyPress (keyMatrix[i]);
        else
            kp = juce::KeyPress (juce::KeyPress::F1Key + (i - keyMatrix.length()));

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
        dw->addKeyListener (this);
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
    commands.add (ShowControlCommandIDs::showAboutDialog);
    commands.add (ShowControlCommandIDs::checkForUpdates);
    commands.add (ShowControlCommandIDs::openPreferences);
}

void MainComponent::getCommandInfo (juce::CommandID commandID, juce::ApplicationCommandInfo& result)
{
    switch (commandID)
    {
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

        default:
            break;
    }
}

bool MainComponent::perform (const juce::ApplicationCommandTarget::InvocationInfo& info)
{
    switch (info.commandID)
    {
        case ShowControlCommandIDs::showAboutDialog:
            showAboutDialog();
            return true;

        case ShowControlCommandIDs::checkForUpdates:
            checkForUpdates();
            return true;

        case ShowControlCommandIDs::openPreferences:
            showPreferencesDialog (0);
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

    if (auto* scroll = dynamic_cast<ScrollableContainer*> (scrollContent.get()))
        scroll->setDarkMode (shouldBeDark);

    const auto pal = ShowTheme::get (shouldBeDark);
    gridSizeSlider.setColour (juce::Slider::thumbColourId, pal.accent);
    gridSizeSlider.setColour (juce::Slider::trackColourId, pal.sliderTrack);
    gridSizeSlider.setColour (juce::Slider::backgroundColourId, pal.panelElevated);
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

    // Ép TẤT CẢ windows trên màn hình (kể cả hộp thoại Cài đặt modal) cập nhật đồng loạt.
    broadcastLookAndFeelToAllWindows();

    lookAndFeelChanged();

    for (int i = 0; i < getNumChildComponents(); ++i)
        if (auto* child = getChildComponent (i))
            forceLookAndFeelRefreshRecursively (*child);

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

        sidebarPanel.updatePlayingStatus (i, listActive);
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

    const auto defaultTag = ShowTheme::darkPalette().textMuted;

    if (cue.tagColour != defaultTag)
        padElem.setAttribute ("tagColour", (int) cue.tagColour.getARGB());
}

static void readCueMetaFromPadElem (const juce::XmlElement& padElem, CueItem& cue)
{
    cue.preWaitMs  = padElem.getDoubleAttribute ("cuePreWaitMs", 0.0);
    cue.postWaitMs = padElem.getDoubleAttribute ("cuePostWaitMs", 0.0);
    cue.autoFollow = padElem.getBoolAttribute ("cueAutoFollow", false);
    cue.isEnabled  = padElem.getBoolAttribute ("cueEnabled", true);

    if (padElem.hasAttribute ("tagColour"))
        cue.tagColour = juce::Colour ((juce::uint32) padElem.getIntAttribute ("tagColour"));
}

static void writePadProjectState (juce::XmlElement& padElem, const SoundPad& pad)
{
    const auto custom = pad.getPadName();

    if (pad.getFilePath().isNotEmpty())
    {
        juce::File f (pad.getFilePath());
        if (custom != f.getFileNameWithoutExtension())
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
    gridSizeSlider.setValue (180.0, juce::dontSendNotification);
    sidebarWidth   = 250;
    inspectorWidth = 360;
    sidebarVisible   = true;
    inspectorVisible = true;

    inspectorPanel.selectPad (nullptr);
    masterDeckPanel.setActivePad (nullptr);
    lastUiSyncedPlayingPad = nullptr;

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

            gridSizeSlider.setValue (xml->getDoubleAttribute ("gridPadSize", 180.0), juce::dontSendNotification);
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

                        newList->pads.add (p);
                    }
                }

                allLists.add (newList);
                sidebarPanel.addSet (listName, newList->pads.size(), newList->isGrid,
                                     newList->useCueListPanel, newList->isLocked);
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

    xml->setAttribute ("gridPadSize", gridSizeSlider.getValue());
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

void MainComponent::saveApplicationState()
{
    const auto configFile = showcontrol::state::getCanonicalConfigFile();
    std::cout << "[CONFIG] saveApplicationState() — target: "
              << configFile.getFullPathName().toStdString() << std::endl;

    if (! showcontrol::state::ensureConfigParentDirectory (configFile))
        return;

    auto xml = buildProjectXml();

    if (xml == nullptr)
    {
        std::cout << "[CONFIG] [ERROR] buildProjectXml() returned null" << std::endl;
        return;
    }

    xml->writeTo (getProjectFile());

    juce::DynamicObject::Ptr root (new juce::DynamicObject());
    root->setProperty ("appTheme", themePreferenceId);
    root->setProperty ("appLanguage", languagePreferenceIndex);
    root->setProperty ("projectXml", xml->toString());
    root->setProperty ("savedAtMs", juce::Time::getMillisecondCounterHiRes());
    root->setProperty ("configPath", configFile.getFullPathName());

    const auto playlist = buildPlaylistJson();
    root->setProperty ("playlist", playlist);
    int trackCount = 0;

    if (playlist.isArray())
        for (const auto& listVar : *playlist.getArray())
            if (const auto tracks = listVar.getProperty ("tracks", juce::var()); tracks.isArray())
                trackCount += tracks.getArray()->size();

    std::cout << "[CONFIG] Saving theme=" << themePreferenceId
              << " lang=" << languagePreferenceIndex
              << " lists=" << (playlist.isArray() ? playlist.getArray()->size() : 0)
              << " tracks=" << trackCount << std::endl;

    if (! showcontrol::state::hardFlushJsonConfig (juce::var (root.get())))
        std::cout << "[CONFIG] [ERROR] hardFlushJsonConfig failed" << std::endl;
}

void MainComponent::saveProject()
{
    saveApplicationState();
}