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

    if (pad->getFadeOutMs() < 5.0)
    {
        pad->triggerStopImmediate();
    }
    else if (pad->isPlaying() || pad->isPaused())
    {
        pad->startFadeOut();
    }
    else
    {
        // Đang fade-out dở — ép dừng theo cấu hình (0ms = hard stop).
        pad->stopTransportWithConfiguredFade();
    }

    return true;
}

bool AudioEngine::anyCueActive (const juce::OwnedArray<SoundPad>& pads) noexcept
{
    for (auto* p : pads)
        if (p != nullptr && p->isTransportActive())
            return true;

    return false;
}
