#pragma once
#include <juce_data_structures/juce_data_structures.h>

/** Ghi/đọc Theme + Ngôn ngữ ngay lập tức — không phụ thuộc saveProject() lúc thoát app. */
namespace showcontrol::prefs
{
    inline juce::PropertiesFile& appPreferencesFile()
    {
        static juce::PropertiesFile::Options opts = []
        {
            juce::PropertiesFile::Options o;
            o.applicationName             = "ShowCue";
            o.filenameSuffix              = "settings";
            o.folderName                  = "ShowCue";
            o.osxLibrarySubFolder         = "Application Support";
            o.storageFormat               = juce::PropertiesFile::storeAsXML;
            o.millisecondsBeforeSaving    = 0;
            return o;
        }();

        static juce::PropertiesFile prefs (opts);
        return prefs;
    }

    inline int loadThemeId (int defaultThemeId = 1) noexcept
    {
        return juce::jlimit (1, 3, appPreferencesFile().getIntValue ("appTheme", defaultThemeId));
    }

    inline int loadLanguageIndex (int defaultLanguageIndex = 1) noexcept
    {
        return juce::jlimit (0, 2, appPreferencesFile().getIntValue ("appLanguage", defaultLanguageIndex));
    }

    inline void saveThemeId (int themeId)
    {
        auto& prefs = appPreferencesFile();
        prefs.setValue ("appTheme", juce::jlimit (1, 3, themeId));
        prefs.saveIfNeeded();
        prefs.save();
    }

    inline void saveLanguageIndex (int languageIndex)
    {
        auto& prefs = appPreferencesFile();
        prefs.setValue ("appLanguage", juce::jlimit (0, 2, languageIndex));
        prefs.saveIfNeeded();
        prefs.save();
    }

    inline void loadUiPreferences (int& themeId, int& languageIndex) noexcept
    {
        themeId       = loadThemeId (themeId);
        languageIndex = loadLanguageIndex (languageIndex);
    }

    inline bool loadOscEnabled() noexcept
    {
        return appPreferencesFile().getBoolValue ("oscEnabled", false);
    }

    inline int loadOscPort() noexcept
    {
        return juce::jlimit (1024, 65535, appPreferencesFile().getIntValue ("oscPort", 9000));
    }

    inline void saveOscSettings (bool enabled, int port)
    {
        auto& prefs = appPreferencesFile();
        prefs.setValue ("oscEnabled", enabled);
        prefs.setValue ("oscPort", juce::jlimit (1024, 65535, port));
        prefs.save();
    }

    inline int loadBackupRole() noexcept
    {
        return juce::jlimit (0, 2, appPreferencesFile().getIntValue ("backupRole", 0));
    }

    inline juce::StringArray normaliseBackupPeerHosts (const juce::String& raw)
    {
        juce::StringArray hosts;
        hosts.addTokens (raw, "\n,;", "\"");
        hosts.trim();
        hosts.removeEmptyStrings();

        juce::StringArray unique;

        for (const auto& host : hosts)
        {
            bool duplicate = false;

            for (const auto& existing : unique)
            {
                if (existing.equalsIgnoreCase (host))
                {
                    duplicate = true;
                    break;
                }
            }

            if (! duplicate)
                unique.add (host);
        }

        while (unique.size() > 16)
            unique.remove (unique.size() - 1);

        return unique;
    }

    inline juce::StringArray loadBackupPeerHosts() noexcept
    {
        const auto multi = appPreferencesFile().getValue ("backupPeerHosts", {});

        if (multi.isNotEmpty())
            return normaliseBackupPeerHosts (multi);

        const auto single = appPreferencesFile().getValue ("backupPeerHost", {});

        if (single.isNotEmpty())
            return normaliseBackupPeerHosts (single);

        return {};
    }

    inline juce::String loadBackupPeerHost() noexcept
    {
        const auto hosts = loadBackupPeerHosts();
        return hosts.size() > 0 ? hosts[0] : juce::String();
    }

    inline int loadBackupSyncPort() noexcept
    {
        return juce::jlimit (1024, 65535,
                            appPreferencesFile().getIntValue ("backupSyncPort", 9000));
    }

    inline bool loadBackupFollowerLock() noexcept
    {
        return appPreferencesFile().getBoolValue ("backupFollowerLock", true);
    }

    inline void saveBackupSyncSettings (int role,
                                        const juce::StringArray& peerHosts,
                                        int syncPort,
                                        bool followerLock,
                                        bool oscEnabled,
                                        int oscPort)
    {
        const auto hosts = normaliseBackupPeerHosts (peerHosts.joinIntoString ("\n"));
        auto& prefs      = appPreferencesFile();

        prefs.setValue ("backupRole", juce::jlimit (0, 2, role));
        prefs.setValue ("backupPeerHosts", hosts.joinIntoString ("\n"));
        prefs.setValue ("backupPeerHost", hosts.size() > 0 ? hosts[0] : juce::String());
        prefs.setValue ("backupSyncPort", juce::jlimit (1024, 65535, syncPort));
        prefs.setValue ("backupFollowerLock", followerLock);
        prefs.setValue ("oscEnabled", oscEnabled);
        prefs.setValue ("oscPort", juce::jlimit (1024, 65535, oscPort));
        prefs.save();
    }

    inline void saveBackupSyncSettings (int role,
                                        const juce::String& peerHost,
                                        int syncPort,
                                        bool followerLock,
                                        bool oscEnabled,
                                        int oscPort)
    {
        juce::StringArray hosts;

        if (peerHost.trim().isNotEmpty())
            hosts.add (peerHost.trim());

        saveBackupSyncSettings (role, hosts, syncPort, followerLock, oscEnabled, oscPort);
    }
} // namespace showcontrol::prefs
