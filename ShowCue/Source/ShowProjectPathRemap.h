#pragma once

#include <JuceHeader.h>
#include <map>
#include <utility>

/** Cross-platform path matching when importing/syncing project XML (Mac ↔ Win). */
namespace showcontrol::persistence::pathremap
{

inline juce::String syncFileTokenFromPath (const juce::String& path)
{
    if (path.isEmpty())
        return {};

    return juce::File (path).getFileName();
}

inline bool pathsMatchForSync (const juce::String& incomingPath, const juce::String& localPath)
{
    if (incomingPath.isEmpty() || localPath.isEmpty())
        return false;

    if (incomingPath.equalsIgnoreCase (localPath))
        return true;

    return syncFileTokenFromPath (incomingPath).equalsIgnoreCase (
        syncFileTokenFromPath (localPath));
}

struct LocalPathIndex
{
    std::map<std::pair<int, int>, juce::String> bySlot;
    std::map<std::pair<int, juce::String>, juce::String> byListFileName;
};

inline LocalPathIndex buildLocalPathIndex (const juce::XmlElement& localXml)
{
    LocalPathIndex index;
    int listIdx = 0;

    for (auto* listElem : localXml.getChildIterator())
    {
        if (! listElem->hasTagName ("List"))
            continue;

        int padIdx = 0;

        for (auto* padElem : listElem->getChildIterator())
        {
            if (! padElem->hasTagName ("Pad"))
                continue;

            const int slot = padElem->getIntAttribute ("index", padIdx);
            const auto path = padElem->getStringAttribute ("file", "");

            if (path.isNotEmpty())
            {
                index.bySlot[{ listIdx, slot }] = path;

                const auto token = syncFileTokenFromPath (path);

                if (token.isNotEmpty())
                    index.byListFileName[{ listIdx, token }] = path;
            }

            ++padIdx;
        }

        ++listIdx;
    }

    return index;
}

inline juce::String resolveLocalPathForIncoming (const LocalPathIndex& index,
                                                 int listIdx,
                                                 int padIdx,
                                                 const juce::String& incomingPath)
{
    if (incomingPath.isEmpty())
        return {};

    const auto slotIt = index.bySlot.find ({ listIdx, padIdx });

    if (slotIt != index.bySlot.end())
    {
        if (pathsMatchForSync (incomingPath, slotIt->second))
            return slotIt->second;
    }

    const auto token = syncFileTokenFromPath (incomingPath);

    if (token.isNotEmpty())
    {
        const auto nameIt = index.byListFileName.find ({ listIdx, token });

        if (nameIt != index.byListFileName.end())
            return nameIt->second;
    }

    return incomingPath;
}

struct RemapSummary
{
    int remappedCount   = 0;
    int unchangedCount  = 0;
    int unmatchedCount  = 0;
    juce::StringArray unmatchedTokens;
};

inline RemapSummary summarizeRemap (const juce::String& incomingXml, const juce::String& remappedXml)
{
    RemapSummary summary;

    const auto incoming = juce::parseXML (incomingXml);
    const auto remapped = juce::parseXML (remappedXml);

    if (incoming == nullptr || remapped == nullptr)
        return summary;

    auto countPads = [] (const juce::XmlElement& root, auto&& visitor)
    {
        int listIdx = 0;

        for (auto* listElem : root.getChildIterator())
        {
            if (! listElem->hasTagName ("List"))
                continue;

            int padIdx = 0;

            for (auto* padElem : listElem->getChildIterator())
            {
                if (! padElem->hasTagName ("Pad"))
                    continue;

                visitor (listIdx, padElem->getIntAttribute ("index", padIdx),
                         padElem->getStringAttribute ("file", ""));
                ++padIdx;
            }

            ++listIdx;
        }
    };

    std::map<std::pair<int, int>, juce::String> remappedPaths;

    countPads (*remapped, [&] (int listIdx, int padIdx, const juce::String& path)
    {
        remappedPaths[{ listIdx, padIdx }] = path;
    });

    countPads (*incoming, [&] (int listIdx, int padIdx, const juce::String& incomingPath)
    {
        if (incomingPath.isEmpty())
            return;

        const auto remappedPath = remappedPaths[{ listIdx, padIdx }];

        if (remappedPath.isEmpty())
            return;

        if (incomingPath.equalsIgnoreCase (remappedPath))
        {
            ++summary.unchangedCount;
            return;
        }

        const juce::File remappedFile (remappedPath);

        if (remappedFile.existsAsFile())
        {
            ++summary.remappedCount;
            return;
        }

        ++summary.unmatchedCount;

        const auto token = syncFileTokenFromPath (incomingPath);

        if (token.isNotEmpty() && ! summary.unmatchedTokens.contains (token))
            summary.unmatchedTokens.add (token);
    });

    return summary;
}

inline juce::String remapIncomingProjectXml (const juce::String& incomingXml,
                                             const juce::String& localXml)
{
    auto incoming = juce::parseXML (incomingXml);

    if (incoming == nullptr)
        return incomingXml;

    const auto local = juce::parseXML (localXml);

    if (local == nullptr)
        return incomingXml;

    const auto index = buildLocalPathIndex (*local);
    int listIdx = 0;

    for (auto* listElem : incoming->getChildIterator())
    {
        if (! listElem->hasTagName ("List"))
            continue;

        int padIdx = 0;

        for (auto* padElem : listElem->getChildIterator())
        {
            if (! padElem->hasTagName ("Pad"))
                continue;

            const auto incomingPath = padElem->getStringAttribute ("file", "");

            if (incomingPath.isNotEmpty())
            {
                const int slot = padElem->getIntAttribute ("index", padIdx);
                const auto resolved = resolveLocalPathForIncoming (index, listIdx, slot, incomingPath);

                if (! resolved.equalsIgnoreCase (incomingPath))
                    padElem->setAttribute ("file", resolved);
            }

            ++padIdx;
        }

        ++listIdx;
    }

    return incoming->toString();
}

} // namespace showcontrol::persistence::pathremap
