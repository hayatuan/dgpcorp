#pragma once

#include "ShowApplicationState.h"

namespace showcontrol::persistence
{
inline constexpr int kProjectSchemaVersion = 1;
inline constexpr int kMaxRotatingBackups   = 12;
inline constexpr int kAutosaveIntervalMs   = 5 * 60 * 1000;

inline juce::File getBackupsDirectory() noexcept
{
    return showcontrol::state::getCanonicalConfigFile()
               .getParentDirectory()
               .getChildFile ("backups");
}

inline juce::String backupTimestampTag()
{
    return juce::Time::getCurrentTime().formatted ("%Y%m%d-%H%M%S");
}

inline void pruneRotatingBackups (const juce::File& dir,
                                  const juce::String& namePrefix,
                                  int maxKeep)
{
    if (! dir.isDirectory() || maxKeep < 1)
        return;

    juce::Array<juce::File> matches;
    dir.findChildFiles (matches, juce::File::findFiles, false, namePrefix + "-*");

    struct Entry
    {
        juce::File file;
        juce::Time modified;
    };

    juce::Array<Entry> entries;

    for (const auto& f : matches)
        entries.add ({ f, f.getLastModificationTime() });

    struct EntryTimeComparator
    {
        static int compareElements (const Entry& a, const Entry& b)
        {
            if (a.modified > b.modified) return -1;
            if (a.modified < b.modified) return 1;
            return 0;
        }
    };

    EntryTimeComparator comparator;
    entries.sort (comparator);

    for (int i = maxKeep; i < entries.size(); ++i)
        entries.getReference (i).file.deleteFile();
}

inline bool copyBackupSnapshot (const juce::File& source,
                                const juce::File& backupsDir,
                                const juce::String& prefix)
{
    if (! source.existsAsFile())
        return false;

    if (! backupsDir.createDirectory() && ! backupsDir.isDirectory())
        return false;

    const auto dest = backupsDir.getChildFile (prefix + "-" + backupTimestampTag()
                                                 + source.getFileExtension());

    if (source.copyFileTo (dest))
    {
        pruneRotatingBackups (backupsDir, prefix, kMaxRotatingBackups);
        return true;
    }

    return false;
}

inline void snapshotProjectAfterSave (const juce::File& configFile,
                                      const juce::File& projectXmlFile)
{
    const auto backupsDir = getBackupsDirectory();
    copyBackupSnapshot (configFile, backupsDir, "config");
    copyBackupSnapshot (projectXmlFile, backupsDir, "project");
}

inline void addZipUtf8Entry (juce::ZipFile::Builder& zip,
                             const juce::String& entryPath,
                             const juce::String& utf8Text)
{
    const auto numBytes = (size_t) utf8Text.getNumBytesAsUTF8();
    auto stream = std::make_unique<juce::MemoryInputStream> (utf8Text.toRawUTF8(),
                                                             numBytes,
                                                             true);
    zip.addEntry (std::move (stream), 9, entryPath, juce::Time::getCurrentTime());
}

inline juce::File getCanonicalProjectFile() noexcept
{
    const auto home = juce::File::getSpecialLocation (juce::File::userHomeDirectory);
    const auto newFile    = home.getChildFile ("ShowCue_Project.dat");
    const auto legacyFile = home.getChildFile ("Show_Control_Project.dat");

    if (legacyFile.existsAsFile() && ! newFile.existsAsFile())
        return legacyFile;

    return newFile;
}

enum class ImportPackageError
{
    none,
    fileNotFound,
    emptyPackage,
    missingEntries,
    wrongFormat,
    unsupportedSchema,
    invalidManifest,
    invalidConfig,
    invalidProject
};

struct ImportedShowcuePackage
{
    bool success = false;
    ImportPackageError error = ImportPackageError::none;
    juce::String configJson;
    juce::String projectXml;
};

inline juce::String readZipUtf8Entry (juce::ZipFile& zip, const juce::String& entryPath)
{
    if (const auto* entry = zip.getEntry (entryPath, true))
        if (auto in = std::unique_ptr<juce::InputStream> (zip.createStreamForEntry (*entry)))
            return in->readEntireStreamAsString();

    return {};
}

inline ImportedShowcuePackage readShowcuePackage (const juce::File& source)
{
    ImportedShowcuePackage result;

    if (! source.existsAsFile())
    {
        result.error = ImportPackageError::fileNotFound;
        return result;
    }

    juce::ZipFile zip (source);

    if (zip.getNumEntries() < 3)
    {
        result.error = ImportPackageError::emptyPackage;
        return result;
    }

    const auto manifestJson = readZipUtf8Entry (zip, "manifest.json");
    const auto configJson   = readZipUtf8Entry (zip, "config.json");
    const auto projectXml   = readZipUtf8Entry (zip, "project.xml");

    if (manifestJson.isEmpty() || configJson.isEmpty() || projectXml.isEmpty())
    {
        result.error = ImportPackageError::missingEntries;
        return result;
    }

    const auto manifest = juce::JSON::parse (manifestJson);

    if (auto* obj = manifest.getDynamicObject())
    {
        if (obj->getProperty ("format").toString() != "showcue")
        {
            result.error = ImportPackageError::wrongFormat;
            return result;
        }

        const int schema = static_cast<int> (obj->getProperty ("schema"));

        if (schema < 1 || schema > kProjectSchemaVersion)
        {
            result.error = ImportPackageError::unsupportedSchema;
            return result;
        }
    }
    else
    {
        result.error = ImportPackageError::invalidManifest;
        return result;
    }

    if (juce::JSON::parse (configJson).isVoid())
    {
        result.error = ImportPackageError::invalidConfig;
        return result;
    }

    const auto xml = juce::parseXML (projectXml);

    if (xml == nullptr || ! xml->hasTagName ("ShowControlProject"))
    {
        result.error = ImportPackageError::invalidProject;
        return result;
    }

    result.configJson  = configJson;
    result.projectXml  = projectXml;
    result.success     = true;
    return result;
}

inline bool installImportedConfiguration (const juce::String& configJson,
                                          const juce::String& projectXml)
{
    const auto configFile  = showcontrol::state::getCanonicalConfigFile();
    const auto projectFile = getCanonicalProjectFile();

    showcontrol::state::ensureConfigParentDirectory (configFile);

    snapshotProjectAfterSave (configFile, projectFile);

    if (! showcontrol::state::writeTextFileAtomically (configFile, configJson))
        return false;

    return showcontrol::state::writeTextFileAtomically (projectFile, projectXml);
}

inline bool exportShowcuePackage (const juce::File& destination,
                                  const juce::String& configJson,
                                  const juce::String& projectXml)
{
    if (destination.existsAsFile())
        destination.deleteFile();

    juce::TemporaryFile temp (destination);
    juce::FileOutputStream stream (temp.getFile());

    if (stream.failedToOpen())
        return false;

    juce::DynamicObject::Ptr manifest (new juce::DynamicObject());
    manifest->setProperty ("format", "showcue");
    manifest->setProperty ("schema", kProjectSchemaVersion);
    manifest->setProperty ("app", "ShowCue");
    manifest->setProperty ("exportedAt", juce::Time::getCurrentTime().toISO8601 (true));

    juce::ZipFile::Builder zip;
    addZipUtf8Entry (zip, "manifest.json", juce::JSON::toString (juce::var (manifest.get()), true));
    addZipUtf8Entry (zip, "config.json", configJson);
    addZipUtf8Entry (zip, "project.xml", projectXml);

    if (! zip.writeToStream (stream, nullptr))
        return false;

    stream.flush();
    return temp.overwriteTargetFileWithTemporary();
}

struct ConfigurationPackageSummary
{
    int listCount  = 0;
    int trackCount = 0;
};

inline ConfigurationPackageSummary summarizeProjectXml (const juce::XmlElement& xml)
{
    ConfigurationPackageSummary summary;

    for (auto* listElem : xml.getChildIterator())
    {
        if (! listElem->hasTagName ("List"))
            continue;

        ++summary.listCount;

        for (auto* padElem : listElem->getChildIterator())
        {
            if (padElem->hasTagName ("Pad"))
                ++summary.trackCount;
        }
    }

    return summary;
}

} // namespace showcontrol::persistence
