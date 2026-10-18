#pragma once
#include <algorithm>
#include <unordered_map>

namespace hem::ui {
// Message-thread state for host-owned panels that persist until host teardown.
// The initial size is intrinsic; later viewport expansion is never a new minimum.
class UtilityPanelSizing {
public:
    struct Size {
        int width, height;
        bool operator==(const Size&) const = default;
    };
    Size minimumFor(const void* panel, Size initialSize) {
        const auto entry = minimums.try_emplace(panel, Size{
            std::max(600, initialSize.width), std::max(350, initialSize.height)});
        return entry.first->second;
    }
private:
    std::unordered_map<const void*, Size> minimums;
};
} // namespace hem::ui
