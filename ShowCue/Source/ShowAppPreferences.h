#pragma once
#include <juce_data_structures/juce_data_structures.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <array>
#include "ShowOutputRouting.h"

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

    inline void saveDirectRoutingSettings (const std::array<juce::String, showcontrol::routing::kRouteCount>& outputChoices,
                                           const juce::StringArray& customBusNames)
    {
        auto& prefs = appPreferencesFile();
        prefs.setValue ("routeMasterOutput", outputChoices[(size_t) showcontrol::routing::kMasterRouteId]);

        for (int i = 0; i < showcontrol::routing::kMaxCustomBuses; ++i)
        {
            prefs.setValue ("routeBusOutput" + juce::String (i),
                            outputChoices[(size_t) (i + 1)]);
            prefs.setValue ("routeBusName" + juce::String (i),
                            i < customBusNames.size() ? customBusNames[i] : juce::String());
        }

        prefs.save();
    }

    inline juce::String loadRouteOutputChoice (int routeId) noexcept
    {
        auto& prefs = appPreferencesFile();

        if (routeId == showcontrol::routing::kMasterRouteId)
            return prefs.getValue ("routeMasterOutput", {});

        if (routeId > showcontrol::routing::kMasterRouteId
            && routeId < showcontrol::routing::kRouteCount)
        {
            return prefs.getValue ("routeBusOutput" + juce::String (routeId - 1), {});
        }

        return {};
    }

    inline std::array<juce::String, showcontrol::routing::kRouteCount> loadAllRouteOutputChoices() noexcept
    {
        std::array<juce::String, showcontrol::routing::kRouteCount> choices {};

        for (int r = 0; r < showcontrol::routing::kRouteCount; ++r)
            choices[(size_t) r] = loadRouteOutputChoice (r);

        return choices;
    }

    inline void loadDirectRoutingIntoLiveTable (juce::AudioDeviceManager* manager = nullptr) noexcept
    {
        auto* device = manager != nullptr ? manager->getCurrentAudioDevice() : nullptr;
        const auto choices = loadAllRouteOutputChoices();

        for (int r = 0; r < showcontrol::routing::kRouteCount; ++r)
        {
            auto choice = choices[(size_t) r].trim();

            if (choice.isEmpty() && manager != nullptr)
            {
                juce::AudioDeviceManager::AudioDeviceSetup setup;
                manager->getAudioDeviceSetup (setup);
                const auto endpoints = showcontrol::routing::scanAvailableOutputEndpoints (*manager);

                if (r == showcontrol::routing::kMasterRouteId)
                {
                    choice = setup.outputDeviceName.trim();

                    if (choice.isEmpty() && endpoints.size() > 0)
                        choice = endpoints[0];
                }
                else if (r > showcontrol::routing::kMasterRouteId
                         && (r - 1) < endpoints.size())
                {
                    choice = endpoints[(size_t) (r - 1)];
                }
                else if (endpoints.size() > 0)
                {
                    choice = endpoints[0];
                }
            }

            showcontrol::routing::bindRouteOutputChoice (r, choice, device);
        }
    }

    inline void loadDirectRoutingIntoLiveTable() noexcept
    {
        loadDirectRoutingIntoLiveTable (nullptr);
    }

    inline juce::StringArray loadCustomBusNamesFromPrefs() noexcept
    {
        juce::StringArray names;
        auto& prefs = appPreferencesFile();

        for (int i = 0; i < showcontrol::routing::kMaxCustomBuses; ++i)
        {
            const auto saved = prefs.getValue ("routeBusName" + juce::String (i), {}).trim();
            names.add (saved.isNotEmpty()
                           ? saved
                           : showcontrol::routing::defaultCustomBusName (i));
        }

        return names;
    }
} // namespace showcontrol::prefs
