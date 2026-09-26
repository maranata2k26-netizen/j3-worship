#include <JuceHeader.h>
#include "MainComponent.h"

class J3WorshipApplication final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return "J3 Worship"; }
    const juce::String getApplicationVersion() override { return "1.4.0"; }
    bool moreThanOneInstanceAllowed() override { return false; }

    void initialise(const juce::String&) override
    {
        mainWindow = std::make_unique<MainWindow>(getApplicationName());
    }

    void shutdown() override { mainWindow.reset(); }
    void systemRequestedQuit() override { quit(); }
    void anotherInstanceStarted(const juce::String&) override {}

private:
    class MainWindow final : public juce::DocumentWindow
    {
    public:
        explicit MainWindow(juce::String name)
            : DocumentWindow(std::move(name), juce::Colour(0xff111317), allButtons)
        {
            setUsingNativeTitleBar(true);
            setResizable(true, true);
            setResizeLimits(1080, 660, 8192, 4320);
            setContentOwned(new MainComponent(), true);
            setWantsKeyboardFocus(true);

            auto initialWidth = 1600;
            auto initialHeight = 960;
            if (auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
            {
                const auto work = display->userBounds;
                const int workWidth = static_cast<int>(std::lround(work.getWidth()));
                const int workHeight = static_cast<int>(std::lround(work.getHeight()));
                initialWidth = juce::jlimit(1080, 1600, std::max(1080, workWidth - 40));
                initialHeight = juce::jlimit(660, 960, std::max(660, workHeight - 40));
            }
            centreWithSize(initialWidth, initialHeight);
            setVisible(true);
        }

        bool keyPressed(const juce::KeyPress& key) override
        {
            if (key.getKeyCode() == juce::KeyPress::F11Key)
            {
                setFullScreen(!isFullScreen());
                return true;
            }
            if (key.getKeyCode() == juce::KeyPress::escapeKey && isFullScreen())
            {
                setFullScreen(false);
                return true;
            }
            return juce::DocumentWindow::keyPressed(key);
        }

        void closeButtonPressed() override
        {
            juce::JUCEApplication::getInstance()->systemRequestedQuit();
        }
    };

    std::unique_ptr<MainWindow> mainWindow;
};

START_JUCE_APPLICATION(J3WorshipApplication)
