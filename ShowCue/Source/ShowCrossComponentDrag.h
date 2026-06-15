#pragma once
#include "ShowTheme.h"
#include "ShowTagColors.h"
#include <juce_gui_basics/juce_gui_basics.h>

/** Giao thức kéo thả sao chép xuyên linh kiện — BGM / CUE list / PAD grid (Farrago). */
namespace showcontrol::crossdrag
{
inline constexpr auto kActionType         = "actionType";
inline constexpr auto kCrossComponentCopy = "CROSS_COMPONENT_COPY";
inline constexpr auto kLocalPadMovePrefix   = "LOCAL_PAD_MOVE:";
inline constexpr auto kLocalRowReorderPrefix = "LOCAL_ROW_REORDER:";
inline constexpr auto kGridReorder        = "GRID_REORDER";

inline constexpr auto kSourceType       = "sourceType";
inline constexpr auto kSourceListName   = "sourceListName";
inline constexpr auto kAudioFilePaths   = "audioFilePaths";
inline constexpr auto kTrackNames       = "trackNames";
inline constexpr auto kTagColourArgb    = "tagColourArgb";
inline constexpr auto kOutputGains      = "outputGains";
inline constexpr auto kLoopFlags        = "loopFlags";

inline constexpr auto kItemIds     = "itemIds";
inline constexpr auto kPadIndices    = "padIndices";
inline constexpr auto kAnchorIndex = "anchorIndex";

inline constexpr auto kSourceSidebarListView = "SIDEBAR_LIST_VIEW";
inline constexpr auto kSourceSidebarList     = "SIDEBAR_LIST";
inline constexpr auto kSourcePadPanel        = "PAD_PANEL";

inline constexpr auto kLegacyMultiPadKind = "ShowCueMultiPadGridDrag";

struct TrackCopyRecord
{
    juce::String filePath;
    juce::String customName;
    juce::Colour tagColour = showcontrol::colours::defaultTagColour();
    float outputGain = 1.0f;
    bool looping = false;
};

inline juce::var buildCrossComponentCopyPayload (const juce::String& sourceListName,
                                                 const juce::Array<TrackCopyRecord>& tracks) noexcept
{
    juce::DynamicObject::Ptr obj (new juce::DynamicObject());
    obj->setProperty (kActionType, kCrossComponentCopy);
    obj->setProperty (kSourceType, kSourceSidebarListView);
    obj->setProperty (kSourceListName, sourceListName);

    juce::Array<juce::var> paths;
    juce::Array<juce::var> names;
    juce::Array<juce::var> colours;
    juce::Array<juce::var> gains;
    juce::Array<juce::var> loops;

    for (const auto& track : tracks)
    {
        paths.add (track.filePath);
        names.add (track.customName);
        colours.add ((int) track.tagColour.getARGB());
        gains.add (track.outputGain);
        loops.add (track.looping);
    }

    obj->setProperty (kAudioFilePaths, paths);
    obj->setProperty (kTrackNames, names);
    obj->setProperty (kTagColourArgb, colours);
    obj->setProperty (kOutputGains, gains);
    obj->setProperty (kLoopFlags, loops);
    return juce::var (obj.get());
}

inline juce::String buildLocalPadMoveDragToken (int padIndex) noexcept
{
    return juce::String (kLocalPadMovePrefix) + juce::String (padIndex);
}

inline juce::String buildLocalPadMoveDragToken (const juce::Array<int>& padIndices,
                                                int anchorIndex) noexcept
{
    if (padIndices.isEmpty())
        return buildLocalPadMoveDragToken (anchorIndex);

    if (padIndices.size() == 1)
        return buildLocalPadMoveDragToken (padIndices.getFirst());

    juce::String token = juce::String (kLocalPadMovePrefix) + juce::String (anchorIndex) + ":";

    for (int i = 0; i < padIndices.size(); ++i)
    {
        if (i > 0)
            token += ",";

        token += juce::String (padIndices[i]);
    }

    return token;
}

inline bool isLocalPadMoveDrag (const juce::var& description) noexcept
{
    return description.toString().startsWith (kLocalPadMovePrefix);
}

inline bool decodeLocalPadMoveDrag (const juce::var& description,
                                    juce::Array<int>& outIndices,
                                    int& outAnchorIndex) noexcept
{
    const auto text = description.toString();

    if (! text.startsWith (kLocalPadMovePrefix))
        return false;

    const auto body = text.fromFirstOccurrenceOf (kLocalPadMovePrefix, false, false).trim();
    outIndices.clear();
    outAnchorIndex = -1;

    if (body.contains (":"))
    {
        outAnchorIndex = body.upToFirstOccurrenceOf (":", false, false).trim().getIntValue();
        const auto listPart = body.fromFirstOccurrenceOf (":", false, false);

        for (const auto& part : juce::StringArray::fromTokens (listPart, ",", ""))
            outIndices.add (part.trim().getIntValue());
    }
    else
    {
        outAnchorIndex = body.getIntValue();
        outIndices.add (outAnchorIndex);
    }

    return outAnchorIndex >= 0 && outIndices.size() > 0;
}

inline juce::String buildLocalRowReorderDragToken (int rowIndex) noexcept
{
    return juce::String (kLocalRowReorderPrefix) + juce::String (rowIndex);
}

inline bool isLocalRowReorderDrag (const juce::var& description) noexcept
{
    return description.toString().startsWith (kLocalRowReorderPrefix);
}

inline int parseLocalRowReorderSourceIndex (const juce::var& description) noexcept
{
    const auto text = description.toString();

    if (! text.startsWith (kLocalRowReorderPrefix))
        return -1;

    return text.fromFirstOccurrenceOf (kLocalRowReorderPrefix, false, false).trim().getIntValue();
}

/** Ranh giới tiệm cận — làm tròn Y về biên dòng vật lý gần nhất (đồng bộ BGM + CUE list). */
inline int computeRoundedRowInsertionIndex (int yInContent, int rowHeight, int maxInsertionIndex) noexcept
{
    if (rowHeight <= 0)
        return 0;

    const int targetIndex = juce::roundToInt ((float) yInContent / (float) rowHeight);
    return juce::jlimit (0, maxInsertionIndex, targetIndex);
}

/** Bù trừ chỉ số đích khi kéo dòng xuống dưới (sau khi gỡ nguồn khỏi mảng). */
inline int computeFinalRowIndexAfterMoveDown (int sourceRowIndex, int insertionIndex) noexcept
{
    int dest = insertionIndex;

    if (sourceRowIndex >= 0 && sourceRowIndex < dest)
        --dest;

    return dest;
}

inline juce::var buildPadPanelPayload (const juce::Array<int>& padIndices,
                                       int anchorIndex,
                                       bool crossComponentCopy = false) noexcept
{
    juce::DynamicObject::Ptr obj (new juce::DynamicObject());
    obj->setProperty (kSourceType, kSourcePadPanel);
    obj->setProperty (kAnchorIndex, anchorIndex);

    if (crossComponentCopy)
        obj->setProperty (kActionType, kCrossComponentCopy);

    juce::Array<juce::var> indices;
    for (const auto idx : padIndices)
        indices.add (idx);

    obj->setProperty (kPadIndices, indices);
    return juce::var (obj.get());
}

/** @deprecated Chỉ dùng cho tương thích payload cũ. */
inline juce::var buildSidebarListPayload (const juce::String& listName,
                                          const juce::Array<int>& itemIds) noexcept
{
    juce::DynamicObject::Ptr obj (new juce::DynamicObject());
    obj->setProperty (kSourceType, kSourceSidebarList);
    obj->setProperty (kSourceListName, listName);

    juce::Array<juce::var> idArray;
    for (const auto id : itemIds)
        idArray.add (id);

    obj->setProperty (kItemIds, idArray);
    return juce::var (obj.get());
}

inline bool isCrossCopyDropInterest (const juce::var& description) noexcept
{
    if (! description.isObject())
        return false;

    return description.getProperty (kActionType, {}).toString() == kCrossComponentCopy;
}

inline bool decodeCrossComponentCopyPayload (const juce::var& description,
                                             juce::Array<TrackCopyRecord>& outTracks,
                                             juce::String& outListName) noexcept
{
    if (! description.isObject())
        return false;

    if (description.getProperty (kActionType, {}).toString() != kCrossComponentCopy)
        return false;

    const auto sourceType = description.getProperty (kSourceType, {}).toString();

    if (sourceType != kSourceSidebarListView && sourceType != kSourceSidebarList)
        return false;

    outListName = description.getProperty (kSourceListName, {}).toString();
    outTracks.clear();

    auto* paths = description.getProperty (kAudioFilePaths, {}).getArray();

    if (paths == nullptr || paths->isEmpty())
        return false;

    auto* names  = description.getProperty (kTrackNames, {}).getArray();
    auto* colours = description.getProperty (kTagColourArgb, {}).getArray();
    auto* gains  = description.getProperty (kOutputGains, {}).getArray();
    auto* loops  = description.getProperty (kLoopFlags, {}).getArray();

    for (int i = 0; i < paths->size(); ++i)
    {
        TrackCopyRecord rec;
        rec.filePath = (*paths)[i].toString();

        if (rec.filePath.isEmpty())
            continue;

        if (names != nullptr && i < names->size())
            rec.customName = (*names)[i].toString();

        if (colours != nullptr && i < colours->size())
            rec.tagColour = juce::Colour ((juce::uint32) (int) (*colours)[i]);

        if (gains != nullptr && i < gains->size())
            rec.outputGain = (float) (*gains)[i];

        if (loops != nullptr && i < loops->size())
            rec.looping = (bool) (*loops)[i];

        outTracks.add (rec);
    }

    return outTracks.size() > 0;
}

inline bool decodeSidebarListPayload (const juce::var& description,
                                      juce::Array<int>& outItemIds,
                                      juce::String& outListName) noexcept
{
    if (! description.isObject())
        return false;

    if (description.getProperty (kSourceType, {}).toString() != kSourceSidebarList)
        return false;

    outListName = description.getProperty (kSourceListName, {}).toString();
    outItemIds.clear();

    if (auto* arr = description.getProperty (kItemIds, {}).getArray())
    {
        for (const auto& v : *arr)
            outItemIds.add ((int) v);
    }

    return outItemIds.size() > 0;
}

inline bool decodePadPanelPayload (const juce::var& description,
                                   juce::Array<int>& outIndices,
                                   int& outAnchorIndex) noexcept
{
    if (! description.isObject())
        return false;

    if (description.getProperty (kSourceType, {}).toString() == kSourcePadPanel)
    {
        outAnchorIndex = (int) description.getProperty (kAnchorIndex, -1);
        outIndices.clear();

        if (auto* arr = description.getProperty (kPadIndices, {}).getArray())
        {
            for (const auto& v : *arr)
                outIndices.add ((int) v);
        }

        return outIndices.size() > 0 && outAnchorIndex >= 0;
    }

    if (description.getProperty ("kind", {}).toString() == kLegacyMultiPadKind)
    {
        outAnchorIndex = (int) description.getProperty (kAnchorIndex, -1);
        outIndices.clear();

        if (auto* arr = description.getProperty ("indices", {}).getArray())
        {
            for (const auto& v : *arr)
                outIndices.add ((int) v);
        }

        return outIndices.size() > 0 && outAnchorIndex >= 0;
    }

    return false;
}

inline bool isCrossComponentCopyPayload (const juce::var& description) noexcept
{
    return isCrossCopyDropInterest (description);
}

inline bool isPadPanelPayload (const juce::var& description) noexcept
{
    if (isLocalPadMoveDrag (description))
        return true;

    juce::Array<int> tmp;
    int anchor = -1;
    return decodePadPanelPayload (description, tmp, anchor);
}

inline bool isSidebarListCopyPayload (const juce::var& description) noexcept
{
    return isCrossComponentCopyPayload (description);
}

inline bool isSidebarListPayload (const juce::var& description) noexcept
{
    if (isCrossComponentCopyPayload (description))
        return true;

    juce::Array<int> tmp;
    juce::String name;
    return decodeSidebarListPayload (description, tmp, name);
}

inline bool isPadPanelCrossCopyPayload (const juce::var& description) noexcept
{
    return isPadPanelPayload (description)
           && description.getProperty (kActionType, {}).toString() == kCrossComponentCopy;
}

inline int dragPayloadItemCount (const juce::var& description) noexcept
{
    if (isLocalPadMoveDrag (description))
    {
        juce::Array<int> indices;
        int anchor = -1;

        if (decodeLocalPadMoveDrag (description, indices, anchor))
            return juce::jmax (1, indices.size());
    }

    if (auto* paths = description.getProperty (kAudioFilePaths, {}).getArray())
        return paths->size();

    if (auto* ids = description.getProperty (kItemIds, {}).getArray())
        return ids->size();

    if (auto* indices = description.getProperty (kPadIndices, {}).getArray())
        return indices->size();

    if (auto* legacy = description.getProperty ("indices", {}).getArray())
        return legacy->size();

    return 1;
}

inline void paintNeonDropTargetGlow (juce::Graphics& g,
                                     juce::Rectangle<float> bounds,
                                     juce::Colour colour) noexcept
{
    const auto neon = colour.isTransparent() ? ShowTheme::get (true).accent : colour;

    for (int ring = 4; ring >= 1; --ring)
    {
        g.setColour (neon.withAlpha (0.07f * (float) ring));
        g.drawRoundedRectangle (bounds.expanded ((float) ring * 3.5f), 10.0f, 2.0f);
    }

    g.setColour (neon.withAlpha (0.24f));
    g.fillRoundedRectangle (bounds, 9.0f);

    g.setColour (neon.withAlpha (0.98f));
    g.drawRoundedRectangle (bounds, 9.0f, 2.8f);

    g.setColour (neon.brighter (0.35f).withAlpha (0.55f));
    g.drawRoundedRectangle (bounds.reduced (1.5f), 8.0f, 1.2f);
}

/** Thanh chỉ vị trí chèn — dải neon 2 đầu mút tròn đối xứng (Farrago Pro). */
inline void paintNeonRoundedCapInsertLine (juce::Graphics& g,
                                           float lineY,
                                           float componentWidth,
                                           juce::Colour neonColour = juce::Colour (0xFF4A90E2),
                                           float edgePaddingX = 12.0f,
                                           float dotRadius = 4.0f,
                                           float lineThickness = 2.5f) noexcept
{
    g.setColour (neonColour);

    g.fillEllipse (edgePaddingX - dotRadius, lineY - dotRadius, dotRadius * 2.0f, dotRadius * 2.0f);
    g.fillEllipse (componentWidth - edgePaddingX - dotRadius, lineY - dotRadius, dotRadius * 2.0f, dotRadius * 2.0f);
    g.drawLine (edgePaddingX, lineY, componentWidth - edgePaddingX, lineY, lineThickness);
}

/** Bong bóng Capsule Pill kéo thả — kính mờ sáng gương + badge (đồng bộ BGM + CUE). */
inline juce::Image createPremiumDragImage (const juce::String& trackTitle, int selectedItemsCount) noexcept
{
    const int imgW = 240;
    const int imgH = 38;
    const int count = juce::jmax (1, selectedItemsCount);
    const float cornerRadius = (float) imgH * 0.5f;
    const juce::Rectangle<float> bounds (1.5f, 1.5f, (float) imgW - 3.0f, (float) imgH - 3.0f);

    juce::Image dragImg (juce::Image::ARGB, imgW, imgH, true);
    juce::Graphics g (dragImg);

    g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);

    g.setColour (juce::Colour (0xCC16161A));
    g.fillRoundedRectangle (bounds, cornerRadius);

    juce::ColourGradient glassGrad (juce::Colours::white.withAlpha (0.09f), 0.0f, 0.0f,
                                    juce::Colours::white.withAlpha (0.01f), 0.0f, (float) imgH, false);
    g.setGradientFill (glassGrad);
    g.fillRoundedRectangle (bounds, cornerRadius);

    g.setColour (juce::Colour (0xFF4A90E2));
    g.drawRoundedRectangle (bounds, cornerRadius, 1.5f);

    g.setColour (juce::Colours::white);
    g.setFont (juce::FontOptions (13.0f, juce::Font::plain));
    g.drawText (trackTitle, 18, 0, imgW - 65, imgH, juce::Justification::centredLeft, true);

    g.setColour (juce::Colour (0xFF4A90E2));
    const float badgeSize = 22.0f;
    const float badgeX = (float) imgW - badgeSize - 10.0f;
    const float badgeY = ((float) imgH - badgeSize) * 0.5f;
    g.fillEllipse (badgeX, badgeY, badgeSize, badgeSize);

    g.setColour (juce::Colours::white);
    g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
    g.drawText (juce::String (count), (int) badgeX, (int) badgeY, (int) badgeSize, (int) badgeSize, juce::Justification::centred);

    return dragImg;
}

/** Vẽ capsule proxy tại tâm con trỏ — đồng bộ overlay nội bộ với JUCE drag image. */
inline void paintPremiumDragCapsuleProxy (juce::Graphics& g,
                                          float centreX,
                                          float centreY,
                                          const juce::String& trackTitle,
                                          int selectedItemsCount) noexcept
{
    const auto dragImg = createPremiumDragImage (trackTitle, selectedItemsCount);

    if (! dragImg.isValid())
        return;

    g.drawImageAt (dragImg,
                   juce::roundToInt (centreX - (float) dragImg.getWidth() * 0.5f),
                   juce::roundToInt (centreY - (float) dragImg.getHeight() * 0.5f));
}

/** Drag ghost trong suốt — fallback khi không có tiêu đề. */
inline juce::Image createMinimalRowReorderDragImage() noexcept
{
    juce::Image image (juce::Image::ARGB, 1, 1, true);
    image.clear (image.getBounds(), juce::Colours::transparentBlack);
    return image;
}

inline juce::Image createMultiItemDragImage (int itemCount, bool isDarkMode) noexcept
{
    const int count = juce::jmax (1, itemCount);
    const auto pal = ShowTheme::get (isDarkMode);

    const int badgeSize = juce::jlimit (36, 56, 28 + count * 2);
    juce::Image image (juce::Image::ARGB, badgeSize, badgeSize, true);
    juce::Graphics g (image);

    const auto bounds = image.getBounds().toFloat().reduced (2.0f);
    g.setColour (pal.accent.withAlpha (0.92f));
    g.fillEllipse (bounds);

    g.setColour (juce::Colours::white);
    g.setFont (ShowTheme::fontBold ((float) juce::jmax (11, badgeSize / 2)));
    g.drawText (juce::String (count),
                image.getBounds(),
                juce::Justification::centred);

    return image;
}

} // namespace showcontrol::crossdrag
