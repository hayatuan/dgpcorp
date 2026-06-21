#include "AudioEngine.h"

bool AudioEngine::playCue (SoundPad* pad) noexcept
{
    if (pad == nullptr || ! pad->hasAudioFile())
        return false;

    if (pad->isFadeOutInProgress())
        return false;

    if (pad->isPaused())
        return resumeCue (pad);

    if (pad->isPlaying() || pad->isFading())
        return true;

    pad->triggerPlay();
    return true;
}

bool AudioEngine::pauseCue (SoundPad* pad) noexcept
{
    if (pad == nullptr || ! pad->hasAudioFile())
        return false;

    pad->triggerPause();
    return true;
}

bool AudioEngine::resumeCue (SoundPad* pad) noexcept
{
    if (pad == nullptr || ! pad->hasAudioFile() || ! pad->isPaused())
        return false;

    pad->triggerResume();
    return true;
}

bool AudioEngine::toggleCuePauseResume (SoundPad* pad) noexcept
{
    if (pad == nullptr || ! pad->hasAudioFile())
        return false;

    if (pad->isPaused())
        return resumeCue (pad);

    if (pad->isPlaying() || pad->isFading())
    {
        pad->triggerPause();
        return true;
    }

    return false;
}

bool AudioEngine::stopCue (SoundPad* pad) noexcept
{
    if (pad == nullptr)
        return false;

    if (! pad->isTransportActive())
        return false;

    pad->triggerStopImmediate();
    return true;
}

bool AudioEngine::playPflPreview (SoundPad* pad) noexcept
{
    if (pad == nullptr || ! pad->hasAudioFile())
        return false;

    if (pad->isFadeOutInProgress())
        return false;

    pad->setPflPreviewActive (true);

    if (pad->isPaused())
        return resumeCue (pad);

    if (pad->isPlaying() || pad->isFading())
        return true;

    pad->triggerPlay();
    return true;
}

bool AudioEngine::anyCueActive (const juce::OwnedArray<SoundPad>& pads) noexcept
{
    for (auto* p : pads)
        if (p != nullptr && p->isTransportActive())
            return true;

    return false;
}

void AudioEngine::requestTrackPreload (const juce::File& file) noexcept
{
    showcontrol::preload::sharedPool().requestPreload (file);
}

void AudioEngine::releaseTrackPreload (const juce::File& file) noexcept
{
    showcontrol::preload::sharedPool().releaseFile (file);
}

void AudioEngine::clearPreloadPool() noexcept
{
    showcontrol::preload::sharedPool().clear();
}

size_t AudioEngine::getPreloadPoolUsedBytes() const noexcept
{
    return showcontrol::preload::sharedPool().getUsedBytes();
}

void AudioEngine::initPluginScanner()
{
    showcontrol::plugins::ShowPluginHost::shared().initialize();
}

juce::KnownPluginList& AudioEngine::getKnownPluginList() noexcept
{
    return showcontrol::plugins::ShowPluginHost::shared().getKnownPluginList();
}

const juce::KnownPluginList& AudioEngine::getKnownPluginList() const noexcept
{
    return showcontrol::plugins::ShowPluginHost::shared().getKnownPluginList();
}

juce::AudioPluginFormatManager& AudioEngine::getPluginFormatManager() noexcept
{
    return showcontrol::plugins::ShowPluginHost::shared().getFormatManager();
}

const juce::AudioPluginFormatManager& AudioEngine::getPluginFormatManager() const noexcept
{
    return showcontrol::plugins::ShowPluginHost::shared().getFormatManager();
}

void AudioEngine::loadEffectIntoPad (SoundPad* pad, const juce::PluginDescription& desc)
{
    if (pad == nullptr)
        return;

    pad->applyAudioFxDescription (desc);
}

void AudioEngine::removeEffectFromPad (SoundPad* pad)
{
    if (pad == nullptr)
        return;

    pad->clearAudioFx();
}

void AudioEngine::openPluginEditorForPad (SoundPad* pad, juce::Component* centreRelativeTo)
{
    if (pad == nullptr)
        return;

    pad->openAudioFxEditor (centreRelativeTo);
}
