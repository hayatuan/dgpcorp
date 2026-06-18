#pragma once
#include "SoundPad.h"
#include "ShowPadGridMatrix.h"
#include "ShowTheme.h"
#include "ShowLocalization.h"
#include "ShowTagColors.h"
#include "ShowCrossComponentDrag.h"
#include <unordered_map>

class MainComponent;

class PadPanel  : public juce::Component,
                  public juce::DragAndDropTarget,
                  public juce::Timer
{
public:
    enum class EmptyListHint
    {
        none,
        cueGrid,
        bgmRows
    };

    struct BoundedGridLayout
    {
        int activeRows = 1;
        int activeCols = showcontrol::padgrid::kMinBoundedCols;
        int cellW = 1;
        int cellH = 1;
    };

    PadPanel() { setOpaque (true); setWantsKeyboardFocus (true); }
    ~PadPanel() override { stopTimer(); }

    std::function<void (const juce::MouseEvent&)> onBackgroundRightClick;
    std::function<void (const juce::MouseEvent&)> onBackgroundMouseDown;
    std::function<void (const juce::MouseEvent&)> onBackgroundMouseDrag;
    std::function<void (const juce::MouseEvent&)> onBackgroundMouseUp;
    std::function<void (SoundPad*, int row, int col)> onPadDroppedAtCell;
    std::function<void (SoundPad* pad)> onGridFocusPadChanged;
    std::function<bool (const juce::KeyPress&)> onChainedKeyPressed;

    void setPadList (juce::OwnedArray<SoundPad>* padsIn) noexcept;

    void setMatrixLayoutEnabled (bool enabled) noexcept { matrixLayoutEnabled = enabled; }
    bool isMatrixLayoutEnabled() const noexcept { return matrixLayoutEnabled; }

    void setPadChildrenMousePassthrough (bool passthrough) noexcept;

    void beginInternalGridDragSession (SoundPad* sourcePad) noexcept;
    void applyGridDragHoverAtLocalPoint (juce::Point<int> localPosition,
                                         SoundPad* sourcePad = nullptr) noexcept;

    void setDraggingActive (bool active);
    bool getDraggingActive() const noexcept { return isDraggingActive; }

    void setDarkMode (bool dark);
    void setEmptyListHint (EmptyListHint hint);

    void resyncAndLayout();
    /** Nạp lại bàn cờ PAD từ gridRow/gridCol đã lưu — tối đa kMaxPadsAllowed ô. */
    void refreshPadGrid();
    SoundPad* getPadAtGrid (int row, int col) const noexcept;
    bool hasAnyPadPlaying() const noexcept;
    SoundPad* getCurrentlyPlayingPadTrack() const noexcept;
    juce::Point<int> gridCellAtPoint (juce::Point<int> local) const noexcept;
    BoundedGridLayout getBoundedGridLayout() const noexcept { return boundedLayout; }

    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent& e) override;
    bool keyPressed (const juce::KeyPress& key) override;
    void resized() override;
    void paint (juce::Graphics& g) override;
    void paintOverChildren (juce::Graphics& g) override;

    bool isInterestedInDragSource (const SourceDetails& dragSourceDetails) override;
    void itemDragEnter (const SourceDetails& dragSourceDetails) override;
    void itemDragMove (const SourceDetails& dragSourceDetails) override;
    void itemDragExit (const SourceDetails& dragSourceDetails) override;
    void itemDropped (const SourceDetails& dragSourceDetails) override;

    void timerCallback() override;

protected:
    void visibilityChanged() override;

private:
    juce::OwnedArray<SoundPad>* pads = nullptr;
    bool matrixLayoutEnabled = true;
    bool isDraggingActive = false;
    bool isDarkMode = true;
    EmptyListHint emptyListHint = EmptyListHint::none;
    BoundedGridLayout boundedLayout;

    void paintDragLandingGrid (juce::Graphics& g) const;
    void paintHoveredCellNeonHighlight (juce::Graphics& g) const;
    void clearDragPreview() noexcept;
    void collapseDragGridToStaticFloor() noexcept;
    void captureDragSource (const SourceDetails& dragSourceDetails) noexcept;
    void restoreSourcePadDragVisual (const SourceDetails& dragSourceDetails) const;
    juce::Colour resolveActiveDragColour (const SoundPad* pad) const noexcept;
    bool isCellTreatAsEmptyForDragGrid (int row, int col) const noexcept;
    void resetDragGridToStaticBase() noexcept;
    void rebuildStaticBaseGridFromPads() noexcept;
    void ensureLiquidTimerRunning() noexcept;
    juce::Rectangle<int> cellBoundsForActiveGrid (int row, int col) const noexcept;
    juce::Point<int> activeGridCellAtLocalPoint (juce::Point<int> local) const noexcept;
    SoundPad* resolveDragSourcePad (const SourceDetails& dragSourceDetails) const noexcept;
    void finalizeLocalDropAndCollapseGrid (MainComponent* mainComp,
                                           const SourceDetails& dragSourceDetails) noexcept;

    bool isSearchWindowFocused() const noexcept;

    int lastActiveRows = showcontrol::padgrid::kMinBoundedRows;
    int lastActiveCols = showcontrol::padgrid::kMinBoundedCols;
    int dragActiveRows = showcontrol::padgrid::kMinBoundedRows;
    int dragActiveCols = showcontrol::padgrid::kMinBoundedCols;
    int baseRows = showcontrol::padgrid::kMinBoundedRows;
    int baseCols = showcontrol::padgrid::kMinBoundedCols;
    int hoveredRow = -1;
    int hoveredCol = -1;
    SoundPad* dragSourcePad = nullptr;
    juce::Colour activeDragColour;

    std::unordered_map<SoundPad*, juce::Rectangle<int>> targetBoundsMap;
    bool isInitialLayout = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PadPanel)
};
