#include <JuceHeader.h>
#include "../Source/ShowKeyboardInput.h"

class ShowKeyboardInputTests final : public juce::UnitTest
{
public:
    ShowKeyboardInputTests() : juce::UnitTest ("ShowKeyboardInput", "showcue") {}

    void runTest() override
    {
        beginTest ("playlist hotkey prefix");
        expect (showcontrol::keyboard::playlistHotkeyPrefix (true).isNotEmpty());
        expect (showcontrol::keyboard::playlistHotkeyPrefix (false).isNotEmpty());
        expect (showcontrol::keyboard::playlistHotkeyPrefix (true)
                    != showcontrol::keyboard::playlistHotkeyPrefix (false));

        beginTest ("playlist modifier routing");
       #if JUCE_MAC
        const juce::ModifierKeys cmdOnly (juce::ModifierKeys::commandModifier);
        const juce::ModifierKeys ctrlOnly (juce::ModifierKeys::ctrlModifier);
        expect (showcontrol::keyboard::playlistModifierTargetsGrid (cmdOnly));
        expect (! showcontrol::keyboard::playlistModifierTargetsCueList (cmdOnly));
        expect (showcontrol::keyboard::playlistModifierTargetsCueList (ctrlOnly));
        expect (! showcontrol::keyboard::playlistModifierTargetsGrid (ctrlOnly));
       #elif JUCE_WINDOWS
        const juce::ModifierKeys ctrlOnly (juce::ModifierKeys::ctrlModifier);
        const juce::ModifierKeys altOnly (juce::ModifierKeys::altModifier);
        expect (showcontrol::keyboard::playlistModifierTargetsGrid (ctrlOnly));
        expect (! showcontrol::keyboard::playlistModifierTargetsCueList (ctrlOnly));
        expect (showcontrol::keyboard::playlistModifierTargetsCueList (altOnly));
        expect (! showcontrol::keyboard::playlistModifierTargetsGrid (altOnly));
       #endif

        beginTest ("hotkey index mapping");
        expectEquals (showcontrol::keyboard::hotkeyIndexForKeyPress (
                          juce::KeyPress ('1')), 0);
        expectEquals (showcontrol::keyboard::hotkeyIndexForKeyPress (
                          juce::KeyPress ('Q')), 10);
    }
};

static ShowKeyboardInputTests showKeyboardInputTestsInstance;
