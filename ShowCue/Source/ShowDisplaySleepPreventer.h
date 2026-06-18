#pragma once

namespace showcontrol::display
{
    /** Ref-counted: block display sleep while any ShowCue window is fullscreen. */
    void acquireDisplaySleepBlock() noexcept;
    void releaseDisplaySleepBlock() noexcept;
    void releaseAllDisplaySleepBlocks() noexcept;

    /** Per-window helper — sync on fullscreen transitions and release on destroy. */
    struct FullscreenSleepGuard
    {
        void sync (bool isFullscreen) noexcept
        {
            if (isFullscreen == active)
                return;

            if (isFullscreen)
                acquireDisplaySleepBlock();
            else
                releaseDisplaySleepBlock();

            active = isFullscreen;
        }

        ~FullscreenSleepGuard() noexcept
        {
            if (active)
                releaseDisplaySleepBlock();
        }

    private:
        bool active = false;
    };
}
