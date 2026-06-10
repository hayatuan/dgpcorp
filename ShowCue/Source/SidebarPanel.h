#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "ShowTheme.h"
#include "ShowControlLookAndFeel.h"
#include "ConfirmDeleteDialog.h"
#include "ShowFlatIcons.h"

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

//==============================================================================
class SidebarPanel : public juce::Component,
                       public juce::FileDragAndDropTarget
{
public:
    static constexpr int searchBarTop = 8;
    static constexpr int searchBarHeight = 32;
    static constexpr int listContentTop = searchBarTop + searchBarHeight + 12;

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
    };

    enum class SidebarMenuId : int
    {
        rename         = 1,
        duplicate      = 2,
        addSounds      = 3,
        switchView     = 4,
        toggleListLoop = 5,
        deleteItem     = 6
    };

    SidebarPanel()
    {
        searchBar.setLookAndFeel (&searchBarStyle);
        addAndMakeVisible (searchBar);
        searchBar.setTextToShowWhenEmpty (juce::String::fromUTF8 (u8"🔍 Tìm kiếm..."), ShowTheme::darkPalette().textMuted); // overridden by updateThemeColors()
        searchBar.setFont (ShowTheme::font (13.5f));
        searchBar.setJustification (juce::Justification::centredLeft);
        searchBar.onTextChange = [this] { if (onSearchChanged) onSearchChanged (searchBar.getText()); };

        listNameEditor.setLookAndFeel (&searchBarStyle);
        listNameEditor.setVisible (false);
        listNameEditor.setMultiLine (false);
        listNameEditor.setReturnKeyStartsNewLine (false);
        listNameEditor.setFont (ShowTheme::font (13.5f));
        listNameEditor.onReturnKey = [this] { commitInlineRename(); };
        listNameEditor.onFocusLost = [this] { commitInlineRename(); };
        addChildComponent (listNameEditor);

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
        listNameEditor.setLookAndFeel (nullptr);
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
    /** Smart Import: thả thư mục nhạc — tạo list mới theo tên folder (targetIsBgm: true = BGM, false = CUE). */
    std::function<void (const juce::StringArray& folderPaths, bool targetIsBgm)> onFoldersSmartImport;

    juce::String getSearchText() const { return searchBar.getText(); }

    /** Smart Import: vùng BGM (phía trên) vs CUE (phía dưới) trong sidebar. */
    bool isSmartImportTargetBgm (int localY) const noexcept { return isDropTargetBgm (localY); }

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragEnter (const juce::StringArray& files, int x, int y) override;
    void fileDragMove (const juce::StringArray& files, int x, int y) override;
    void fileDragExit (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;

    void setPanicKeyListener (juce::KeyListener* listener) noexcept { panicKeyListener = listener; }

    void lookAndFeelChanged() override
    {
        juce::Component::lookAndFeelChanged();

        if (auto* showLaf = dynamic_cast<ShowControlLookAndFeel*> (&getLookAndFeel()))
            updateThemeColors (showLaf->isDarkMode());
    }

    void updateThemeColors (bool isDark)
    {
        isDarkMode = isDark;
        searchBarStyle.setDarkMode (isDark);
        const auto pal = ShowTheme::get (isDark);

        searchBar.setTextToShowWhenEmpty (juce::String::fromUTF8 (u8"🔍 Tìm kiếm..."), pal.textMuted);
        searchBar.setColour (juce::TextEditor::backgroundColourId, pal.panelElevated);
        searchBar.setColour (juce::TextEditor::textColourId, pal.textPrimary);
        searchBar.setColour (juce::CaretComponent::caretColourId, pal.accent);
        listNameEditor.setColour (juce::TextEditor::backgroundColourId, pal.panelElevated);
        listNameEditor.setColour (juce::TextEditor::textColourId, pal.textPrimary);
        listNameEditor.setColour (juce::TextEditor::outlineColourId, pal.accent);
        listNameEditor.setColour (juce::CaretComponent::caretColourId, pal.accent);

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

    void addSet (juce::String name, int tracks, bool isGrid, bool useListView = true, bool locked = false)
    {
        sets.add ({ name, tracks, isGrid, false, true, false, useListView, locked });
        repaint();
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
    void clearAllLists() { sets.clear(); repaint(); }
    int getListCount() const { return sets.size(); }
    int getListTrackCount (int index) const { return (index >= 0 && index < sets.size()) ? sets[index].tracks : 0; }
    juce::String getListName (int index) const { return (index >= 0 && index < sets.size()) ? sets[index].name : ""; }
    int getSelectedIndex() const { return selectedIndex; }
    void setSelectedIndex (int i) { selectedIndex = i; repaint(); }

    void paint (juce::Graphics& g) override
    {
        const auto cols = showcontrol::ui::ThemePaintColours::read (*this);
        isDarkMode = cols.isDark;
        g.fillAll (cols.panelBg);

        const int width = getWidth();
        int y = listContentTop;

        g.setColour (cols.textSecondary);
        g.setFont (ShowTheme::fontBold (12.0f));
        g.drawText (juce::String::fromUTF8 (bgmExpanded ? u8"▼  NHẠC NỀN BGM" : u8"▶  NHẠC NỀN BGM"), 16, y, width, 24, juce::Justification::left);
        y += 24;

        int bgmCount = 1;
        for (int i = 0; i < sets.size(); ++i)
        {
            if (! sets[i].isGridMode)
            {
                if (bgmExpanded && sets[i].matchesSearch)
                {
                    drawRowItem (g, i, y, bgmCount);
                    y += 35;
                }
                bgmCount++;
            }
        }

        y += 15;
        g.setColour (cols.textSecondary);
        g.setFont (ShowTheme::fontBold (12.0f));
        g.drawText (juce::String::fromUTF8 (cueExpanded ? u8"▼  NHẠC CUE" : u8"▶  NHẠC CUE"), 16, y, width, 24, juce::Justification::left);
        y += 24;

        int cueCount = 1;
        for (int i = 0; i < sets.size(); ++i)
        {
            if (sets[i].isGridMode)
            {
                if (cueExpanded && sets[i].matchesSearch)
                {
                    drawRowItem (g, i, y, cueCount);
                    y += 35;
                }
                cueCount++;
            }
        }

        if (dragListIndex >= 0 && listDragMoved && dragListInsertBefore >= 0)
        {
            if (auto lineY = getInsertLineY (dragListInsertBefore))
            {
                const auto lineColour = cols.accent;
                g.setColour (lineColour.withAlpha (0.55f));
                g.fillEllipse (14.0f, (float) *lineY - 4.0f, 8.0f, 8.0f);
                g.fillEllipse ((float) width - 22.0f, (float) *lineY - 4.0f, 8.0f, 8.0f);
                g.setColour (lineColour);
                g.fillRect (18, *lineY - 1, width - 36, 2);
            }
        }

        if (folderDragActive)
        {
            const auto zone = getFolderDropZoneBounds (folderDragTargetIsBgm);
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
        if (juce::Rectangle<int> (0, y, getWidth(), 24).contains (pos))
        {
            bgmExpanded = ! bgmExpanded;
            repaint();
            return;
        }

        y += 24;
        for (int i = 0; i < sets.size(); ++i)
        {
            if (! sets[i].isGridMode && bgmExpanded && sets[i].matchesSearch)
                y += 35;
        }

        y += 15;
        if (juce::Rectangle<int> (0, y, getWidth(), 24).contains (pos))
        {
            cueExpanded = ! cueExpanded;
            repaint();
        }
    }

    void mouseDrag (const juce::MouseEvent& e) override
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

    void mouseUp (const juce::MouseEvent& e) override
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
        searchBar.setBounds (12, searchBarTop, getWidth() - 24, searchBarHeight);
        addBtn.setBounds (12, getHeight() - 44, 36, 34);
        removeBtn.setBounds (getWidth() - 48, getHeight() - 44, 36, 34);

        if (renamingListIndex >= 0)
        {
            if (auto row = getRowBoundsForListIndex (renamingListIndex))
            {
                listNameEditor.setBounds (row->withTrimmedLeft (22).withTrimmedRight (88));
                listNameEditor.toFront (false);
            }
        }
    }

private:
    juce::TextEditor searchBar, listNameEditor;
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
    bool folderDragActive = false;
    bool folderDragTargetIsBgm = true;

    int computeCueSectionStartY() const
    {
        int y = listContentTop;
        y += 24;

        for (int i = 0; i < sets.size(); ++i)
        {
            if (! sets[i].isGridMode && bgmExpanded && sets[i].matchesSearch)
                y += 35;
        }

        y += 15;
        return y;
    }

    bool isDropTargetBgm (int localY) const
    {
        return localY < computeCueSectionStartY();
    }

    juce::Rectangle<int> getFolderDropZoneBounds (bool targetIsBgm) const
    {
        const int width = getWidth();
        const int cueStartY = computeCueSectionStartY();

        if (targetIsBgm)
            return juce::Rectangle<int> (0, listContentTop, width, cueStartY - listContentTop);

        return juce::Rectangle<int> (0, cueStartY, width, juce::jmax (24, getHeight() - cueStartY));
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
        int y = listContentTop;
        y += 24;

        for (int i = 0; i < sets.size(); ++i)
        {
            if (! sets[i].isGridMode && bgmExpanded && sets[i].matchesSearch)
            {
                if (juce::Rectangle<int> (0, y, getWidth(), 31).contains (pos))
                    return i;
                y += 35;
            }
        }

        y += 15;
        y += 24;

        for (int i = 0; i < sets.size(); ++i)
        {
            if (sets[i].isGridMode && cueExpanded && sets[i].matchesSearch)
            {
                if (juce::Rectangle<int> (0, y, getWidth(), 31).contains (pos))
                    return i;
                y += 35;
            }
        }

        return -1;
    }

    juce::Optional<juce::Rectangle<int>> getRowBoundsForListIndex (int listIndex) const
    {
        int y = listContentTop;
        y += 24;

        for (int i = 0; i < sets.size(); ++i)
        {
            if (! sets[i].isGridMode && bgmExpanded && sets[i].matchesSearch)
            {
                if (i == listIndex)
                    return juce::Rectangle<int> (10, y, getWidth() - 20, 31);
                y += 35;
            }
        }

        y += 15;
        y += 24;

        for (int i = 0; i < sets.size(); ++i)
        {
            if (sets[i].isGridMode && cueExpanded && sets[i].matchesSearch)
            {
                if (i == listIndex)
                    return juce::Rectangle<int> (10, y, getWidth() - 20, 31);
                y += 35;
            }
        }

        return {};
    }

    int hitTestListInsertBefore (juce::Point<int> pos) const
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

    juce::Optional<int> getInsertLineY (int insertBefore) const
    {
        if (insertBefore < 0)
            return {};

        if (insertBefore < sets.size())
        {
            if (auto row = getRowBoundsForListIndex (insertBefore))
                return row->getY();
        }
        else if (insertBefore == sets.size() && sets.size() > 0)
        {
            for (int i = sets.size() - 1; i >= 0; --i)
            {
                if (auto row = getRowBoundsForListIndex (i))
                    return row->getBottom();
            }
        }

        return {};
    }

    void drawRowItem (juce::Graphics& g, int index, int y, int displayCount)
    {
        const bool isSelected = (index == selectedIndex);
        const bool isDraggedRow = (index == dragListIndex && listDragMoved);

        const auto cols = showcontrol::ui::ThemePaintColours::read (*this);

        if (isSelected)
        {
            g.setColour (cols.rowSelected);
            g.fillRoundedRectangle (10, y, getWidth() - 20, 31, 5.0f);
            g.setColour (cols.accent);
            g.fillRoundedRectangle (10, y + 4, 3, 23, 1.5f);
        }

        if (renamingListIndex == index)
            return;

        if (isDraggedRow)
            g.setOpacity (0.38f);

        g.setColour (isSelected ? cols.textPrimary : cols.textSecondary);
        g.setFont (ShowTheme::font (13.5f));

        juce::String displayName = sets[index].name;
        if (sets[index].isLocked)
            displayName += juce::String::fromUTF8 (u8" 🔒");
        g.drawText (displayName, 22, y, getWidth() - 110, 31, juce::Justification::centredLeft);

        constexpr float kSidebarRowH = 31.0f;
        const float sidebarIconY = (float) y + (kSidebarRowH - showcontrol::icons::kListIconSize) * 0.5f;

        if (sets[index].isLooping)
        {
            showcontrol::icons::paintLoopIcon (g,
                                               { (float) getWidth() - 118.0f, sidebarIconY,
                                                 showcontrol::icons::kListIconSize, showcontrol::icons::kListIconSize },
                                               cols.accent, true);
        }

        if (sets[index].isPlaying)
        {
            showcontrol::icons::paintSpeakerIcon (g,
                                                  { (float) getWidth() - 95.0f, sidebarIconY,
                                                    showcontrol::icons::kListIconSize, showcontrol::icons::kListIconSize },
                                                  showcontrol::icons::speakerPlayingColour (isSelected),
                                                  isSelected);
        }

        juce::String shortcutText;
        #if JUCE_MAC
        shortcutText = juce::String::fromUTF8 (sets[index].isGridMode ? u8"⌘" : u8"⌃") + juce::String (displayCount);
        #else
        shortcutText = (sets[index].isGridMode ? "Cmd+" : "Ctrl+") + juce::String (displayCount);
        #endif
        g.setColour (cols.textMuted);
        g.setFont (ShowTheme::font (11.0f));
        g.drawText (shortcutText, getWidth() - 85, y, 70, 31, juce::Justification::centredRight);

        if (isDraggedRow)
            g.setOpacity (1.0f);
    }

    void showNewSetMenu()
    {
        juce::PopupMenu m;
        m.addItem (1, juce::String::fromUTF8 (u8"Thêm BGM"));
        m.addItem (2, juce::String::fromUTF8 (u8"Thêm Cue"));
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (addBtn), [this] (int result)
        {
            if (result == 1 && onAddList)
                onAddList (sets.size(), juce::String::fromUTF8 (u8"Bộ BGM Mới"), 12, false);
            if (result == 2 && onAddList)
                onAddList (sets.size(), juce::String::fromUTF8 (u8"Bộ Cue Mới"), 0, true);
        });
    }

    void showContextMenu (int index)
    {
        if (index < 0 || index >= sets.size())
            return;

        juce::PopupMenu menu;
        menu.addItem ((int) SidebarMenuId::rename,    juce::String::fromUTF8 (u8"Đổi tên"));
        menu.addItem ((int) SidebarMenuId::duplicate, juce::String::fromUTF8 (u8"Nhân bản"));
        menu.addItem ((int) SidebarMenuId::addSounds, juce::String::fromUTF8 (u8"Thêm âm thanh..."));

        // isGridMode: false = BGM (danh sách), true = CUE (lưới PAD) — luôn hiển thị, không disable
        const juce::String switchLabel = sets[index].isGridMode
            ? juce::String::fromUTF8 (u8"Chuyển sang dạng danh sách BGM")
            : juce::String::fromUTF8 (u8"Chuyển sang dạng lưới CUE");
        menu.addItem ((int) SidebarMenuId::switchView, switchLabel);

        if (! sets[index].isGridMode)
        {
            menu.addItem ((int) SidebarMenuId::toggleListLoop,
                          juce::String::fromUTF8 (u8"Lặp lại danh sách"),
                          true,
                          sets[index].isLooping);
        }

        menu.addSeparator();
        menu.addItem ((int) SidebarMenuId::deleteItem, juce::String::fromUTF8 (u8"Xóa"));

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
        listNameEditor.setText (sets[index].name, juce::dontSendNotification);
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

        juce::String newName = listNameEditor.getText().trim();
        if (newName.isEmpty())
            newName = sets[index].isGridMode ? juce::String::fromUTF8 (u8"Danh sách Cue") : juce::String::fromUTF8 (u8"Danh sách BGM");

        sets.getReference (index).name = newName;
        if (onRenameList)
            onRenameList (index, newName);

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
    juce::ignoreUnused (files);
    folderDragActive = true;
    updateFolderDragTargetFromY (y);
    repaint();
}

inline void SidebarPanel::fileDragMove (const juce::StringArray& files, int x, int y)
{
    juce::ignoreUnused (files);

    if (! folderDragActive)
        return;

    const bool prev = folderDragTargetIsBgm;
    updateFolderDragTargetFromY (y);

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
    clearFolderDragState();

    const bool targetIsBgm = isDropTargetBgm (y);
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
