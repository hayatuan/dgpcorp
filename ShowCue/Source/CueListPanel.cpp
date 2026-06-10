#include "CueListPanel.h"
#include "ShowControlLookAndFeel.h"
#include "ShowGraphicsSafe.h"
#include "ShowFlatIcons.h"
#include "HotkeyManager.h"

namespace
{
    juce::String formatCueTimeString (double timeInSeconds)
    {
        timeInSeconds = juce::jmax (0.0, timeInSeconds);
        const int mins = static_cast<int> (timeInSeconds) / 60;
        const int secs = static_cast<int> (timeInSeconds) % 60;
        const int ms   = static_cast<int> (timeInSeconds * 10.0) % 10;
        return juce::String::formatted ("%02d:%02d.%d", mins, secs, ms);
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
}

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
            owner.autoScrollListBoxForReorder (local);
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
        juce::ListBox::mouseDown (e);
        return;
    }

    owner.cancelCueRowReorder();
    owner.endCueMarquee();

    const auto rel = e.getEventRelativeTo (this);
    const int rowUnderMouse = hitRowIndexAt (rel.x, rel.y);

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

    juce::ListBox::mouseDrag (e);
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

//==============================================================================
class CueListPanel::CueListBoxModel : public juce::ListBoxModel
{
public:
    explicit CueListBoxModel (CueListPanel& ownerIn) : owner (ownerIn) {}

    int getNumRows() override
    {
        return owner.cues.size();
    }

    void paintListBoxItem (int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected) override
    {
        if (! juce::isPositiveAndBelow (rowNumber, owner.cues.size()))
            return;

        // Làm mờ dòng nguồn khi reorder — đồng bộ BGM list (setAlpha 0.30 trên SoundPad).
        juce::Graphics::ScopedSaveState rowOpacity (g);
        if (owner.cueRowReorderActive && owner.listBox.isRowSelected (rowNumber))
            g.setOpacity (0.30f);

        const auto cols = showcontrol::ui::ThemePaintColours::read (owner);
        const auto& pal = ShowTheme::get (cols.isDark);
        const auto& cue = owner.cues.getReference (rowNumber);

        juce::Rectangle<int> row (0, 0, width, height);

        if (rowIsSelected)
            g.setColour (cols.rowSelected);
        else if (rowNumber % 2 == 0)
            g.setColour (cols.listRowBg);
        else
            g.setColour (cols.isDark ? cols.panelBg : cols.windowBg);
        showcontrol::gfx::safeFillRect (g, row);

        SoundPad* pad = nullptr;
        if (owner.padAccessor)
            pad = owner.padAccessor (rowNumber);

        const bool isActivePlay = (pad != nullptr && pad->isPlaying());
        const bool isPaused     = (pad != nullptr && pad->isPaused());
        const bool isRowLive    = isActivePlay || isPaused;

        if (isRowLive)
        {
            g.setColour (pal.success.withAlpha (0.16f));
            showcontrol::gfx::safeFillRect (g, row.reduced (0, 1));
            g.setColour (pal.success);
            showcontrol::gfx::safeFillRect (g, 0, 4, 3, height - 8);
        }

        g.setColour (cue.tagColour);
        showcontrol::gfx::safeFillRect (g, isActivePlay ? 3 : 0, 0, 4, height);

        g.setColour (cols.textMuted);
        g.setFont (ShowTheme::fontBold (11.0f));
        g.drawText (juce::String (cue.cueNumber),
                    showcontrol::bgmList::kIndexX, 0,
                    showcontrol::bgmList::kIndexWidth, height,
                    juce::Justification::centred);

        int textX = showcontrol::bgmList::kNameStartDefault;
        if (isActivePlay)
        {
            const auto iconBounds = showcontrol::bgmList::statusIconBounds (height);
            showcontrol::icons::paintSpeakerIcon (g, iconBounds,
                                                  showcontrol::icons::speakerPlayingColour (rowIsSelected),
                                                  rowIsSelected);
            textX = showcontrol::bgmList::kNameStartWithStatusIcon;
        }
        else if (isPaused)
        {
            const auto iconBounds = showcontrol::bgmList::statusIconBounds (height);
            const auto iconCol = showcontrol::icons::iconColourForListState (rowIsSelected, cols.isDark);
            showcontrol::icons::paintPauseIcon (g, iconBounds, iconCol);
            textX = showcontrol::bgmList::kNameStartWithStatusIcon;
        }

        if (rowNumber == owner.armedIndex)
        {
            g.setColour (pal.accent);
            g.drawRoundedRectangle (row.reduced (2, 2).toFloat(), 4.0f, 1.5f);
        }

        const bool highlightRow = isRowLive || rowIsSelected;
        g.setColour (cue.isEnabled ? (highlightRow ? (isPaused ? pal.warning : pal.success) : pal.textPrimary)
                                   : pal.textMuted);
        g.setFont (ShowTheme::font (13.5f, highlightRow ? "Bold" : "Plain"));

        const int nameMaxW = showcontrol::bgmList::nameColumnMaxWidth (width, textX);
        g.drawText (cue.name, textX, 0, nameMaxW, height, juce::Justification::centredLeft, true);

        if (cue.autoFollow)
        {
            g.setColour (pal.success);
            g.setFont (ShowTheme::fontBold (11.0f));
            g.drawText (juce::String::fromUTF8 (u8"→"), width - 320, 0, 28, height, juce::Justification::centredRight);
        }

        double remainingSec = 0.0;
        double elapsedSec   = 0.0;

        if (pad != nullptr && pad->hasAudioFile())
        {
            if (isActivePlay)
            {
                elapsedSec   = pad->getElapsedSeconds();
                remainingSec = pad->getRemainingSeconds();
            }
            else if (isPaused)
            {
                elapsedSec   = pad->getElapsedSeconds();
                remainingSec = 0.0;
            }
            else
            {
                elapsedSec   = 0.0;
                remainingSec = 0.0;
            }
        }

        g.setColour (pal.textSecondary);
        g.setFont (ShowTheme::timerFont (12.5f, true));
        const auto remainingRect = showcontrol::bgmList::timeRemainingBounds (width, height);
        g.drawText (formatCueTimeString (remainingSec), remainingRect, juce::Justification::centred);

        g.setColour (pal.textMuted);
        g.setFont (ShowTheme::timerFont (12.5f));
        const auto elapsedRect = showcontrol::bgmList::totalDurationBounds (width, height);
        g.drawText (formatCueTimeString (elapsedSec), elapsedRect, juce::Justification::centred);

        g.setColour (pal.borderSubtle);
        g.drawHorizontalLine (height - 1, 0.0f, (float) width);
    }

    void listBoxItemClicked (int row, const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu())
        {
            owner.listBox.selectRow (row, false, true);
            owner.selectedIndex = row;
            owner.fireSelectionFromListBox();

            if (owner.onCueRightClick)
                owner.onCueRightClick (row);

            owner.showTrackContextMenu (row);
            return;
        }

        // ListBox đã gọi selectRowsBasedOnModifierKeys — đồng bộ state + callback BGM-style.
        owner.applySelectionForRowClick (row, e.mods);

        if (e.getNumberOfClicks() == 2 && owner.onCueTriggered)
            owner.onCueTriggered (row);
    }

    void backgroundClicked (const juce::MouseEvent& e) override
    {
        const auto local = owner.listBox.getLocalPoint (e.eventComponent, e.getPosition());
        owner.beginCueMarquee (local, e.mods);
    }

    void selectedRowsChanged (int lastRowSelected) override
    {
        juce::ignoreUnused (lastRowSelected);
        owner.fireSelectionFromListBox();
    }

    juce::var getDragSourceDescription (const juce::SparseSet<int>&) override
    {
        // Reorder nội bộ qua CueListBox::mouseDrag (mirror SoundPad) — không dùng native ListBox DnD.
        return juce::var();
    }

    bool mayDragToExternalWindows() const override { return false; }

private:
    CueListPanel& owner;
};

//==============================================================================
CueListPanel::CueListPanel()
    : listBox ("CueListBox", *this)
{
    setWantsKeyboardFocus (true);

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

    updateTheme (true);
}

CueListPanel::~CueListPanel()
{
    listBox.removeMouseListener (this);
    stopTimer();
    listBox.setModel (nullptr);
    reorderOverlay.reset();
}

void CueListPanel::lookAndFeelChanged()
{
    juce::Component::lookAndFeelChanged();

    if (auto* showLaf = dynamic_cast<ShowControlLookAndFeel*> (&getLookAndFeel()))
        isDarkMode = showLaf->isDarkMode();

    repaint();
}

void CueListPanel::updateTheme (bool isDark)
{
    isDarkMode = isDark;
    listBox.repaint();
    repaint();
}

void CueListPanel::refreshListBoxData()
{
    listBox.updateContent();
    listBox.repaint();
    repaint();
}

void CueListPanel::setCues (const juce::Array<CueItem>& newCues)
{
    cues = newCues;
    refreshListBoxData();
    syncLiveTimer();
}

void CueListPanel::addCue (const CueItem& item)
{
    cues.add (item);
    listBox.updateContent();
    listBox.repaint();
}

void CueListPanel::removeCue (int index)
{
    if (index >= 0 && index < cues.size())
    {
        cues.remove (index);
        if (selectedIndex >= cues.size())
            selectedIndex = cues.size() - 1;

        listBox.updateContent();
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
        playingIndex = idx;
        listBox.repaint();
    }

    syncLiveTimer();
}

void CueListPanel::setArmedIndex (int idx)
{
    if (armedIndex != idx)
    {
        armedIndex = idx;
        listBox.repaint();
    }
}

const CueItem* CueListPanel::getCue (int index) const
{
    return (index >= 0 && index < cues.size()) ? &cues.getReference (index) : nullptr;
}

void CueListPanel::setPadAccessor (std::function<SoundPad* (int index)> accessor)
{
    padAccessor = std::move (accessor);
    listBox.repaint();
}

void CueListPanel::notifyPlaybackActivity()
{
    syncLiveTimer();
    listBox.repaint();
}

bool CueListPanel::handleTransportKey (const juce::KeyPress& key)
{
    if (! isVisible())
        return false;

    const int keyCode = key.getKeyCode();

    if (keyCode == juce::KeyPress::spaceKey || keyCode == 32)
    {
        if (selectedIndex >= 0 && onCueListPlay)
            onCueListPlay (selectedIndex);

        return true;
    }

    const auto ch = key.getTextCharacter();

    if (ch == 'p' || ch == 'P')
    {
        if (selectedIndex >= 0 && onCueListPause)
            onCueListPause (selectedIndex);

        return true;
    }

    if (ch == 's' || ch == 'S')
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

    return cueListHasLoadedContent (cues) ? (kHeaderH + cues.size() * kRowH) : kRowH;
}

void CueListPanel::paint (juce::Graphics& g)
{
    const auto cols = showcontrol::ui::ThemePaintColours::read (*this);
    isDarkMode = cols.isDark;
    g.fillAll (cols.windowBg);

    if (! cueListHasLoadedContent (cues))
    {
        g.setColour (cols.textMuted);
        g.setFont (ShowTheme::font (15.0f));
        g.drawFittedText (juce::String::fromUTF8 (u8"Danh sách CUE trống. Hãy kéo thả file âm thanh vào đây để tự động cấu hình các ô PAD biểu diễn."),
                          getLocalBounds().reduced (32),
                          juce::Justification::centred,
                          4);
        return;
    }

    paintHeader (g, { 0, 0, getWidth(), kHeaderH });
}

void CueListPanel::resized()
{
    auto area = getLocalBounds();

    if (cueListHasLoadedContent (cues))
        area.removeFromTop (kHeaderH);

    listBox.setBounds (area);
    updateCueReorderOverlayBounds();
}

void CueListPanel::updateCueReorderOverlayBounds()
{
    if (reorderOverlay == nullptr)
        return;

    reorderOverlay->setBounds (getLocalBounds());
    reorderOverlay->toFront (false);
}

juce::Rectangle<int> CueListPanel::getCueListInsertLineBounds() const
{
    const int n = cues.size();
    const int target = juce::jlimit (0, n, cueRowReorderInsertIndex);
    const int width = getWidth();

    if (target == n)
    {
        if (n > 0)
        {
            const auto rowPos = listBox.getRowPosition (n - 1, true);
            const auto listBounds = listBox.getBounds();
            return { 0, listBounds.getY() + rowPos.getBottom(), width, 0 };
        }

        return {};
    }

    const auto rowPos = listBox.getRowPosition (target, true);
    const auto listBounds = listBox.getBounds();
    return { 0, listBounds.getY() + rowPos.getY(), width, 0 };
}

void CueListPanel::paintCueReorderOverlay (juce::Graphics& g) const
{
    paintMarquee (g);

    if (! cueRowReorderActive || cueRowReorderInsertIndex < 0)
        return;

    paintReorderInsertLine (g);
    paintCueRowReorderGhost (g);
}

bool CueListPanel::keyPressed (const juce::KeyPress& key)
{
    if (handleTransportKey (key))
        return true;

    const int keyCode = key.getKeyCode();

    if (keyCode == juce::KeyPress::deleteKey || keyCode == juce::KeyPress::backspaceKey)
    {
        if (onDeleteKeyPressed)
        {
            onDeleteKeyPressed();
            return true;
        }

        return false;
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
    if (anyRowTransportActive())
        listBox.repaint();
    else
        stopTimer();
}

void CueListPanel::syncLiveTimer()
{
    if (anyRowTransportActive())
        startTimerHz (20);
    else
        stopTimer();
}

bool CueListPanel::anyRowTransportActive() const
{
    if (! padAccessor)
        return false;

    for (int i = 0; i < cues.size(); ++i)
        if (auto* pad = padAccessor (i); pad != nullptr && pad->isTransportActive())
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

    g.setColour (pal.textSecondary);
    g.setFont (ShowTheme::fontBold (13.0f));

    const auto titleRect     = showcontrol::bgmList::titleBounds (bounds.getWidth(), bounds.getHeight())
                                   .translated (bounds.getX(), bounds.getY());
    const auto remainingRect = showcontrol::bgmList::timeRemainingBounds (bounds.getWidth(), bounds.getHeight())
                                   .translated (bounds.getX(), bounds.getY());
    const auto elapsedRect   = showcontrol::bgmList::totalDurationBounds (bounds.getWidth(), bounds.getHeight())
                                   .translated (bounds.getX(), bounds.getY());

    g.drawText (juce::String::fromUTF8 (u8"TÊN CUE KỊCH BẢN"), titleRect, juce::Justification::centredLeft);
    g.drawText (juce::String::fromUTF8 (u8"CÒN LẠI"), remainingRect, juce::Justification::centred);
    g.drawText (juce::String::fromUTF8 (u8"ĐÃ CHẠY"), elapsedRect, juce::Justification::centred);
}

void CueListPanel::showTrackContextMenu (int cueIndex)
{
    juce::PopupMenu menu;
    menu.addItem ((int) TrackMenuId::replaceFile, juce::String::fromUTF8 (u8"Thay đổi file nhạc..."));
    menu.addItem ((int) TrackMenuId::duplicate,   juce::String::fromUTF8 (u8"Nhân bản"));
    menu.addItem ((int) TrackMenuId::trimEditor,  juce::String::fromUTF8 (u8"Chỉnh sửa (Trim Editor)..."));
    menu.addItem ((int) TrackMenuId::revealFile,  juce::String::fromUTF8 (u8"Mở vị trí tệp..."));
    menu.addSeparator();
    menu.addItem ((int) TrackMenuId::resetFade,   juce::String::fromUTF8 (u8"Reset Fade về mặc định (0 ms)"));
    menu.addItem ((int) TrackMenuId::deleteItem,  juce::String::fromUTF8 (u8"Xóa"));

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&listBox).withMousePosition(),
                        [this, cueIndex] (int result)
                        {
                            if (onTrackMenuResult)
                                onTrackMenuResult (cueIndex, result);
                        });
}

void CueListPanel::applySelectionForRowClick (int clickedIndex, const juce::ModifierKeys& mods)
{
    juce::ignoreUnused (mods);

    // JUCE ListBox đã xử lý Cmd/Shift + giữ multi-select khi click dòng đã chọn (chuẩn BGM drag-block).
    const int last = listBox.getLastRowSelected();
    selectedIndex = last >= 0 ? last : clickedIndex;
    fireSelectionFromListBox();
}

void CueListPanel::applyListBoxSelectedRows (const juce::SparseSet<int>& newRows)
{
    listBox.setSelectedRows (newRows, juce::NotificationType::dontSendNotification);
    listBox.repaint();
}

juce::Array<int> CueListPanel::collectSelectedRowIndices() const
{
    juce::Array<int> selected;

    for (int i = 0; i < listBox.getNumSelectedRows(); ++i)
        selected.add (listBox.getSelectedRow (i));

    selected.sort();
    return selected;
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

    listBox.updateContent();
    listBox.repaint();
    repaint();

    fireSelectionFromListBox();
}

void CueListPanel::paintReorderInsertLine (juce::Graphics& g) const
{
    const auto line = getCueListInsertLineBounds();

    if (line.getWidth() <= 0)
        return;

    const auto& pal = ShowTheme::get (isDarkMode);
    const int lineY = line.getY();

    // Sao chép paintPadReorderOverlay — nhánh BGM list mode (MainComponent.cpp).
    g.setColour (pal.accent.withAlpha (0.55f));
    g.fillEllipse (18.0f, (float) lineY - 4.0f, 8.0f, 8.0f);
    g.fillEllipse ((float) getWidth() - 26.0f, (float) lineY - 4.0f, 8.0f, 8.0f);
    g.setColour (pal.accent);
    g.fillRect (22, lineY - 1, getWidth() - 44, 2);
}

void CueListPanel::paintCueRowReorderGhost (juce::Graphics& g) const
{
    if (! cueRowReorderActive)
        return;

    const auto& pal = ShowTheme::get (isDarkMode);
    const auto selected = collectSelectedRowIndices();
    const int draggedCount = juce::jmax (1, selected.size());
    const int anchorRow = dragSourceAnchorRow >= 0 ? dragSourceAnchorRow
                                                     : (selected.isEmpty() ? -1 : selected.getFirst());

    juce::String title;
    if (juce::isPositiveAndBelow (anchorRow, cues.size()))
        title = cues.getReference (anchorRow).name;

    if (title.isEmpty())
        title = juce::String::fromUTF8 (u8"Di chuyển CUE...");

    const float animDurationMs = 120.0f;
    const float elapsedMs = (float) (juce::Time::getMillisecondCounter() - cueRowReorderStackAnimStartMs);
    const float animT = juce::jlimit (0.0f, 1.0f, elapsedMs / animDurationMs);
    const float easeOut = 1.0f - std::pow (1.0f - animT, 3.0f);
    const bool draggingGroup = draggedCount > 1;
    const bool stackIntroActive = draggingGroup && cueRowReorderStackAnimActive && animT < 1.0f;

    const auto titleFont = ShowTheme::fontBold (12.0f);
    g.setFont (titleFont);
    juce::GlyphArrangement glyphs;
    glyphs.addLineOfText (titleFont, title, 0.0f, 0.0f);
    const float textW = glyphs.getBoundingBox (0, -1, true).getWidth();
    const float pillW = juce::jmin (420.0f, textW + 28.0f);
    const float pillH = 32.0f;

    const int pointerX = cueRowReorderPointerPos.x;
    const int pointerY = cueRowReorderPointerPos.y;

    juce::Rectangle<float> ghost ((float) pointerX - pillW * 0.5f,
                                  (float) pointerY - pillH * 0.5f,
                                  pillW, pillH);

    if (stackIntroActive && juce::isPositiveAndBelow (anchorRow, cues.size()))
    {
        const auto rowPos = listBox.getRowPosition (anchorRow, true);
        const auto listBounds = listBox.getBounds();
        ghost.setCentre ((float) listBounds.getCentreX(),
                         (float) (listBounds.getY() + rowPos.getCentreY()));
    }

    if (stackIntroActive)
        ghost = ghost.withSizeKeepingCentre (ghost.getWidth() * (1.0f + 0.04f * std::sin (easeOut * juce::MathConstants<float>::pi)),
                                               ghost.getHeight() * (1.0f + 0.04f * std::sin (easeOut * juce::MathConstants<float>::pi)));

    if (draggingGroup)
    {
        const int layers = juce::jmin (4, draggedCount - 1);

        for (int i = layers; i >= 1; --i)
        {
            const float introMul = stackIntroActive ? easeOut : 1.0f;
            auto layer = ghost.translated ((float) i * 12.0f * introMul, (float) i * 7.0f * introMul)
                              .withSizeKeepingCentre (ghost.getWidth() * (1.0f - 0.04f * (float) i),
                                                      ghost.getHeight() * (1.0f - 0.03f * (float) i));
            g.setColour (pal.dragGhostFill.withAlpha (0.42f - (float) (i - 1) * 0.08f));
            g.fillRoundedRectangle (layer, pillH * 0.5f);
            g.setColour (pal.accent.withAlpha (0.42f - (float) (i - 1) * 0.08f));
            g.drawRoundedRectangle (layer, pillH * 0.5f, 1.5f);
        }
    }

    g.setColour (juce::Colours::black.withAlpha (0.28f));
    g.fillRoundedRectangle (ghost.translated (0.0f, 2.0f), pillH * 0.5f);

    g.setColour (pal.dragGhostFill);
    g.fillRoundedRectangle (ghost, pillH * 0.5f);

    g.setColour (pal.dragGhostText);
    g.drawText (title, ghost.reduced (12.0f, 4.0f), juce::Justification::centredLeft, true);

    g.setColour (pal.accent.withAlpha (0.75f));
    g.drawRoundedRectangle (ghost, pillH * 0.5f, 1.5f);

    if (draggingGroup)
    {
        const juce::String countText = "x" + juce::String (draggedCount);
        juce::Rectangle<float> badge (ghost.getRight() - 34.0f, ghost.getY() - 12.0f, 30.0f, 18.0f);
        g.setColour (pal.accent);
        g.fillRoundedRectangle (badge, 8.0f);
        g.setColour (juce::Colours::white);
        g.setFont (ShowTheme::fontBold (10.5f));
        g.drawText (countText, badge, juce::Justification::centred);
    }
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

    listBox.repaint();
}

void CueListPanel::updateCueRowReorder (const juce::MouseEvent& e)
{
    if (! cueRowReorderActive)
        return;

    const auto local = listBox.getLocalPoint (e.eventComponent, e.getPosition());
    cueRowReorderPointerPos = getLocalPoint (e.eventComponent, e.getPosition());
    cueRowReorderInsertIndex = listBox.getInsertionIndexForPosition (local.x, local.y);

    autoScrollListBoxForReorder (local);
    listBox.repaint();

    if (reorderOverlay != nullptr)
        reorderOverlay->repaint();
}

void CueListPanel::endCueRowReorder()
{
    if (! cueRowReorderActive)
        return;

    const int insertRowPosition = cueRowReorderInsertIndex;
    const int anchorRow = dragSourceAnchorRow;
    const auto rowsToMove = dragSourceRows;

    cancelCueRowReorder();
    listBoxItemsDropped (anchorRow, rowsToMove, insertRowPosition);
}

void CueListPanel::cancelCueRowReorder()
{
    const bool wasActive = cueRowReorderActive;

    cueRowReorderActive = false;
    cueRowReorderPressLocked = false;
    cueRowReorderInsertIndex = -1;
    cueRowReorderStackAnimActive = false;
    cueRowReorderStackAnimStartMs = 0;

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

        listBox.repaint();
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

    const auto rel = e.getEventRelativeTo (&listBox);
    const int startRow = listBox.hitRowIndexAt (rel.getMouseDownX(), rel.getMouseDownY());

    if (startRow < 0 || startRow >= cues.size())
        return false;

    if (listBox.getSelectedRows().contains (startRow))
    {
        triggerCueDragSession (e);
        return true;
    }

    // Kịch bản B — kéo dòng chưa chọn: ép chọn ngay rồi bốc nhả tức thì.
    listBox.selectRow (startRow, false, true);
    selectedIndex = startRow;
    fireSelectionFromListBox();
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
    const int row = listBox.hitRowIndexAt (local.x, local.y);

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
    const auto selected = collectSelectedRowIndices();

    if (! selected.isEmpty())
        selectedIndex = selected.getLast();

    if (onCueSelectionChanged)
        onCueSelectionChanged (selected);

    if (onCueSelected && selectedIndex >= 0)
        onCueSelected (selectedIndex);

    listBox.repaint();
}
