#include "ShowControlMacWindow.h"

#if JUCE_MAC
 #import <Cocoa/Cocoa.h>
#endif

namespace showcontrol::mac
{
bool startDraggingWindow (juce::ComponentPeer& peer, const juce::MouseEvent& e)
{
   #if JUCE_MAC
    juce::ignoreUnused (e);

    if (auto* nsView = (__bridge NSView*) peer.getNativeHandle())
    {
        if (NSWindow* nsWindow = [nsView window])
        {
            if (NSEvent* ev = [NSApp currentEvent])
            {
                [nsWindow performWindowDragWithEvent: ev];
                return true;
            }
        }
    }

    return false;
   #else
    juce::ignoreUnused (peer, e);
    return false;
   #endif
}

void applyFarragoFullSizeContentView (juce::Component& topLevelWindow)
{
   #if JUCE_MAC
    if (auto* peer = topLevelWindow.getPeer())
    {
        // getNativeHandle() trả về NSView (JUCEView) — không phải NSWindow
        auto* nsView = (__bridge NSView*) peer->getNativeHandle();

        if (nsView != nil)
        {
            NSWindow* nsWindow = [nsView window];

            if (nsWindow != nil)
            {
                nsWindow.titlebarAppearsTransparent = YES;
                nsWindow.titleVisibility = NSWindowTitleHidden;
                nsWindow.styleMask = (nsWindow.styleMask | NSWindowStyleMaskFullSizeContentView);

                // Cứu hộ di chuyển: kéo cửa sổ bằng vùng nền trống phía trên (Farrago)
                [nsWindow setMovableByWindowBackground: YES];
            }
        }
    }
   #else
    juce::ignoreUnused (topLevelWindow);
   #endif
}
}
