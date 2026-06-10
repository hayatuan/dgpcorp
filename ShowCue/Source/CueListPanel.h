#pragma once
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include "SoundPad.h"
#include "ShowTheme.h"

class CueListPanel;

/** ListBox tùy chỉnh — đồng bộ BGM list (SoundPad): reorder capsule, không marquee khi kéo cụm. */
class CueListBox : public juce::ListBox
{
public:
    CueListBox (const juce::String& componentName, CueListPanel& ownerIn);

    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp   (const juce::MouseEvent& e) override;

    /** Vị trí dòng tại (x,y) — có padding 15px mép dưới hàng cuối. */
    int hitRowIndexAt (int x, int y) const;
    void triggerCueDragSession (const juce::MouseEvent& e);

private:
    CueListPanel& owner;
};

struct CueItem
{
    juce::String name;
    juce::String filePath;
    juce::Colour tagColour = ShowTheme::darkPalette().textMuted;
    double preWaitMs  = 0.0;
    double postWaitMs = 0.0;
    bool   isEnabled  = true;
    bool   autoFollow = false;
    int    cueNumber  = 0;
};

//==============================================================================
/** Danh sách CUE QLab-style — multi-select + kéo thả đồng bộ BGM list. */
class CueListPanel : public juce::Component,
                     private juce::Timer
{
public:
    static constexpr int kHeaderH = 28;
    static constexpr int kRowH    = 44;

    CueListPanel();
    ~CueListPanel() override;

    std::function<void(int)>                         onCueSelected;
    std::function<void(int)>                         onCueTriggered;
    std::function<void(int, int)>                    onCueReordered;
    std::function<void(const juce::Array<int>&, int)> onCuesBlockReordered;
    std::function<void(int, juce::Colour)>           onCueColorChanged;
    std::function<void(int, int)>                    onTrackMenuResult;
    std::function<void()>                            onDeleteKeyPressed;
    std::function<void(int)>                         onCueRightClick;
    std::function<void(const juce::Array<int>&)>     onCueSelectionChanged;
    std::function<void(int)>                         onCueListPlay;
    std::function<void(int)>                         onCueListPause;
    std::function<void(int)>                         onCueListStop;

    enum class TrackMenuId : int
    {
        replaceFile = 1,
        duplicate   = 2,
        trimEditor  = 3,
        revealFile  = 4,
        deleteItem  = 5,
        resetFade   = 6
    };

    void lookAndFeelChanged() override;
    void updateTheme (bool isDark);
    void setCues (const juce::Array<CueItem>& newCues);
    void addCue (const CueItem& item);
    void removeCue (int index);
    void setSelectedIndex (int idx);
    void setSelectedIndices (const juce::Array<int>& indices);
    int  getSelectedIndex() const;
    int  getCueCount() const;
    void setPlayingIndex (int idx);
    void setArmedIndex (int idx);
    const CueItem* getCue (int index) const;

    void setPadAccessor (std::function<SoundPad* (int index)> accessor);
    void notifyPlaybackActivity();

    bool handleTransportKey (const juce::KeyPress& key);

    int getPreferredHeight() const;

    /** Ép ListBox đọc lại mảng cues và vẽ lại tức thì (live rename). */
    void refreshListBoxData();

    void paint (juce::Graphics& g) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress& key) override;

private:
    friend class CueListBox;
    friend class CueListBoxModel;

    class CueListBoxModel;
    class CueReorderOverlay;

    void timerCallback() override;
    void syncLiveTimer();
    bool anyRowTransportActive() const;
    void paintHeader (juce::Graphics& g, juce::Rectangle<int> bounds) const;
    void showTrackContextMenu (int cueIndex);
    void fireSelectionFromListBox();
    void applySelectionForRowClick (int clickedIndex, const juce::ModifierKeys& mods);
    void applyListBoxSelectedRows (const juce::SparseSet<int>& newRows);
    juce::Array<int> collectSelectedRowIndices() const;

    /** Thuật toán splice 3 bước — đồng bộ movePadsBlockInList (BGM). */
    void listBoxItemsDropped (int dragSourceRow,
                              const juce::SparseSet<int>& selectedRows,
                              int insertRowPosition);

    /** Overlay reorder + marquee — mirror MainComponent::paintPadReorderOverlay (BGM list). */
    void paintCueReorderOverlay (juce::Graphics& g) const;
    void paintReorderInsertLine (juce::Graphics& g) const;
    void paintCueRowReorderGhost (juce::Graphics& g) const;
    void paintMarquee (juce::Graphics& g) const;
    void updateCueReorderOverlayBounds();
    juce::Rectangle<int> getCueListInsertLineBounds() const;

    void beginCueMarquee (juce::Point<int> posInListBox, const juce::ModifierKeys& mods);
    void updateCueMarquee (juce::Point<int> posInListBox);
    void endCueMarquee();
    void handleCueListBoxMouseDragForMarquee (const juce::MouseEvent& e);
    /** Kịch bản A/B — kéo tức thì (đơn + đa chọn), không marquee. */
    bool tryImmediateCueRowDrag (const juce::MouseEvent& e);
    void triggerCueDragSession (const juce::MouseEvent& e);
    void lockCueRowReorderPressAt (int rowIndex);

    /** Luồng reorder — mirror beginPadReorder / updatePadReorder / endPadReorder (BGM). */
    void beginCueRowReorder (const juce::MouseEvent& e);
    void updateCueRowReorder (const juce::MouseEvent& e);
    void endCueRowReorder();
    void cancelCueRowReorder();
    void autoScrollListBoxForReorder (juce::Point<int> posInListBox);

    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp   (const juce::MouseEvent& e) override;

    std::unique_ptr<CueListBoxModel> model;
    std::unique_ptr<CueReorderOverlay> reorderOverlay;
    CueListBox listBox;
    juce::Array<CueItem> cues;
    std::function<SoundPad* (int index)> padAccessor;
    int  selectedIndex = -1;
    int  playingIndex  = -1;
    int  armedIndex    = -1;
    bool isDarkMode    = true;

    juce::SparseSet<int> dragSourceRows;
    int  dragSourceAnchorRow   = -1;
    int  cueRowReorderInsertIndex = -1;
    juce::Point<int> cueRowReorderPointerPos { 0, 0 };
    bool cueRowReorderActive   = false;
    /** true khi mouseDown trên dòng đã chọn — khóa marquee/reselect suốt phiên kéo. */
    bool cueRowReorderPressLocked = false;
    bool cueRowReorderStackAnimActive = false;
    juce::uint32 cueRowReorderStackAnimStartMs = 0;
    juce::int64  cueRowReorderLastAutoScrollMs = 0;

    bool marqueePrimed       = false;
    bool marqueeActive       = false;
    bool marqueeAdditive = false;
    juce::Array<int> marqueeBaseSelection;
    juce::Point<int> marqueeStartPos { 0, 0 };
    juce::Point<int> marqueeEndPos   { 0, 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CueListPanel)
};
