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
} // namespace showcontrol::prefs
