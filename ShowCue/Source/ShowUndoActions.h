#pragma once
#include <functional>
#include <juce_data_structures/juce_data_structures.h>

/** Snapshot undo/redo qua lambda — không giữ con trỏ UI Component. */
class GlobalStateUndoAction final : public juce::UndoableAction
{
public:
    GlobalStateUndoAction (std::function<void()> performAction,
                           std::function<void()> undoAction)
        : performCallback (std::move (performAction)),
          undoCallback (std::move (undoAction))
    {
    }

    bool perform() override
    {
        if (performCallback)
            performCallback();

        return true;
    }

    bool undo() override
    {
        if (undoCallback)
            undoCallback();

        return true;
    }

private:
    std::function<void()> performCallback;
    std::function<void()> undoCallback;
};
