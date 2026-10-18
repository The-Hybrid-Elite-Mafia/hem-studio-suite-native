#include "MainComponent.h"

class StudioApplication final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return "Hybrid Elite Mafia - Studio Suite"; }
    const juce::String getApplicationVersion() override { return ProjectInfo::versionString; }
    void initialise (const juce::String&) override { window = std::make_unique<Window> (getApplicationName()); }
    void shutdown() override { window.reset(); }
    void systemRequestedQuit() override { if(window)window->requestClose();else quit(); }
private:
    class Window final : public juce::DocumentWindow
    {
    public:
        explicit Window (const juce::String& name)
            : DocumentWindow (name, juce::Colour (0xff020508), DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar (true);
            setContentOwned (new MainComponent(), true);
            setResizable (true, false);
            setResizeLimits (768, 432, 7680, 4320);
            // Leave room for the native frame/title at the actual display scale.
            // A 1280x720 client at 200% exceeded the 2560x1504 work area and
            // placed part of the title bar above the visible desktop.
            double fit = 1.0;
            if (const auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
                fit = juce::jmin (1.0, (display->userArea.getWidth() - 32.0) / 1280.0,
                                 (display->userArea.getHeight() - 64.0) / 720.0);
            fit = juce::jmax (0.6, fit);
            centreWithSize (juce::roundToInt (1280 * fit), juce::roundToInt (720 * fit));
            setVisible (true);
        }
        void requestClose(){if(auto* studio=dynamic_cast<MainComponent*>(getContentComponent()))studio->requestClose([]{juce::JUCEApplication::getInstance()->quit();});else juce::JUCEApplication::getInstance()->quit();}
        void closeButtonPressed() override { requestClose(); }
    };
    std::unique_ptr<Window> window;
};
START_JUCE_APPLICATION (StudioApplication)
