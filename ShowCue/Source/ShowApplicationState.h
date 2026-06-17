#pragma once
#include <iostream>
#include <juce_core/juce_core.h>
#include <memory>

/** Hard flush JSON — ~/Library/Application Support/ShowCue/config.json (macOS). */
namespace showcontrol::state
{
    inline bool writeTextFileAtomically (const juce::File& destination, const juce::String& text)
    {
        const auto tmpFile = destination.getSiblingFile (destination.getFileName() + ".tmp");
        tmpFile.deleteFile();

        if (auto stream = std::unique_ptr<juce::FileOutputStream> (tmpFile.createOutputStream()))
        {
            stream->writeText (text, false, false, nullptr);
            stream->flush();

            if (! stream->getStatus().wasOk())
            {
                tmpFile.deleteFile();
                return false;
            }
        }
        else
        {
            return false;
        }

        if (tmpFile.replaceFileIn (destination))
            return true;

        if (destination.existsAsFile())
            destination.deleteFile();

        return tmpFile.moveFileTo (destination);
    }

    /** Canonical path: userApplicationDataDirectory đã là ~/Library/Application Support trên macOS. */
    inline juce::File getCanonicalConfigFile() noexcept
    {
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                   .getChildFile ("ShowCue")
                   .getChildFile ("config.json");
    }

    inline juce::File getLegacyHayatuanConfigFile() noexcept
    {
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                   .getChildFile ("HAYATUAN")
                   .getChildFile ("ShowCue")
                   .getChildFile ("config.json");
    }

    /** Đường dẫn nhầm từng dùng khi gọi .getChildFile("Application Support") hai lần. */
    inline juce::File getLegacyDoubleAppSupportConfigFile() noexcept
    {
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                   .getChildFile ("Application Support")
                   .getChildFile ("ShowCue")
                   .getChildFile ("config.json");
    }

    inline juce::File findExistingConfigFile() noexcept
    {
        const auto canonical = getCanonicalConfigFile();
        if (canonical.existsAsFile())
            return canonical;

        const auto legacyHayatuan = getLegacyHayatuanConfigFile();
        if (legacyHayatuan.existsAsFile())
            return legacyHayatuan;

        const auto legacyDouble = getLegacyDoubleAppSupportConfigFile();
        if (legacyDouble.existsAsFile())
            return legacyDouble;

        return canonical;
    }

    inline void migrateLegacyConfigIfNeeded()
    {
        const auto canonical = getCanonicalConfigFile();
        if (canonical.existsAsFile())
            return;

        const auto legacyHayatuan = getLegacyHayatuanConfigFile();
        const auto legacyDouble   = getLegacyDoubleAppSupportConfigFile();

        juce::File source;

        if (legacyHayatuan.existsAsFile())
            source = legacyHayatuan;
        else if (legacyDouble.existsAsFile())
            source = legacyDouble;
        else
            return;

        const auto parent = canonical.getParentDirectory();
        parent.createDirectory();
        source.copyFileTo (canonical);

        std::cout << "[CONFIG] [MIGRATE] Copied legacy config to: "
                  << canonical.getFullPathName().toStdString() << std::endl;
    }

    inline bool ensureConfigParentDirectory (const juce::File& configFile) noexcept
    {
        const auto parent = configFile.getParentDirectory();
        const bool ok     = parent.createDirectory();

        if (! ok && ! parent.exists())
        {
            std::cout << "[CONFIG] [ERROR] Cannot create directory: "
                      << parent.getFullPathName().toStdString() << std::endl;
            return false;
        }

        return true;
    }

    inline void ensureDefaultConfigExists()
    {
        migrateLegacyConfigIfNeeded();

        const auto file = getCanonicalConfigFile();
        ensureConfigParentDirectory (file);
    }

    inline bool hardFlushJsonConfig (const juce::var& root)
    {
        const auto configFile = getCanonicalConfigFile();

        if (! ensureConfigParentDirectory (configFile))
            return false;

        const juce::String jsonText = juce::JSON::toString (root, true);

        if (! writeTextFileAtomically (configFile, jsonText))
        {
            std::cout << "[CONFIG] [ERROR] Atomic write failed at: "
                      << configFile.getFullPathName().toStdString() << std::endl;
            return false;
        }

        std::cout << "[CONFIG] [SUCCESS] Saved to: "
                  << configFile.getFullPathName().toStdString() << std::endl;
        return true;
    }

    inline juce::var readJsonConfig()
    {
        migrateLegacyConfigIfNeeded();

        const auto configFile = findExistingConfigFile();
        const auto absPath    = configFile.getFullPathName();

        std::cout << "[CONFIG] Loading from: " << absPath.toStdString() << std::endl;

        if (! configFile.existsAsFile())
        {
            std::cout << "[CONFIG] [INFO] No config file yet — using defaults." << std::endl;
            return juce::var (new juce::DynamicObject());
        }

        const auto text = configFile.loadFileAsString();

        if (text.trim().isEmpty())
        {
            std::cout << "[CONFIG] [WARN] Config file is empty: " << absPath.toStdString() << std::endl;
            return juce::var (new juce::DynamicObject());
        }

        const auto parsed = juce::JSON::parse (text);

        if (parsed.isVoid())
        {
            std::cout << "[CONFIG] [ERROR] JSON parse failed: " << absPath.toStdString() << std::endl;
            return juce::var (new juce::DynamicObject());
        }

        std::cout << "[CONFIG] [SUCCESS] Parsed config (" << text.length() << " chars)" << std::endl;
        return parsed;
    }

    inline bool writeJsonConfig (const juce::var& root)
    {
        return hardFlushJsonConfig (root);
    }

    // Backward compat alias.
    inline juce::File getConfigFile() noexcept { return getCanonicalConfigFile(); }
} // namespace showcontrol::state
