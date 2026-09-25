#pragma once

#include <JuceHeader.h>
#include "j3/Updater.h"

#include <optional>

namespace j3ui
{
struct AvailableUpdate
{
    j3::SemVer version;
    juce::String versionText;
    juce::String downloadUrl;
    juce::String sha256;
    juce::String notes;
};

class UpdateService
{
public:
    static std::optional<AvailableUpdate> checkLatest(j3::SemVer current, juce::String& error);
    static bool downloadAndVerify(const AvailableUpdate&, juce::File& installer, juce::String& error);
    static bool launchInstallerAndRestart(const juce::File& installer, juce::String& error);

private:
    static bool requestBytes(const juce::String& url, juce::MemoryBlock& bytes, juce::String& error);
};
}
