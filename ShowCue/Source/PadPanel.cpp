#include "PadPanel.h"
#include "MainComponent.h"
#include "ShowCrossComponentDrag.h"
#include "ShowKeyboardInput.h"
#include "HotkeyManager.h"
#include <cmath>
#include <limits>

namespace
{
constexpr int kMatrixInsetPx = 3;
constexpr int kBorderProximityStickyPx = 40;

void drawDashedCellOutline (juce::Graphics& g, juce::Rectangle<float> cellBounds)
{
    const float dashPattern[] = { 4.0f, 4.0f };
    const float x = cellBounds.getX();
    const float y = cellBounds.getY();
    const float r = cellBounds.getRight();
    const float b = cellBounds.getBottom();

    g.drawDashedLine (juce::Line<float> (x, y, r, y), dashPattern, 2, 1.0f);
    g.drawDashedLine (juce::Line<float> (r, y, r, b), dashPattern, 2, 1.0f);
    g.drawDashedLine (juce::Line<float> (r, b, x, b), dashPattern, 2, 1.0f);
    g.drawDashedLine (juce::Line<float> (x, b, x, y), dashPattern, 2, 1.0f);
}
} // namespace

juce::Colour PadPanel::resolveActiveDragColour (const SoundPad* pad) const noexcept
{
    const auto pal = ShowTheme::get (isDarkMode);

    if (pad == nullptr)
        return pal.accent;

    if (! showcontrol::colours::isDefaultTagColour (pad->getTagColour()))
        return pad->getPadThemeColour();

    return pal.accent;
}

bool PadPanel::isCellTreatAsEmptyForDragGrid (int row, int col) const noexcept
{
    auto* occupant = getPadAtGrid (row, col);

    if (occupant == nullptr)
        return true;

    return dragSourcePad != nullptr
           && occupant == dragSourcePad
           && dragSourcePad->isCurrentlyDraggedState();
}

void PadPanel::resetDragGridToStaticBase() noexcept
{
    rebuildStaticBaseGridFromPads();
}

void PadPanel::rebuildStaticBaseGridFromPads() noexcept
{
    int staticMaxRow = 0;
    int staticMaxCol = 0;
    bool hasAnyAudio = false;

    if (pads != nullptr)
    {
        for (auto* pad : *pads)
        {
            if (pad != nullptr && pad->hasAudioFile())
            {
                staticMaxRow = juce::jmax (staticMaxRow, pad->getGridRow());
                staticMaxCol = juce::jmax (staticMaxCol, pad->getGridCol());
                hasAnyAudio = true;
            }
        }
    }

    baseRows = hasAnyAudio
                   ? juce::jmax (showcontrol::padgrid::kMinBoundedRows, staticMaxRow + 1)
                   : showcontrol::padgrid::kMinBoundedRows;
    baseCols = hasAnyAudio
                   ? juce::jmax (showcontrol::padgrid::kMinBoundedCols, staticMaxCol + 1)
                   : showcontrol::padgrid::kMinBoundedCols;

    baseRows = juce::jlimit (showcontrol::padgrid::kMinBoundedRows,
                             showcontrol::padgrid::kRows,
                             baseRows);
    baseCols = juce::jlimit (showcontrol::padgrid::kMinBoundedCols,
                             showcontrol::padgrid::kCols,
                             baseCols);

    dragActiveRows = baseRows;
    dragActiveCols = baseCols;
}

void PadPanel::ensureLiquidTimerRunning() noexcept
{
    if (! isTimerRunning())
        startTimerHz (60);
}

juce::Rectangle<int> PadPanel::cellBoundsForActiveGrid (int row, int col) const noexcept
{
    const int cellW = boundedLayout.cellW;
    const int cellH = boundedLayout.cellH;

    return { col * cellW + kMatrixInsetPx,
             row * cellH + kMatrixInsetPx,
             juce::jmax (1, cellW - kMatrixInsetPx * 2),
             juce::jmax (1, cellH - kMatrixInsetPx * 2) };
}

juce::Point<int> PadPanel::activeGridCellAtLocalPoint (juce::Point<int> local) const noexcept
{
    const int cols = juce::jmax (1, dragActiveCols);
    const int rows = juce::jmax (1, dragActiveRows);
    const int currentCellW = juce::jmax (1, getWidth() / cols);
    const int currentCellH = juce::jmax (1, getHeight() / rows);

    return { juce::jlimit (0, showcontrol::padgrid::kCols - 1, local.x / currentCellW),
             juce::jlimit (0, showcontrol::padgrid::kRows - 1, local.y / currentCellH) };
}

SoundPad* PadPanel::resolveDragSourcePad (const SourceDetails& dragSourceDetails) const noexcept
{
    if (auto* sourcePad = dynamic_cast<SoundPad*> (dragSourceDetails.sourceComponent.get()))
        return sourcePad;

    if (pads == nullptr)
        return nullptr;

    juce::Array<int> padIndices;
    int anchorIndex = -1;

    if (! showcontrol::crossdrag::decodeLocalPadMoveDrag (dragSourceDetails.description,
                                                            padIndices,
                                                            anchorIndex))
        return nullptr;

    if (juce::isPositiveAndBelow (anchorIndex, pads->size()))
    {
        if (auto* anchored = (*pads)[anchorIndex])
            return anchored;
    }

    for (const auto padIndex : padIndices)
    {
        if (! juce::isPositiveAndBelow (padIndex, pads->size()))
            continue;

        if (auto* pad = (*pads)[padIndex])
            return pad;
    }

    return nullptr;
}

void PadPanel::setPadList (juce::OwnedArray<SoundPad>* padsIn) noexcept
{
    isInitialLayout = true;
    pads = padsIn;
}

void PadPanel::visibilityChanged()
{
    if (isVisible())
    {
        isInitialLayout = true;
        isDraggingActive = false;
        hoveredRow = -1;
        hoveredCol = -1;

        resyncAndLayout();
        repaint();
    }
}

void PadPanel::beginInternalGridDragSession (SoundPad* sourcePad) noexcept
{
    isDraggingActive = true;
    isInitialLayout = false;
    dragSourcePad = sourcePad;

    if (sourcePad != nullptr)
        activeDragColour = resolveActiveDragColour (sourcePad);

    setPadChildrenMousePassthrough (true);
    rebuildStaticBaseGridFromPads();
    resyncAndLayout();
    ensureLiquidTimerRunning();
    repaint();
}

void PadPanel::applyGridDragHoverAtLocalPoint (juce::Point<int> localPosition,
                                                SoundPad* sourcePad) noexcept
{
    if (! matrixLayoutEnabled)
        return;

    if (sourcePad != nullptr)
    {
        dragSourcePad = sourcePad;
        activeDragColour = resolveActiveDragColour (sourcePad);
    }

    const int currentCellW = juce::jmax (1, boundedLayout.cellW > 0
                                             ? boundedLayout.cellW
                                             : getWidth() / juce::jmax (1, dragActiveCols));
    const int currentCellH = juce::jmax (1, boundedLayout.cellH > 0
                                             ? boundedLayout.cellH
                                             : getHeight() / juce::jmax (1, dragActiveRows));

    const int newHoverCol = localPosition.x / currentCellW;
    const int newHoverRow = localPosition.y / currentCellH;

    hoveredCol = juce::jlimit (0, dragActiveCols - 1, newHoverCol);
    hoveredRow = juce::jlimit (0, dragActiveRows - 1, newHoverRow);
    isDraggingActive = true;

    const int panelW = getWidth();
    const int panelH = getHeight();

    if (panelW > 0 && localPosition.x >= (panelW - kBorderProximityStickyPx))
    {
        dragActiveCols = juce::jlimit (showcontrol::padgrid::kMinBoundedCols,
                                       showcontrol::padgrid::kCols,
                                       dragActiveCols + 1);
    }

    if (panelH > 0 && localPosition.y >= (panelH - kBorderProximityStickyPx))
    {
        dragActiveRows = juce::jlimit (showcontrol::padgrid::kMinBoundedRows,
                                       showcontrol::padgrid::kRows,
                                       dragActiveRows + 1);
    }

    if (dragSourcePad != nullptr)
        activeDragColour = resolveActiveDragColour (dragSourcePad);

    isInitialLayout = false;
    resyncAndLayout();
    ensureLiquidTimerRunning();
    repaint();
}

void PadPanel::setDraggingActive (bool active)
{
    if (isDraggingActive == active)
        return;

    isDraggingActive = active;

    if (active)
        resetDragGridToStaticBase();
    else
    {
        clearDragPreview();
        setPadChildrenMousePassthrough (false);
    }

    resyncAndLayout();
    repaint();
}

void PadPanel::setDarkMode (bool dark)
{
    isDarkMode = dark;
    repaint();
}

void PadPanel::setEmptyListHint (EmptyListHint hint)
{
    if (emptyListHint != hint)
    {
        emptyListHint = hint;
        repaint();
    }
}

SoundPad* PadPanel::getPadAtGrid (int row, int col) const noexcept
{
    if (pads == nullptr || ! showcontrol::padgrid::isValidGridCell (row, col))
        return nullptr;

    for (auto* pad : *pads)
    {
        if (pad == nullptr)
            continue;

        if (pad->getGridRow() == row && pad->getGridCol() == col && pad->occupiesCueGridSlot())
            return pad;
    }

    return nullptr;
}

juce::Point<int> PadPanel::gridCellAtPoint (juce::Point<int> local) const noexcept
{
    return showcontrol::padgrid::boundedGridCellAtLocalPoint (local,
                                                              boundedLayout.cellW,
                                                              boundedLayout.cellH,
                                                              boundedLayout.activeCols,
                                                              boundedLayout.activeRows);
}

juce::Point<int> PadPanel::resolveDropGridCell (juce::Point<int> localFallback) const noexcept
{
    if (hoveredRow >= 0 && hoveredCol >= 0)
        return { hoveredCol, hoveredRow };

    if (isDraggingActive)
    {
        const auto hoverCell = activeGridCellAtLocalPoint (localFallback);

        if (hoverCell.x >= 0 && hoverCell.y >= 0)
            return hoverCell;
    }

    return gridCellAtPoint (localFallback);
}

juce::Point<int> PadPanel::localPointFromDragDetails (const SourceDetails& dragSourceDetails) const noexcept
{
    return showcontrol::crossdrag::dragTargetLocalPoint (*this, dragSourceDetails, true);
}

bool PadPanel::isInterestedInDragSource (const SourceDetails& dragSourceDetails)
{
    if (! matrixLayoutEnabled)
        return false;

    return dragSourceDetails.description.toString()
               .startsWith (showcontrol::crossdrag::kLocalPadMovePrefix);
}

void PadPanel::itemDragEnter (const SourceDetails& dragSourceDetails)
{
    if (! matrixLayoutEnabled)
        return;

    if (! showcontrol::crossdrag::isLocalPadMoveDrag (dragSourceDetails.description))
        return;

    captureDragSource (dragSourceDetails);
    isDraggingActive = true;
    isInitialLayout = false;
    setPadChildrenMousePassthrough (true);
    rebuildStaticBaseGridFromPads();
    resyncAndLayout();
    itemDragMove (dragSourceDetails);
    repaint();
}

void PadPanel::itemDragMove (const SourceDetails& dragSourceDetails)
{
    if (! matrixLayoutEnabled)
        return;

    if (dragSourcePad == nullptr)
        captureDragSource (dragSourceDetails);

    auto* sourcePad = dynamic_cast<SoundPad*> (dragSourceDetails.sourceComponent.get());
    applyGridDragHoverAtLocalPoint (localPointFromDragDetails (dragSourceDetails), sourcePad);
}

void PadPanel::itemDragExit (const SourceDetails& dragSourceDetails)
{
    juce::ignoreUnused (dragSourceDetails);
    hoveredRow = -1;
    hoveredCol = -1;
    repaint();
}

void PadPanel::finalizeLocalDropAndCollapseGrid (MainComponent* mainComp,
                                                   const SourceDetails& dragSourceDetails) noexcept
{
    juce::ignoreUnused (dragSourceDetails);

    setPadChildrenMousePassthrough (false);
    dragSourcePad = nullptr;
    activeDragColour = juce::Colours::transparentBlack;

    hoveredRow = -1;
    hoveredCol = -1;
    isDraggingActive = false;

    rebuildStaticBaseGridFromPads();
    resyncAndLayout();

    if (mainComp != nullptr)
        mainComp->triggerSave();

    repaint();
}

void PadPanel::itemDropped (const SourceDetails& dragSourceDetails)
{
    auto* mainComp = findParentComponentOfClass<MainComponent>();

    if (! showcontrol::crossdrag::isLocalPadMoveDrag (dragSourceDetails.description))
    {
        restoreSourcePadDragVisual (dragSourceDetails);
        finalizeLocalDropAndCollapseGrid (mainComp, dragSourceDetails);
        return;
    }

    if (auto* sourcePad = resolveDragSourcePad (dragSourceDetails))
    {
        if (mainComp != nullptr)
            mainComp->consumeInternalJucePadDrop();

        const auto dropCell = resolveDropGridCell (localPointFromDragDetails (dragSourceDetails));
        const int targetCol = dropCell.x;
        const int targetRow = dropCell.y;

        if (showcontrol::padgrid::isValidGridCell (targetRow, targetCol))
        {
            juce::Array<int> multiIndices;
            int anchorIndex = -1;

            if (mainComp != nullptr
                && showcontrol::crossdrag::decodeLocalPadMoveDrag (dragSourceDetails.description,
                                                                   multiIndices,
                                                                   anchorIndex)
                && multiIndices.size() > 1)
            {
                mainComp->movePadsToGridCell (mainComp->getActiveListIndex(),
                                              multiIndices,
                                              anchorIndex,
                                              targetRow,
                                              targetCol);
            }
            else
            {
                if (mainComp != nullptr)
                {
                    mainComp->applyPadGridDropAt (mainComp->getActiveListIndex(),
                                                  sourcePad,
                                                  targetRow,
                                                  targetCol);
                }
                else
                {
                    auto* existingPadAtTarget = getPadAtGrid (targetRow, targetCol);

                    if (existingPadAtTarget == nullptr)
                    {
                        sourcePad->setGridPosition (targetRow, targetCol, true);
                    }
                    else if (existingPadAtTarget != sourcePad)
                    {
                        const int oldRow = sourcePad->getGridRow();
                        const int oldCol = sourcePad->getGridCol();

                        sourcePad->setGridPosition (targetRow, targetCol, true);
                        existingPadAtTarget->setGridPosition (oldRow, oldCol, true);
                        existingPadAtTarget->refreshHotkeyLabel();
                    }

                    sourcePad->refreshHotkeyLabel();
                }
            }
        }
    }

    restoreSourcePadDragVisual (dragSourceDetails);
    finalizeLocalDropAndCollapseGrid (mainComp, dragSourceDetails);
}

void PadPanel::resyncAndLayout()
{
    if (! matrixLayoutEnabled || pads == nullptr)
        return;

    int activeRows = showcontrol::padgrid::kMinBoundedRows;
    int activeCols = showcontrol::padgrid::kMinBoundedCols;

    if (isDraggingActive)
    {
        activeRows = dragActiveRows;
        activeCols = dragActiveCols;
    }
    else
    {
        int staticMaxRow = 0;
        int staticMaxCol = 0;
        bool hasAnyAudio = false;

        for (auto* pad : *pads)
        {
            if (pad != nullptr && pad->hasAudioFile())
            {
                staticMaxRow = juce::jmax (staticMaxRow, pad->getGridRow());
                staticMaxCol = juce::jmax (staticMaxCol, pad->getGridCol());
                hasAnyAudio = true;
            }
        }

        activeRows = hasAnyAudio
                         ? juce::jmax (showcontrol::padgrid::kMinBoundedRows, staticMaxRow + 1)
                         : showcontrol::padgrid::kMinBoundedRows;
        activeCols = hasAnyAudio
                         ? juce::jmax (showcontrol::padgrid::kMinBoundedCols, staticMaxCol + 1)
                         : showcontrol::padgrid::kMinBoundedCols;

        baseRows = activeRows;
        baseCols = activeCols;
        dragActiveRows = baseRows;
        dragActiveCols = baseCols;
    }

    activeRows = juce::jlimit (showcontrol::padgrid::kMinBoundedRows,
                               showcontrol::padgrid::kRows,
                               activeRows);
    activeCols = juce::jlimit (showcontrol::padgrid::kMinBoundedCols,
                               showcontrol::padgrid::kCols,
                               activeCols);

    lastActiveRows = activeRows;
    lastActiveCols = activeCols;

    const int cellW = juce::jmax (1, getWidth() / activeCols);
    const int cellH = juce::jmax (1, getHeight() / activeRows);

    boundedLayout.activeRows = activeRows;
    boundedLayout.activeCols = activeCols;
    boundedLayout.cellW      = cellW;
    boundedLayout.cellH      = cellH;

    if (isInitialLayout && ! isDraggingActive)
        stopTimer();

    int visiblePadCount = 0;

    for (auto* pad : *pads)
    {
        if (pad == nullptr)
            continue;

        const int r = pad->getGridRow();
        const int c = pad->getGridCol();

        if (! pad->hasAudioFile() || ! pad->occupiesCueGridSlot()
            || r < 0 || c < 0 || r >= activeRows || c >= activeCols
            || ! showcontrol::padgrid::isValidGridCell (r, c))
        {
            pad->setVisible (false);
            continue;
        }

        if (visiblePadCount >= showcontrol::padgrid::kMaxPadsAllowed)
        {
            pad->setVisible (false);
            continue;
        }

        ++visiblePadCount;
        pad->setVisible (true);

        const juce::Rectangle<int> calculatedBounds (c * cellW + kMatrixInsetPx,
                                                    r * cellH + kMatrixInsetPx,
                                                    juce::jmax (1, cellW - kMatrixInsetPx * 2),
                                                    juce::jmax (1, cellH - kMatrixInsetPx * 2));

        if (isInitialLayout && ! isDraggingActive)
        {
            pad->setBounds (calculatedBounds);
            targetBoundsMap[pad] = calculatedBounds;
        }
        else
        {
            targetBoundsMap[pad] = calculatedBounds;
        }
    }

    if (isInitialLayout && ! isDraggingActive)
        isInitialLayout = false;

    ensureLiquidTimerRunning();
}

void PadPanel::refreshPadGrid()
{
    if (! matrixLayoutEnabled || pads == nullptr)
        return;

    resyncAndLayout();
    repaint();
}

void PadPanel::timerCallback()
{
    if (pads == nullptr)
        return;

    bool allComponentsMatched = true;
    const float smoothingFactor = 0.28f;

    for (auto* pad : *pads)
    {
        if (pad == nullptr || ! pad->isVisible())
            continue;

        const auto targetIt = targetBoundsMap.find (pad);

        if (targetIt == targetBoundsMap.end())
            continue;

        const auto current = pad->getBounds();
        const auto target = targetIt->second;

        if (current == target)
            continue;

        int nextX = current.getX() + juce::roundToInt ((target.getX() - current.getX()) * smoothingFactor);
        int nextY = current.getY() + juce::roundToInt ((target.getY() - current.getY()) * smoothingFactor);
        int nextW = current.getWidth() + juce::roundToInt ((target.getWidth() - current.getWidth()) * smoothingFactor);
        int nextH = current.getHeight() + juce::roundToInt ((target.getHeight() - current.getHeight()) * smoothingFactor);

        if (std::abs (nextX - target.getX()) <= 1) nextX = target.getX();
        if (std::abs (nextY - target.getY()) <= 1) nextY = target.getY();
        if (std::abs (nextW - target.getWidth()) <= 1) nextW = target.getWidth();
        if (std::abs (nextH - target.getHeight()) <= 1) nextH = target.getHeight();

        pad->setBounds (nextX, nextY, nextW, nextH);
        allComponentsMatched = false;
    }

    if (isDraggingActive)
    {
        repaint();
        return;
    }

    if (allComponentsMatched)
        stopTimer();
}

void PadPanel::clearDragPreview() noexcept
{
    hoveredRow = -1;
    hoveredCol = -1;
    dragSourcePad = nullptr;
    activeDragColour = juce::Colours::transparentBlack;
}

void PadPanel::collapseDragGridToStaticFloor() noexcept
{
    isDraggingActive = false;
    hoveredRow = -1;
    hoveredCol = -1;
    rebuildStaticBaseGridFromPads();
}

void PadPanel::captureDragSource (const SourceDetails& dragSourceDetails) noexcept
{
    dragSourcePad = resolveDragSourcePad (dragSourceDetails);

    if (dragSourcePad != nullptr)
        activeDragColour = resolveActiveDragColour (dragSourcePad);
}

void PadPanel::restoreSourcePadDragVisual (const SourceDetails& dragSourceDetails) const
{
    if (auto* sourcePad = dynamic_cast<SoundPad*> (dragSourceDetails.sourceComponent.get()))
    {
        sourcePad->setIsCurrentlyDragged (false);
        sourcePad->resized();
        sourcePad->repaint();
    }
}

void PadPanel::paintDragLandingGrid (juce::Graphics& g) const
{
    if (! isDraggingActive)
        return;

    const int cellW = boundedLayout.cellW;
    const int cellH = boundedLayout.cellH;

    if (cellW <= 0 || cellH <= 0)
        return;

    const auto pal = ShowTheme::get (isDarkMode);

    for (int r = 0; r < lastActiveRows; ++r)
    {
        for (int c = 0; c < lastActiveCols; ++c)
        {
            const auto cellBounds = cellBoundsForActiveGrid (r, c);
            const auto cellFloat = cellBounds.toFloat();
            const bool isEmpty  = isCellTreatAsEmptyForDragGrid (r, c);

            if (isEmpty)
            {
                g.setColour (pal.textMuted.withAlpha (0.28f));
                drawDashedCellOutline (g, cellFloat);

                const auto hk = showcontrol::padgrid::hotkeyForCell (r, c);

                if (hk.label.isNotEmpty())
                {
                    g.setColour (pal.textMuted.withAlpha (0.38f));
                    g.setFont (ShowTheme::fontBold (juce::jmax (9.0f, (float) cellH * 0.18f)));
                    g.drawText (hk.label, cellBounds, juce::Justification::centred);
                }
            }
        }
    }
}

void PadPanel::paintHoveredCellNeonHighlight (juce::Graphics& g) const
{
    if (! isDraggingActive || hoveredRow < 0 || hoveredCol < 0)
        return;

    if (hoveredRow >= lastActiveRows || hoveredCol >= lastActiveCols)
        return;

    const auto pal = ShowTheme::get (isDarkMode);
    const auto dragColour = activeDragColour.isTransparent()
                                ? pal.accent
                                : activeDragColour;

    const auto cellBounds = cellBoundsForActiveGrid (hoveredRow, hoveredCol).toFloat().reduced (1.0f);

    showcontrol::crossdrag::paintNeonDropTargetGlow (g, cellBounds, dragColour);
    g.setColour (dragColour.withAlpha (0.95f));
    g.drawRoundedRectangle (cellBounds, 6.0f, 2.5f);
}

void PadPanel::paint (juce::Graphics& g)
{
    const auto pal = ShowTheme::get (isDarkMode);
    g.fillAll (pal.centerBg);

    if (emptyListHint != EmptyListHint::none)
    {
        g.setColour (pal.textMuted);
        g.setFont (showcontrol::ui::emptyHintFont());

        const juce::String message = (emptyListHint == EmptyListHint::cueGrid)
            ? showcontrol::localization::tr (u8"Danh sách CUE trống. Hãy kéo thả file âm thanh vào đây để tự động cấu hình các ô PAD biểu diễn.")
            : showcontrol::localization::tr (u8"Danh sách BGM trống. Hãy kéo thả file nhạc nền vào phân vùng này để thiết lập danh sách phát.");

        g.drawFittedText (message,
                          getLocalBounds().reduced (32),
                          juce::Justification::centred,
                          4);
        return;
    }

    paintDragLandingGrid (g);
}

void PadPanel::paintOverChildren (juce::Graphics& g)
{
    paintHoveredCellNeonHighlight (g);
}

void PadPanel::resized()
{
    resyncAndLayout();
    repaint();
}

void PadPanel::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isRightButtonDown() && onBackgroundRightClick)
        onBackgroundRightClick (e);
    else if (onBackgroundMouseDown)
        onBackgroundMouseDown (e);
}

void PadPanel::mouseDrag (const juce::MouseEvent& e)
{
    if (onBackgroundMouseDrag)
        onBackgroundMouseDrag (e);
}

void PadPanel::mouseUp (const juce::MouseEvent& e)
{
    if (onBackgroundMouseUp)
        onBackgroundMouseUp (e);
}

bool PadPanel::isSearchWindowFocused() const noexcept
{
    return showcontrol::keyboard::isTextEntryComponent (
        juce::Component::getCurrentlyFocusedComponent());
}

bool PadPanel::keyPressed (const juce::KeyPress& key)
{
    if (showcontrol::keyboard::isUndoRedoKeyPress (key))
    {
        if (onChainedKeyPressed != nullptr && onChainedKeyPressed (key))
            return true;
    }

    if (isSearchWindowFocused())
        return false;

    if (! matrixLayoutEnabled || pads == nullptr)
        return false;

    const int arrowCode = HotkeyManager::normalizeArrowKeyCode (
        showcontrol::keyboard::physicalKeyCode (key));

    const bool isArrowKey = (arrowCode == juce::KeyPress::upKey
                          || arrowCode == juce::KeyPress::downKey
                          || arrowCode == juce::KeyPress::leftKey
                          || arrowCode == juce::KeyPress::rightKey);

    if (! isArrowKey)
        return false;

    SoundPad* currentlyFocusedPad = nullptr;

    for (auto* pad : *pads)
    {
        if (pad != nullptr && pad->hasKeyboardFocus (true))
        {
            currentlyFocusedPad = pad;
            break;
        }
    }

    if (currentlyFocusedPad == nullptr)
    {
        for (auto* pad : *pads)
        {
            if (pad != nullptr && pad->isRowSelected())
            {
                currentlyFocusedPad = pad;
                break;
            }
        }
    }

    if (currentlyFocusedPad == nullptr)
    {
        for (auto* pad : *pads)
        {
            if (pad != nullptr && pad->occupiesCueGridSlot())
            {
                currentlyFocusedPad = pad;
                break;
            }
        }

        if (currentlyFocusedPad == nullptr)
            return false;

        currentlyFocusedPad->grabKeyboardFocus();

        if (onGridFocusPadChanged != nullptr)
            onGridFocusPadChanged (currentlyFocusedPad);

        currentlyFocusedPad->repaint();
    }

    if (currentlyFocusedPad != nullptr)
    {
        const int curRow = currentlyFocusedPad->getGridRow();
        const int curCol = currentlyFocusedPad->getGridCol();

        // Walk the grid cell-by-cell in the pressed direction (skip empty cells).
        if (arrowCode == juce::KeyPress::rightKey
            || arrowCode == juce::KeyPress::leftKey
            || arrowCode == juce::KeyPress::upKey
            || arrowCode == juce::KeyPress::downKey)
        {
            SoundPad* occupancy[showcontrol::padgrid::kRows][showcontrol::padgrid::kCols] = {};

            for (auto* pad : *pads)
            {
                if (pad == nullptr || ! pad->occupiesCueGridSlot())
                    continue;

                const int r = pad->getGridRow();
                const int c = pad->getGridCol();

                if (showcontrol::padgrid::isValidGridCell (r, c))
                    occupancy[r][c] = pad;
            }

            SoundPad* target = nullptr;

            if (arrowCode == juce::KeyPress::rightKey)
            {
                for (int c = curCol + 1; c < showcontrol::padgrid::kCols && target == nullptr; ++c)
                    target = occupancy[curRow][c];

                for (int r = curRow + 1; r < showcontrol::padgrid::kRows && target == nullptr; ++r)
                {
                    for (int c = 0; c < showcontrol::padgrid::kCols && target == nullptr; ++c)
                        target = occupancy[r][c];
                }
            }
            else if (arrowCode == juce::KeyPress::leftKey)
            {
                for (int c = curCol - 1; c >= 0 && target == nullptr; --c)
                    target = occupancy[curRow][c];

                for (int r = curRow - 1; r >= 0 && target == nullptr; --r)
                {
                    for (int c = showcontrol::padgrid::kCols - 1; c >= 0 && target == nullptr; --c)
                        target = occupancy[r][c];
                }
            }
            else if (arrowCode == juce::KeyPress::downKey)
            {
                for (int r = curRow + 1; r < showcontrol::padgrid::kRows && target == nullptr; ++r)
                    target = occupancy[r][curCol];

                for (int c = curCol + 1; c < showcontrol::padgrid::kCols && target == nullptr; ++c)
                {
                    for (int r = 0; r < showcontrol::padgrid::kRows && target == nullptr; ++r)
                        target = occupancy[r][c];
                }
            }
            else // upKey
            {
                for (int r = curRow - 1; r >= 0 && target == nullptr; --r)
                    target = occupancy[r][curCol];

                for (int c = curCol - 1; c >= 0 && target == nullptr; --c)
                {
                    for (int r = showcontrol::padgrid::kRows - 1; r >= 0 && target == nullptr; --r)
                        target = occupancy[r][c];
                }
            }

            if (target != nullptr && target != currentlyFocusedPad)
            {
                currentlyFocusedPad->repaint();
                target->grabKeyboardFocus();

                if (onGridFocusPadChanged != nullptr)
                    onGridFocusPadChanged (target);

                target->repaint();
            }

            return true;
        }
    }

    return false;
}

void PadPanel::setPadChildrenMousePassthrough (bool passthrough) noexcept
{
    if (pads == nullptr)
        return;

    for (auto* pad : *pads)
    {
        if (pad != nullptr)
            pad->setInterceptsMouseClicks (! passthrough, ! passthrough);
    }
}

bool PadPanel::hasAnyPadPlaying() const noexcept
{
    return getCurrentlyPlayingPadTrack() != nullptr;
}

SoundPad* PadPanel::getCurrentlyPlayingPadTrack() const noexcept
{
    if (pads == nullptr)
        return nullptr;

    for (auto* pad : *pads)
    {
        if (pad != nullptr && (pad->isPlaying() || pad->isPaused()))
            return pad;
    }

    return nullptr;
}
