#pragma once
#include "SoundPad.h"
#include "ShowAudioPreloadCache.h"
#include "ShowPluginHost.h"

//==============================================================================
/**
 * AudioEngine — lớp điều phối phát CUE đồng thời (polyphony).
 *
 * Mixer thực tế: MultiOutputAudioCallback (mỗi SoundPad = một PadRealtimeSource).
 * Lớp này chỉ định nghĩa chính sách message-thread: play không cắt pad khác.
 * Slice preload pool (QLab/Farrago-style) giảm latency GO và tăng tốc mở app.
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

    /** PFL Preview — nghe thử qua bus monitor, không đổi route GO đã lưu. */
    bool playPflPreview (SoundPad* pad) noexcept;

    /** Có ít nhất một pad trong danh sách đang transport-active. */
    static bool anyCueActive (const juce::OwnedArray<SoundPad>& pads) noexcept;

    /** Queue slice preload trên background thread — không chặn UI. */
    void requestTrackPreload (const juce::File& file) noexcept;

    /** Gỡ cache preload cho một file (khi xóa/replace track). */
    void releaseTrackPreload (const juce::File& file) noexcept;

    /** Xóa toàn bộ pool preload (shutdown). */
    void clearPreloadPool() noexcept;

    size_t getPreloadPoolUsedBytes() const noexcept;

    /** Quét / cache VST3 + AU (macOS) — gọi một lần lúc khởi động app. */
    void initPluginScanner();

    juce::KnownPluginList& getKnownPluginList() noexcept;
    const juce::KnownPluginList& getKnownPluginList() const noexcept;

    juce::AudioPluginFormatManager& getPluginFormatManager() noexcept;
    const juce::AudioPluginFormatManager& getPluginFormatManager() const noexcept;

    void loadEffectIntoPad (SoundPad* pad, const juce::PluginDescription& desc);
    void removeEffectFromPad (SoundPad* pad);
    void openPluginEditorForPad (SoundPad* pad, juce::Component* centreRelativeTo);

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioEngine)
};
