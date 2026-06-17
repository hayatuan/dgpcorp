#include <JuceHeader.h>
#include "../Source/ShowApplicationState.h"

class ShowApplicationStateTests final : public juce::UnitTest
{
public:
    ShowApplicationStateTests() : juce::UnitTest ("ShowApplicationState", "showcue") {}

    void runTest() override
    {
        beginTest ("writeTextFileAtomically creates file");
        const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                             .getChildFile ("ShowCueTests")
                             .getChildFile (juce::Uuid().toString());
        expect (dir.createDirectory());

        const auto dest = dir.getChildFile ("config.json");
        const juce::String payload = "{\"schema\":1}\n";
        expect (showcontrol::state::writeTextFileAtomically (dest, payload));
        expect (dest.existsAsFile());
        expectEquals (dest.loadFileAsString(), payload);
        expect (! dir.getChildFile ("config.json.tmp").existsAsFile());

        beginTest ("writeTextFileAtomically overwrites in place");
        const juce::String payload2 = "{\"schema\":2,\"pads\":[]}\n";
        expect (showcontrol::state::writeTextFileAtomically (dest, payload2));
        expectEquals (dest.loadFileAsString(), payload2);

        dir.deleteRecursively();
    }
};

static ShowApplicationStateTests showApplicationStateTestsInstance;
