#include <JuceHeader.h>
#include "../Source/ShowProjectPersistence.h"
#include "../Source/ShowApplicationState.h"

class ShowProjectPersistenceTests final : public juce::UnitTest
{
public:
    ShowProjectPersistenceTests() : juce::UnitTest ("ShowProjectPersistence", "showcue") {}

    void runTest() override
    {
        beginTest ("exportShowcuePackage round-trip files");
        const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                             .getChildFile ("ShowCueTests")
                             .getChildFile (juce::Uuid().toString());
        expect (dir.createDirectory());

        const juce::String configJson = "{\"projectSchema\":1,\"appTheme\":1}";
        const juce::String projectXml = "<ShowControlProject version=\"2.0\"/>";
        const auto dest = dir.getChildFile ("test.showcue");

        expect (showcontrol::persistence::exportShowcuePackage (dest, configJson, projectXml));
        expect (dest.existsAsFile());

        juce::ZipFile zip (dest);
        expect (zip.getNumEntries() >= 3);

        beginTest ("readShowcuePackage round-trip");
        const auto imported = showcontrol::persistence::readShowcuePackage (dest);
        expect (imported.success);
        expect (imported.configJson == configJson);
        expect (imported.projectXml == projectXml);

        beginTest ("readShowcuePackage rejects invalid file");
        const auto badFile = dir.getChildFile ("bad.showcue");
        badFile.replaceWithText ("not a zip");
        const auto badImport = showcontrol::persistence::readShowcuePackage (badFile);
        expect (! badImport.success);

        beginTest ("pruneRotatingBackups");
        const auto backupDir = dir.getChildFile ("backups");
        expect (backupDir.createDirectory());

        for (int i = 0; i < 15; ++i)
            backupDir.getChildFile ("config-test-" + juce::String (i) + ".json")
                     .replaceWithText ("{}");

        showcontrol::persistence::pruneRotatingBackups (backupDir, "config-test", 12);
        juce::Array<juce::File> remaining;
        backupDir.findChildFiles (remaining, juce::File::findFiles, false, "config-test-*");
        expectEquals (remaining.size(), 12);

        dir.deleteRecursively();
    }
};

static ShowProjectPersistenceTests showProjectPersistenceTestsInstance;
