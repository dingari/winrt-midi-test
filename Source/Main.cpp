#include <JuceHeader.h>

#include "MainComponent.h"

//==============================================================================
class WinRTMidiTest  : public JUCEApplication
{
public:
    //==============================================================================
    WinRTMidiTest() {}

    const String getApplicationName() override       { return ProjectInfo::projectName; }
    const String getApplicationVersion() override    { return ProjectInfo::versionString; }
    bool moreThanOneInstanceAllowed() override       { return true; }

    //==============================================================================
    void initialise (const String&) override
    {
        mainWindow.reset (new MainAppWindow (getApplicationName()));
    }

    void shutdown() override             { mainWindow = nullptr; }

    //==============================================================================
    void systemRequestedQuit() override                                 { quit(); }
    void anotherInstanceStarted (const String&) override                {}

private:
    class MainAppWindow    : public DocumentWindow
    {
    public:
        MainAppWindow (const String& name)
                : DocumentWindow (name, Desktop::getInstance().getDefaultLookAndFeel()
                        .findColour (ResizableWindow::backgroundColourId),
                DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar (true);
            setResizable (true, false);
            setResizeLimits (400, 400, 10000, 10000);

            setContentOwned (new MainComponent(), false);
            setVisible (true);
            setSize(600, 200);
        }

        void closeButtonPressed() override    { JUCEApplication::getInstance()->systemRequestedQuit(); }

        //==============================================================================
    private:
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainAppWindow)
    };

    std::unique_ptr<MainAppWindow> mainWindow;
};

//==============================================================================
// This macro generates the main() routine that launches the app.
START_JUCE_APPLICATION (WinRTMidiTest)
