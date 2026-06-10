#pragma once
#include "SoundPad.h"

//==============================================================================
/**
 * AudioEngine — lớp điều phối phát CUE đồng thời (polyphony).
 *
 * Mixer thực tế: MultiOutputAudioCallback (mỗi SoundPad = một PadRealtimeSource).
 * Lớp này chỉ định nghĩa chính sách message-thread: play không cắt pad khác.
 */
class AudioEngine
{
public:
    AudioEngine() = default;

    /** Phát CUE — mix chồng lên các CUE đang chạy, không fade/stop pad khác. */
    bool playCue (SoundPad* pad) noexcept;

    /** Tạm dừng một CUE (chỉ cue-list playback). */
    bool pauseCue (SoundPad* pad) noexcept;

    /**
     * Resume thuần túy — chỉ postResume → transport.start() trên audio thread.
     * KHÔNG gọi play/postPlay/startPlaybackFromTrim (tránh setPosition về trimStart).
     */
    bool resumeCue (SoundPad* pad) noexcept;

    /** P — đang phát thì pause; đang pause thì resumeCue tại đúng vị trí. */
    bool toggleCuePauseResume (SoundPad* pad) noexcept;

    /** Dừng một CUE. */
    bool stopCue (SoundPad* pad) noexcept;

    /** Có ít nhất một pad trong danh sách đang transport-active. */
    static bool anyCueActive (const juce::OwnedArray<SoundPad>& pads) noexcept;

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioEngine)
};
