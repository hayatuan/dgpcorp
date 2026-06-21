#include "ShowPluginHost.h"
#include "ShowControlMacWindow.h"
#include "ShowStockFxEditorDialog.h"

namespace showcontrol::plugins
{

struct ShowPluginHost::EditorEntry : public juce::DocumentWindow
{
    EditorEntry (PadPluginSlot& slotIn,
                 juce::AudioPluginInstance& instanceIn,
                 juce::Component* centreRelativeTo,
                 std::function<void()> onClosedIn)
        : juce::DocumentWindow (instanceIn.getName(),
                                juce::Desktop::getInstance().getDefaultLookAndFeel()
                                    .findColour (juce::ResizableWindow::backgroundColourId),
                                juce::DocumentWindow::closeButton),
          slot (&slotIn),
          onClosed (std::move (onClosedIn))
    {
        setUsingNativeTitleBar (true);
        setResizable (true, true);

        if (auto* editor = instanceIn.createEditorAndMakeActive())
        {
            setContentOwned (editor, true);

            const int w = juce::jmax (320, editor->getWidth());
            const int h = juce::jmax (180, editor->getHeight());
            centreWithSize (w, h);

            if (centreRelativeTo != nullptr)
                showcontrol::ui::centreFloatingWindowInMainApp (*this, centreRelativeTo);
        }
        else
        {
            setContentOwned (new juce::Label ({}, "Plugin has no editor."), true);
            centreWithSize (360, 120);
        }

        setVisible (true);
        toFront (true);
    }

    void closeButtonPressed() override
    {
        if (onClosed)
            onClosed();

        delete this;
    }

    PadPluginSlot* slot = nullptr;
    std::function<void()> onClosed;
};

ShowPluginHost& ShowPluginHost::shared() noexcept
{
    static ShowPluginHost host;
    return host;
}

ShowPluginHost::ShowPluginHost()
    : juce::Thread ("ShowCue Plugin Scanner")
{
}

ShowPluginHost::~ShowPluginHost()
{
    shutdown();
}

void ShowPluginHost::initialize()
{
    addDefaultFormatsToManager (formatManager);

    const auto cacheFile = getPluginCacheFile();

    if (cacheFile.existsAsFile())
    {
        if (auto xml = juce::XmlDocument::parse (cacheFile))
            knownList.recreateFromXml (*xml);
    }

    juce::PluginDirectoryScanner::applyBlacklistingsFromDeadMansPedal (knownList,
                                                                        getPluginDeadMansPedalFile());

    startBackgroundScan();
}

void ShowPluginHost::shutdown() noexcept
{
    shuttingDown.store (true, std::memory_order_release);
    scanRequested.store (false, std::memory_order_release);
    signalThreadShouldExit();

    if (isThreadRunning())
        waitForThreadToExit (8000);

    openEditors.clear();
    stockEditors.clear();
    knownList.clear();
}

void ShowPluginHost::startBackgroundScan()
{
    if (shuttingDown.load (std::memory_order_acquire))
        return;

    scanRequested.store (true, std::memory_order_release);
    startThread (juce::Thread::Priority::background);
}

void ShowPluginHost::saveCache() const
{
    const auto cacheFile = getPluginCacheFile();
    cacheFile.getParentDirectory().createDirectory();

    if (auto xml = knownList.createXml())
        xml->writeTo (cacheFile);
}

void ShowPluginHost::run()
{
    while (! threadShouldExit())
    {
        if (! scanRequested.exchange (false, std::memory_order_acq_rel))
        {
            wait (250);
            continue;
        }

        performScanAndCache();
    }
}

void ShowPluginHost::performScanAndCache()
{
    if (shuttingDown.load (std::memory_order_acquire))
        return;

    scanInProgress.store (true, std::memory_order_release);

    const auto searchPaths = getStandardPluginSearchPaths();
    const auto deadMansPedal = getPluginDeadMansPedalFile();

    for (int i = 0; i < formatManager.getNumFormats(); ++i)
    {
        if (threadShouldExit())
            break;

        if (auto* format = formatManager.getFormat (i))
        {
            auto paths = searchPaths;

            if (paths.getNumPaths() == 0)
                paths = format->getDefaultLocationsToSearch();

            juce::PluginDirectoryScanner scanner (knownList, *format, paths, true, deadMansPedal);

            juce::String nameOfPluginBeingScanned;
            while (scanner.scanNextFile (true, nameOfPluginBeingScanned))
            {
                if (threadShouldExit())
                    break;
            }
        }
    }

    juce::PluginDirectoryScanner::applyBlacklistingsFromDeadMansPedal (knownList, deadMansPedal);

    saveCache();
    scanInProgress.store (false, std::memory_order_release);

    juce::MessageManager::callAsync ([this]
    {
        if (! shuttingDown.load (std::memory_order_acquire))
            sendChangeMessage();
    });
}

std::unique_ptr<juce::AudioPluginInstance> ShowPluginHost::createInstance (const juce::PluginDescription& desc,
                                                                            double sampleRate,
                                                                            int blockSize,
                                                                            juce::String& error) const
{
    auto instance = formatManager.createPluginInstance (desc, sampleRate, blockSize, error);

    if (instance == nullptr)
        return nullptr;

    instance->setPlayConfigDetails (juce::jmax (1, instance->getTotalNumInputChannels()),
                                    juce::jmax (1, instance->getTotalNumOutputChannels()),
                                    sampleRate,
                                    blockSize);
    instance->prepareToPlay (sampleRate, blockSize);
    return instance;
}

void ShowPluginHost::commitStockFxToSlot (PadPluginSlot& slot,
                                           const juce::PluginDescription& desc,
                                           uint32_t expectedGeneration,
                                           const std::function<void()>& waitUntilAudioIdle,
                                           std::function<void (bool success)> onComplete)
{
    if (expectedGeneration != slot.getLoadGeneration())
    {
        if (onComplete)
            onComplete (false);

        return;
    }

    slot.setLoadInProgress (false);

    if (waitUntilAudioIdle)
        waitUntilAudioIdle();

    if (expectedGeneration != slot.getLoadGeneration())
    {
        if (onComplete)
            onComplete (false);

        return;
    }

    auto* oldInstance = slot.exchangeActive (nullptr, 0);
    schedulePluginInstanceDeletion (oldInstance);
    slot.clearStockFx();

    const auto mode = stockModeFromDescription (desc);

    if (mode == PadStockFxProcessor::Mode::bypass)
    {
        slot.clearStoredDescription();

        if (onComplete)
            onComplete (false);

        return;
    }

    slot.activateStockFx (mode);
    slot.setStoredDescription (desc);

    if (onComplete)
        onComplete (true);
}

void ShowPluginHost::commitInstanceToSlot (PadPluginSlot& slot,
                                            std::unique_ptr<juce::AudioPluginInstance> instance,
                                            const juce::PluginDescription& desc,
                                            uint32_t expectedGeneration,
                                            const std::function<void()>& waitUntilAudioIdle,
                                            std::function<void (bool success)> onComplete)
{
    if (expectedGeneration != slot.getLoadGeneration())
    {
        schedulePluginInstanceDeletion (instance.release());

        if (onComplete)
            onComplete (false);

        return;
    }

    slot.setLoadInProgress (false);

    if (instance == nullptr)
    {
        slot.clearStoredDescription();

        if (onComplete)
            onComplete (false);

        return;
    }

    if (waitUntilAudioIdle)
        waitUntilAudioIdle();

    if (expectedGeneration != slot.getLoadGeneration())
    {
        schedulePluginInstanceDeletion (instance.release());

        if (onComplete)
            onComplete (false);

        return;
    }

    const int latency = instance->getLatencySamples();
    auto* oldInstance = slot.exchangeActive (instance.release(), latency);
    slot.clearStockFx();
    slot.setStoredDescription (desc);
    schedulePluginInstanceDeletion (oldInstance);

    if (onComplete)
        onComplete (true);
}

void ShowPluginHost::asyncLoadPluginIntoSlot (PadPluginSlot& slot,
                                               const juce::PluginDescription& desc,
                                               double sampleRate,
                                               int blockSize,
                                               const std::function<void()>& waitUntilAudioIdle,
                                               std::function<void (bool success)> onComplete)
{
    if (shuttingDown.load (std::memory_order_acquire))
        return;

    auto alive = slot.getLifetimeToken();

    if (alive == nullptr || ! alive->load (std::memory_order_acquire))
        return;

    if (isStockFxDescription (desc))
    {
        const uint32_t generation = slot.bumpLoadGeneration();
        slot.setStoredDescription (desc);
        slot.setLoadInProgress (true);
        closeEditorsForSlot (slot);

        juce::MessageManager::callAsync ([this, slotPtr = &slot, generation, desc,
                                          waitUntilAudioIdle,
                                          onComplete = std::move (onComplete)]() mutable
        {
            commitStockFxToSlot (*slotPtr, desc, generation, waitUntilAudioIdle, std::move (onComplete));
        });
        return;
    }

    const uint32_t generation = slot.bumpLoadGeneration();
    slot.setStoredDescription (desc);
    slot.setLoadInProgress (true);
    closeEditorsForSlot (slot);

    auto* slotPtr = &slot;

    juce::Thread::launch (juce::Thread::Priority::background,
                          [host = this, slotPtr, alive, generation, desc, sampleRate, blockSize,
                           waitUntilAudioIdle, onComplete = std::move (onComplete)]() mutable
    {
        if (! alive->load (std::memory_order_acquire)
            || host->shuttingDown.load (std::memory_order_acquire))
        {
            juce::MessageManager::callAsync ([slotPtr, generation, onComplete = std::move (onComplete)]() mutable
            {
                if (generation == slotPtr->getLoadGeneration())
                    slotPtr->setLoadInProgress (false);

                if (onComplete)
                    onComplete (false);
            });
            return;
        }

        juce::String error;
        auto instance = host->createInstance (desc, sampleRate, blockSize, error);

        juce::MessageManager::callAsync ([host, slotPtr, alive, generation, desc,
                                            instance = std::move (instance),
                                            waitUntilAudioIdle,
                                            onComplete = std::move (onComplete)]() mutable
        {
            if (! alive->load (std::memory_order_acquire)
                || host->shuttingDown.load (std::memory_order_acquire))
            {
                schedulePluginInstanceDeletion (instance.release());

                if (generation == slotPtr->getLoadGeneration())
                    slotPtr->setLoadInProgress (false);

                if (onComplete)
                    onComplete (false);

                return;
            }

            host->commitInstanceToSlot (*slotPtr, std::move (instance), desc, generation,
                                      waitUntilAudioIdle, std::move (onComplete));
        });
    });
}

void ShowPluginHost::asyncClearPluginFromSlot (PadPluginSlot& slot,
                                              const std::function<void()>& waitUntilAudioIdle,
                                              std::function<void()> onComplete)
{
    closeEditorsForSlot (slot);
    slot.bumpLoadGeneration();
    slot.setLoadInProgress (false);

    auto alive = slot.getLifetimeToken();
    auto* slotPtr = &slot;

    juce::MessageManager::callAsync ([slotPtr, alive, waitUntilAudioIdle,
                                      onComplete = std::move (onComplete)]() mutable
    {
        if (alive == nullptr || ! alive->load (std::memory_order_acquire))
            return;

        if (waitUntilAudioIdle)
            waitUntilAudioIdle();

        auto* oldInstance = slotPtr->exchangeActive (nullptr, 0);
        slotPtr->clearStoredDescription();
        slotPtr->clearStockFx();
        schedulePluginInstanceDeletion (oldInstance);

        if (onComplete)
            onComplete();
    });
}

void ShowPluginHost::openStockFxEditorForSlot (PadPluginSlot& slot,
                                              juce::Component* centreRelativeTo,
                                              const std::function<void()>& onClosed)
{
    if (! slot.isStockFxActive())
        return;

    closeEditorsForSlot (slot);
    stockEditors.add (new showcontrol::ui::ShowStockFxEditorDialog (slot, centreRelativeTo, onClosed));
}

void ShowPluginHost::openPluginEditorForSlot (PadPluginSlot& slot,
                                               juce::Component* centreRelativeTo,
                                               const std::function<void()>& onClosed)
{
    auto* instance = slot.getInstanceForEditor();

    if (instance == nullptr)
        return;

    closeEditorsForSlot (slot);

    openEditors.add (new EditorEntry (slot, *instance, centreRelativeTo, onClosed));
}

void ShowPluginHost::closeEditorsForSlot (PadPluginSlot& slot) noexcept
{
    for (int i = openEditors.size(); --i >= 0;)
    {
        if (openEditors[i]->slot == &slot)
            openEditors.remove (i);
    }

    for (int i = stockEditors.size(); --i >= 0;)
    {
        if (&stockEditors[i]->getPluginSlot() == &slot)
            stockEditors.remove (i);
    }
}

} // namespace showcontrol::plugins
