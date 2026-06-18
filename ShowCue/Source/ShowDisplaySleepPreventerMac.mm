#include "ShowDisplaySleepPreventer.h"

#include <IOKit/pwr_mgt/IOPMLib.h>

namespace showcontrol::display
{
namespace
{
    int blockRefCount = 0;
    IOPMAssertionID assertionId = 0;

    void platformAcquire()
    {
        if (assertionId != 0)
            return;

        IOPMAssertionCreateWithName (kIOPMAssertionTypePreventUserIdleDisplaySleep,
                                     kIOPMAssertionLevelOn,
                                     CFSTR ("ShowCue fullscreen"),
                                     &assertionId);
    }

    void platformRelease()
    {
        if (assertionId == 0)
            return;

        IOPMAssertionRelease (assertionId);
        assertionId = 0;
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
