#pragma once
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include "SoundPad.h"
#include "ShowTheme.h"
#include "ShowControlLookAndFeel.h"
#include "ShowTagColors.h"
#include "ShowCrossComponentDrag.h"

class CueListPanel;

/** ListBox tùy chỉnh — đồng bộ BGM list (SoundPad): reorder capsule, không marquee khi kéo cụm. */
class CueListBox : public juce::ListBox
{
public:
    CueListBox (const juce::String& componentName, CueListPanel& ownerIn);

    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp   (const juce::MouseEvent& e) override;

    bool keyPressed (const juce::KeyPress& key) override;

    /** Vị trí dòng tại (x,y) — có padding 15px mép dưới hàng cuối. */
    int hitRowIndexAt (int x, int y) const;
    void triggerCueDragSession (const juce::MouseEvent& e);

    /** Thay snapshot hàng dọc mặc định — Capsule Pill đồng bộ BGM List. */
    juce::ScaledImage createSnapshotOfRows (const juce::SparseSet<int>& rows, int& x, int& y) override;

private:
    CueListPanel& owner;
};

struct CueItem
{
    juce::String name;
    juce::String filePath;
    juce::Colour tagColour = showcontrol::colours::defaultTagColour();
    double preWaitMs  = 0.0;
    double postWaitMs = 0.0;
    bool   isEnabled  = true;
    bool   autoFollow = false;
    int    cueNumber  = 0;
};

//==============================================================================
/** Danh sách CUE QLab-style — multi-select + kéo thả đồng bộ BGM list. */
class CueListPanel : public juce::Component,
                     public juce::DragAndDropTarget,
                     private juce::Timer,
                     private juce::Label::Listener
{
public:
    static constexpr int kHeaderH  = showcontrol::bgmList::kPlaylistHeaderHeight;
    static constexpr int kHeaderGap = showcontrol::bgmList::kPlaylistHeaderGap;
    static constexpr int kRowH     = showcontrol::bgmList::kPlaylistRowHeight;

    CueListPanel();
    ~CueListPanel() override;

    void haltActiveTimers() noexcept;

    std::function<void(int)>                         onCueSelected;
    std::function<void(int)>                         onCueTriggered;
    std::function<void(int, int)>                    onCueReordered;
    std::function<void()>                            onSortRowsAscending;
    std::function<bool()>                            canSortRows;
    std::function<void(const juce::Array<int>&, int)> onCuesBlockReordered;
    std::function<void(int, juce::Colour)>           onCueColorChanged;
    std::function<void(int, int)>                    onTrackMenuResult;
    std::function<void()>                            onDeleteKeyPressed;
    std::function<void(int)>                         onCueRightClick;
    std::function<void(const juce::Array<int>&)>     onCueSelectionChanged;
    std::function<void(int)>                         onCueListPlay;
    std::function<void(int)>                         onCueListPause;
    std::function<void(int)>                         onCueListStop;
    std::function<void(int, const juce::String&, const juce::String&)> onTrackRenamed;
    std::function<bool(const juce::KeyPress&)>       onChainedKeyPressed;

    enum class TrackMenuId : int
    {
        replaceFile  = 1,
        duplicate    = 2,
        trimEditor   = 3,
        revealFile   = 4,
        deleteItem   = 5,
        resetFade    = 6,
        renameTrack  = 7,
        sortAscending = 8
    };

    void lookAndFeelChanged() override;
    void refreshLocalizedText();
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

    /** Đồng bộ màu tag một dòng — live từ Inspector mà không reset scroll. */
    void syncCueTagColourAt (int index, juce::Colour colour);

    /** Ép ListBox đọc lại mảng cues và vẽ lại tức thì (live rename). */
    void refreshListBoxData (bool resetScroll = true);

    /** Vẽ lại một dòng (loop icon, highlight) — không repaint toàn list. */
    void repaintCueRow (int rowIndex);

    /** Khóa thanh cuộn nội bộ ListBox về dòng 0 — sau load/migration. */
    void resetListScrollToTop();

    void deleteSelectedCues();
    /** Xóa hàng loạt — duyệt index giảm dần, một giao dịch undo (BGM/CUE qua MainComponent). */
    void removeSelectedCues();
    void updateTableContent (bool resetScroll = true);

    juce::Array<int> getSelectedRowIndices() const;

    void paint (juce::Graphics& g) override;
    void paintOverChildren (juce::Graphics& g) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress& key) override;

    bool isInterestedInDragSource (const SourceDetails& dragSourceDetails) override;
    void itemDragEnter (const SourceDetails& dragSourceDetails) override;
    void itemDragMove (const SourceDetails& dragSourceDetails) override;
    void itemDragExit (const SourceDetails& dragSourceDetails) override;
    void itemDropped (const SourceDetails& dragSourceDetails) override;

    void setCrossCopyDropHighlight (bool active);

private:
    friend class CueListBox;
    friend class CueListBoxModel;

    class CueListBoxModel;
    class CueListRowCell;
    class CueReorderOverlay;
    class CueListHeaderComponent;

    friend class CueListRowCell;

    void timerCallback() override;
    void labelTextChanged (juce::Label* labelThatHasChanged) override;
    void editorShown (juce::Label* label, juce::TextEditor& editor) override;
    void editorHidden (juce::Label* label, juce::TextEditor& editor) override;
    void syncLiveTimer();
    bool anyRowTransportActive() const;
    void rebuildRowPaintFonts();
    void syncRowLiveTextCaches();
    bool reserveLoopSlotForRow (int rowIndex) const noexcept;
    bool shouldSilenceListenersForStateOp() const noexcept;
    bool shouldShowColumnHeader() const noexcept;
    void repaintReorderSourceRows();
    void paintHeader (juce::Graphics& g, juce::Rectangle<int> bounds) const;
    void syncHeaderToListScrollbar();
    void updateListBoxContentIfLaidOut() noexcept;
    void showTrackContextMenu (int cueIndex);
    void showListBackgroundSortMenu (const juce::MouseEvent& e);
    void sortCueRowsAscending();
    void beginTrackRename (int rowIndex);
    /** Ghi tên mới vào cues + pad; refresh list — idempotent. */
    bool commitTrackRenameFromLabel (int rowIndex);
    bool isPointInTrackNameColumn (int rowIndex, int localXInListBox) const;
    void layoutTrackNameLabelForRow (int rowIndex);
    void fireSelectionFromListBox();
    void removeCueFromDataModelAtIndex (int rowIndex);
    void applySelectionAnchorAfterRowRemoval (int firstDeletedRow);
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
    /** Capsule Glassmorphism — bóng lướt CUE độc quyền (đồng bộ thẩm mỹ BGM). */
    static juce::Image createPremiumCueDragImage (const juce::String& cueTitle, int selectedItemsCount);
    juce::String getCueTitleRowAtIndex (int rowIndex) const;
    void paintPremiumCueDragCapsuleAt (juce::Graphics& g,
                                       float centreX,
                                       float centreY,
                                       const juce::String& cueTitle,
                                       int selectedItemsCount) const;
    void repaintReorderInsertLineStrip (int insertIndex) const;
    void repaintReorderInsertLineStrips (int prevIndex, int nextIndex) const;
    void repaintDragCapsuleProxyStrip (juce::Point<int> centreInPanel) const;
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
    void stashCueRowReorderForJuceDrop();
    void commitCueRowReorderFromJuceDrop (int insertRowPosition);
    void clearCueRowReorderJuceDropPending() noexcept;
    void autoScrollListBoxForReorder (juce::Point<int> posInListBox);
    int computeRowInsertionIndexAtListY (int localYInListBox) const noexcept;
    void startCueJuceCrossDrag (const juce::MouseEvent& e);

    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp   (const juce::MouseEvent& e) override;

    std::unique_ptr<CueListBoxModel> model;
    std::unique_ptr<CueReorderOverlay> reorderOverlay;
    std::unique_ptr<CueListHeaderComponent> headerComponent;
    CueListBox listBox;
    TrackNameEditLabel trackNameLabel;
    juce::Array<CueItem> cues;
    int renamingTrackIndex = -1;
    /** Giữ row đang sửa qua editorHidden → labelTextChanged (JUCE gọi hidden trước). */
    int pendingRenameRowIndex = -1;
    std::function<SoundPad* (int index)> padAccessor;
    int  selectedIndex = -1;
    int  playingIndex  = -1;
    int  armedIndex    = -1;
    bool isDarkMode    = true;

    struct RowPaintFonts
    {
        juce::Font indexBold  { juce::FontOptions() };
        juce::Font namePlain  { juce::FontOptions() };
        juce::Font nameBold   { juce::FontOptions() };
        juce::Font timer      { juce::FontOptions() };
        juce::Font timerBold  { juce::FontOptions() };
        juce::Font autoFollow { juce::FontOptions() };
    };

    RowPaintFonts rowFonts;
    juce::Array<juce::String> rowRemainingText;
    juce::Array<juce::String> rowElapsedText;

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

    bool localRowReorderDragActive = false;
    bool cueJuceDragStarted   = false;
    bool cueRowReorderAwaitingJuceDrop = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CueListPanel)
};
