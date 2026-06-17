#include <JuceHeader.h>

class ConsoleLogger final : public juce::Logger
{
    void logMessage (const juce::String& message) override
    {
        std::cerr << message << std::endl;
    }
};

static int countFailures (const juce::UnitTestRunner& runner)
{
    int failures = 0;

    for (int i = 0; i < runner.getNumResults(); ++i)
        if (const auto* result = runner.getResult (i))
            failures += result->failures;

    return failures;
}

int main (int argc, char** argv)
{
    juce::ignoreUnused (argc, argv);

    ConsoleLogger logger;
    juce::Logger::setCurrentLogger (&logger);

    juce::UnitTestRunner runner;
    runner.runTestsInCategory ("showcue");

    const auto failures = countFailures (runner);
    juce::Logger::setCurrentLogger (nullptr);

    return failures > 0 ? 1 : 0;
}
