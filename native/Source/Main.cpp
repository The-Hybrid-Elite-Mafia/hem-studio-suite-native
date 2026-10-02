#include "MainComponent.h"

class StudioApplication final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return "Hybrid Elite Mafia - Studio Suite"; }
    const juce::String getApplicationVersion() override { return "0.10.0"; }
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
            centreWithSize (1280, 720);
            setVisible (true);
        }
        void requestClose(){if(auto* studio=dynamic_cast<MainComponent*>(getContentComponent()))studio->requestClose([]{juce::JUCEApplication::getInstance()->quit();});else juce::JUCEApplication::getInstance()->quit();}
        void closeButtonPressed() override { requestClose(); }
    };
    std::unique_ptr<Window> window;
};
START_JUCE_APPLICATION (StudioApplication)
