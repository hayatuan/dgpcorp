#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "ShowTheme.h"
#include "ShowControlLookAndFeel.h"
#include "ConfirmDeleteDialog.h"
#include "ShowFlatIcons.h"
#include "ShowLocalization.h"
#include "ShowTagColors.h"
#include "ShowCrossComponentDrag.h"
#include "ShowKeyboardInput.h"

class SearchBarStyle : public juce::LookAndFeel_V4
{
public:
    void setDarkMode (bool dark) { isDarkMode = dark; }

    void drawTextEditorOutline (juce::Graphics& g, int width, int height, juce::TextEditor& te) override
    {
        const auto pal = ShowTheme::get (isDarkMode);
        g.setColour (te.hasKeyboardFocus (true) ? pal.accent : pal.border);
        g.drawRoundedRectangle (0.5f, 0.5f, (float) width - 1.0f, (float) height - 1.0f, 6.0f, 1.0f);
    }

private:
    bool isDarkMode = true;
};

/** Ô sửa tên danh sách BGM/CUE — Enter lưu, Escape hủy. Message thread only. */
class ListNameTextEditor : public juce::TextEditor
{
public:
    std::function<void()> onCommit;
    std::function<void()> onCancel;

    ListNameTextEditor()
    {
        setComponentID ("showcue-inline-list-name-editor");
        setFont (showcontrol::bgmList::playlistCellFont());
    }

    bool keyPressed (const juce::KeyPress& key) override
    {
        if (key == juce::KeyPress::returnKey)
        {
            if (onCommit != nullptr)
                onCommit();

            return true;
        }

        if (key == juce::KeyPress::escapeKey)
        {
            if (onCancel != nullptr)
                onCancel();

            return true;
        }

        return juce::TextEditor::keyPressed (key);
    }
};

//==============================================================================
class SidebarPanel : public juce::Component,
                       public juce::FileDragAndDropTarget,
                       public juce::DragAndDropTarget
{
public:
    static constexpr int searchBarTop = 8;
    static constexpr int searchBarHeight = 32;
    static constexpr int searchBarBottomGap = 12;
    static constexpr int kBottomBarHeight = 48;
    /** Tọa độ dọc trong vùng cuộn (listScrollArea) — luôn bắt đầu từ 0. */
    static constexpr int listContentTop = 0;
    static constexpr int kSectionHeaderHeight = 28;
    static constexpr float kSectionHeaderFontSize = 16.0f;
    static constexpr int kSectionGapY = 8;
    static constexpr int kListRowStride = showcontrol::bgmList::kPlaylistRowHeight;
    static constexpr int kListRowVisualHeight = 31;
    static constexpr float kListRowFontSize = showcontrol::bgmList::kPlaylistCellFontSize;
    static constexpr float kListRowHotkeyFontSize = showcontrol::bgmList::kSidebarHotkeyFontSize;

    struct SetInfo
    {
        juce::String name;
        int tracks = 0;
        bool isGridMode = true;
        bool isPlaying = false;
        bool matchesSearch = true;
        bool isLooping = false;
        bool useListView = true;
        bool isLocked = false;
        juce::Colour themeColour = showcontrol::colours::defaultTagColour();
    };

    enum class SidebarMenuId : int
    {
        rename         = 1,
        duplicate      = 2,
        addSounds      = 3,
        switchView     = 4,
        toggleListLoop = 5,
        deleteItem     = 6,
        changeListColour = 100
    };

    SidebarPanel()
    {
        searchBar.setLookAndFeel (&searchBarStyle);
        addAndMakeVisible (searchBar);
        refreshSearchPlaceholder (ShowTheme::darkPalette().textMuted);
        lockSidebarRowFonts();
        searchBar.setJustification (juce::Justification::centredLeft);
        searchBar.onTextChange = [this] { if (onSearchChanged) onSearchChanged (searchBar.getText()); };

        listNameEditor.setVisible (false);
        listNameEditor.setMultiLine (false);
        listNameEditor.setReturnKeyStartsNewLine (false);
        listNameEditor.setScrollbarsShown (false);
        listNameEditor.setPopupMenuEnabled (false);
        listNameEditor.onCommit = [this] { commitInlineRename(); };
        listNameEditor.onCancel = [this] { cancelInlineRename(); };
        listNameEditor.onFocusLost = [this]
        {
            if (! inlineRenameEscapePressed)
                commitInlineRename();

            inlineRenameEscapePressed = false;
        };
        addChildComponent (listNameEditor);

        addAndMakeVisible (sidebarViewport);
        sidebarViewport.setViewedComponent (&listScrollArea, false);
        sidebarViewport.setScrollBarsShown (true, false);
        sidebarViewport.setScrollBarThickness (6);
        listScrollArea.setOpaque (false);

        addBtn.setButtonText ("+");
        addAndMakeVisible (addBtn);
        addBtn.onClick = [this] { showNewSetMenu(); };

        removeBtn.setButtonText ("-");
        addAndMakeVisible (removeBtn);
        removeBtn.onClick = [this] { requestDeleteList (selectedIndex); };

        setOpaque (true);
        updateThemeColors (true);
    }

    ~SidebarPanel() override
    {
        searchBar.setLookAndFeel (nullptr);
        sets.clear();
    }

    std::function<void (int, int, bool)> onListSelected;
    std::function<void (int, juce::String, int, bool)> onAddList;
    std::function<void (int)> onDeleteList;
    std::function<void (int, juce::String)> onRenameList;
    std::function<void (int, int)> onMoveList;
    std::function<void (int, bool)> onModeChanged;
    std::function<void (int, bool)> onLoopListToggled;
    std::function<void (const juce::String&)> onSearchChanged;
    std::function<void (int)> onDuplicateSet;
    std::function<void (int)> onAddSounds;
    /** Biến hình cấu trúc BGM (danh sách) <-> CUE (lưới PAD). */
    std::function<void (int)> onMorphSetStructure;
    /** Đổi màu nhãn danh sách BGM/CUE — MainComponent lưu JSON. */
    std::function<void (int, juce::Colour)> onListThemeColourChanged;
    /** Smart Import: thả thư mục nhạc — tạo list mới theo tên folder (targetIsBgm: true = BGM, false = CUE). */
    std::function<void (const juce::StringArray& folderPaths, bool targetIsBgm)> onFoldersSmartImport;
    /** Sao chép xuyên phân khu — nạp track vào playlist mục tiêu mà không đổi view trung tâm. */
    std::function<void (int, const juce::Array<showcontrol::crossdrag::TrackCopyRecord>&)> onCrossCopyDroppedToPlaylist;

    juce::String getSearchText() const { return searchBar.getText(); }
    bool isSearchBarFocused() const noexcept { return searchBar.hasKeyboardFocus (true); }

    bool isInterestedInDragSource (const juce::DragAndDropTarget::SourceDetails& dragSourceDetails) override;
    void itemDragEnter (const juce::DragAndDropTarget::SourceDetails& dragSourceDetails) override;
    void itemDragMove (const juce::DragAndDropTarget::SourceDetails& dragSourceDetails) override;
    void itemDragExit (const juce::DragAndDropTarget::SourceDetails& dragSourceDetails) override;
    void itemDropped (const juce::DragAndDropTarget::SourceDetails& dragSourceDetails) override;

    /** Smart Import: vùng BGM (phía trên) vs CUE (phía dưới) trong sidebar — localY theo tọa độ panel. */
    bool isSmartImportTargetBgm (int localY) const noexcept { return isDropTargetBgm (panelYToListContentY (localY)); }

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragEnter (const juce::StringArray& files, int x, int y) override;
    void fileDragMove (const juce::StringArray& files, int x, int y) override;
    void fileDragExit (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;

    void setPanicKeyListener (juce::KeyListener* listener) noexcept { panicKeyListener = listener; }

    void lookAndFeelChanged() override
    {
        juce::Component::lookAndFeelChanged();
        lockSidebarRowFonts();

        if (auto* showLaf = dynamic_cast<ShowControlLookAndFeel*> (&getLookAndFeel()))
            updateThemeColors (showLaf->isDarkMode());
    }

    void refreshLocalizedText()
    {
        refreshSearchPlaceholder (ShowTheme::get (isDarkMode).textMuted);
        repaint();
    }

    void updateThemeColors (bool isDark)
    {
        isDarkMode = isDark;
        searchBarStyle.setDarkMode (isDark);
        const auto pal = ShowTheme::get (isDark);

        refreshSearchPlaceholder (pal.textMuted);
        lockSidebarRowFonts();
        searchBar.setColour (juce::TextEditor::backgroundColourId, pal.panelElevated);
        searchBar.setColour (juce::TextEditor::textColourId, pal.textPrimary);
        searchBar.setColour (juce::CaretComponent::caretColourId, pal.accent);

        addBtn.setColour (juce::TextButton::buttonColourId, pal.panelElevated);
        addBtn.setColour (juce::TextButton::textColourOffId, pal.textPrimary);
        removeBtn.setColour (juce::TextButton::buttonColourId, pal.panelElevated);
        removeBtn.setColour (juce::TextButton::textColourOffId, pal.textPrimary);
        repaint();
    }

    void updatePlayingStatus (int index, bool playing)
    {
        if (index >= 0 && index < sets.size() && sets.getReference (index).isPlaying != playing)
        {
            sets.getReference (index).isPlaying = playing;
            repaint();
        }
    }

    void setListSearchMatch (int index, bool isMatch)
    {
        if (index >= 0 && index < sets.size())
        {
            sets.getReference (index).matchesSearch = isMatch;
            updateScrollableListHeight();
            repaint();
        }
    }

    void setListLooping (int index, bool isLooping)
    {
        if (index >= 0 && index < sets.size())
        {
            sets.getReference (index).isLooping = isLooping;
            repaint();
        }
    }

    void addSet (juce::String name, int tracks, bool isGrid, bool useListView = true, bool locked = false,
                 juce::Colour themeColour = showcontrol::colours::defaultTagColour())
    {
        sets.add ({ name, tracks, isGrid, false, true, false, useListView, locked, themeColour });
        updateScrollableListHeight();
        repaint();
    }

    void setListThemeColour (int index, juce::Colour colour)
    {
        if (index >= 0 && index < sets.size())
        {
            sets.getReference (index).themeColour = showcontrol::colours::snapToPalette (colour);
            repaint();
        }
    }

    juce::Colour getListThemeColour (int index) const
    {
        return (index >= 0 && index < sets.size())
                   ? sets[index].themeColour
                   : showcontrol::colours::defaultTagColour();
    }

    juce::Colour& getListThemeColourRef (int index) noexcept
    {
        return sets.getReference (index).themeColour;
    }

    void setListViewMode (int index, bool useListView)
    {
        if (index >= 0 && index < sets.size())
        {
            sets.getReference (index).useListView = useListView;
            repaint();
        }
    }

    void setListLocked (int index, bool locked)
    {
        if (index >= 0 && index < sets.size())
        {
            sets.getReference (index).isLocked = locked;
            repaint();
        }
    }

    bool isListLocked (int index) const
    {
        return index >= 0 && index < sets.size() && sets[index].isLocked;
    }
    void clearAllLists()
    {
        sets.clear();
        updateScrollableListHeight();
        repaint();
    }
    int getListCount() const { return sets.size(); }
    int getListTrackCount (int index) const { return (index >= 0 && index < sets.size()) ? sets[index].tracks : 0; }
    juce::String getListName (int index) const { return (index >= 0 && index < sets.size()) ? sets[index].name : ""; }

    void setListName (int index, const juce::String& name)
    {
        if (index >= 0 && index < sets.size())
        {
            sets.getReference (index).name = name;
            repaint();
        }
    }

    int getSelectedIndex() const { return selectedIndex; }
    void setSelectedIndex (int i) { selectedIndex = i; repaint(); }

    void setListTrackCount (int index, int tracks)
    {
        if (index >= 0 && index < sets.size())
        {
            sets.getReference (index).tracks = tracks;
            repaint();
        }
    }

    void paintOverChildren (juce::Graphics& g) override
    {
        juce::Component::paintOverChildren (g);

        if (dragListIndex >= 0 && listDragMoved && dragListInsertBefore >= 0)
        {
            if (auto lineY = getInsertLineY (dragListInsertBefore, dragListIndex))
            {
                const auto scroll = sidebarViewport.getViewPosition();
                const float panelLineY = (float) (sidebarViewport.getY() + *lineY - scroll.y);
                showcontrol::crossdrag::paintNeonRoundedCapInsertLine (g,
                                                                       panelLineY,
                                                                       (float) getWidth(),
                                                                       juce::Colour (0xFF4A90E2),
                                                                       6.0f,
                                                                       4.0f,
                                                                       2.5f);
            }
        }
    }

    void paint (juce::Graphics& g) override
    {
        const auto cols = showcontrol::ui::ThemePaintColours::read (*this);
        isDarkMode = cols.isDark;
        g.fillAll (cols.panelBg);
    }

    void paintListScrollArea (juce::Graphics& g)
    {
        const auto cols = showcontrol::ui::ThemePaintColours::read (*this);
        const int width = listScrollArea.getWidth();
        int y = listContentTop;

        paintSectionHeader (g, cols.textPrimary, y, width,
                            localizedSectionHeader (u8"DANH SÁCH NHẠC NỀN BGM", bgmExpanded));
        y += kSectionHeaderHeight;

        int bgmCount = 1;
        for (int i = 0; i < sets.size(); ++i)
        {
            if (! sets[i].isGridMode)
            {
                if (bgmExpanded && sets[i].matchesSearch)
                {
                    drawRowItem (g, i, y, bgmCount, width);
                    y += kListRowStride;
                }
                bgmCount++;
            }
        }

        y += kSectionGapY;
        paintSectionHeader (g, cols.textPrimary, y, width,
                            localizedSectionHeader (u8"DANH SÁCH CUE KỊCH BẢN", cueExpanded));
        y += kSectionHeaderHeight;

        int cueCount = 1;
        for (int i = 0; i < sets.size(); ++i)
        {
            if (sets[i].isGridMode)
            {
                if (cueExpanded && sets[i].matchesSearch)
                {
                    drawRowItem (g, i, y, cueCount, width);
                    y += kListRowStride;
                }
                cueCount++;
            }
        }

        if (folderDragActive)
        {
            const auto zone = getFolderDropZoneBounds (folderDragTargetIsBgm, width);
            g.setColour (cols.accent.withAlpha (0.14f));
            g.fillRect (zone);
            g.setColour (cols.accent.withAlpha (0.65f));
            g.drawRect (zone, 2);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const auto pos = e.getPosition();
        const bool clickInSearchBar = searchBar.getBounds().contains (pos);
        if (! clickInSearchBar && searchBar.hasKeyboardFocus (true))
            searchBar.giveAwayKeyboardFocus();
    }

    void handleListAreaMouseDown (const juce::MouseEvent& e)
    {
        const auto pos = e.getPosition();
        const int hit = hitTestListIndexAt (pos);

        if (hit >= 0)
        {
            if (e.getNumberOfClicks() >= 2 && ! e.mods.isRightButtonDown())
            {
                selectedIndex = hit;
                beginInlineRename (hit);
                return;
            }

            executeSelection (hit, e.mods.isRightButtonDown());

            if (! e.mods.isRightButtonDown())
            {
                dragListIndex = hit;
                dragListInsertBefore = hit;
                listDragMoved = false;
            }
            return;
        }

        dragListIndex = -1;
        dragListInsertBefore = -1;
        listDragMoved = false;

        int y = listContentTop;
        const int width = listScrollArea.getWidth();

        if (juce::Rectangle<int> (0, y, width, kSectionHeaderHeight).contains (pos))
        {
            bgmExpanded = ! bgmExpanded;
            updateScrollableListHeight();
            repaint();
            return;
        }

        y += kSectionHeaderHeight;
        for (int i = 0; i < sets.size(); ++i)
        {
            if (! sets[i].isGridMode && bgmExpanded && sets[i].matchesSearch)
                y += kListRowStride;
        }

        y += kSectionGapY;
        if (juce::Rectangle<int> (0, y, width, kSectionHeaderHeight).contains (pos))
        {
            cueExpanded = ! cueExpanded;
            updateScrollableListHeight();
            repaint();
        }
    }

    void handleListAreaMouseDrag (const juce::MouseEvent& e)
    {
        if (dragListIndex < 0)
            return;

        if (! listDragMoved && e.getDistanceFromDragStart() < 6)
            return;

        listDragMoved = true;
        dragListInsertBefore = hitTestListInsertBefore (e.getPosition());

        if (dragListInsertBefore >= 0)
            repaint();
    }

    void handleListAreaMouseUp (const juce::MouseEvent& e)
    {
        juce::ignoreUnused (e);

        if (dragListIndex >= 0 && listDragMoved && dragListInsertBefore >= 0)
        {
            int toIdx = dragListInsertBefore;
            if (toIdx > dragListIndex)
                --toIdx;

            if (toIdx != dragListIndex
                && ! sets[dragListIndex].isLocked
                && sets[dragListIndex].isGridMode == sets[juce::jlimit (0, sets.size() - 1, toIdx)].isGridMode
                && onMoveList)
            {
                onMoveList (dragListIndex, toIdx);
            }
        }

        dragListIndex = -1;
        dragListInsertBefore = -1;
        listDragMoved = false;
        repaint();
    }

    void resized() override
    {
        auto b = getLocalBounds();

        searchBar.setBounds (std::max (0, 12),
                             searchBarTop,
                             std::max (0, b.getWidth() - 24),
                             searchBarHeight);

        auto bottomBounds = b.removeFromBottom (kBottomBarHeight);
        addBtn.setBounds (std::max (0, bottomBounds.getX() + 12),
                          std::max (0, bottomBounds.getY() + 8),
                          44, 32);
        removeBtn.setBounds (std::max (0, bottomBounds.getRight() - 56),
                             std::max (0, bottomBounds.getY() + 8),
                             44, 32);

        b.removeFromTop (searchBarTop + searchBarHeight + searchBarBottomGap);
        sidebarViewport.setBounds (std::max (0, b.getX()),
                                   std::max (0, b.getY()),
                                   std::max (0, b.getWidth()),
                                   std::max (0, b.getHeight()));

        updateScrollableListHeight();

        if (renamingListIndex >= 0)
        {
            if (auto row = getRowBoundsForListIndex (renamingListIndex))
            {
                const auto regions = layoutSidebarRowRegions (*row);
                const auto scroll = sidebarViewport.getViewPosition();
                const auto vpBounds = sidebarViewport.getBounds();
                listNameEditor.setBounds (vpBounds.getX() + regions.nameArea.getX(),
                                          vpBounds.getY() + regions.nameArea.getY() - scroll.y,
                                          regions.nameArea.getWidth(),
                                          regions.nameArea.getHeight());
                listNameEditor.setJustification (juce::Justification::centredLeft);
                listNameEditor.toFront (false);
            }
        }
    }

private:
    class ListScrollArea : public juce::Component
    {
    public:
        explicit ListScrollArea (SidebarPanel& owner) : sidebar (owner) {}

        void paint (juce::Graphics& g) override { sidebar.paintListScrollArea (g); }

        void mouseDown (const juce::MouseEvent& e) override { sidebar.handleListAreaMouseDown (e); }
        void mouseDrag (const juce::MouseEvent& e) override { sidebar.handleListAreaMouseDrag (e); }
        void mouseUp (const juce::MouseEvent& e) override { sidebar.handleListAreaMouseUp (e); }

    private:
        SidebarPanel& sidebar;
    };

    juce::Viewport sidebarViewport;
    ListScrollArea listScrollArea { *this };
    juce::TextEditor searchBar;
    ListNameTextEditor listNameEditor;
    juce::TextButton addBtn, removeBtn;
    juce::Array<SetInfo> sets;
    int selectedIndex = 0;
    int renamingListIndex = -1;
    int dragListIndex = -1;
    int dragListInsertBefore = -1;
    bool listDragMoved = false;
    SearchBarStyle searchBarStyle;
    juce::KeyListener* panicKeyListener = nullptr;
    bool bgmExpanded = true, cueExpanded = true, isDarkMode = true;
    bool inlineRenameEscapePressed = false;
    juce::String inlineRenameOriginalName;
    bool folderDragActive = false;
    bool folderDragTargetIsBgm = true;
    int potentialDropRowIndex = -1;
    bool crossComponentDragActive = false;

    void refreshSearchPlaceholder (juce::Colour mutedColour)
    {
        searchBar.setTextToShowWhenEmpty (showcontrol::localization::tr (u8"🔍 Tìm kiếm..."), mutedColour);
    }

    int countBgmPlaylistRows() const noexcept
    {
        int count = 0;

        for (int i = 0; i < sets.size(); ++i)
            if (! sets[i].isGridMode && bgmExpanded && sets[i].matchesSearch)
                ++count;

        return count;
    }

    int countCuePlaylistRows() const noexcept
    {
        int count = 0;

        for (int i = 0; i < sets.size(); ++i)
            if (sets[i].isGridMode && cueExpanded && sets[i].matchesSearch)
                ++count;

        return count;
    }

    int computeListContentHeight() const noexcept
    {
        const int bgmRows = countBgmPlaylistRows();
        const int cueRows = countCuePlaylistRows();

        return listContentTop
             + kSectionHeaderHeight + bgmRows * kListRowStride
             + kSectionGapY
             + kSectionHeaderHeight + cueRows * kListRowStride;
    }

    void updateScrollableListHeight()
    {
        const auto vpBounds = sidebarViewport.getBounds();

        if (vpBounds.isEmpty())
            return;

        const int scrollBarW = sidebarViewport.getScrollBarThickness();
        const int contentWidth = std::max (0, vpBounds.getWidth() - scrollBarW);

        if (contentWidth <= 0)
            return;

        const int requiredHeight = computeListContentHeight();
        const int minViewportH = std::max (0, vpBounds.getHeight());
        const int contentHeight = std::max (minViewportH, std::max (1, requiredHeight));

        listScrollArea.setSize (contentWidth, contentHeight);
    }

    juce::Optional<juce::Point<int>> panelPointToListContent (juce::Point<int> panelPos) const noexcept
    {
        const auto vpBounds = sidebarViewport.getBounds();

        if (! vpBounds.contains (panelPos))
            return {};

        const auto scroll = sidebarViewport.getViewPosition();
        return juce::Point<int> (panelPos.x, panelPos.y - vpBounds.getY() + scroll.y);
    }

    int panelYToListContentY (int panelY) const noexcept
    {
        const auto vpBounds = sidebarViewport.getBounds();
        const auto scroll = sidebarViewport.getViewPosition();
        return panelY - vpBounds.getY() + scroll.y;
    }

    /** Phông chữ dòng danh sách BGM/CUE — Roboto 14.5pt, mọi trạng thái paint/editor. */
    static juce::Font sidebarListRowNameFont() noexcept
    {
        return showcontrol::bgmList::playlistCellFont();
    }

    static juce::Font sidebarListRowHotkeyFont() noexcept
    {
       #if JUCE_WINDOWS
        return ShowTheme::font (11.0f);
       #else
        return showcontrol::bgmList::playlistCellFontBold();
       #endif
    }

    /** Ép lại font sau PopupMenu / lookAndFeelChanged — tránh sụt size khi chuột phải hoặc chọn dòng. */
    void lockSidebarRowFonts()
    {
        const auto rowFont = sidebarListRowNameFont();
        searchBar.setFont (rowFont);
        listNameEditor.setFont (rowFont);

        if (renamingListIndex >= 0)
            ShowControlLookAndFeel::applyInlineListNameEditorStyle (listNameEditor, isDarkMode,
                                                                    renamingListIndex == selectedIndex);
    }

    /** Mũi tên mở/đóng — codepoint Unicode, tách khỏi chuỗi dịch để tránh vỡ UTF-8 "â ¼". */
    static juce::String sidebarChevronPrefix (bool expanded) noexcept
    {
        const auto glyph = expanded ? (juce::juce_wchar) 0x25BC  // ▼
                                    : (juce::juce_wchar) 0x25B6; // ▶
        return juce::String::charToString (glyph) + "  ";
    }

    static juce::String localizedSectionHeader (const char* utf8Key, bool expanded)
    {
        return sidebarChevronPrefix (expanded) + showcontrol::localization::tr (utf8Key);
    }

    static void paintSectionHeader (juce::Graphics& g, juce::Colour textColour,
                                    int y, int width, const juce::String& headerText)
    {
        g.setColour (textColour);
        g.setFont (ShowTheme::fontBold (kSectionHeaderFontSize));
        g.drawText (headerText, 16, y, width, kSectionHeaderHeight, juce::Justification::centredLeft);
    }

    int computeCueSectionStartY() const
    {
        int y = listContentTop;
        y += kSectionHeaderHeight;

        for (int i = 0; i < sets.size(); ++i)
        {
            if (! sets[i].isGridMode && bgmExpanded && sets[i].matchesSearch)
                y += kListRowStride;
        }

        y += kSectionGapY;
        return y;
    }

    bool isDropTargetBgm (int localY) const
    {
        return localY < computeCueSectionStartY();
    }

    juce::Rectangle<int> getFolderDropZoneBounds (bool targetIsBgm, int width) const
    {
        const int cueStartY = computeCueSectionStartY();

        if (targetIsBgm)
            return juce::Rectangle<int> (0, listContentTop, width, cueStartY - listContentTop);

        return juce::Rectangle<int> (0, cueStartY, width, juce::jmax (kSectionHeaderHeight, listScrollArea.getHeight() - cueStartY));
    }

    void updateFolderDragTargetFromY (int localY)
    {
        folderDragTargetIsBgm = isDropTargetBgm (localY);

        if (folderDragTargetIsBgm)
            bgmExpanded = true;
        else
            cueExpanded = true;
    }

    void clearFolderDragState()
    {
        folderDragActive = false;
        repaint();
    }

    void executeSelection (int index, bool isRightClick)
    {
        selectedIndex = index;
        if (isRightClick)
            showContextMenu (index);
        else if (onListSelected)
            onListSelected (index, sets[index].tracks, sets[index].isGridMode);
        repaint();
    }

    int hitTestListIndexAt (juce::Point<int> pos) const
    {
        const int width = listScrollArea.getWidth();
        int y = listContentTop;
        y += kSectionHeaderHeight;

        for (int i = 0; i < sets.size(); ++i)
        {
            if (! sets[i].isGridMode && bgmExpanded && sets[i].matchesSearch)
            {
                if (juce::Rectangle<int> (0, y, width, kListRowVisualHeight).contains (pos))
                    return i;
                y += kListRowStride;
            }
        }

        y += kSectionGapY;
        y += kSectionHeaderHeight;

        for (int i = 0; i < sets.size(); ++i)
        {
            if (sets[i].isGridMode && cueExpanded && sets[i].matchesSearch)
            {
                if (juce::Rectangle<int> (0, y, width, kListRowVisualHeight).contains (pos))
                    return i;
                y += kListRowStride;
            }
        }

        return -1;
    }

    struct SidebarRowLayout
    {
        juce::Rectangle<int> nameArea;
        juce::Rectangle<int> loopIconArea;
        juce::Rectangle<int> speakerIconArea;
        juce::Rectangle<int> hotkeyArea;
    };

    static constexpr int kRowRightPadding   = 12;
   #if JUCE_WINDOWS
    static constexpr int kRowHotkeyWidth    = 78;
   #else
    static constexpr int kRowHotkeyWidth    = 28;
   #endif
    static constexpr int kRowIconSlotWidth  = 18;
    static constexpr int kRowIconGap        = 6;
    static constexpr int kRowLeftPadding    = 14;

    /** Cắt lát ngang dòng list — icon cố định lề phải, tên chiếm phần còn lại bên trái. */
    static SidebarRowLayout layoutSidebarRowRegions (juce::Rectangle<int> rowArea) noexcept
    {
        SidebarRowLayout layout;
        auto area = rowArea;

        area.removeFromRight (kRowRightPadding);

        layout.hotkeyArea = area.removeFromRight (kRowHotkeyWidth);
        area.removeFromRight (kRowIconGap);

        layout.speakerIconArea = area.removeFromRight (kRowIconSlotWidth);
        area.removeFromRight (kRowIconGap);

        layout.loopIconArea = area.removeFromRight (kRowIconSlotWidth);
        area.removeFromRight (kRowIconGap);

        area.removeFromLeft (kRowLeftPadding);
        layout.nameArea = area;

        return layout;
    }

    static juce::Rectangle<float> centerIconInSlot (juce::Rectangle<int> slot) noexcept
    {
        const float sz = showcontrol::icons::kListIconSize;
        return { (float) slot.getX() + ((float) slot.getWidth() - sz) * 0.5f,
                 (float) slot.getY() + ((float) slot.getHeight() - sz) * 0.5f,
                 sz, sz };
    }

    /** Vùng tên — đồng bộ nhãn paint() và TextEditor inline (resized). */
    static juce::Rectangle<int> layoutListNameArea (juce::Rectangle<int> rowArea) noexcept
    {
        return layoutSidebarRowRegions (rowArea).nameArea;
    }

    juce::Optional<juce::Rectangle<int>> getRowBoundsForListIndex (int listIndex) const
    {
        const int width = listScrollArea.getWidth();
        int y = listContentTop;
        y += kSectionHeaderHeight;

        for (int i = 0; i < sets.size(); ++i)
        {
            if (! sets[i].isGridMode && bgmExpanded && sets[i].matchesSearch)
            {
                if (i == listIndex)
                    return juce::Rectangle<int> (10, y, width - 20, kListRowVisualHeight);
                y += kListRowStride;
            }
        }

        y += kSectionGapY;
        y += kSectionHeaderHeight;

        for (int i = 0; i < sets.size(); ++i)
        {
            if (sets[i].isGridMode && cueExpanded && sets[i].matchesSearch)
            {
                if (i == listIndex)
                    return juce::Rectangle<int> (10, y, width - 20, kListRowVisualHeight);
                y += kListRowStride;
            }
        }

        return {};
    }

    struct ListSectionBounds
    {
        int firstListIndex = -1;
        int lastListIndex = -1;
        int insertMin = 0;
        int insertMax = 0;
    };

    ListSectionBounds getListSectionBounds (int listIndex) const noexcept
    {
        ListSectionBounds bounds;

        if (! juce::isPositiveAndBelow (listIndex, sets.size()))
            return bounds;

        const bool isCueSection = sets[listIndex].isGridMode;

        for (int i = 0; i < sets.size(); ++i)
        {
            if (sets[i].isGridMode != isCueSection)
                continue;

            if (bounds.firstListIndex < 0)
                bounds.firstListIndex = i;

            bounds.lastListIndex = i;
        }

        if (bounds.firstListIndex >= 0)
        {
            bounds.insertMin = bounds.firstListIndex;
            bounds.insertMax = bounds.lastListIndex + 1;
        }

        return bounds;
    }

    juce::Optional<juce::Rectangle<int>> getListSectionRowsBounds (int listIndex) const
    {
        if (! juce::isPositiveAndBelow (listIndex, sets.size()))
            return {};

        const bool isCue = sets[listIndex].isGridMode;
        int y = isCue ? (computeCueSectionStartY() + kSectionHeaderHeight)
                      : (listContentTop + kSectionHeaderHeight);

        int top = y;
        int bottom = top;
        bool any = false;

        for (int i = 0; i < sets.size(); ++i)
        {
            const bool inSection = isCue ? sets[i].isGridMode : ! sets[i].isGridMode;
            const bool expanded = isCue ? cueExpanded : bgmExpanded;

            if (inSection && expanded && sets[i].matchesSearch)
            {
                if (! any)
                {
                    top = y;
                    any = true;
                }

                bottom = y + kListRowVisualHeight;
                y += kListRowStride;
            }
        }

        if (! any)
            return {};

        return juce::Rectangle<int> (0, top, listScrollArea.getWidth(), bottom - top);
    }

    int clampListInsertBeforeToSection (int insertBefore, int dragListIndex) const noexcept
    {
        const auto section = getListSectionBounds (dragListIndex);

        if (section.firstListIndex < 0)
            return insertBefore;

        return juce::jlimit (section.insertMin, section.insertMax, insertBefore);
    }

    int hitTestListInsertBeforeUnlocked (juce::Point<int> pos) const
    {
        const int hit = hitTestListIndexAt (pos);

        if (hit < 0)
            return -1;

        const auto row = getRowBoundsForListIndex (hit);

        if (! row.hasValue())
            return -1;

        if (pos.y < row->getCentreY())
            return hit;

        for (int j = hit + 1; j < sets.size(); ++j)
            if (sets[j].isGridMode == sets[hit].isGridMode)
                return j;

        return hit + 1;
    }

    /** Section Region Lock — chỉ số chèn luôn nằm trong phân khu BGM hoặc CUE nguồn. */
    int hitTestListInsertBefore (juce::Point<int> pos) const
    {
        if (dragListIndex < 0 || dragListIndex >= sets.size())
            return -1;

        const auto section = getListSectionBounds (dragListIndex);
        int insertBefore = hitTestListInsertBeforeUnlocked (pos);

        if (insertBefore < 0)
        {
            if (auto rows = getListSectionRowsBounds (dragListIndex))
            {
                if (pos.y < rows->getY())
                    insertBefore = section.insertMin;
                else if (pos.y >= rows->getBottom())
                    insertBefore = section.insertMax;
                else
                    return -1;
            }
            else
            {
                return -1;
            }
        }

        return clampListInsertBeforeToSection (insertBefore, dragListIndex);
    }

    juce::Optional<int> getInsertLineY (int insertBefore, int dragListIndex = -1) const
    {
        if (insertBefore < 0)
            return {};

        if (dragListIndex >= 0 && dragListIndex < sets.size())
        {
            const auto section = getListSectionBounds (dragListIndex);

            if (section.firstListIndex >= 0 && insertBefore >= section.insertMax)
            {
                if (auto row = getRowBoundsForListIndex (section.lastListIndex))
                    return row->getBottom();
            }
        }

        if (insertBefore < sets.size())
        {
            if (auto row = getRowBoundsForListIndex (insertBefore))
                return row->getY();
        }
        else if (insertBefore == sets.size() && sets.size() > 0)
        {
            if (dragListIndex >= 0 && dragListIndex < sets.size())
            {
                const auto section = getListSectionBounds (dragListIndex);

                if (auto row = getRowBoundsForListIndex (section.lastListIndex))
                    return row->getBottom();
            }

            for (int i = sets.size() - 1; i >= 0; --i)
            {
                if (auto row = getRowBoundsForListIndex (i))
                    return row->getBottom();
            }
        }

        return {};
    }

    void drawRowItem (juce::Graphics& g, int index, int y, int displayCount, int width)
    {
        const bool isSelected = (index == selectedIndex);
        const bool isDraggedRow = (index == dragListIndex && listDragMoved);

        const auto cols = showcontrol::ui::ThemePaintColours::read (*this);

        if (isSelected)
        {
            g.setColour (cols.rowSelected);
            g.fillRoundedRectangle (10, y, width - 20, kListRowVisualHeight, 5.0f);
            g.setColour (cols.accent);
            g.fillRoundedRectangle (10, y + 3, 3, 23, 1.5f);
        }

        if (renamingListIndex == index)
            return;

        if (isDraggedRow)
            g.setOpacity (0.38f);

        const auto rowBounds = juce::Rectangle<float> (10.0f, (float) y, (float) width - 20.0f, (float) kListRowVisualHeight);

        if (crossComponentDragActive && index == potentialDropRowIndex)
        {
            g.setColour (juce::Colour (0xFF4A90E2).withAlpha (0.4f));
            g.fillRoundedRectangle (rowBounds, 4.0f);
            g.setColour (juce::Colour (0xFF4A90E2));
            g.drawRoundedRectangle (rowBounds, 4.0f, 1.5f);
        }

        const auto rowLayout = layoutSidebarRowRegions ({ 10, y, width - 20, kListRowVisualHeight });

        const auto listTheme = sets[index].themeColour;
        const bool hasListTheme = ! showcontrol::colours::isDefaultTagColour (listTheme);
        auto nameBounds = rowLayout.nameArea;

        if (hasListTheme)
        {
            g.setColour (listTheme);
            g.fillRoundedRectangle ((float) nameBounds.getX() - 12.0f,
                                    (float) y + 9.0f,
                                    4.0f, 11.0f, 1.5f);
            nameBounds = nameBounds.withTrimmedLeft (10);
        }

        g.setColour (hasListTheme ? listTheme
                                  : (isSelected ? cols.textPrimary : cols.textSecondary));
        g.setFont (sidebarListRowNameFont());

        juce::String displayName = sets[index].name;
        if (sets[index].isLocked)
            displayName += juce::String::fromUTF8 (u8" 🔒");

        g.drawText (displayName, nameBounds, juce::Justification::centredLeft, true);

        if (sets[index].isLooping)
        {
            showcontrol::icons::paintLoopIcon (g,
                                               centerIconInSlot (rowLayout.loopIconArea),
                                               cols.accent, true);
        }

        if (sets[index].isPlaying)
        {
            const juce::Colour speakerCol = hasListTheme
                                                ? listTheme
                                                : showcontrol::icons::speakerPlayingColour (isSelected);
            showcontrol::icons::paintSpeakerIcon (g,
                                                  centerIconInSlot (rowLayout.speakerIconArea),
                                                  speakerCol,
                                                  hasListTheme || isSelected);
        }

        const auto hotkeyChar = showcontrol::keyboard::getHotkeyCharForIndex (displayCount - 1);
        const juce::String shortcutText = showcontrol::keyboard::formatPlaylistShortcut (
            sets[index].isGridMode, hotkeyChar);
        g.setColour (cols.textMuted);
        g.setFont (sidebarListRowHotkeyFont());
        g.drawFittedText (shortcutText, rowLayout.hotkeyArea, juce::Justification::centredRight, 1, 0.85f);

        if (isDraggedRow)
            g.setOpacity (1.0f);
    }

    void showNewSetMenu()
    {
        juce::PopupMenu m;
        m.addItem (1, showcontrol::localization::tr (u8"Thêm BGM"));
        m.addItem (2, showcontrol::localization::tr (u8"Thêm Cue"));
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (addBtn), [this] (int result)
        {
            if (result == 1 && onAddList)
                onAddList (sets.size(), showcontrol::localization::defaultBgmListName(), 12, false);
            if (result == 2 && onAddList)
                onAddList (sets.size(), showcontrol::localization::defaultCueListName(), 0, true);
        });
    }

    void showContextMenu (int index)
    {
        if (index < 0 || index >= sets.size())
            return;

        juce::PopupMenu menu;
        menu.addItem ((int) SidebarMenuId::rename,    showcontrol::localization::tr (u8"Đổi tên"));
        menu.addItem ((int) SidebarMenuId::duplicate, showcontrol::localization::tr (u8"Nhân bản"));
        menu.addItem ((int) SidebarMenuId::addSounds, showcontrol::localization::tr (u8"Thêm âm thanh..."));

        // isGridMode: false = BGM (danh sách), true = CUE (lưới PAD) — luôn hiển thị, không disable
        const juce::String switchLabel = sets[index].isGridMode
            ? showcontrol::localization::tr (u8"Chuyển sang dạng danh sách BGM")
            : showcontrol::localization::tr (u8"Chuyển sang dạng lưới CUE");
        menu.addItem ((int) SidebarMenuId::switchView, switchLabel);

        if (! sets[index].isGridMode)
        {
            menu.addItem ((int) SidebarMenuId::toggleListLoop,
                          showcontrol::localization::tr (u8"Lặp lại danh sách"),
                          true,
                          sets[index].isLooping);
        }

        menu.addSeparator();
        menu.addCustomItem ((int) SidebarMenuId::changeListColour,
                            showcontrol::colours::makeTagColourMenuRow (
                                getListThemeColourRef (index),
                                [this, index] (juce::Colour picked)
                                {
                                    setListThemeColour (index, picked);

                                    if (onListThemeColourChanged)
                                        onListThemeColourChanged (index, picked);
                                }),
                            nullptr,
                            showcontrol::localization::tr (u8"Đổi màu danh sách"));

        menu.addSeparator();
        menu.addItem ((int) SidebarMenuId::deleteItem, showcontrol::localization::tr (u8"Xóa"));

        const auto options = juce::PopupMenu::Options()
                                 .withTargetComponent (this)
                                 .withMousePosition();

        menu.showMenuAsync (options, [this, index] (int result)
        {
            handleSidebarMenuResult (result, index);
        });
    }

    void handleSidebarMenuResult (int result, int targetItemIndex)
    {
        if (targetItemIndex < 0 || targetItemIndex >= sets.size() || result == 0)
            return;

        switch ((SidebarMenuId) result)
        {
            case SidebarMenuId::rename:
                beginInlineRename (targetItemIndex);
                break;

            case SidebarMenuId::duplicate:
                if (onDuplicateSet) onDuplicateSet (targetItemIndex);
                break;

            case SidebarMenuId::addSounds:
                if (onAddSounds) onAddSounds (targetItemIndex);
                break;

            case SidebarMenuId::switchView:
                if (onMorphSetStructure) onMorphSetStructure (targetItemIndex);
                break;

            case SidebarMenuId::toggleListLoop:
                if (onLoopListToggled)
                    onLoopListToggled (targetItemIndex, ! sets[targetItemIndex].isLooping);
                break;

            case SidebarMenuId::deleteItem:
                requestDeleteList (targetItemIndex);
                break;

            default:
                break;
        }
    }

    void beginInlineRename (int index)
    {
        if (index < 0 || index >= sets.size())
            return;

        renamingListIndex = index;
        selectedIndex = index;
        inlineRenameOriginalName = sets[index].name;
        inlineRenameEscapePressed = false;
        lockSidebarRowFonts();
        listNameEditor.setText (inlineRenameOriginalName, juce::dontSendNotification);
        listNameEditor.setVisible (true);
        resized();
        listNameEditor.selectAll();

        juce::Component::SafePointer<SidebarPanel> safe (this);
        juce::MessageManager::callAsync ([safe]
        {
            if (safe == nullptr || ! safe->listNameEditor.isVisible())
                return;

            if (safe->listNameEditor.isShowing())
                safe->listNameEditor.grabKeyboardFocus();
        });

        repaint();
    }

    void commitInlineRename()
    {
        if (renamingListIndex < 0)
            return;

        const int index = renamingListIndex;
        renamingListIndex = -1;
        listNameEditor.setVisible (false);
        listNameEditor.giveAwayKeyboardFocus();
        lockSidebarRowFonts();

        juce::String newName = listNameEditor.getText().trim();
        if (newName.isEmpty())
            newName = sets[index].isGridMode ? juce::String::fromUTF8 (u8"Danh sách Cue") : juce::String::fromUTF8 (u8"Danh sách BGM");

        if (newName == sets[index].name)
        {
            repaint();
            return;
        }

        sets.getReference (index).name = newName;
        if (onRenameList)
            onRenameList (index, newName);

        repaint();
    }

    void cancelInlineRename()
    {
        if (renamingListIndex < 0)
            return;

        inlineRenameEscapePressed = true;
        renamingListIndex = -1;
        listNameEditor.setVisible (false);
        listNameEditor.setText (inlineRenameOriginalName, juce::dontSendNotification);
        listNameEditor.giveAwayKeyboardFocus();
        lockSidebarRowFonts();
        repaint();
    }

    void requestDeleteList (int index)
    {
        if (index < 0 || index >= sets.size())
            return;

        const auto& info = sets.getReference (index);
        const juce::String typeLabel = info.isGridMode ? juce::String::fromUTF8 (u8"Cue") : juce::String::fromUTF8 (u8"BGM");
        const juce::String subtext = juce::String::fromUTF8 (u8"Danh sách \"")
                                   + info.name
                                   + juce::String::fromUTF8 (u8"\" (")
                                   + typeLabel
                                   + juce::String::fromUTF8 (u8") chứa ")
                                   + juce::String (info.tracks)
                                   + juce::String::fromUTF8 (
                                         u8" mục sẽ bị xóa hoàn toàn khỏi hệ thống. Hành động này không thể hoàn tác.");

        showcontrol::ui::showConfirmDeleteDialog (this,
                                                  juce::String::fromUTF8 (u8"Xóa danh sách phát"),
                                                  subtext,
                                                  juce::String::fromUTF8 (u8"Xóa"),
                                                  [this, index] (bool confirmed)
                                                  {
                                                      if (confirmed && onDeleteList)
                                                          onDeleteList (index);
                                                  },
                                                  panicKeyListener);
    }
};

//==============================================================================
inline bool SidebarPanel::isInterestedInFileDrag (const juce::StringArray& files)
{
    juce::ignoreUnused (files);
    return true;
}

inline void SidebarPanel::fileDragEnter (const juce::StringArray& files, int x, int y)
{
    juce::ignoreUnused (files, x);
    folderDragActive = true;
    updateFolderDragTargetFromY (panelYToListContentY (y));
    repaint();
}

inline void SidebarPanel::fileDragMove (const juce::StringArray& files, int x, int y)
{
    juce::ignoreUnused (files, x);

    if (! folderDragActive)
        return;

    const bool prev = folderDragTargetIsBgm;
    updateFolderDragTargetFromY (panelYToListContentY (y));

    if (prev != folderDragTargetIsBgm)
        repaint();
}

inline void SidebarPanel::fileDragExit (const juce::StringArray& files)
{
    juce::ignoreUnused (files);
    clearFolderDragState();
}

inline void SidebarPanel::filesDropped (const juce::StringArray& files, int x, int y)
{
    juce::ignoreUnused (x);
    clearFolderDragState();

    const bool targetIsBgm = isDropTargetBgm (panelYToListContentY (y));
    juce::StringArray folderPaths;

    for (const auto& path : files)
    {
        const juce::File item (path);
        if (item.isDirectory())
            folderPaths.add (path);
    }

    if (folderPaths.isEmpty() || ! onFoldersSmartImport)
        return;

    onFoldersSmartImport (folderPaths, targetIsBgm);
}

//==============================================================================
inline bool SidebarPanel::isInterestedInDragSource (const juce::DragAndDropTarget::SourceDetails& dragSourceDetails)
{
    return showcontrol::crossdrag::isCrossCopyDropInterest (dragSourceDetails.description);
}

inline void SidebarPanel::itemDragEnter (const juce::DragAndDropTarget::SourceDetails& dragSourceDetails)
{
    if (! isInterestedInDragSource (dragSourceDetails))
        return;

    crossComponentDragActive = true;
    itemDragMove (dragSourceDetails);
}

inline void SidebarPanel::itemDragMove (const juce::DragAndDropTarget::SourceDetails& dragSourceDetails)
{
    if (! crossComponentDragActive)
        return;

    const auto contentPos = panelPointToListContent (dragSourceDetails.localPosition);
    const int row = contentPos.hasValue() ? hitTestListIndexAt (*contentPos) : -1;

    if (row >= 0 && row < sets.size())
    {
        if (potentialDropRowIndex != row)
        {
            potentialDropRowIndex = row;
            repaint();
        }
    }
    else if (potentialDropRowIndex != -1)
    {
        potentialDropRowIndex = -1;
        repaint();
    }
}

inline void SidebarPanel::itemDragExit (const juce::DragAndDropTarget::SourceDetails& dragSourceDetails)
{
    juce::ignoreUnused (dragSourceDetails);
    crossComponentDragActive = false;
    potentialDropRowIndex = -1;
    repaint();
}

inline void SidebarPanel::itemDropped (const juce::DragAndDropTarget::SourceDetails& dragSourceDetails)
{
    const int dropRow = potentialDropRowIndex;
    crossComponentDragActive = false;
    potentialDropRowIndex = -1;
    repaint();

    if (dropRow < 0 || dropRow >= sets.size())
        return;

    juce::Array<showcontrol::crossdrag::TrackCopyRecord> tracks;
    juce::String sourceListName;

    if (! showcontrol::crossdrag::decodeCrossComponentCopyPayload (dragSourceDetails.description,
                                                                   tracks,
                                                                   sourceListName))
        return;

    if (tracks.isEmpty() || onCrossCopyDroppedToPlaylist == nullptr)
        return;

    onCrossCopyDroppedToPlaylist (dropRow, tracks);
}
