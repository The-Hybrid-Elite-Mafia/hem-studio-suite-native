#include "../Source/UI/UtilityPanelSizing.h"
#include <iostream>
#include <stdexcept>

int main() {
    try {
        hem::ui::UtilityPanelSizing sizing;
        using Size = hem::ui::UtilityPanelSizing::Size;
        const int exportPanel = 0, analysisPanel = 1, vocalPanel = 2;
        auto require = [](bool ok, const char* message) {
            if (!ok) throw std::runtime_error(message);
        };
        require(sizing.minimumFor(&exportPanel, {850, 470}) == Size{850, 470},
                "Export initial dimensions changed");
        require(sizing.minimumFor(&exportPanel, {1490, 608}) == Size{850, 470},
                "Reopening expanded Export increased its minimum");
        require(sizing.minimumFor(&analysisPanel, {900, 520}) == Size{900, 520},
                "Analysis inherited another panel's minimum");
        require(sizing.minimumFor(&analysisPanel, {2200, 1000}) == Size{900, 520},
                "Reopening expanded Analysis increased its minimum");
        for (int cycle = 0; cycle < 5; ++cycle) {
            require(sizing.minimumFor(&exportPanel, {500, 250}) == Size{850, 470},
                    "Shrinking or switching panels changed Export minimum");
            require(sizing.minimumFor(&exportPanel, {3000, 1600}) == Size{850, 470},
                    "Repeated expansion ratcheted Export minimum");
        }
        require(sizing.minimumFor(&vocalPanel, {0, 0}) == Size{600, 350},
                "Host minimum floor changed");
        require(sizing.minimumFor(&vocalPanel, {1800, 900}) == Size{600, 350},
                "Uninitialised panel minimum was not retained");
        std::cout << "PASS immutable per-panel utility minima, repeated reopen, resize and panel switching\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL " << error.what() << '\n';
        return 1;
    }
}
