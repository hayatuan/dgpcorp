#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace showcontrol::mac
{
    /** Farrago-style: titlebar trong suốt, content tràn viền, traffic lights nổi trên nền app. */
    void applyFarragoFullSizeContentView (juce::Component& topLevelWindow);

    /** true nếu click trúng Button/Slider/TextEditor con — không kích hoạt kéo cửa sổ. */
    inline bool isMouseOverInteractiveDescendant (juce::Component& container, juce::Point<int> localPos)
    {
        if (auto* hit = container.getComponentAt (localPos))
        {
            for (auto* c = hit; c != nullptr; c = c->getParentComponent())
            {
                if (c == &container)
                    return false;

                if (dynamic_cast<juce::Button*> (c) != nullptr)
                    return true;

                if (dynamic_cast<juce::Slider*> (c) != nullptr)
                    return true;

                if (dynamic_cast<juce::TextEditor*> (c) != nullptr)
                    return true;
            }
        }

        return false;
    }

    /** Kéo cửa sổ native (macOS performWindowDragWithEvent). false → dùng ComponentDragger. */
    bool startDraggingWindow (juce::ComponentPeer& peer, const juce::MouseEvent& e);

    /** Farrago chrome + defer sau message loop (dialog phụ Trim/EQ/Confirm). */
    inline void deferFarragoFullSizeContentView (juce::Component& topLevelWindow)
    {
       #if JUCE_MAC
        applyFarragoFullSizeContentView (topLevelWindow);

        juce::Component::SafePointer<juce::Component> safeWindow (&topLevelWindow);
        juce::MessageManager::callAsync ([safeWindow]
        {
            if (safeWindow != nullptr)
                applyFarragoFullSizeContentView (*safeWindow);
        });
       #else
        juce::ignoreUnused (topLevelWindow);
       #endif
    }

    /** Dải đỉnh dialog — kéo cửa sổ độc lập; trả true nếu đã bắt drag. */
    inline bool tryDragTopLevelWindowFromMouseDown (juce::Component& context,
                                                    const juce::MouseEvent& e,
                                                    juce::ComponentDragger& dragger,
                                                    bool& dragActive,
                                                    int maxY = 40)
    {
        if (e.y >= maxY || ! e.mods.isLeftButtonDown())
            return false;

        if (isMouseOverInteractiveDescendant (context, e.getPosition()))
            return false;

        if (auto* topLevel = context.getTopLevelComponent())
        {
            if (auto* peer = topLevel->getPeer())
            {
               #if JUCE_MAC
                if (startDraggingWindow (*peer, e))
                    return true;
               #endif

                dragger.startDraggingComponent (topLevel, e.getEventRelativeTo (topLevel));
                dragActive = true;
                return true;
            }
        }

        return false;
    }
}

namespace showcontrol::ui
{
    /** Giữ khung cửa sổ trong vùng hiển thị an toàn của monitor (tương đương keepOnScreen). */
    inline juce::Rectangle<int> clampBoundsToDesktop (juce::Rectangle<int> bounds)
    {
        const auto& displays = juce::Desktop::getInstance().getDisplays();

        if (auto* display = displays.getDisplayForRect (bounds, false))
        {
            const auto safeArea = display->userBounds.toNearestInt();
            bounds.setPosition (juce::jlimit (safeArea.getX(),
                                              juce::jmax (safeArea.getX(), safeArea.getRight() - bounds.getWidth()),
                                              bounds.getX()),
                                juce::jlimit (safeArea.getY(),
                                              juce::jmax (safeArea.getY(), safeArea.getBottom() - bounds.getHeight()),
                                              bounds.getY()));
            return bounds;
        }

        const auto total = displays.getTotalBounds (true);
        bounds.setPosition (juce::jlimit (total.getX(),
                                          juce::jmax (total.getX(), total.getRight() - bounds.getWidth()),
                                          bounds.getX()),
                            juce::jlimit (total.getY(),
                                          juce::jmax (total.getY(), total.getBottom() - bounds.getHeight()),
                                          bounds.getY()));
        return bounds;
    }

    /** Ép cửa sổ phụ lọt tâm hình học app chính (DocumentWindow) trên màn hình. */
    inline void centreFloatingWindowInMainApp (juce::Component& floatingWindow,
                                               juce::Component* contextComponent)
    {
        if (contextComponent == nullptr)
            return;

        auto* mainApp = contextComponent->getTopLevelComponent();
        if (mainApp == nullptr)
            return;

        const auto mainAppBounds = mainApp->getScreenBounds();
        const int windowWidth  = floatingWindow.getWidth();
        const int windowHeight = floatingWindow.getHeight();

        auto targetBounds = juce::Justification { juce::Justification::centred }.appliedToRectangle (
            juce::Rectangle<int> (windowWidth, windowHeight),
            mainAppBounds);

        targetBounds = clampBoundsToDesktop (targetBounds);

        if (auto* resizable = dynamic_cast<juce::ResizableWindow*> (&floatingWindow))
            resizable->setUsingNativeTitleBar (true);

        floatingWindow.setBounds (targetBounds);
    }
}
