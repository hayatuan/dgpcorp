#include <juce_gui_extra/juce_gui_extra.h>

#include "MainComponent.h"
#include "ShowAboutDialog.h"
#include "ShowTypography.h"
#include "ShowLocalization.h"
#include "ShowDisplaySleepPreventer.h"

#if JUCE_MAC

 #include "ShowControlMacWindow.h"

#endif



namespace

{

#if JUCE_MAC

/** Menu bar tối giản — Edit (Undo/Redo) + menu ShowCue qua extraAppleMenu. */

class ShowControlMacMenuBar final : public juce::MenuBarModel

{

public:

    explicit ShowControlMacMenuBar (juce::ApplicationCommandManager& managerIn)
        : commandManager (managerIn)
    {
    }

    juce::StringArray getMenuBarNames() override { return { showcontrol::localization::tr (u8"Chỉnh sửa") }; }



    juce::PopupMenu getMenuForIndex (int /*topLevelMenuIndex*/, const juce::String& menuName) override

    {

        juce::PopupMenu menu;

        if (menuName == showcontrol::localization::tr (u8"Chỉnh sửa"))
        {
            menu.addCommandItem (&commandManager, juce::StandardApplicationCommandIDs::undo);
            menu.addCommandItem (&commandManager, juce::StandardApplicationCommandIDs::redo);
        }

        return menu;

    }



    void menuItemSelected (int /*menuItemID*/, int /*topLevelMenuIndex*/) override {}

private:
    juce::ApplicationCommandManager& commandManager;

};

#endif

#if ! JUCE_MAC
/** Menu bar desktop (Windows/Linux) — File/Edit/Help ngay trong cửa sổ app. */
class ShowControlDesktopMenuBar final : public juce::MenuBarModel
{
public:
    explicit ShowControlDesktopMenuBar (juce::ApplicationCommandManager& managerIn)
        : commandManager (managerIn)
    {
    }

    juce::StringArray getMenuBarNames() override
    {
        return {
            showcontrol::localization::tr (u8"Tệp"),
            showcontrol::localization::tr (u8"Chỉnh sửa"),
            showcontrol::localization::tr (u8"Trợ giúp")
        };
    }

    juce::PopupMenu getMenuForIndex (int /*topLevelMenuIndex*/, const juce::String& menuName) override
    {
        juce::PopupMenu menu;

        if (menuName == showcontrol::localization::tr (u8"Tệp"))
        {
            menu.addCommandItem (&commandManager, ShowControlCommandIDs::importShowcuePackage);
            menu.addCommandItem (&commandManager, ShowControlCommandIDs::exportShowcuePackage);
            menu.addCommandItem (&commandManager, ShowControlCommandIDs::openPreferences);
            menu.addSeparator();
            menu.addItem (kQuitMenuItemId, showcontrol::localization::tr (u8"Thoát"));
        }
        else if (menuName == showcontrol::localization::tr (u8"Chỉnh sửa"))
        {
            menu.addCommandItem (&commandManager, juce::StandardApplicationCommandIDs::undo);
            menu.addCommandItem (&commandManager, juce::StandardApplicationCommandIDs::redo);
        }
        else if (menuName == showcontrol::localization::tr (u8"Trợ giúp"))
        {
            menu.addCommandItem (&commandManager, ShowControlCommandIDs::showAboutDialog);
            menu.addCommandItem (&commandManager, ShowControlCommandIDs::checkForUpdates);
        }

        return menu;
    }

    void menuItemSelected (int menuItemID, int /*topLevelMenuIndex*/) override
    {
        if (menuItemID == kQuitMenuItemId)
            juce::JUCEApplication::getInstance()->systemRequestedQuit();
    }

private:
    static constexpr int kQuitMenuItemId = 9001;
    juce::ApplicationCommandManager& commandManager;
};
#endif

} // namespace



//==============================================================================

class GuiAppApplication final : public juce::JUCEApplication

{

public:

    GuiAppApplication() {}



    const juce::String getApplicationName() override       { return "ShowCue"; }

    const juce::String getApplicationVersion() override
    {
        return showcontrol::about::marketingVersionLine();
    }

    bool moreThanOneInstanceAllowed() override             { return false; }



    void initialise (const juce::String& commandLine) override

    {

        juce::ignoreUnused (commandLine);

        showcontrol::typography::ensureLoaded();

        mainWindow.reset (new MainWindow (getApplicationName()));

    }



    void shutdown() override

    {

        if (mainWindow != nullptr)

            if (auto* main = dynamic_cast<MainComponent*> (mainWindow->getContentComponent()))

                main->prepareForApplicationShutdown();

        mainWindow = nullptr;
        showcontrol::display::releaseAllDisplaySleepBlocks();

    }



    void systemRequestedQuit() override

    {

        quit();

    }



    void anotherInstanceStarted (const juce::String& commandLine) override

    {

        juce::ignoreUnused (commandLine);

        if (mainWindow != nullptr)
        {
            mainWindow->setVisible (true);
            mainWindow->toFront (true);
        }

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

           #if ! JUCE_MAC
            setMenuBar (&desktopMenuBar);

            if (auto* main = dynamic_cast<MainComponent*> (getContentComponent()))
                main->syncWindowChromeWithTheme();
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

            syncDisplaySleepFromWindowState();
        }

        void resized() override
        {
            DocumentWindow::resized();
            syncDisplaySleepFromWindowState();
        }

        void maximiseButtonPressed() override
        {
            DocumentWindow::maximiseButtonPressed();
            syncDisplaySleepFromWindowState();
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

           #if ! JUCE_MAC
            setMenuBar (nullptr);
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

            extraAppleMenu.addCommandItem (&commandManager, ShowControlCommandIDs::importShowcuePackage);

            extraAppleMenu.addCommandItem (&commandManager, ShowControlCommandIDs::exportShowcuePackage);

            extraAppleMenu.addCommandItem (&commandManager, ShowControlCommandIDs::openPreferences);

            extraAppleMenu.addSeparator();

        }

       #endif



        juce::ApplicationCommandManager commandManager;

       #if JUCE_MAC

        ShowControlMacMenuBar macMenuBar { commandManager };

        juce::PopupMenu extraAppleMenu;

       #endif

       #if ! JUCE_MAC
        ShowControlDesktopMenuBar desktopMenuBar { commandManager };
       #endif

        void syncDisplaySleepFromWindowState()
        {
            displaySleepGuard.sync (isFullScreen());
        }

        showcontrol::display::FullscreenSleepGuard displaySleepGuard;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainWindow)

    };



private:

    std::unique_ptr<MainWindow> mainWindow;

};



//==============================================================================

START_JUCE_APPLICATION (GuiAppApplication)

