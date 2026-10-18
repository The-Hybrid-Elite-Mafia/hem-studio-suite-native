#pragma once
#include <juce_core/juce_core.h>
#include <filesystem>

namespace hem::noReplaceFile {

inline std::filesystem::path nativePath(const juce::File& file) {
    return std::filesystem::u8path(file.getFullPathName().toStdString());
}

// This is only a preflight check, never permission to replace the name later.
// Unlike File::exists(), symlink_status detects a dangling symlink as occupied.
inline bool available(const juce::File& destination, std::error_code& error) {
    const auto status = std::filesystem::symlink_status(nativePath(destination), error);
    if (error == std::errc::no_such_file_or_directory) error.clear();
    return !error && status.type() == std::filesystem::file_type::not_found;
}

// The filesystem atomically creates the destination name or fails if ANY
// entry already owns it. Never fall back to JUCE move/replace, rename, or copy:
// those can replace another writer's file. The closed sibling source remains
// intact on both success and failure; its owner removes it only after success.
// Filesystems without hard-link support fail safely with the source retained.
inline std::error_code publish(const juce::File& source, const juce::File& destination) {
    std::error_code error;
    std::filesystem::create_hard_link(nativePath(source), nativePath(destination), error);
    return error;
}

} // namespace hem::noReplaceFile
