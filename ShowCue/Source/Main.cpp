#include <juce_gui_extra/juce_gui_extra.h>

#include "MainComponent.h"

#if JUCE_MAC

 #include "ShowControlMacWindow.h"

#endif



namespace

{

#if JUCE_MAC

/** Menu bar tối giản — không có tab File/Edit/Settings; menu ShowCue qua extraAppleMenu. */

class ShowControlMacMenuBar final : public juce::MenuBarModel

{

public:

    juce::StringArray getMenuBarNames() override { return {}; }



    juce::PopupMenu getMenuForIndex (int /*topLevelMenuIndex*/, const juce::String& /*menuName*/) override

    {

        return {};

    }



    void menuItemSelected (int /*menuItemID*/, int /*topLevelMenuIndex*/) override {}

};

#endif

} // namespace



//==============================================================================

class GuiAppApplication final : public juce::JUCEApplication

{

public:

    GuiAppApplication() {}



    const juce::String getApplicationName() override       { return "ShowCue"; }

    const juce::String getApplicationVersion() override    { return "1.0.0-beta"; }

    bool moreThanOneInstanceAllowed() override             { return true; }



    void initialise (const juce::String& commandLine) override

    {

        juce::ignoreUnused (commandLine);

        mainWindow.reset (new MainWindow (getApplicationName()));

    }



    void shutdown() override

    {

        if (mainWindow != nullptr)

            if (auto* main = dynamic_cast<MainComponent*> (mainWindow->getContentComponent()))

                main->prepareForApplicationShutdown();

        mainWindow = nullptr;

    }



    void systemRequestedQuit() override

    {

        quit();

    }



    void anotherInstanceStarted (const juce::String& commandLine) override

    {

        juce::ignoreUnused (commandLine);

    }



    class MainWindow final : public juce::DocumentWindow

    {

    public:

        explicit MainWindow (juce::String name)

            : DocumentWindow (name,

                              juce::Desktop::getInstance().getDefaultLookAndFeel()

                                                          .findColour (backgroundColourId),

                              allButtons)

        {

            setUsingNativeTitleBar (true);

            auto* main = new MainComponent();

            setContentOwned (main, true);



            commandManager.registerAllCommandsForTarget (main);

            commandManager.setFirstCommandTarget (main);



           #if JUCE_MAC

            macMenuBar.setApplicationCommandManagerToWatch (&commandManager);

            rebuildExtraAppleMenu();

            showcontrol::mac::registerMacMenuBarHooks ([this] { rebuildExtraAppleMenu(); });

            juce::MenuBarModel::setMacMainMenu (&macMenuBar, &extraAppleMenu);

           #endif



           #if JUCE_IOS || JUCE_ANDROID

            setFullScreen (true);

           #else

            setResizable (true, true);

            centreWithSize (1280, 800);

           #endif



            setVisible (true);



           #if JUCE_MAC

            juce::Component::SafePointer<MainWindow> safeWindow (this);

            juce::MessageManager::callAsync ([safeWindow]

            {

                if (safeWindow != nullptr)

                    showcontrol::mac::applyFarragoFullSizeContentView (*safeWindow);

            });

           #endif

        }



        void visibilityChanged() override

        {

            DocumentWindow::visibilityChanged();



           #if JUCE_MAC

            if (isShowing())

                showcontrol::mac::applyFarragoFullSizeContentView (*this);

           #endif

        }



        ~MainWindow() override

        {

           #if JUCE_MAC

            showcontrol::mac::registerMacMenuBarHooks ({});

            juce::MenuBarModel::setMacMainMenu (nullptr);

           #endif

        }



        void closeButtonPressed() override

        {

            JUCEApplication::getInstance()->systemRequestedQuit();

        }



        bool keyPressed (const juce::KeyPress& key) override

        {

            const auto mods = key.getModifiers();



            if (mods.isCommandDown() && key.getTextCharacter() == ',')

            {

                if (auto* main = dynamic_cast<MainComponent*> (getContentComponent()))

                {

                    main->showPreferencesDialog();

                    return true;

                }

            }



            return DocumentWindow::keyPressed (key);

        }



    private:

       #if JUCE_MAC

        void rebuildExtraAppleMenu()

        {

            extraAppleMenu = {};

            extraAppleMenu.addCommandItem (&commandManager, ShowControlCommandIDs::showAboutDialog);

            extraAppleMenu.addCommandItem (&commandManager, ShowControlCommandIDs::checkForUpdates);

            extraAppleMenu.addSeparator();

            extraAppleMenu.addCommandItem (&commandManager, ShowControlCommandIDs::openPreferences);

            extraAppleMenu.addSeparator();

        }

       #endif



        juce::ApplicationCommandManager commandManager;

       #if JUCE_MAC

        ShowControlMacMenuBar macMenuBar;

        juce::PopupMenu extraAppleMenu;

       #endif



        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainWindow)

    };



private:

    std::unique_ptr<MainWindow> mainWindow;

};



//==============================================================================

START_JUCE_APPLICATION (GuiAppApplication)

