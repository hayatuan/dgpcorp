#include "ShowDisplaySleepPreventer.h"

#ifndef WIN32_LEAN_AND_MEAN
 #define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace showcontrol::display
{
namespace
{
    int blockRefCount = 0;

    void platformAcquire()
    {
        SetThreadExecutionState (ES_CONTINUOUS | ES_DISPLAY_REQUIRED | ES_SYSTEM_REQUIRED);
    }

    void platformRelease()
    {
        SetThreadExecutionState (ES_CONTINUOUS);
    }
} // namespace

void acquireDisplaySleepBlock() noexcept
{
    if (++blockRefCount == 1)
        platformAcquire();
}

void releaseDisplaySleepBlock() noexcept
{
    if (blockRefCount <= 0)
        return;

    if (--blockRefCount == 0)
        platformRelease();
}

void releaseAllDisplaySleepBlocks() noexcept
{
    blockRefCount = 0;
    platformRelease();
}

} // namespace showcontrol::display
