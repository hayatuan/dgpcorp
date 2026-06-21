#pragma once

#include <atomic>
#include <functional>
#include <memory>

#include <juce_audio_processors/juce_audio_processors.h>

#include "ShowDsp.h"

namespace showcontrol::ui { class ShowStockFxEditorDialog; }

namespace showcontrol::plugins
{

inline juce::File getPluginCacheFile()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
               .getChildFile ("Application Support/DGP/ShowCue/plugin_cache.xml");
}

inline void schedulePluginInstanceDeletion (juce::AudioPluginInstance* instance) noexcept
{
    if (instance == nullptr)
        return;

    juce::MessageManager::callAsync ([instance]
    {
        instance->releaseResources();
        delete instance;
    });
}

inline bool pluginDescriptionsMatch (const juce::PluginDescription& a,
                                     const juce::PluginDescription& b) noexcept
{
    return a.name == b.name && a.fileOrIdentifier == b.fileOrIdentifier;
}

inline constexpr const char* kStockEqIdentifier      = "showcue://stock/eq3";
inline constexpr const char* kStockLimiterIdentifier = "showcue://stock/limiter";

inline constexpr int kFxComboIdNone            = 1;
inline constexpr int kFxComboIdStockEq         = 2;
inline constexpr int kFxComboIdStockLimiter    = 3;
inline constexpr int kFxComboIdThirdPartyBase  = 1000;

inline int thirdPartyComboIdForIndex (int index) noexcept
{
    return kFxComboIdThirdPartyBase + index;
}

inline bool isThirdPartyComboId (int comboId) noexcept
{
    return comboId >= kFxComboIdThirdPartyBase;
}

inline int indexFromThirdPartyComboId (int comboId) noexcept
{
    return comboId - kFxComboIdThirdPartyBase;
}

inline juce::PluginDescription makeStockEqDescription() noexcept
{
    juce::PluginDescription desc;
    desc.name              = "Stock EQ (3-Band)";
    desc.descriptiveName   = "ShowCue Stock EQ (3-Band)";
    desc.pluginFormatName  = "ShowCueInternal";
    desc.fileOrIdentifier  = kStockEqIdentifier;
    desc.manufacturerName  = "ShowCue";
    desc.category          = "ShowCue Stock FX";
    desc.isInstrument      = false;
    return desc;
}

inline juce::PluginDescription makeStockLimiterDescription() noexcept
{
    juce::PluginDescription desc;
    desc.name              = "Stock Limiter (Bảo vệ loa)";
    desc.descriptiveName   = "ShowCue Stock Limiter";
    desc.pluginFormatName  = "ShowCueInternal";
    desc.fileOrIdentifier  = kStockLimiterIdentifier;
    desc.manufacturerName  = "ShowCue";
    desc.category          = "ShowCue Stock FX";
    desc.isInstrument      = false;
    return desc;
}

inline bool isStockFxDescription (const juce::PluginDescription& desc) noexcept
{
    return desc.fileOrIdentifier == kStockEqIdentifier
        || desc.fileOrIdentifier == kStockLimiterIdentifier;
}

inline PadStockFxProcessor::Mode stockModeFromDescription (const juce::PluginDescription& desc) noexcept
{
    if (desc.fileOrIdentifier == kStockEqIdentifier)
        return PadStockFxProcessor::Mode::eq3;

    if (desc.fileOrIdentifier == kStockLimiterIdentifier)
        return PadStockFxProcessor::Mode::limiter;

    return PadStockFxProcessor::Mode::bypass;
}

inline juce::File getPluginDeadMansPedalFile()
{
    return getPluginCacheFile().getSiblingFile ("plugin_scan_failures.txt");
}

inline juce::FileSearchPath getStandardPluginSearchPaths()
{
    juce::FileSearchPath paths;

   #if JUCE_WINDOWS
    paths.add (juce::File ("C:\\Program Files\\Common Files\\VST3"));
    paths.add (juce::File ("C:\\Program Files\\VST3"));
   #elif JUCE_MAC
    paths.add (juce::File ("/Library/Audio/Plug-Ins/Components"));
    paths.add (juce::File ("/Library/Audio/Plug-Ins/VST3"));
    paths.add (juce::File::getSpecialLocation (juce::File::userHomeDirectory)
                   .getChildFile ("Library/Audio/Plug-Ins/Components"));
    paths.add (juce::File::getSpecialLocation (juce::File::userHomeDirectory)
                   .getChildFile ("Library/Audio/Plug-Ins/VST3"));
   #endif

    return paths;
}

//==============================================================================
/** Slot FX per pad — load trên background thread, swap con trỏ atomic 0ms, PDC. */
class PadPluginSlot
{
public:
    PadPluginSlot()
        : lifetimeToken (std::make_shared<std::atomic<bool>> (true))
    {
    }

    ~PadPluginSlot()
    {
        lifetimeToken->store (false, std::memory_order_release);

        if (auto* inst = activePlugin.exchange (nullptr, std::memory_order_acq_rel))
            schedulePluginInstanceDeletion (inst);
    }

    void prepareToPlay (double sampleRateIn, int blockSizeIn) noexcept
    {
        sampleRateHz = sampleRateIn;
        blockSizeSamples = juce::jmax (1, blockSizeIn);
        pdcWorkBuffer.setSize (2, blockSizeSamples + 8192, false, false, true);
        stockFx.prepare (sampleRateHz, blockSizeSamples);

        if (auto* inst = activePlugin.load (std::memory_order_acquire))
            inst->prepareToPlay (sampleRateHz, blockSizeSamples);
    }

    void releaseResources() noexcept
    {
        if (auto* inst = activePlugin.load (std::memory_order_acquire))
            inst->releaseResources();

        stockFx.reset();
    }

    /** @return false = smart bypass (không có FX, caller bỏ qua hoàn toàn). */
    bool process (juce::AudioBuffer<float>& buffer, int startSample, int numSamples,
                  const juce::AudioBuffer<float>* lookaheadSamples, int lookaheadCount) noexcept
    {
        if (stockFx.getMode() != PadStockFxProcessor::Mode::bypass)
        {
            stockFx.process (buffer, startSample, numSamples);
            return true;
        }

        auto* inst = activePlugin.load (std::memory_order_acquire);

        if (inst == nullptr || numSamples <= 0)
            return false;

        const int channels = juce::jmin (2, buffer.getNumChannels());

        if (channels <= 0)
            return false;

        const int latency = latencySamples.load (std::memory_order_relaxed);

        if (latency <= 0 || lookaheadSamples == nullptr || lookaheadCount <= 0)
            return processDirect (inst, buffer, startSample, numSamples, channels);

        return processWithPdc (inst, buffer, startSample, numSamples, channels,
                             *lookaheadSamples, juce::jmin (latency, lookaheadCount), latency);
    }

    bool hasActivePlugin() const noexcept
    {
        return activePlugin.load (std::memory_order_acquire) != nullptr;
    }

    bool hasActiveFx() const noexcept
    {
        return hasActivePlugin()
            || stockFx.getMode() != PadStockFxProcessor::Mode::bypass;
    }

    bool isStockFxActive() const noexcept
    {
        return stockFx.getMode() != PadStockFxProcessor::Mode::bypass;
    }

    PadStockFxProcessor& getStockFx() noexcept { return stockFx; }
    const PadStockFxProcessor& getStockFx() const noexcept { return stockFx; }

    void activateStockFx (PadStockFxProcessor::Mode mode) noexcept
    {
        stockFx.setMode (mode);
        latencySamples.store (0, std::memory_order_release);
    }

    void clearStockFx() noexcept
    {
        stockFx.setMode (PadStockFxProcessor::Mode::bypass);
        latencySamples.store (0, std::memory_order_release);
    }

    bool isLoading() const noexcept
    {
        return loadInProgress.load (std::memory_order_relaxed);
    }

    int getLatencySamples() const noexcept
    {
        return latencySamples.load (std::memory_order_relaxed);
    }

    juce::PluginDescription getStoredDescription() const noexcept
    {
        return storedDescription;
    }

    void setStoredDescription (const juce::PluginDescription& desc) noexcept
    {
        storedDescription = desc;
    }

    void clearStoredDescription() noexcept
    {
        storedDescription = {};
    }

    bool hasStoredDescription() const noexcept
    {
        return storedDescription.name.isNotEmpty();
    }

    void setLoadInProgress (bool loading) noexcept
    {
        loadInProgress.store (loading, std::memory_order_release);
    }

    /** Message thread: hủy load async đang chờ commit. */
    uint32_t bumpLoadGeneration() noexcept
    {
        return loadGeneration.fetch_add (1, std::memory_order_acq_rel) + 1;
    }

    uint32_t getLoadGeneration() const noexcept
    {
        return loadGeneration.load (std::memory_order_acquire);
    }

    std::shared_ptr<std::atomic<bool>> getLifetimeToken() const noexcept
    {
        return lifetimeToken;
    }

    /** Message thread — hoán đổi con trỏ sau waitUntilAudioIdle; trả instance cũ để giải phóng. */
    juce::AudioPluginInstance* exchangeActive (juce::AudioPluginInstance* newInstance,
                                               int latency) noexcept
    {
        latencySamples.store (latency, std::memory_order_release);
        return activePlugin.exchange (newInstance, std::memory_order_acq_rel);
    }

    juce::AudioPluginInstance* getInstanceForEditor() const noexcept
    {
        return activePlugin.load (std::memory_order_acquire);
    }

private:
    bool processDirect (juce::AudioPluginInstance* inst,
                        juce::AudioBuffer<float>& buffer,
                        int startSample, int numSamples, int channels) noexcept
    {
        juce::AudioBuffer<float> sub (buffer.getArrayOfWritePointers(), channels,
                                      startSample, numSamples);
        juce::MidiBuffer emptyMidi;
        inst->processBlock (sub, emptyMidi);
        return true;
    }

    bool processWithPdc (juce::AudioPluginInstance* inst,
                         juce::AudioBuffer<float>& buffer,
                         int startSample, int numSamples, int channels,
                         const juce::AudioBuffer<float>& lookahead,
                         int lookaheadUsed, int latency) noexcept
    {
        const int workLen = numSamples + lookaheadUsed;

        if (pdcWorkBuffer.getNumSamples() < workLen)
            return processDirect (inst, buffer, startSample, numSamples, channels);

        for (int ch = 0; ch < channels; ++ch)
        {
            pdcWorkBuffer.copyFrom (ch, 0, buffer, ch, startSample, numSamples);

            const int laCh = juce::jmin (ch, lookahead.getNumChannels() - 1);
            pdcWorkBuffer.copyFrom (ch, numSamples, lookahead, laCh, 0, lookaheadUsed);
        }

        juce::MidiBuffer emptyMidi;
        inst->processBlock (pdcWorkBuffer, emptyMidi);

        const int outStart = juce::jmin (latency, workLen - numSamples);

        for (int ch = 0; ch < channels; ++ch)
            buffer.copyFrom (ch, startSample, pdcWorkBuffer, ch, outStart, numSamples);

        return true;
    }

    std::atomic<juce::AudioPluginInstance*> activePlugin { nullptr };
    std::atomic<int> latencySamples { 0 };
    std::atomic<bool> loadInProgress { false };
    std::atomic<uint32_t> loadGeneration { 0 };
    std::shared_ptr<std::atomic<bool>> lifetimeToken;

    juce::PluginDescription storedDescription;
    juce::AudioBuffer<float> pdcWorkBuffer;
    PadStockFxProcessor stockFx;
    double sampleRateHz = 44100.0;
    int blockSizeSamples = 512;
};

//==============================================================================
class ShowPluginHost final : public juce::ChangeBroadcaster,
                             private juce::Thread
{
public:
    static ShowPluginHost& shared() noexcept;

    void initialize();
    void shutdown() noexcept;

    juce::AudioPluginFormatManager& getFormatManager() noexcept { return formatManager; }
    const juce::AudioPluginFormatManager& getFormatManager() const noexcept { return formatManager; }

    juce::KnownPluginList& getKnownPluginList() noexcept { return knownList; }
    const juce::KnownPluginList& getKnownPluginList() const noexcept { return knownList; }

    void startBackgroundScan();
    bool isScanning() const noexcept { return scanInProgress.load (std::memory_order_acquire); }

    void saveCache() const;

    std::unique_ptr<juce::AudioPluginInstance> createInstance (const juce::PluginDescription& desc,
                                                               double sampleRate,
                                                               int blockSize,
                                                               juce::String& error) const;

    /** Nạp plugin trên background thread; swap con trỏ atomic trên message thread. */
    void asyncLoadPluginIntoSlot (PadPluginSlot& slot,
                                  const juce::PluginDescription& desc,
                                  double sampleRate,
                                  int blockSize,
                                  const std::function<void()>& waitUntilAudioIdle,
                                  std::function<void (bool success)> onComplete = {});

    void asyncClearPluginFromSlot (PadPluginSlot& slot,
                                   const std::function<void()>& waitUntilAudioIdle,
                                   std::function<void()> onComplete = {});

    void openPluginEditorForSlot (PadPluginSlot& slot,
                                  juce::Component* centreRelativeTo,
                                  const std::function<void()>& onClosed);

    void openStockFxEditorForSlot (PadPluginSlot& slot,
                                   juce::Component* centreRelativeTo,
                                   const std::function<void()>& onClosed = {});

    void closeEditorsForSlot (PadPluginSlot& slot) noexcept;

private:
    ShowPluginHost();
    ~ShowPluginHost() override;

    void run() override;
    void performScanAndCache();

    void commitInstanceToSlot (PadPluginSlot& slot,
                               std::unique_ptr<juce::AudioPluginInstance> instance,
                               const juce::PluginDescription& desc,
                               uint32_t loadGeneration,
                               const std::function<void()>& waitUntilAudioIdle,
                               std::function<void (bool success)> onComplete);

    void commitStockFxToSlot (PadPluginSlot& slot,
                              const juce::PluginDescription& desc,
                              uint32_t expectedGeneration,
                              const std::function<void()>& waitUntilAudioIdle,
                              std::function<void (bool success)> onComplete);

    juce::AudioPluginFormatManager formatManager;
    juce::KnownPluginList knownList;

    std::atomic<bool> scanRequested { false };
    std::atomic<bool> scanInProgress { false };
    std::atomic<bool> shuttingDown { false };

    struct EditorEntry;
    juce::OwnedArray<EditorEntry> openEditors;
    juce::OwnedArray<showcontrol::ui::ShowStockFxEditorDialog> stockEditors;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ShowPluginHost)
};

} // namespace showcontrol::plugins
