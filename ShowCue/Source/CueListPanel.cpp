#include "CueListPanel.h"
#include "MainComponent.h"
#include "ShowControlLookAndFeel.h"
#include "ShowGraphicsSafe.h"
#include "ShowFlatIcons.h"
#include "HotkeyManager.h"
#include "ShowKeyboardInput.h"
#include "ShowLocalization.h"

namespace
{
    juce::String formatCueTimeString (double timeInSeconds)
    {
        return showcontrol::bgmList::formatPlaylistTime (timeInSeconds);
    }

    bool cueListHasLoadedContent (const juce::Array<CueItem>& cues)
    {
        for (const auto& cue : cues)
            if (cue.filePath.isNotEmpty())
                return true;

        return false;
    }

    juce::Array<int> sparseSetToSortedArray (const juce::SparseSet<int>& rows)
    {
        juce::Array<int> sorted;

        for (int i = 0; i < rows.size(); ++i)
            sorted.addIfNotAlreadyThere (rows[i]);

        sorted.sort();
        return sorted;
    }

    /** Bulk delete: index cao → thấp — tránh lệch pha mảng khi xóa nhiều dòng. */
    juce::Array<int> sparseSetToDescendingArray (const juce::SparseSet<int>& rows)
    {
        const auto ascending = sparseSetToSortedArray (rows);
        juce::Array<int> descending;

        for (int i = ascending.size(); --i >= 0;)
            descending.add (ascending.getUnchecked (i));

        return descending;
    }
}

//==============================================================================
/** Thanh tiêu đề cột — bounds tách khỏi ListBox, không hardcode Y trong paint panel. */
class CueListPanel::CueListHeaderComponent : public juce::Component
{
public:
    explicit CueListHeaderComponent (CueListPanel& ownerIn) : owner (ownerIn)
    {
        setInterceptsMouseClicks (false, false);
        setOpaque (true);
    }

    void paint (juce::Graphics& g) override
    {
        owner.paintHeader (g, getLocalBounds());
    }

private:
    CueListPanel& owner;
};

//==============================================================================
/** Mirror MainComponent::PadReorderOverlay — vẽ trên listBox, không bị che. */
class CueListPanel::CueReorderOverlay : public juce::Component,
                                        private juce::Timer
{
public:
    explicit CueReorderOverlay (CueListPanel& ownerIn) : owner (ownerIn)
    {
        setInterceptsMouseClicks (false, false);
        setOpaque (false);
    }

    ~CueReorderOverlay() override
    {
        stopTimer();
    }

    void visibilityChanged() override
    {
        if (isVisible())
            startTimerHz (60);
        else
            stopTimer();
    }

    void timerCallback() override
    {
        if (! owner.cueRowReorderActive)
            return;

        if (auto* vp = owner.listBox.getViewport())
        {
            const auto local = owner.listBox.getLocalPoint (&owner, owner.cueRowReorderPointerPos);
            const int prevInsert = owner.cueRowReorderInsertIndex;
            owner.autoScrollListBoxForReorder (local);

            if (owner.cueRowReorderInsertIndex != prevInsert)
                owner.repaintReorderInsertLineStrips (prevInsert, owner.cueRowReorderInsertIndex);
        }

        repaint();
    }

    void paint (juce::Graphics& g) override { owner.paintCueReorderOverlay (g); }

private:
    CueListPanel& owner;
};

//==============================================================================
CueListBox::CueListBox (const juce::String& componentName, CueListPanel& ownerIn)
    : juce::ListBox (componentName, nullptr),
      owner (ownerIn)
{
}

int CueListBox::hitRowIndexAt (int x, int y) const
{
    const int totalRows = owner.cues.size();

    if (totalRows <= 0)
        return -1;

    int row = getRowContainingPosition (x, y);

    if (row >= 0 && row < totalRows)
        return row;

    const int insertIdx = getInsertionIndexForPosition (x, y);

    if (insertIdx >= 0 && insertIdx < totalRows)
        return insertIdx;

    // Mạch phòng vệ — click trong dải an toàn 15px dưới mép hàng cuối (image_19).
    constexpr int kBottomSafetyPadding = 15;
    const auto lastRowBounds = getRowPosition (totalRows - 1, false);

    if (x >= lastRowBounds.getX() && x <= lastRowBounds.getRight()
        && y >= lastRowBounds.getY()
        && y <= lastRowBounds.getBottom() + kBottomSafetyPadding)
    {
        return totalRows - 1;
    }

    return insertIdx;
}

void CueListBox::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
    {
        const auto rel = e.getEventRelativeTo (this);
        const int rowUnderMouse = hitRowIndexAt (rel.x, rel.y);

        if (rowUnderMouse >= 0 && rowUnderMouse < owner.cues.size())
        {
            const auto selectedRows = getSelectedRows();
            const bool clickedInsideSelection = selectedRows.contains (rowUnderMouse) && selectedRows.size() > 0;

            // Keep existing multi-selection when right-clicking inside it.
            // Avoid forwarding to ListBox default popup selection logic.
            if (clickedInsideSelection)
            {
                if (owner.onCueRightClick)
                    owner.onCueRightClick (rowUnderMouse);

                owner.showTrackContextMenu (rowUnderMouse);
                return;
            }
        }

        juce::ListBox::mouseDown (e);
        return;
    }

    owner.cancelCueRowReorder();
    owner.endCueMarquee();

    const auto rel = e.getEventRelativeTo (this);
    const int rowUnderMouse = getRowContainingPosition (rel.x, rel.y);

    // Cụm đã chọn — giữ highlight, chọn trên mouseUp (selectOnMouseUp).
    if (rowUnderMouse >= 0 && rowUnderMouse < owner.cues.size()
        && getSelectedRows().contains (rowUnderMouse))
    {
        owner.lockCueRowReorderPressAt (rowUnderMouse);
        return;
    }

    owner.cueRowReorderPressLocked = false;
    juce::ListBox::mouseDown (e);
}

void CueListBox::mouseDrag (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
        return;

    if (owner.tryImmediateCueRowDrag (e))
        return; // TUYỆT ĐỐI không gọi juce::ListBox::mouseDrag (e) — chặn marquee xanh

    const auto rel = e.getEventRelativeTo (this);
    const int rowUnderDrag = hitRowIndexAt (rel.getMouseDownX(), rel.getMouseDownY());

    if (rowUnderDrag >= 0 && rowUnderDrag < owner.cues.size())
        return; // Chặn snapshot hàng dọc JUCE — chỉ Capsule Pill custom

    juce::ListBox::mouseDrag (e);
}

juce::ScaledImage CueListBox::createSnapshotOfRows (const juce::SparseSet<int>& rows, int& x, int& y)
{
    if (rows.isEmpty())
        return juce::ListBox::createSnapshotOfRows (rows, x, y);

    const int anchorRow = rows[0];
    const juce::String title = owner.getCueTitleRowAtIndex (anchorRow);
    const int itemCount = juce::jmax (1, rows.size());
    juce::Image premiumDragProxy = CueListPanel::createPremiumCueDragImage (title, itemCount);

    if (! premiumDragProxy.isValid())
        return juce::ListBox::createSnapshotOfRows (rows, x, y);

    const auto rowBounds = getRowPosition (anchorRow, true);
    x = rowBounds.getCentreX() - premiumDragProxy.getWidth() / 2;
    y = rowBounds.getCentreY() - premiumDragProxy.getHeight() / 2;

    return juce::ScaledImage (premiumDragProxy);
}

void CueListBox::triggerCueDragSession (const juce::MouseEvent& e)
{
    owner.triggerCueDragSession (e);
}

void CueListBox::mouseUp (const juce::MouseEvent& e)
{
    if (owner.cueRowReorderActive)
        owner.endCueRowReorder();
    else if (owner.cueRowReorderPressLocked)
    {
        // Click trên cụm đã chọn không kéo — giữ nguyên highlight, không bubble ListBox.
        owner.cueRowReorderPressLocked = false;
        owner.endCueMarquee();
        return;
    }

    owner.cueRowReorderPressLocked = false;
    juce::ListBox::mouseUp (e);
}

bool CueListBox::keyPressed (const juce::KeyPress& key)
{
    if (owner.keyPressed (key))
        return true;

    const int keyCode = showcontrol::keyboard::physicalKeyCode (key);

    // Delete/Backspace do MainComponent + CueListPanel xử lý — không để ListBox nuốt IME backspace.
    if (keyCode == juce::KeyPress::deleteKey || keyCode == juce::KeyPress::backspaceKey)
        return false;

    return juce::ListBox::keyPressed (key);
}

//==============================================================================
/** Ô dòng CUE tái sử dụng — zero-allocation qua ListBoxModel::refreshComponentForRow. */
class CueListPanel::CueListRowCell : public juce::Component
{
public:
    class TrackLoopIconComponent : public juce::Component
    {
    public:
        explicit TrackLoopIconComponent (CueListPanel& ownerIn) : owner (ownerIn)
        {
            setInterceptsMouseClicks (false, false);
        }

        void paint (juce::Graphics& g) override
        {
            const auto& pal = ShowTheme::get (owner.isDarkMode);
            showcontrol::icons::paintLoopIcon (g, getLocalBounds().toFloat(), pal.accent, true);
        }

    private:
        CueListPanel& owner;
    };

    explicit CueListRowCell (CueListPanel& ownerIn)
        : owner (ownerIn), trackLoopIconComponent (ownerIn)
    {
        setInterceptsMouseClicks (false, false);
        addAndMakeVisible (trackLoopIconComponent);
    }

    void updateRowData (int rowIndexIn, bool selected)
    {
        rowIndex = rowIndexIn;
        rowSelected = selected;

        bool isLooping = false;

        if (owner.padAccessor != nullptr)
        {
            if (auto* pad = owner.padAccessor (rowIndex))
                isLooping = pad->isLooping() && pad->hasAudioFile();
        }

        trackLoopIconComponent.setVisible (isLooping);
        resized();
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        if (! juce::isPositiveAndBelow (rowIndex, owner.cues.size()))
            return;

        layoutLoopIconInNameColumn();

        const int width = getWidth();
        const int height = getHeight();

        juce::Graphics::ScopedSaveState rowOpacity (g);
        juce::ignoreUnused (rowOpacity);

        const auto cols = showcontrol::ui::ThemePaintColours::read (owner);
        const auto& pal = ShowTheme::get (cols.isDark);
        const auto& cue = owner.cues.getReference (rowIndex);

        juce::Rectangle<int> row (0, 0, width, height);

        showcontrol::bgmList::paintPlaylistRowBackground (g, row, rowSelected, pal);

        SoundPad* pad = nullptr;
        if (owner.padAccessor)
            pad = owner.padAccessor (rowIndex);

        const bool isActivePlay = (pad != nullptr && pad->isPlaying());
        const bool isPaused     = (pad != nullptr && pad->isPaused());
        const bool isRowLive    = isActivePlay || isPaused;

        if (! showcontrol::colours::isDefaultTagColour (cue.tagColour))
        {
            g.setColour (cue.tagColour);
            showcontrol::gfx::safeFillRect (g, 0, 0, showcontrol::bgmList::kLeftRailWidth, height);
        }
        else
        {
            showcontrol::bgmList::paintPlaylistRowLeftRail (g, height, pal);
        }

        g.setColour (cols.textMuted);
        g.setFont (owner.rowFonts.indexBold);
        g.drawText (juce::String (cue.cueNumber),
                    showcontrol::bgmList::kIndexX, 0,
                    showcontrol::bgmList::kIndexWidth, height,
                    juce::Justification::centred);

        int textX = showcontrol::bgmList::kNameStartDefault;

        if (isActivePlay)
        {
            const auto iconBounds = showcontrol::bgmList::statusIconBounds (height);
            showcontrol::icons::paintSpeakerIcon (g, iconBounds,
                                                  showcontrol::icons::speakerPlayingColour (rowSelected),
                                                  rowSelected);
            textX = showcontrol::bgmList::kNameStartWithStatusIcon;
        }
        else if (isPaused)
        {
            const auto iconBounds = showcontrol::bgmList::statusIconBounds (height);
            const auto iconCol = showcontrol::icons::iconColourForListState (rowSelected, cols.isDark);
            showcontrol::icons::paintPauseIcon (g, iconBounds, iconCol);
            textX = showcontrol::bgmList::kNameStartWithStatusIcon;
        }

        if (rowIndex == owner.armedIndex)
        {
            g.setColour (pal.accent);
            g.drawRoundedRectangle (row.reduced (2, 2).toFloat(), 4.0f, 1.5f);
        }

        juce::Colour trackNameColour = pal.textMuted;

        if (cue.isEnabled)
        {
            if (isPaused)
                trackNameColour = pal.warning;
            else if (isActivePlay)
                trackNameColour = pal.success;
            else
                trackNameColour = pal.textPrimary;
        }

        g.setColour (trackNameColour);
        g.setFont (owner.rowFonts.namePlain);

        const bool reserveLoopSlot = owner.reserveLoopSlotForRow (rowIndex);
        const auto nameLayout = showcontrol::bgmList::layoutListNameRow (width, height, textX, reserveLoopSlot);

        if (owner.renamingTrackIndex != rowIndex)
        {
            const juce::String displayName = cue.name.isNotEmpty()
                ? cue.name
                : (pad != nullptr ? pad->getPadName() : juce::String());

            g.drawText (displayName, nameLayout.nameArea, juce::Justification::centredLeft, true);
        }

        if (cue.autoFollow)
        {
            g.setColour (pal.success);
            g.setFont (owner.rowFonts.autoFollow);
            g.drawText (juce::String::fromUTF8 (u8"→"), width - 320, 0, 28, height, juce::Justification::centredRight);
        }

        const auto remainingRect = showcontrol::bgmList::timeRemainingBounds (width, height);
        const auto elapsedRect   = showcontrol::bgmList::totalDurationBounds (width, height);
        const bool hasTimedTrack = (pad != nullptr && pad->hasAudioFile());

        if (hasTimedTrack || isRowLive)
        {
            juce::String remainingText = juce::String::fromUTF8 (u8"00:00.0");
            juce::String elapsedText   = juce::String::fromUTF8 (u8"00:00.0");

            if (isRowLive && juce::isPositiveAndBelow (rowIndex, owner.rowRemainingText.size()))
                remainingText = owner.rowRemainingText.getReference (rowIndex);

            if (isRowLive && juce::isPositiveAndBelow (rowIndex, owner.rowElapsedText.size()))
                elapsedText = owner.rowElapsedText.getReference (rowIndex);

            double remainingSecs = 0.0;

            if (isRowLive && pad != nullptr)
                remainingSecs = pad->getRemainingSeconds();

            if (isActivePlay && remainingSecs <= 5.0)
            {
                if ((juce::Time::getMillisecondCounter() % 400) < 200)
                    g.setColour (pal.danger);
                else
                    g.setColour (pal.textSecondary);
            }
            else
            {
                g.setColour (pal.textSecondary);
            }

            g.setFont (owner.rowFonts.timer);
            showcontrol::bgmList::drawPlaylistTimeCell (g, remainingText, remainingRect);

            g.setColour (pal.textMuted);
            g.setFont (owner.rowFonts.timer);
            showcontrol::bgmList::drawPlaylistTimeCell (g, elapsedText, elapsedRect);
        }

        g.setColour (pal.borderSubtle);
        g.drawHorizontalLine (height - 1, 0.0f, (float) width);
    }

    void resized() override
    {
        layoutLoopIconInNameColumn();
    }

private:
    int nameColumnStartX() const
    {
        int textX = showcontrol::bgmList::kNameStartDefault;

        if (owner.padAccessor != nullptr)
        {
            if (auto* pad = owner.padAccessor (rowIndex))
            {
                if (pad->isPlaying() || pad->isPaused())
                    textX = showcontrol::bgmList::kNameStartWithStatusIcon;
            }
        }

        return textX;
    }

    /** Vùng cột 1 (tên cue) — kết thúc trước cột thời gian, đồng bộ BGM list. */
    juce::Rectangle<int> nameColumnArea() const
    {
        return showcontrol::bgmList::listNameCellArea (getWidth(), getHeight(), nameColumnStartX());
    }

    void layoutLoopIconInNameColumn()
    {
        auto cellArea = nameColumnArea();
        cellArea.removeFromRight (showcontrol::bgmList::kLoopIconRightPad);
        auto loopIconArea = cellArea.removeFromRight (showcontrol::bgmList::kLoopIconSlotWidth);
        trackLoopIconComponent.setBounds (loopIconArea.withSizeKeepingCentre (16, 16));
    }

    CueListPanel& owner;
    int rowIndex = -1;
    bool rowSelected = false;
    TrackLoopIconComponent trackLoopIconComponent;
};

//==============================================================================
class CueListPanel::CueListBoxModel : public juce::ListBoxModel
{
public:
    explicit CueListBoxModel (CueListPanel& ownerIn) : owner (ownerIn) {}

    int getNumRows() override
    {
        return owner.cues.size();
    }

    void paintListBoxItem (int, juce::Graphics&, int, int, bool) override
    {
        // CueListRowCell vẽ toàn bộ nội dung — tránh double-paint.
    }

    juce::Component* refreshComponentForRow (int rowNumber, bool isRowSelected,
                                             juce::Component* existingComponentToUpdate) override
    {
        if (! juce::isPositiveAndBelow (rowNumber, owner.cues.size()))
        {
            delete existingComponentToUpdate;
            return nullptr;
        }

        auto* cell = dynamic_cast<CueListRowCell*> (existingComponentToUpdate);

        if (cell == nullptr)
            cell = new CueListRowCell (owner);

        cell->updateRowData (rowNumber, isRowSelected);
        return cell;
    }

    void listBoxItemClicked (int row, const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu())
        {
            if (! juce::isPositiveAndBelow (row, owner.cues.size()))
                return;

            const auto selectedRows = owner.listBox.getSelectedRows();
            const bool clickedInsideSelection = selectedRows.contains (row) && selectedRows.size() > 0;

            // Right-click on an already-selected row keeps the current multi-selection.
            // Right-click outside the selection resets to the clicked row.
            if (! clickedInsideSelection)
            {
                owner.listBox.selectRow (row, false, true);
                owner.selectedIndex = row;
                owner.fireSelectionFromListBox();
            }

            if (owner.onCueRightClick)
                owner.onCueRightClick (row);

            owner.showTrackContextMenu (row);
            return;
        }

        // ListBox đã gọi selectRowsBasedOnModifierKeys — đồng bộ state + callback BGM-style.
        owner.applySelectionForRowClick (row, e.mods);

        if (e.getNumberOfClicks() == 2)
        {
            const auto local = owner.listBox.getLocalPoint (e.eventComponent, e.getPosition());

            if (owner.isPointInTrackNameColumn (row, local.x))
            {
                owner.beginTrackRename (row);
                return;
            }

            if (owner.onCueTriggered)
                owner.onCueTriggered (row);
        }
    }

    void backgroundClicked (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu())
        {
            owner.showListBackgroundSortMenu (e);
            return;
        }

        const auto local = owner.listBox.getLocalPoint (e.eventComponent, e.getPosition());
        owner.beginCueMarquee (local, e.mods);
    }

    void selectedRowsChanged (int lastRowSelected) override
    {
        juce::ignoreUnused (lastRowSelected);

        if (owner.shouldSilenceListenersForStateOp())
            return;

        owner.fireSelectionFromListBox();
    }

    juce::var getDragSourceDescription (const juce::SparseSet<int>& selectedRows) override
    {
        if (selectedRows.isEmpty())
            return juce::var();

        return juce::var (showcontrol::crossdrag::buildLocalRowReorderDragToken (selectedRows[0]));
    }

    bool mayDragToExternalWindows() const override { return false; }

private:
    CueListPanel& owner;
};

//==============================================================================
CueListPanel::CueListPanel()
    : listBox ("CueListBox", *this)
{
    setOpaque (true);
    setWantsKeyboardFocus (true);

    headerComponent = std::make_unique<CueListHeaderComponent> (*this);
    addAndMakeVisible (*headerComponent);

    reorderOverlay = std::make_unique<CueReorderOverlay> (*this);
    addChildComponent (*reorderOverlay);
    reorderOverlay->setVisible (false);

    model = std::make_unique<CueListBoxModel> (*this);
    listBox.setModel (model.get());
    listBox.setMultipleSelectionEnabled (true);
    // selectOnMouseUp(true) — JUCE 8: setRowSelectedOnMouseDown(false); bảo vệ cụm đa chọn khi nhấn giữ.
    listBox.setRowSelectedOnMouseDown (false);
    // Kéo nội bộ qua getDragSourceDescription (selection-aware) + DragAndDropTarget.
    listBox.setRowHeight (kRowH);
    listBox.setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
    listBox.setOutlineThickness (0);
    addAndMakeVisible (listBox);

    // Marquee quét chọn nhiều (vùng trống / kéo chéo) — không can thiệp kéo hàng native.
    listBox.addMouseListener (this, true);

    trackNameLabel.setVisible (false);
    trackNameLabel.addListener (this);
    addChildComponent (trackNameLabel);

    refreshLocalizedText();
    updateTheme (true);
}

CueListPanel::~CueListPanel()
{
    stopTimer();
    haltActiveTimers();
    trackNameLabel.removeListener (this);
    listBox.removeMouseListener (this);
    listBox.setModel (nullptr);
    reorderOverlay.reset();
}

void CueListPanel::haltActiveTimers() noexcept
{
    stopTimer();

    cancelCueRowReorder();
    clearCueRowReorderJuceDropPending();
    marqueePrimed = false;
    marqueeActive = false;
    marqueeAdditive = false;
    marqueeBaseSelection.clear();

    if (reorderOverlay != nullptr)
    {
        reorderOverlay->setBufferedToImage (false);
        reorderOverlay->setVisible (false);
    }
}

void CueListPanel::lookAndFeelChanged()
{
    juce::Component::lookAndFeelChanged();

    if (auto* showLaf = dynamic_cast<ShowControlLookAndFeel*> (&getLookAndFeel()))
        isDarkMode = showLaf->isDarkMode();

    refreshLocalizedText();
    repaint();
}

void CueListPanel::refreshLocalizedText()
{
    listBox.setTooltip (showcontrol::localization::tr (
        u8"Rê chuột để kéo thả di chuyển vị trí kịch bản"));
    repaint();
    listBox.repaint();
}

void CueListPanel::rebuildRowPaintFonts()
{
    const auto typography = showcontrol::bgmList::makePlaylistRowTypography();

    rowFonts.indexBold  = typography.index;
    rowFonts.namePlain  = typography.cellPlain;
    rowFonts.nameBold   = typography.cellPlain;
    rowFonts.timer      = typography.timerPlain;
    rowFonts.timerBold  = typography.timerBold;
    rowFonts.autoFollow = showcontrol::bgmList::cueAutoFollowFont();
}

bool CueListPanel::shouldSilenceListenersForStateOp() const noexcept
{
    if (auto* main = findParentComponentOfClass<MainComponent>())
        return main->isOperatingState();

    return false;
}

bool CueListPanel::reserveLoopSlotForRow (int rowIndex) const noexcept
{
    if (padAccessor == nullptr || ! juce::isPositiveAndBelow (rowIndex, cues.size()))
        return false;

    if (auto* pad = padAccessor (rowIndex))
        return pad->isLooping() && pad->hasAudioFile();

    return false;
}

void CueListPanel::repaintReorderSourceRows()
{
    for (int i = 0; i < dragSourceRows.size(); ++i)
        listBox.repaintRow (dragSourceRows[i]);
}

void CueListPanel::syncRowLiveTextCaches()
{
    rowRemainingText.resize (cues.size());
    rowElapsedText.resize (cues.size());

    if (! padAccessor)
        return;

    for (int i = 0; i < cues.size(); ++i)
    {
        auto* pad = padAccessor (i);

        if (pad == nullptr || ! pad->isPlaybackPositionLive())
        {
            rowRemainingText.set (i, juce::String::fromUTF8 (u8"00:00.0"));
            rowElapsedText.set (i, juce::String::fromUTF8 (u8"00:00.0"));
            continue;
        }

        rowRemainingText.set (i, formatCueTimeString (pad->getRemainingSeconds()));
        rowElapsedText.set (i, formatCueTimeString (pad->getElapsedSeconds()));
    }
}

void CueListPanel::updateTheme (bool isDark)
{
    isDarkMode = isDark;
    rebuildRowPaintFonts();
    ShowControlLookAndFeel::applyTrackNameLabelStyle (trackNameLabel, isDark,
                                                      renamingTrackIndex >= 0
                                                          && listBox.isRowSelected (renamingTrackIndex));
    listBox.repaint();

    if (headerComponent != nullptr)
        headerComponent->repaint();

    repaint();
}

bool CueListPanel::shouldShowColumnHeader() const noexcept
{
    return ! cues.isEmpty();
}

void CueListPanel::updateListBoxContentIfLaidOut() noexcept
{
    if (listBox.getWidth() > 0 && listBox.getHeight() > 0)
        listBox.updateContent();
}

void CueListPanel::refreshListBoxData (bool resetScroll)
{
    resized();
    updateListBoxContentIfLaidOut();
    syncHeaderToListScrollbar();

    if (resetScroll)
        resetListScrollToTop();

    listBox.repaint();
    repaint();
}

void CueListPanel::syncCueTagColourAt (int index, juce::Colour colour)
{
    if (! juce::isPositiveAndBelow (index, cues.size()))
        return;

    cues.getReference (index).tagColour = showcontrol::colours::snapToPalette (colour);
    repaintCueRow (index);
}

void CueListPanel::repaintCueRow (int rowIndex)
{
    if (! juce::isPositiveAndBelow (rowIndex, cues.size()))
        return;

    if (auto* cell = dynamic_cast<CueListRowCell*> (listBox.getComponentForRowNumber (rowIndex)))
        cell->updateRowData (rowIndex, listBox.isRowSelected (rowIndex));
    else
        listBox.repaintRow (rowIndex);
}

void CueListPanel::resetListScrollToTop()
{
    if (auto* vp = listBox.getViewport())
        vp->setViewPosition (0, 0);
}

void CueListPanel::setCues (const juce::Array<CueItem>& newCues)
{
    cues = newCues;
    syncRowLiveTextCaches();
    refreshListBoxData();
    syncLiveTimer();
}

void CueListPanel::addCue (const CueItem& item)
{
    cues.add (item);
        updateListBoxContentIfLaidOut();
        resized();
        listBox.repaint();
}

void CueListPanel::removeCueFromDataModelAtIndex (int rowIndex)
{
    if (! juce::isPositiveAndBelow (rowIndex, cues.size()))
        return;

    cues.remove (rowIndex);

    if (selectedIndex >= cues.size())
        selectedIndex = cues.size() - 1;
}

void CueListPanel::applySelectionAnchorAfterRowRemoval (int firstDeletedRow)
{
    if (cues.isEmpty())
    {
        selectedIndex = -1;
        listBox.deselectAllRows();
        return;
    }

    const int targetRow = juce::jmin (juce::jmax (0, firstDeletedRow - 1), cues.size() - 1);
    setSelectedIndex (targetRow);
}

void CueListPanel::updateTableContent (bool resetScroll)
{
    refreshListBoxData (resetScroll);
}

void CueListPanel::deleteSelectedCues()
{
    removeSelectedCues();
}

void CueListPanel::removeSelectedCues()
{
    const juce::SparseSet<int> selectedRows = listBox.getSelectedRows();

    if (selectedRows.isEmpty())
        return;

    const auto rowsDescending = sparseSetToDescendingArray (selectedRows);
    const auto rowsAscending  = sparseSetToSortedArray (selectedRows);

    if (onCueSelectionChanged)
        onCueSelectionChanged (rowsAscending);

    if (onDeleteKeyPressed)
    {
        onDeleteKeyPressed();
        return;
    }

    const int lowestDeletedRow = rowsDescending.getLast();

    for (const int row : rowsDescending)
        removeCueFromDataModelAtIndex (row);

    updateTableContent (false);
    applySelectionAnchorAfterRowRemoval (lowestDeletedRow);

    if (auto* mainComp = findParentComponentOfClass<MainComponent>())
        mainComp->triggerSave();
}

void CueListPanel::removeCue (int index)
{
    if (index >= 0 && index < cues.size())
    {
        cues.remove (index);
        if (selectedIndex >= cues.size())
            selectedIndex = cues.size() - 1;

        resized();
        updateListBoxContentIfLaidOut();
        listBox.repaint();
        syncLiveTimer();
    }
}

void CueListPanel::setSelectedIndex (int idx)
{
    selectedIndex = idx;

    if (idx >= 0 && idx < cues.size())
    {
        juce::SparseSet<int> rows;
        rows.addRange (juce::Range<int> (idx, idx + 1));
        applyListBoxSelectedRows (rows);
        listBox.scrollToEnsureRowIsOnscreen (idx);
    }
}

void CueListPanel::setSelectedIndices (const juce::Array<int>& indices)
{
    juce::SparseSet<int> rows;

    for (auto idx : indices)
        if (juce::isPositiveAndBelow (idx, cues.size()))
            rows.addRange (juce::Range<int> (idx, idx + 1));

    applyListBoxSelectedRows (rows);
    selectedIndex = indices.isEmpty() ? -1 : indices.getLast();
}

int CueListPanel::getSelectedIndex() const { return selectedIndex; }
int CueListPanel::getCueCount() const { return cues.size(); }

void CueListPanel::setPlayingIndex (int idx)
{
    if (playingIndex != idx)
    {
        const int prev = playingIndex;
        playingIndex = idx;

        if (prev >= 0)
            listBox.repaintRow (prev);

        if (playingIndex >= 0)
            listBox.repaintRow (playingIndex);
    }

    syncLiveTimer();
}

void CueListPanel::setArmedIndex (int idx)
{
    if (armedIndex != idx)
    {
        const int prev = armedIndex;
        armedIndex = idx;

        if (prev >= 0)
            listBox.repaintRow (prev);

        if (armedIndex >= 0)
            listBox.repaintRow (armedIndex);
    }
}

const CueItem* CueListPanel::getCue (int index) const
{
    return (index >= 0 && index < cues.size()) ? &cues.getReference (index) : nullptr;
}

void CueListPanel::setPadAccessor (std::function<SoundPad* (int index)> accessor)
{
    padAccessor = std::move (accessor);
    updateListBoxContentIfLaidOut();
    syncHeaderToListScrollbar();
}

void CueListPanel::shortCircuitLiveRowVisuals()
{
    const int prevPlaying = playingIndex;
    playingIndex = -1;

    syncRowLiveTextCaches();
    stopTimer();

    if (prevPlaying >= 0)
        listBox.repaintRow (prevPlaying);

    listBox.repaint();
    repaint();
}

void CueListPanel::notifyPlaybackActivity()
{
    syncLiveTimer();
}

bool CueListPanel::handleTransportKey (const juce::KeyPress& key)
{
    if (! isVisible())
        return false;

    const int keyCode = showcontrol::keyboard::physicalKeyCode (key);

    if (keyCode == juce::KeyPress::spaceKey || keyCode == 32)
    {
        if (selectedIndex >= 0 && onCueListPlay)
            onCueListPlay (selectedIndex);

        return true;
    }

    if (keyCode == (int) 'p' || keyCode == (int) 'P')
    {
        if (selectedIndex >= 0 && onCueListPause)
            onCueListPause (selectedIndex);

        return true;
    }

    if (keyCode == (int) 's' || keyCode == (int) 'S')
    {
        if (selectedIndex >= 0 && onCueListStop)
            onCueListStop (selectedIndex);

        return true;
    }

    return false;
}

int CueListPanel::getPreferredHeight() const
{
    if (cues.isEmpty())
        return kRowH;

    return shouldShowColumnHeader()
               ? (kHeaderH + kHeaderGap + cues.size() * kRowH)
               : cues.size() * kRowH;
}

void CueListPanel::paint (juce::Graphics& g)
{
    const auto cols = showcontrol::ui::ThemePaintColours::read (*this);
    isDarkMode = cols.isDark;
    g.fillAll (cols.windowBg);

    if (! cueListHasLoadedContent (cues))
    {
        g.setColour (cols.textMuted);
        auto textArea = getLocalBounds().reduced (32);
        g.setFont (ShowTheme::fontBold (16.0f));
        g.drawFittedText (showcontrol::localization::tr (u8"DANH SÁCH KỊCH BẢN CUE TRỐNG"),
                          textArea.removeFromTop (textArea.getHeight() / 2 - 6),
                          juce::Justification::centred,
                          2);
        g.setFont (ShowTheme::font (14.0f));
        g.drawFittedText (showcontrol::localization::tr (u8"Kéo thả file nhạc vào đây để thiết lập show"),
                          textArea,
                          juce::Justification::centred,
                          3);
        return;
    }
}

void CueListPanel::syncHeaderToListScrollbar()
{
    if (headerComponent == nullptr || ! headerComponent->isVisible())
        return;

    int scrollbarTrim = 0;

    if (listBox.isShowing())
    {
        const auto& vBar = listBox.getVerticalScrollBar();

        if (vBar.isVisible())
            scrollbarTrim = vBar.getWidth();
    }

    auto headerBounds = headerComponent->getBounds();
    headerComponent->setBounds (headerBounds.getX(), headerBounds.getY(),
                                std::max (0, listBox.getWidth() - scrollbarTrim),
                                headerBounds.getHeight());
}

void CueListPanel::resized()
{
    auto area = getLocalBounds();
    const bool showHeader = shouldShowColumnHeader();

    juce::Rectangle<int> headerArea;

    if (showHeader)
        headerArea = area.removeFromTop (kHeaderH);

    if (showHeader)
        area.removeFromTop (kHeaderGap);

    listBox.setBounds (std::max (0, area.getX()),
                       std::max (0, area.getY()),
                       std::max (0, area.getWidth()),
                       std::max (0, area.getHeight()));

    if (headerComponent != nullptr)
    {
        headerComponent->setVisible (showHeader);

        if (showHeader)
        {
            headerComponent->setBounds (headerArea);
            headerComponent->toFront (false);
        }
    }

    syncHeaderToListScrollbar();
    updateCueReorderOverlayBounds();

    if (renamingTrackIndex >= 0)
        layoutTrackNameLabelForRow (renamingTrackIndex);
}

void CueListPanel::updateCueReorderOverlayBounds()
{
    if (reorderOverlay == nullptr)
        return;

    reorderOverlay->setBounds (listBox.getBounds());
    reorderOverlay->toFront (false);

    if (headerComponent != nullptr && headerComponent->isVisible())
        headerComponent->toFront (false);
}

juce::Rectangle<int> CueListPanel::getCueListInsertLineBounds() const
{
    const int rowHeight = listBox.getRowHeight();

    if (rowHeight <= 0)
        return {};

    const int n = cues.size();
    const int target = juce::jlimit (0, n, cueRowReorderInsertIndex);
    const auto listBounds = listBox.getBounds();

    int scrollY = 0;

    if (auto* viewport = listBox.getViewport())
        scrollY = viewport->getViewPositionY();

    const int lineY = listBounds.getY() + target * rowHeight - scrollY;
    return { 0, lineY, getWidth(), 0 };
}

void CueListPanel::repaintReorderInsertLineStrip (int insertIndex) const
{
    if (insertIndex < 0)
        return;

    const int rowHeight = listBox.getRowHeight();

    if (rowHeight <= 0)
        return;

    const auto listBounds = listBox.getBounds();
    int scrollY = 0;

    if (auto* viewport = listBox.getViewport())
        scrollY = viewport->getViewPositionY();

    const int lineY = listBounds.getY() + insertIndex * rowHeight - scrollY;
    constexpr int margin = 12;
    const_cast<CueListPanel*> (this)->repaint (0, lineY - margin, getWidth(), margin * 2);
}

void CueListPanel::repaintReorderInsertLineStrips (int prevIndex, int nextIndex) const
{
    if (prevIndex >= 0)
        repaintReorderInsertLineStrip (prevIndex);

    if (nextIndex >= 0 && nextIndex != prevIndex)
        repaintReorderInsertLineStrip (nextIndex);
}

void CueListPanel::repaintDragCapsuleProxyStrip (juce::Point<int> centreInPanel) const
{
    constexpr int halfW = 120;
    constexpr int halfH = 19;
    const_cast<CueListPanel*> (this)->repaint (centreInPanel.x - halfW,
                                               centreInPanel.y - halfH,
                                               halfW * 2,
                                               halfH * 2);
}

void CueListPanel::paintCueReorderOverlay (juce::Graphics& g) const
{
    paintMarquee (g);

    if (! cueRowReorderActive || cueJuceDragStarted)
        return;

    const int anchorRow = dragSourceAnchorRow >= 0
                              ? dragSourceAnchorRow
                              : (dragSourceRows.isEmpty() ? -1 : dragSourceRows[0]);
    const int itemCount = juce::jmax (1, dragSourceRows.size());

    paintPremiumCueDragCapsuleAt (g,
                                  (float) cueRowReorderPointerPos.x,
                                  (float) cueRowReorderPointerPos.y,
                                  getCueTitleRowAtIndex (anchorRow),
                                  itemCount);
}

bool CueListPanel::keyPressed (const juce::KeyPress& key)
{
    if (showcontrol::keyboard::isUndoRedoKeyPress (key))
    {
        if (onChainedKeyPressed != nullptr && onChainedKeyPressed (key))
            return true;
    }

    if (handleTransportKey (key))
        return true;

    const int keyCode = showcontrol::keyboard::physicalKeyCode (key);

    if ((keyCode == juce::KeyPress::deleteKey || keyCode == juce::KeyPress::backspaceKey)
        && key.getModifiers().isCommandDown())
    {
        deleteSelectedCues();
        return true;
    }

    const int arrowCode = HotkeyManager::normalizeArrowKeyCode (keyCode);
    const int rowCount = cues.size();

    if (rowCount <= 0)
        return false;

    if (arrowCode == juce::KeyPress::upKey || arrowCode == juce::KeyPress::leftKey)
        setSelectedIndex (juce::jmax (0, selectedIndex - 1));
    else if (arrowCode == juce::KeyPress::downKey || arrowCode == juce::KeyPress::rightKey)
        setSelectedIndex (juce::jmin (rowCount - 1, juce::jmax (0, selectedIndex + 1)));
    else
        return false;

    if (onCueSelected && selectedIndex >= 0)
        onCueSelected (selectedIndex);

    return true;
}

void CueListPanel::timerCallback()
{
    if (! padAccessor)
    {
        stopTimer();
        return;
    }

    bool anyActive = false;
    bool needsRepaint = false;

    for (int i = 0; i < cues.size(); ++i)
    {
        if (auto* pad = padAccessor (i); pad != nullptr && (pad->isPlaying() || pad->isPaused()))
            anyActive = true;
    }

    if (! anyActive)
    {
        stopTimer();
        syncRowLiveTextCaches();
        listBox.repaint();
        repaint();
        return;
    }

    syncRowLiveTextCaches();

    for (int i = 0; i < cues.size(); ++i)
    {
        auto* pad = padAccessor (i);

        if (pad == nullptr)
            continue;

        if (pad->isPlaybackPositionLive())
        {
            listBox.repaintRow (i);
            needsRepaint = true;
        }
    }

    if (needsRepaint)
        repaint();
}

void CueListPanel::syncLiveTimer()
{
    if (! isShowing() || ! isVisible())
    {
        stopTimer();
        return;
    }

    if (anyRowTransportActive())
        startTimerHz (60);
    else
        stopTimer();
}

bool CueListPanel::anyRowTransportActive() const
{
    if (! padAccessor)
        return false;

    for (int i = 0; i < cues.size(); ++i)
        if (auto* pad = padAccessor (i); pad != nullptr && pad->isPlaybackPositionLive())
            return true;

    return false;
}

void CueListPanel::paintHeader (juce::Graphics& g, juce::Rectangle<int> bounds) const
{
    const auto cols = showcontrol::ui::ThemePaintColours::read (*this);
    const auto& pal = ShowTheme::get (cols.isDark);

    g.setColour (pal.panelElevated);
    showcontrol::gfx::safeFillRect (g, bounds);
    g.setColour (pal.border);
    g.drawHorizontalLine (bounds.getBottom() - 1, (float) bounds.getX(), (float) bounds.getRight());

    g.setColour (showcontrol::bgmList::playlistHeaderTextColour (cols.isDark));
    g.setFont (showcontrol::bgmList::playlistHeaderFont());

    const auto titleRect     = showcontrol::bgmList::titleBounds (bounds.getWidth(), bounds.getHeight())
                                   .translated (bounds.getX(), bounds.getY());
    const auto remainingRect = showcontrol::bgmList::timeRemainingBounds (bounds.getWidth(), bounds.getHeight())
                                   .translated (bounds.getX(), bounds.getY());
    const auto elapsedRect   = showcontrol::bgmList::totalDurationBounds (bounds.getWidth(), bounds.getHeight())
                                   .translated (bounds.getX(), bounds.getY());

    g.drawText (showcontrol::localization::tr (u8"TÊN CUE KỊCH BẢN"), titleRect, juce::Justification::centredLeft);
    showcontrol::bgmList::drawPlaylistTimeCell (g, showcontrol::localization::tr (u8"CÒN LẠI"), remainingRect);
    showcontrol::bgmList::drawPlaylistTimeCell (g, showcontrol::localization::tr (u8"ĐÃ CHẠY"), elapsedRect);
}

void CueListPanel::showTrackContextMenu (int cueIndex)
{
    if (! juce::isPositiveAndBelow (cueIndex, cues.size()))
        return;

    const bool canSort = ((canSortRows == nullptr) || canSortRows()) && cues.size() > 1;

    juce::PopupMenu menu;
    auto& colourRef = cues.getReference (cueIndex).tagColour;

    menu.addCustomItem (1,
                        showcontrol::colours::makeTagColourMenuRow (
                            colourRef,
                            [this, cueIndex] (juce::Colour picked)
                            {
                                if (onCueColorChanged)
                                    onCueColorChanged (cueIndex, picked);

                                repaintCueRow (cueIndex);
                            }),
                        nullptr,
                        juce::String::fromUTF8 (u8" "));

    menu.addSeparator();
    menu.addItem ((int) TrackMenuId::autoColorList,
                  juce::String::fromUTF8 (u8"Tô màu tự động toàn danh sách"),
                  cues.size() > 0);
    menu.addItem ((int) TrackMenuId::resetItemColour,
                  juce::String::fromUTF8 (u8"Reset màu mục này về mặc định"));
    if (cues.size() > 1)
        menu.addItem ((int) TrackMenuId::resetAllColours,
                      juce::String::fromUTF8 (u8"Reset màu toàn danh sách"));

    menu.addSeparator();

    if (canSort)
    {
        menu.addItem ((int) TrackMenuId::sortAscending,
                      juce::String::fromUTF8 (u8"Sắp xếp hàng kịch bản tăng dần (A-Z) 🔤"));
        menu.addSeparator();
    }

    menu.addItem ((int) TrackMenuId::renameTrack,  showcontrol::localization::tr (u8"Đổi tên bài hát"));
    menu.addItem ((int) TrackMenuId::replaceFile, showcontrol::localization::tr (u8"Thay đổi file nhạc..."));
    menu.addItem ((int) TrackMenuId::duplicate,   showcontrol::localization::tr (u8"Nhân bản"));
    menu.addItem ((int) TrackMenuId::trimEditor,  showcontrol::localization::tr (u8"Chỉnh sửa (Trim Editor)..."));
    menu.addItem ((int) TrackMenuId::revealFile,  showcontrol::localization::tr (u8"Mở vị trí tệp..."));
    menu.addSeparator();
    menu.addItem ((int) TrackMenuId::resetFade,   showcontrol::localization::tr (u8"Reset Fade về mặc định (0 ms)"));
    menu.addItem ((int) TrackMenuId::deleteItem,  showcontrol::localization::tr (u8"Xóa"));

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&listBox).withMousePosition(),
                        [this, cueIndex] (int result)
                        {
                            if (result == (int) TrackMenuId::sortAscending)
                            {
                                sortCueRowsAscending();
                                return;
                            }

                            if (result == (int) TrackMenuId::renameTrack)
                            {
                                beginTrackRename (cueIndex);
                                return;
                            }

                            if (result == (int) TrackMenuId::autoColorList)
                            {
                                if (onAutoTagColoursRequested)
                                    onAutoTagColoursRequested();
                                return;
                            }

                            if (result == (int) TrackMenuId::resetItemColour)
                            {
                                if (onCueColorChanged)
                                    onCueColorChanged (cueIndex, showcontrol::colours::defaultTagColour());
                                return;
                            }

                            if (result == (int) TrackMenuId::resetAllColours)
                            {
                                if (onResetAllTagColoursRequested)
                                    onResetAllTagColoursRequested();
                                return;
                            }

                            if (onTrackMenuResult)
                                onTrackMenuResult (cueIndex, result);
                        });
}

void CueListPanel::showListBackgroundSortMenu (const juce::MouseEvent& e)
{
    juce::ignoreUnused (e);

    if (canSortRows != nullptr && ! canSortRows())
        return;

    juce::PopupMenu menu;
    menu.addItem (1, juce::String::fromUTF8 (u8"Sắp xếp hàng kịch bản tăng dần (A-Z) 🔤"));
    menu.addSeparator();
    menu.addItem (2, juce::String::fromUTF8 (u8"Tô màu tự động toàn danh sách"));
    menu.addItem (3, juce::String::fromUTF8 (u8"Reset màu toàn danh sách"));

    juce::Component::SafePointer<CueListPanel> safeThis (this);

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&listBox).withMousePosition(),
                        [safeThis] (int result)
                        {
                            if (safeThis == nullptr || result == 0)
                                return;

                            if (result == 1)
                            {
                                safeThis->sortCueRowsAscending();
                                return;
                            }

                            if (result == 2)
                            {
                                if (safeThis->onAutoTagColoursRequested)
                                    safeThis->onAutoTagColoursRequested();
                                return;
                            }

                            if (result == 3 && safeThis->onResetAllTagColoursRequested)
                                safeThis->onResetAllTagColoursRequested();
                        });
}

void CueListPanel::sortCueRowsAscending()
{
    if (canSortRows != nullptr && ! canSortRows())
        return;

    if (onSortRowsAscending != nullptr)
        onSortRowsAscending();

    listBox.deselectAllRows();
    selectedIndex = -1;
    fireSelectionFromListBox();
    updateTableContent();
}

bool CueListPanel::isPointInTrackNameColumn (int rowIndex, int localXInListBox) const
{
    if (! juce::isPositiveAndBelow (rowIndex, cues.size()))
        return false;

    int textX = showcontrol::bgmList::kNameStartDefault;

    if (padAccessor)
    {
        if (auto* pad = padAccessor (rowIndex))
        {
            if (pad->isPlaying() || pad->isPaused())
                textX = showcontrol::bgmList::kNameStartWithStatusIcon;
        }
    }

    const auto nameLayout = showcontrol::bgmList::layoutListNameRow (listBox.getWidth(), listBox.getRowHeight(),
                                                                     textX, reserveLoopSlotForRow (rowIndex));
    return nameLayout.nameArea.contains (localXInListBox, listBox.getRowHeight() / 2);
}

void CueListPanel::layoutTrackNameLabelForRow (int rowIndex)
{
    if (! juce::isPositiveAndBelow (rowIndex, cues.size()))
        return;

    auto rowBounds = listBox.getRowPosition (rowIndex, false);
    rowBounds = rowBounds + listBox.getPosition();

    int textX = showcontrol::bgmList::kNameStartDefault;

    if (padAccessor)
    {
        if (auto* pad = padAccessor (rowIndex))
        {
            if (pad->isPlaying() || pad->isPaused())
                textX = showcontrol::bgmList::kNameStartWithStatusIcon;
        }
    }

    const auto nameLayout = showcontrol::bgmList::layoutListNameRow (listBox.getWidth(), rowBounds.getHeight(),
                                                                     textX, reserveLoopSlotForRow (rowIndex));
    trackNameLabel.setBounds (rowBounds.getX() + nameLayout.nameArea.getX(),
                              rowBounds.getY(),
                              nameLayout.nameArea.getWidth(),
                              rowBounds.getHeight());
    trackNameLabel.setJustificationType (juce::Justification::centredLeft);
}

void CueListPanel::beginTrackRename (int rowIndex)
{
    if (! juce::isPositiveAndBelow (rowIndex, cues.size()))
        return;

    renamingTrackIndex = rowIndex;
    pendingRenameRowIndex = rowIndex;
    listBox.selectRow (rowIndex, false, true);
    selectedIndex = rowIndex;

    juce::String initialName = cues.getReference (rowIndex).name;

    if (padAccessor)
    {
        if (auto* pad = padAccessor (rowIndex))
            initialName = pad->getPadName();
    }

    ShowControlLookAndFeel::applyTrackNameLabelStyle (trackNameLabel, isDarkMode,
                                                      listBox.isRowSelected (rowIndex));
    trackNameLabel.setText (initialName, juce::dontSendNotification);
    layoutTrackNameLabelForRow (rowIndex);
    trackNameLabel.setVisible (true);
    trackNameLabel.toFront (false);
    listBox.repaintRow (rowIndex);

    juce::Component::SafePointer<CueListPanel> safe (this);
    juce::MessageManager::callAsync ([safe]
    {
        if (safe == nullptr || ! safe->trackNameLabel.isVisible())
            return;

        safe->trackNameLabel.showEditor();
    });
}

bool CueListPanel::commitTrackRenameFromLabel (int rowIndex)
{
    if (shouldSilenceListenersForStateOp())
        return false;

    if (! juce::isPositiveAndBelow (rowIndex, cues.size()))
        return false;

    const juce::String newName = trackNameLabel.getText().trim();

    if (newName.isEmpty())
        return false;

    const juce::String previousName = cues.getReference (rowIndex).name;

    if (padAccessor)
    {
        if (auto* pad = padAccessor (rowIndex))
        {
            if (pad->getPadName() == newName && previousName == newName)
                return false;
        }
    }
    else if (previousName == newName)
    {
        return false;
    }

    cues.getReference (rowIndex).name = newName;

    if (padAccessor)
    {
        if (auto* pad = padAccessor (rowIndex))
            pad->setCustomName (newName);
    }

    if (onTrackRenamed)
        onTrackRenamed (rowIndex, newName, previousName);

    refreshListBoxData (false);
    pendingRenameRowIndex = -1;
    return true;
}

void CueListPanel::labelTextChanged (juce::Label* labelThatHasChanged)
{
    if (shouldSilenceListenersForStateOp())
        return;

    if (labelThatHasChanged != &trackNameLabel)
        return;

    const int rowIndex = pendingRenameRowIndex >= 0 ? pendingRenameRowIndex : renamingTrackIndex;

    if (rowIndex < 0)
        return;

    commitTrackRenameFromLabel (rowIndex);
}

void CueListPanel::editorShown (juce::Label* label, juce::TextEditor& editor)
{
    if (label != &trackNameLabel || renamingTrackIndex < 0)
        return;

    ShowControlLookAndFeel::applyInlineListNameEditorStyle (editor, isDarkMode,
                                                            listBox.isRowSelected (renamingTrackIndex));
    const auto pal = ShowTheme::get (isDarkMode);
    editor.setTextToShowWhenEmpty (showcontrol::localization::tr (u8"Nhập tên bài hát mới"),
                                   pal.textMuted);
}

void CueListPanel::editorHidden (juce::Label* label, juce::TextEditor& editor)
{
    juce::ignoreUnused (editor);

    if (shouldSilenceListenersForStateOp())
        return;

    if (label != &trackNameLabel)
        return;

    const int rowIndex = pendingRenameRowIndex >= 0 ? pendingRenameRowIndex : renamingTrackIndex;

    // JUCE gọi editorHidden trước labelTextChanged — commit tại đây để Enter không mất tên.
    if (rowIndex >= 0)
        commitTrackRenameFromLabel (rowIndex);

    const int prevRow = rowIndex;
    renamingTrackIndex = -1;
    pendingRenameRowIndex = -1;
    trackNameLabel.setVisible (false);

    if (prevRow >= 0)
        listBox.repaintRow (prevRow);
}

void CueListPanel::applySelectionForRowClick (int clickedIndex, const juce::ModifierKeys& mods)
{
    juce::ignoreUnused (mods);

    if (shouldSilenceListenersForStateOp())
        return;

    // JUCE ListBox đã xử lý Cmd/Shift + giữ multi-select khi click dòng đã chọn (chuẩn BGM drag-block).
    const int last = listBox.getLastRowSelected();
    selectedIndex = last >= 0 ? last : clickedIndex;
    fireSelectionFromListBox();
}

void CueListPanel::applyListBoxSelectedRows (const juce::SparseSet<int>& newRows)
{
    listBox.setSelectedRows (newRows, juce::NotificationType::dontSendNotification);
}

juce::Array<int> CueListPanel::collectSelectedRowIndices() const
{
    juce::Array<int> selected;

    for (int i = 0; i < listBox.getNumSelectedRows(); ++i)
        selected.add (listBox.getSelectedRow (i));

    selected.sort();
    return selected;
}

juce::Array<int> CueListPanel::getSelectedRowIndices() const
{
    return collectSelectedRowIndices();
}

//==============================================================================
// Thuật toán splice 3 bước — đồng bộ movePadsBlockInList (BGM list).
void CueListPanel::listBoxItemsDropped (int dragSourceRow,
                                        const juce::SparseSet<int>& selectedRows,
                                        int insertRowPosition)
{
    juce::ignoreUnused (dragSourceRow);

    juce::SparseSet<int> rowsToMove = selectedRows;

    if (rowsToMove.isEmpty())
        rowsToMove = dragSourceRows;

    if (rowsToMove.isEmpty() && dragSourceRow >= 0)
        rowsToMove.addRange (juce::Range<int> (dragSourceRow, dragSourceRow + 1));

    const auto sorted = sparseSetToSortedArray (rowsToMove);

    if (sorted.isEmpty())
        return;

    // --- Bước 1: Trích xuất ---
    juce::Array<CueItem> itemsToMove;
    itemsToMove.ensureStorageAllocated (sorted.size());

    for (const int idx : sorted)
        if (juce::isPositiveAndBelow (idx, cues.size()))
            itemsToMove.add (cues.getReference (idx));

    if (itemsToMove.isEmpty())
        return;

    const int originalCount = cues.size();
    int clampedInsert = juce::jlimit (0, originalCount, insertRowPosition);

    // --- Bước 2: Xóa ngược (tránh lệch chỉ số) ---
    for (int i = sorted.size() - 1; i >= 0; --i)
    {
        const int idx = sorted.getUnchecked (i);

        if (juce::isPositiveAndBelow (idx, cues.size()))
            cues.remove (idx);
    }

    // Tính lại vị trí chèn sau khi đã gỡ các dòng nguồn
    int adjustedInsert = clampedInsert;
    for (const int idx : sorted)
        if (idx < clampedInsert)
            --adjustedInsert;

    adjustedInsert = juce::jlimit (0, cues.size(), adjustedInsert);

    // --- Bước 3: Chèn xuôi ---
    for (int i = 0; i < itemsToMove.size(); ++i)
        cues.insert (adjustedInsert + i, itemsToMove.getReference (i));

    // Đồng bộ PAD/metadata nguồn sự thật (MainComponent::movePadsBlockInList)
    if (onCuesBlockReordered)
        onCuesBlockReordered (sorted, insertRowPosition);

    // --- Khôi phục highlight xanh dương: chỉ số mới của cụm vừa thả ---
    juce::SparseSet<int> newSelectedRowsIndices;
    newSelectedRowsIndices.addRange (juce::Range<int> (adjustedInsert, adjustedInsert + itemsToMove.size()));

    listBox.setSelectedRows (newSelectedRowsIndices, juce::NotificationType::dontSendNotification);
    selectedIndex = adjustedInsert + itemsToMove.size() - 1;

    updateListBoxContentIfLaidOut();
    repaint();

    fireSelectionFromListBox();
}

void CueListPanel::paintReorderInsertLine (juce::Graphics& g) const
{
    if (cueRowReorderInsertIndex < 0)
        return;

    const auto line = getCueListInsertLineBounds();

    if (line.getWidth() <= 0)
        return;

    const float lineY = (float) line.getY();
    showcontrol::crossdrag::paintNeonRoundedCapInsertLine (g, lineY, (float) getWidth());
}

void CueListPanel::paintCueRowReorderGhost (juce::Graphics& g) const
{
    if (! cueRowReorderActive || cueJuceDragStarted)
        return;

    const int anchorRow = dragSourceAnchorRow >= 0
                              ? dragSourceAnchorRow
                              : (dragSourceRows.isEmpty() ? -1 : dragSourceRows[0]);
    const int itemCount = juce::jmax (1, dragSourceRows.size());

    paintPremiumCueDragCapsuleAt (g,
                                  (float) cueRowReorderPointerPos.x,
                                  (float) cueRowReorderPointerPos.y,
                                  getCueTitleRowAtIndex (anchorRow),
                                  itemCount);
}

juce::Image CueListPanel::createPremiumCueDragImage (const juce::String& cueTitle, int selectedItemsCount)
{
    return showcontrol::crossdrag::createPremiumDragImage (cueTitle, selectedItemsCount);
}

juce::String CueListPanel::getCueTitleRowAtIndex (int rowIndex) const
{
    juce::String title;

    if (juce::isPositiveAndBelow (rowIndex, cues.size()))
        title = cues.getReference (rowIndex).name;

    if (title.isEmpty() && padAccessor != nullptr)
    {
        if (auto* pad = padAccessor (rowIndex))
            title = pad->getPadName();
    }

    if (title.isEmpty())
        title = juce::String::fromUTF8 (u8"Di chuyển CUE...");

    return title;
}

void CueListPanel::paintPremiumCueDragCapsuleAt (juce::Graphics& g,
                                                 float centreX,
                                                 float centreY,
                                                 const juce::String& cueTitle,
                                                 int selectedItemsCount) const
{
    const auto dragImg = createPremiumCueDragImage (cueTitle, selectedItemsCount);

    if (! dragImg.isValid())
        return;

    g.drawImageAt (dragImg,
                   juce::roundToInt (centreX - (float) dragImg.getWidth() * 0.5f),
                   juce::roundToInt (centreY - (float) dragImg.getHeight() * 0.5f));
}

void CueListPanel::paintMarquee (juce::Graphics& g) const
{
    if (! marqueeActive || cueRowReorderActive || cueRowReorderPressLocked)
        return;

    const auto& pal = ShowTheme::get (isDarkMode);
    const auto listBounds = listBox.getBounds();

    const auto marqueeLocal = juce::Rectangle<int>::leftTopRightBottom (
        juce::jmin (marqueeStartPos.x, marqueeEndPos.x),
        juce::jmin (marqueeStartPos.y, marqueeEndPos.y),
        juce::jmax (marqueeStartPos.x, marqueeEndPos.x),
        juce::jmax (marqueeStartPos.y, marqueeEndPos.y));

    auto marqueeScreen = marqueeLocal.translated (listBounds.getX(), listBounds.getY());

    g.setColour (pal.accent.withAlpha (0.14f));
    g.fillRect (marqueeScreen);
    g.setColour (pal.accent.withAlpha (0.72f));
    g.drawRect (marqueeScreen, 1);
}

void CueListPanel::beginCueMarquee (juce::Point<int> posInListBox, const juce::ModifierKeys& mods)
{
    marqueePrimed    = true;
    marqueeStartPos  = posInListBox;
    marqueeEndPos    = posInListBox;
    marqueeActive    = false;
    marqueeAdditive  = mods.isCommandDown() || mods.isCtrlDown();
    marqueeBaseSelection = collectSelectedRowIndices();
}

void CueListPanel::updateCueMarquee (juce::Point<int> posInListBox)
{
    if (cueRowReorderActive || cueRowReorderPressLocked)
        return;

    marqueeEndPos = posInListBox;

    if (! marqueeActive)
    {
        if (marqueeStartPos.getDistanceFrom (marqueeEndPos) < 5)
            return;

        marqueeActive = true;

        if (reorderOverlay != nullptr)
        {
            updateCueReorderOverlayBounds();
            reorderOverlay->setVisible (true);
            reorderOverlay->repaint();
        }
    }

    const auto marqueeLocal = juce::Rectangle<int>::leftTopRightBottom (
        juce::jmin (marqueeStartPos.x, marqueeEndPos.x),
        juce::jmin (marqueeStartPos.y, marqueeEndPos.y),
        juce::jmax (marqueeStartPos.x, marqueeEndPos.x),
        juce::jmax (marqueeStartPos.y, marqueeEndPos.y));

    juce::SparseSet<int> nextSelection;

    if (marqueeAdditive)
        for (const int idx : marqueeBaseSelection)
            nextSelection.addRange (juce::Range<int> (idx, idx + 1));

    for (int row = 0; row < cues.size(); ++row)
    {
        auto rowBounds = listBox.getRowPosition (row, false);

        if (marqueeLocal.intersects (rowBounds))
            nextSelection.addRange (juce::Range<int> (row, row + 1));
    }

    applyListBoxSelectedRows (nextSelection);
    selectedIndex = listBox.getLastRowSelected();
    fireSelectionFromListBox();

    if (reorderOverlay != nullptr)
        reorderOverlay->repaint();
}

void CueListPanel::endCueMarquee()
{
    marqueePrimed  = false;
    marqueeActive  = false;

    if (reorderOverlay != nullptr && ! cueRowReorderActive)
    {
        reorderOverlay->setBufferedToImage (false);
        reorderOverlay->setVisible (false);
        reorderOverlay->repaint();
    }
}

void CueListPanel::beginCueRowReorder (const juce::MouseEvent& e)
{
    const auto local = listBox.getLocalPoint (e.eventComponent, e.getPosition());

    dragSourceAnchorRow = listBox.getRowContainingPosition (local.x, local.y);

    if (dragSourceAnchorRow < 0)
        dragSourceAnchorRow = listBox.getInsertionIndexForPosition (local.x, local.y);

    dragSourceRows = listBox.getSelectedRows();

    if (dragSourceRows.isEmpty())
        return;

    cueRowReorderActive = true;
    cueRowReorderInsertIndex = dragSourceAnchorRow;
    cueRowReorderPointerPos = getLocalPoint (e.eventComponent, e.getPosition());
    cueRowReorderLastAutoScrollMs = 0;
    cueRowReorderStackAnimStartMs = juce::Time::getMillisecondCounter();
    cueRowReorderStackAnimActive = dragSourceRows.size() > 1;

    endCueMarquee();
    setMouseCursor (juce::MouseCursor::DraggingHandCursor);

    if (reorderOverlay != nullptr)
    {
        updateCueReorderOverlayBounds();
        reorderOverlay->setBufferedToImage (true);
        reorderOverlay->setVisible (true);
        reorderOverlay->toFront (false);
        reorderOverlay->repaint();
    }

    repaintReorderSourceRows();
    startCueJuceCrossDrag (e);
}

int CueListPanel::computeRowInsertionIndexAtListY (int localYInListBox) const noexcept
{
    const int rowHeight = listBox.getRowHeight();
    const int totalTracks = cues.size();

    if (rowHeight <= 0)
        return 0;

    int yInContent = localYInListBox;

    if (auto* viewport = listBox.getViewport())
        yInContent += viewport->getViewPositionY();

    return showcontrol::crossdrag::computeRoundedRowInsertionIndex (yInContent, rowHeight, totalTracks);
}

void CueListPanel::startCueJuceCrossDrag (const juce::MouseEvent& e)
{
    if (cueJuceDragStarted)
        return;

    juce::SparseSet<int> selectedRows = listBox.getSelectedRows();

    if (selectedRows.isEmpty())
        selectedRows = dragSourceRows;

    if (selectedRows.isEmpty())
        return;

    if (auto* dragContainer = juce::DragAndDropContainer::findParentDragContainerFor (this))
    {
        const int anchorRow = selectedRows[0];
        const juce::String firstCueTitle = getCueTitleRowAtIndex (anchorRow);
        const int itemCount = juce::jmax (1, selectedRows.size());
        juce::Image premiumDragProxy = createPremiumCueDragImage (firstCueTitle, itemCount);

        if (! premiumDragProxy.isValid())
            return;

        juce::Point<int> offset (premiumDragProxy.getWidth() / 2, premiumDragProxy.getHeight() / 2);
        const juce::var description = showcontrol::crossdrag::buildLocalRowReorderDragToken (anchorRow);

        dragContainer->startDragging (description,
                                      &listBox,
                                      juce::ScaledImage (premiumDragProxy),
                                      true,
                                      &offset);
        cueJuceDragStarted = true;
        juce::ignoreUnused (e);
    }
}

void CueListPanel::updateCueRowReorder (const juce::MouseEvent& e)
{
    if (! cueRowReorderActive)
        return;

    const int prevInsertIndex = cueRowReorderInsertIndex;
    const auto prevPointerPos = cueRowReorderPointerPos;

    const auto local = listBox.getLocalPoint (e.eventComponent, e.getPosition());
    cueRowReorderPointerPos = getLocalPoint (e.eventComponent, e.getPosition());
    cueRowReorderInsertIndex = computeRowInsertionIndexAtListY (local.y);

    autoScrollListBoxForReorder (local);
    repaintReorderSourceRows();

    if (prevInsertIndex != cueRowReorderInsertIndex)
        repaintReorderInsertLineStrips (prevInsertIndex, cueRowReorderInsertIndex);

    if (prevPointerPos != cueRowReorderPointerPos && reorderOverlay != nullptr)
    {
        const auto repaintCapsuleOnOverlay = [this] (juce::Point<int> panelPt)
        {
            const auto local = reorderOverlay->getLocalPoint (this, panelPt);
            constexpr int halfW = 120;
            constexpr int halfH = 19;
            reorderOverlay->repaint (local.x - halfW, local.y - halfH, halfW * 2, halfH * 2);
        };

        repaintCapsuleOnOverlay (prevPointerPos);
        repaintCapsuleOnOverlay (cueRowReorderPointerPos);
    }
}

void CueListPanel::endCueRowReorder()
{
    if (! cueRowReorderActive)
        return;

    if (cueJuceDragStarted)
    {
        stashCueRowReorderForJuceDrop();
        return;
    }

    const int insertRowPosition = cueRowReorderInsertIndex;
    const int anchorRow = dragSourceAnchorRow;
    const auto rowsToMove = dragSourceRows;
    const bool dropInsideList = listBox.getBounds().contains (cueRowReorderPointerPos);

    cancelCueRowReorder();

    if (dropInsideList)
        listBoxItemsDropped (anchorRow, rowsToMove, insertRowPosition);
}

void CueListPanel::stashCueRowReorderForJuceDrop()
{
    if (! cueRowReorderActive)
        return;

    cueRowReorderAwaitingJuceDrop = true;
    cueRowReorderActive = false;
    cueRowReorderPressLocked = false;
    cueRowReorderStackAnimActive = false;
    cueRowReorderStackAnimStartMs = 0;

    setMouseCursor (juce::MouseCursor::NormalCursor);

    if (reorderOverlay != nullptr)
    {
        reorderOverlay->setBufferedToImage (false);

        if (! marqueeActive)
            reorderOverlay->setVisible (false);

        reorderOverlay->repaint();
    }

    repaintReorderSourceRows();
}

void CueListPanel::commitCueRowReorderFromJuceDrop (int insertRowPosition)
{
    if (! cueRowReorderAwaitingJuceDrop)
        return;

    const int anchorRow = dragSourceAnchorRow;
    const auto rowsToMove = dragSourceRows;

    clearCueRowReorderJuceDropPending();

    if (listBox.getBounds().contains (cueRowReorderPointerPos))
        listBoxItemsDropped (anchorRow, rowsToMove, insertRowPosition);
}

void CueListPanel::clearCueRowReorderJuceDropPending() noexcept
{
    cueRowReorderAwaitingJuceDrop = false;
    cueJuceDragStarted = false;
    cueRowReorderInsertIndex = -1;
    dragSourceAnchorRow = -1;
    dragSourceRows.clear();
}

void CueListPanel::cancelCueRowReorder()
{
    const bool wasActive = cueRowReorderActive;

    cueRowReorderActive = false;
    cueRowReorderPressLocked = false;
    cueRowReorderInsertIndex = -1;
    cueRowReorderStackAnimActive = false;
    cueRowReorderStackAnimStartMs = 0;
    cueJuceDragStarted = false;

    if (wasActive)
    {
        setMouseCursor (juce::MouseCursor::NormalCursor);

        if (reorderOverlay != nullptr)
        {
            reorderOverlay->setBufferedToImage (false);

            if (! marqueeActive)
                reorderOverlay->setVisible (false);

            reorderOverlay->repaint();
        }

        repaintReorderSourceRows();
    }
}

void CueListPanel::autoScrollListBoxForReorder (juce::Point<int> posInListBox)
{
    if (auto* vp = listBox.getViewport())
    {
        const auto now = juce::Time::getMillisecondCounter();

        if ((juce::int64) now - cueRowReorderLastAutoScrollMs < 28)
            return;

        const int scrollY = vp->getViewPositionY();
        const int viewH = vp->getViewHeight();
        constexpr int margin = 28;
        int nextY = scrollY;

        if (posInListBox.y < margin)
            nextY = juce::jmax (0, scrollY - 14);
        else if (posInListBox.y > viewH - margin)
            nextY = scrollY + 14;

        if (nextY != scrollY)
        {
            cueRowReorderLastAutoScrollMs = (juce::int64) now;
            vp->setViewPosition (0, nextY);
        }
    }
}

void CueListPanel::lockCueRowReorderPressAt (int rowIndex)
{
    dragSourceAnchorRow = rowIndex;
    dragSourceRows = listBox.getSelectedRows();
    cueRowReorderPressLocked = true;
    endCueMarquee();
}

void CueListPanel::triggerCueDragSession (const juce::MouseEvent& e)
{
    endCueMarquee();

    if (! cueRowReorderActive)
    {
        if (e.getDistanceFromDragStart() <= 4)
            return;

        beginCueRowReorder (e);
    }

    if (cueRowReorderActive)
        updateCueRowReorder (e);
}

bool CueListPanel::tryImmediateCueRowDrag (const juce::MouseEvent& e)
{
    if (! listBox.isParentOf (e.eventComponent) && e.eventComponent != &listBox)
        return false;

    if (cueRowReorderActive)
    {
        triggerCueDragSession (e);
        return true;
    }

    if (e.getDistanceFromDragStart() <= 4)
        return false;

    // Reorder drag only starts from an already-selected press-lock gesture.
    // Otherwise dragging should be reserved for marquee selection.
    if (! cueRowReorderPressLocked)
        return false;

    triggerCueDragSession (e);
    return true;
}

void CueListPanel::handleCueListBoxMouseDragForMarquee (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu() || cueRowReorderActive || cueRowReorderPressLocked)
        return;

    if (! listBox.isParentOf (e.eventComponent) && e.eventComponent != &listBox)
        return;

    const auto local = listBox.getLocalPoint (e.eventComponent, e.getPosition());

    if (! marqueePrimed)
        return;

    updateCueMarquee (local);
}

void CueListPanel::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu() || ! e.mods.isLeftButtonDown())
        return;

    if (! listBox.isParentOf (e.eventComponent) && e.eventComponent != &listBox)
        return;

    const auto local = listBox.getLocalPoint (e.eventComponent, e.getPosition());
    const int row = listBox.getRowContainingPosition (local.x, local.y);

    const auto& selectedRows = listBox.getSelectedRows();

    // Dòng đã chọn → khóa marquee + khóa cụm (RowComponent không bubble mouseDrag lên ListBox).
    if (selectedRows.contains (row) && selectedRows.size() > 0)
    {
        lockCueRowReorderPressAt (row);
        return;
    }

    cueRowReorderPressLocked = false;
    beginCueMarquee (local, e.mods);
}

void CueListPanel::mouseDrag (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
        return;

    if (tryImmediateCueRowDrag (e))
        return;

    handleCueListBoxMouseDragForMarquee (e);
}

void CueListPanel::mouseUp (const juce::MouseEvent& e)
{
    juce::ignoreUnused (e);

    if (cueRowReorderActive)
        endCueRowReorder();
    else if (cueRowReorderPressLocked)
        cueRowReorderPressLocked = false;

    if (marqueePrimed)
        endCueMarquee();
}

void CueListPanel::fireSelectionFromListBox()
{
    if (shouldSilenceListenersForStateOp())
        return;

    const auto selected = collectSelectedRowIndices();

    if (! selected.isEmpty())
        selectedIndex = selected.getLast();

    if (onCueSelectionChanged)
        onCueSelectionChanged (selected);

    // onCueSelected is a single-target callback. Avoid collapsing multi-selection in MainComponent.
    if (onCueSelected && selected.size() == 1 && selectedIndex >= 0)
        onCueSelected (selectedIndex);
}

void CueListPanel::setCrossCopyDropHighlight (bool active)
{
    juce::ignoreUnused (active);
}

void CueListPanel::paintOverChildren (juce::Graphics& g)
{
    juce::Component::paintOverChildren (g);

    const bool isDraggingRowActive = cueRowReorderActive || localRowReorderDragActive;

    if (isDraggingRowActive && cueRowReorderInsertIndex >= 0)
        paintReorderInsertLine (g);
}

bool CueListPanel::isInterestedInDragSource (const SourceDetails& dragSourceDetails)
{
    if (dragSourceDetails.sourceComponent.get() != &listBox)
        return false;

    return showcontrol::crossdrag::isLocalRowReorderDrag (dragSourceDetails.description);
}

void CueListPanel::itemDragEnter (const SourceDetails& dragSourceDetails)
{
    if (! showcontrol::crossdrag::isLocalRowReorderDrag (dragSourceDetails.description))
        return;

    localRowReorderDragActive = true;
    dragSourceRows = listBox.getSelectedRows();

    if (dragSourceRows.isEmpty())
    {
        const int sourceRow = showcontrol::crossdrag::parseLocalRowReorderSourceIndex (dragSourceDetails.description);

        if (sourceRow >= 0)
            dragSourceRows.addRange (juce::Range<int> (sourceRow, sourceRow + 1));
    }

    dragSourceAnchorRow = dragSourceRows.isEmpty()
                              ? showcontrol::crossdrag::parseLocalRowReorderSourceIndex (dragSourceDetails.description)
                              : dragSourceRows[0];

    const auto listLocal = listBox.getLocalPoint (this, dragSourceDetails.localPosition);
    const int prevInsertIndex = cueRowReorderInsertIndex;
    cueRowReorderInsertIndex = computeRowInsertionIndexAtListY (listLocal.y);

    if (reorderOverlay != nullptr)
    {
        updateCueReorderOverlayBounds();
        reorderOverlay->setVisible (true);
        reorderOverlay->toFront (false);
    }

    repaintReorderInsertLineStrips (prevInsertIndex, cueRowReorderInsertIndex);
}

void CueListPanel::itemDragMove (const SourceDetails& dragSourceDetails)
{
    if (! showcontrol::crossdrag::isLocalRowReorderDrag (dragSourceDetails.description))
        return;

    localRowReorderDragActive = true;

    const auto listLocal = listBox.getLocalPoint (this, dragSourceDetails.localPosition);
    const int prevInsertIndex = cueRowReorderInsertIndex;
    cueRowReorderInsertIndex = computeRowInsertionIndexAtListY (listLocal.y);

    autoScrollListBoxForReorder (listLocal);

    if (prevInsertIndex != cueRowReorderInsertIndex)
        repaintReorderInsertLineStrips (prevInsertIndex, cueRowReorderInsertIndex);
}

void CueListPanel::itemDragExit (const SourceDetails& dragSourceDetails)
{
    juce::ignoreUnused (dragSourceDetails);
    const int prevInsertIndex = cueRowReorderInsertIndex;
    cueRowReorderInsertIndex = -1;
    localRowReorderDragActive = false;

    if (reorderOverlay != nullptr && ! cueRowReorderActive && ! marqueeActive)
        reorderOverlay->setVisible (false);

    if (prevInsertIndex >= 0)
        repaintReorderInsertLineStrip (prevInsertIndex);
}

void CueListPanel::itemDropped (const SourceDetails& dragSourceDetails)
{
    localRowReorderDragActive = false;

    if (! showcontrol::crossdrag::isLocalRowReorderDrag (dragSourceDetails.description))
    {
        cueRowReorderInsertIndex = -1;
        listBox.repaint();
        repaint();
        return;
    }

    const int sourceRowIndex = showcontrol::crossdrag::parseLocalRowReorderSourceIndex (dragSourceDetails.description);
    const int destRowIndex = cueRowReorderInsertIndex;

    if (sourceRowIndex >= 0 && destRowIndex >= 0 && sourceRowIndex != destRowIndex)
    {
        juce::SparseSet<int> rowsToMove = dragSourceRows;

        if (rowsToMove.isEmpty())
            rowsToMove.addRange (juce::Range<int> (sourceRowIndex, sourceRowIndex + 1));

        listBoxItemsDropped (sourceRowIndex, rowsToMove, destRowIndex);
    }

    cueRowReorderInsertIndex = -1;
    dragSourceRows.clear();
    dragSourceAnchorRow = -1;
    cueJuceDragStarted = false;
    cueRowReorderAwaitingJuceDrop = false;

    if (reorderOverlay != nullptr && ! cueRowReorderActive && ! marqueeActive)
        reorderOverlay->setVisible (false);

    refreshListBoxData (false);
    listBox.repaint();
    repaint();

    if (auto* mainComp = findParentComponentOfClass<MainComponent>())
        mainComp->triggerSave();
}
