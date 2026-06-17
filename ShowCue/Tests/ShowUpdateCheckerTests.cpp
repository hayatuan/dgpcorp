#include <JuceHeader.h>
#include "../Source/ShowUpdateChecker.h"

class ShowUpdateCheckerTests final : public juce::UnitTest
{
public:
    ShowUpdateCheckerTests() : juce::UnitTest ("ShowUpdateChecker", "showcue") {}

    void runTest() override
    {
        beginTest ("compareVersionStrings patch ordering");
        expect (showcontrol::update::compareVersionStrings ("1.0.10", "1.0.2") > 0);
        expect (showcontrol::update::compareVersionStrings ("1.0.2", "1.0.10") < 0);
        expect (showcontrol::update::compareVersionStrings ("2.0.0", "1.99.99") > 0);

        beginTest ("compareVersionStrings pre-release");
        expect (showcontrol::update::compareVersionStrings ("1.0.0-beta", "1.0.0") < 0);
        expect (showcontrol::update::compareVersionStrings ("1.0.0", "1.0.0-beta") > 0);
        expect (showcontrol::update::compareVersionStrings ("1.0.0-alpha", "1.0.0-beta") != 0);

        beginTest ("compareVersionStrings tag prefix");
        expect (showcontrol::update::compareVersionStrings ("v1.2", "1.2.0") == 0);
        expect (showcontrol::update::compareVersionStrings ("V1.2.0", "1.2") == 0);

        beginTest ("parseUpdateJson");
        const juce::String json =
            R"({"tag_name":"v1.2.3","assets":[{"browser_download_url":"https://example.com/ShowCue.dmg"}]})";
        const auto info = showcontrol::update::parseUpdateJson (json);
        expect (info.has_value());

        if (info)
        {
            expectEquals (info->version, juce::String ("1.2.3"));
            expect (info->downloadUrl.contains ("ShowCue.dmg"));
        }

        beginTest ("parseUpdateJson rejects empty version");
        expect (! showcontrol::update::parseUpdateJson ("{}").has_value());
        expect (! showcontrol::update::parseUpdateJson ("not json").has_value());
    }
};

static ShowUpdateCheckerTests showUpdateCheckerTestsInstance;
