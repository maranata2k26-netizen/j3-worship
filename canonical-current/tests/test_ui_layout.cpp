#include <JuceHeader.h>
#include "../app/DawWorkspace.h"

#include <cmath>
#include <iostream>

namespace
{
bool writePlaybackFixture(const juce::File& file)
{
    constexpr int sampleRate = 48000;
    constexpr int samples = 4800;
    constexpr short channels = 1;
    constexpr short bitsPerSample = 32;
    constexpr short blockAlign = channels * static_cast<short>(sizeof(float));
    constexpr int dataBytes = samples * channels * static_cast<int>(sizeof(float));

    juce::FileOutputStream output(file);
    if (!output.openedOk())
        return false;
    output.setPosition(0);
    output.truncate();

    output.write("RIFF", 4);
    output.writeInt(36 + dataBytes);
    output.write("WAVE", 4);
    output.write("fmt ", 4);
    output.writeInt(16);
    output.writeShort(3); // IEEE float
    output.writeShort(channels);
    output.writeInt(sampleRate);
    output.writeInt(sampleRate * blockAlign);
    output.writeShort(blockAlign);
    output.writeShort(bitsPerSample);
    output.write("data", 4);
    output.writeInt(dataBytes);

    for (int i = 0; i < samples; ++i)
    {
        const float t = static_cast<float>(i) / static_cast<float>(sampleRate);
        const float value = std::sin(juce::MathConstants<float>::twoPi * 440.0f * t) * 0.25f;
        if (!output.write(&value, sizeof(value)))
            return false;
    }
    output.flush();
    return output.getStatus().wasOk();
}
}

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
        {
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
        } // Destroy Graphics before reading pixels / encoding PNG.

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

    // Regression: a dropped audio file must produce real samples immediately after PLAY,
    // and it must do so through the new mixer-insert render path.
    auto fixture = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("j3-worship-playback-regression", ".wav", false);
    const bool fixtureWritten = writePlaybackFixture(fixture);
    ok = ok && fixtureWritten;
    std::cout << (fixtureWritten ? "[PASS] " : "[FAIL] ") << "playback WAV fixture" << std::endl;

    if (fixtureWritten)
    {
        workspace.setSize(1400, 800);
        workspace.prepare(48000.0, 512);
        juce::StringArray dropped { fixture.getFullPathName() };
        workspace.filesDropped(dropped, 0, 0); // beat 0, selected track
        const bool playAccepted = workspace.keyPressed(juce::KeyPress(juce::KeyPress::spaceKey));
        juce::AudioBuffer<float> mixer(96, 512);
        mixer.clear();
        workspace.renderToMixer(mixer, 48, 512);

        double energy = 0.0;
        for (int ch = 0; ch < mixer.getNumChannels(); ++ch)
        {
            const auto* data = mixer.getReadPointer(ch);
            for (int i = 0; i < mixer.getNumSamples(); ++i)
                energy += std::abs(static_cast<double>(data[i]));
        }

        const bool playbackPass = playAccepted && energy > 0.01;
        std::cout << (playbackPass ? "[PASS] " : "[FAIL] ")
                  << "dropped WAV -> PLAY -> mixer insert audio"
                  << " (energy=" << energy << ")" << std::endl;
        ok = ok && playbackPass;
        workspace.emergencyStop();

        static_assert(DawWorkspace::kLiveMaxTracks == 35);
        static_assert(DawWorkspace::kLiveMaxScenes == 15);

        auto liveModel = workspace.liveSessionSnapshot();
        const bool modelPass = liveModel.sceneCount == 15
            && !liveModel.tracks.empty()
            && !liveModel.tracks.front().clips.empty()
            && liveModel.tracks.front().clips.front().mixerInsert == 0;
        std::cout << (modelPass ? "[PASS] " : "[FAIL] ")
                  << "LIVE/CLIPS dynamic model -> real track/clip/insert + 15 scenes" << std::endl;
        ok = ok && modelPass;

        workspace.setLiveQuantizationBeats(1);
        workspace.launchLiveClip(0, 0);
        double liveEnergy = 0.0;
        double lateLoopEnergy = 0.0;
        for (int block = 0; block < 12; ++block)
        {
            mixer.clear();
            workspace.renderToMixer(mixer, 48, 512);
            double blockEnergy = 0.0;
            for (int ch = 0; ch < mixer.getNumChannels(); ++ch)
            {
                const auto* data = mixer.getReadPointer(ch);
                for (int i = 0; i < mixer.getNumSamples(); ++i)
                    blockEnergy += std::abs(static_cast<double>(data[i]));
            }
            liveEnergy += blockEnergy;
            if (block == 11)
                lateLoopEnergy = blockEnergy;
        }

        auto activeLive = workspace.liveSessionSnapshot();
        const bool liveRenderPass = workspace.liveSessionActive()
            && activeLive.editLocked
            && !activeLive.tracks.empty()
            && activeLive.tracks.front().active
            && liveEnergy > 0.01
            && lateLoopEnergy > 0.01;
        std::cout << (liveRenderPass ? "[PASS] " : "[FAIL] ")
                  << "LIVE clip -> quantized real mixer render -> continuous loop"
                  << " (energy=" << liveEnergy << ", late=" << lateLoopEnergy << ")" << std::endl;
        ok = ok && liveRenderPass;

        workspace.launchLiveScene(0);
        const auto queuedScene = workspace.liveSessionSnapshot();
        const bool sceneQueued = queuedScene.pendingScene == 0;
        std::cout << (sceneQueued ? "[PASS] " : "[FAIL] ")
                  << "scene launch queued at quantized boundary" << std::endl;
        ok = ok && sceneQueued;

        workspace.stopLiveTrack(0);
        const bool stopTrackPass = !workspace.liveSessionActive();
        std::cout << (stopTrackPass ? "[PASS] " : "[FAIL] ")
                  << "STOP TRACK clears final active LIVE clip safely" << std::endl;
        ok = ok && stopTrackPass;

        fixture.deleteFile();
    }

    return ok ? 0 : 1;
}
