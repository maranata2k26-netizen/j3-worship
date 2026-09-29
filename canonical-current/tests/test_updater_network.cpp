#include <JuceHeader.h>
#include "../app/UpdateService.h"

#include <iostream>

int main()
{
#if JUCE_WINDOWS
    // Exercise the exact public GitHub release path that failed on a real 1.10.9
    // installation. This is deliberately a real network smoke test: it validates
    // GitHub redirects/CDN download plus SHA-256 verification.
    j3ui::AvailableUpdate update {
        j3::SemVer { 1, 11, 0 },
        "1.11.0",
        "https://github.com/maranata2k26-netizen/j3-worship/releases/download/v1.11.0/J3Worship-Setup.exe",
        "ecaa3c2931d71d0ef91d1ac9dbd83d56fdaf1c0d8d3fc2a7c7a67b81ec7ca8ed",
        {}
    };

    juce::File installer;
    juce::String error;
    const bool ok = j3ui::UpdateService::downloadAndVerify(update, installer, error);

    if (!ok)
    {
        std::cerr << "[FAIL] updater real-release download: " << error << std::endl;
        return 1;
    }

    if (!installer.existsAsFile() || installer.getSize() < 1024 * 1024)
    {
        std::cerr << "[FAIL] updater download is missing or unexpectedly small" << std::endl;
        return 1;
    }

    std::cout << "[PASS] updater real-release download + SHA256: "
              << installer.getFullPathName() << std::endl;
    installer.deleteFile();
    return 0;
#else
    std::cout << "[SKIP] updater network smoke is Windows-only" << std::endl;
    return 0;
#endif
}
