#include <JuceHeader.h>
#include "../app/DawWorkspace.h"

#include <cmath>
#include <iostream>

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;
    DawWorkspace workspace;
    workspace.setVisible(true);

    struct Case
    {
        int physicalWidth;
        int physicalHeight;
        double scale;
        const char* label;
    };

    const Case cases[] {
        { 1366,  768, 1.00, "1366x768 @100%" },
        { 1920, 1080, 1.00, "1920x1080 @100%" },
        { 1920, 1080, 1.25, "1920x1080 @125%" },
        { 2560, 1440, 1.50, "2560x1440 @150%" }
    };

    bool ok = true;
    for (const auto& testCase : cases)
    {
        const int logicalWidth = static_cast<int>(std::lround(testCase.physicalWidth / testCase.scale));
        const int logicalHeight = static_cast<int>(std::lround(testCase.physicalHeight / testCase.scale));

        const int workspaceWidth = std::max(1080, logicalWidth - 16);
        const int workspaceHeight = std::max(520, logicalHeight - 122);
        workspace.setSize(workspaceWidth, workspaceHeight);

        juce::String report;
        const bool pass = workspace.validateLayoutForTesting(report);
        std::cout << (pass ? "[PASS] " : "[FAIL] ") << testCase.label
                  << " -> logical workspace " << workspaceWidth << "x" << workspaceHeight
                  << " : " << report << std::endl;
        ok = ok && pass;

        auto snapshotDir = juce::File::getCurrentWorkingDirectory().getChildFile("ui-snapshots");
        snapshotDir.createDirectory();
        juce::String safeName(testCase.label);
        safeName = safeName.replaceCharacters(" @%", "___");
        auto snapshotFile = snapshotDir.getChildFile(safeName + ".png");
        juce::Image image(juce::Image::RGB, workspaceWidth, workspaceHeight, true);
        juce::Graphics imageGraphics(image);

        // Headless CI does not have a native peer, so paint the workspace and its
        // direct controls explicitly instead of relying on OS-driven component painting.
        workspace.paint(imageGraphics);
        for (int childIndex = 0; childIndex < workspace.getNumChildComponents(); ++childIndex)
        {
            auto* child = workspace.getChildComponent(childIndex);
            if (child == nullptr || !child->isVisible() || child->getBounds().isEmpty())
                continue;

            juce::Graphics::ScopedSaveState state(imageGraphics);
            imageGraphics.setOrigin(child->getPosition());
            imageGraphics.reduceClipRegion(child->getLocalBounds());
            child->paintEntireComponent(imageGraphics, true);
        }

        bool hasVisualContent = false;
        const auto black = juce::Colours::black.getARGB();
        for (int y = 0; y < image.getHeight() && !hasVisualContent; y += 32)
            for (int x = 0; x < image.getWidth(); x += 32)
                if (image.getPixelAt(x, y).getARGB() != black)
                {
                    hasVisualContent = true;
                    break;
                }

        juce::FileOutputStream output(snapshotFile);
        juce::PNGImageFormat png;
        const bool snapshotWritten = hasVisualContent && output.openedOk() && png.writeImageToStream(image, output);
        std::cout << (snapshotWritten ? "[PASS] " : "[FAIL] ")
                  << "snapshot " << snapshotFile.getFileName()
                  << (hasVisualContent ? "" : " (blank render)") << std::endl;
        ok = ok && snapshotWritten;

        const int normalWidth = std::max(1080, static_cast<int>(std::lround(workspaceWidth * 0.86)));
        const int normalHeight = std::max(520, static_cast<int>(std::lround(workspaceHeight * 0.86)));
        workspace.setSize(normalWidth, normalHeight);
        report.clear();
        const bool normalPass = workspace.validateLayoutForTesting(report);
        std::cout << (normalPass ? "[PASS] " : "[FAIL] ") << testCase.label
                  << " normal-window -> " << normalWidth << "x" << normalHeight
                  << " : " << report << std::endl;
        ok = ok && normalPass;
    }

    return ok ? 0 : 1;
}
