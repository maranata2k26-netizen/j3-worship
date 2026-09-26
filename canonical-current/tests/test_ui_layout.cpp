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

        // Full-capacity regression: reopen a persisted 35-track x 15-scene project
        // (525 real clips), launch scene 15, and verify all 35 mixer inserts receive audio.
        auto recovery = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
            .getChildFile("J3Worship").getChildFile("daw-recovery.j3w");
        const bool hadRecovery = recovery.existsAsFile();
        const auto previousRecovery = hadRecovery ? recovery.loadFileAsString() : juce::String();

        juce::XmlElement fullProject("J3DAW");
        fullProject.setAttribute("version", 3);
        fullProject.setAttribute("bpm", 120.0);
        fullProject.setAttribute("trackCount", 35);
        fullProject.setAttribute("viewStartBeat", 0.0);
        fullProject.setAttribute("zoom", 1.0);
        fullProject.setAttribute("firstVisibleTrack", 0);
        fullProject.setAttribute("liveQuantizationBeats", 4);

        auto* tracksXml = fullProject.createNewChildElement("Tracks");
        for (int track = 0; track < 35; ++track)
        {
            auto* t = tracksXml->createNewChildElement("Track");
            t->setAttribute("index", track);
            t->setAttribute("name", "Live Track " + juce::String(track + 1));
            t->setAttribute("colour", static_cast<int>(juce::Colour::fromHSV(
                static_cast<float>(track) / 35.0f, 0.62f, 0.86f, 1.0f).getARGB()));
            t->setAttribute("gain", 1.0);
            t->setAttribute("pan", 0.0);
            t->setAttribute("mute", false);
            t->setAttribute("solo", false);
            t->setAttribute("armed", false);
            t->setAttribute("midi", false);
            t->setAttribute("mixerInsert", track);
        }

        fullProject.createNewChildElement("MidiNotes");
        auto* clipsXml = fullProject.createNewChildElement("Clips");
        int clipId = 1;
        for (int track = 0; track < 35; ++track)
        {
            for (int scene = 0; scene < 15; ++scene)
            {
                auto* x = clipsXml->createNewChildElement("Clip");
                x->setAttribute("id", clipId++);
                x->setAttribute("track", track);
                x->setAttribute("path", fixture.getFullPathName());
                x->setAttribute("startBeat", static_cast<double>(scene) * 4.0);
                x->setAttribute("lengthBeats", 0.2);
                x->setAttribute("sourceOffsetSeconds", 0.0);
                x->setAttribute("gain", 1.0);
                x->setAttribute("pan", 0.0);
                x->setAttribute("muted", false);
                x->setAttribute("loop", true);
                x->setAttribute("reversed", false);
                x->setAttribute("fadeInBeats", 0.0);
                x->setAttribute("fadeOutBeats", 0.0);
                x->setAttribute("mixerInsert", track);
                x->setAttribute("colour", static_cast<int>(juce::Colour::fromHSV(
                    static_cast<float>(track) / 35.0f, 0.62f, 0.86f, 1.0f).getARGB()));
            }
        }

        recovery.getParentDirectory().createDirectory();
        const bool fullProjectWritten = recovery.replaceWithText(fullProject.toString());
        bool fullCapacityPass = false;
        if (fullProjectWritten)
        {
            {
                DawWorkspace capacityWorkspace;
                capacityWorkspace.setSize(1400, 800);
                capacityWorkspace.prepare(48000.0, 512);
                const auto capacityModel = capacityWorkspace.liveSessionSnapshot();

                bool cellsComplete = capacityModel.tracks.size() == 35 && capacityModel.sceneCount == 15;
                if (cellsComplete)
                    for (const auto& track : capacityModel.tracks)
                        cellsComplete = cellsComplete && track.clips.size() == 15;

                capacityWorkspace.launchLiveScene(14);
                juce::AudioBuffer<float> capacityMixer(96, 512);
                capacityMixer.clear();
                capacityWorkspace.renderToMixer(capacityMixer, 48, 512);

                int routedInserts = 0;
                for (int insert = 0; insert < 35; ++insert)
                {
                    const float mag = capacityMixer.getMagnitude(insert * 2, 0, 512)
                                    + capacityMixer.getMagnitude(insert * 2 + 1, 0, 512);
                    if (mag > 1.0e-5f)
                        ++routedInserts;
                }

                const auto launched = capacityWorkspace.liveSessionSnapshot();
                fullCapacityPass = cellsComplete
                    && launched.activeScene == 14
                    && routedInserts == 35;
                capacityWorkspace.stopLiveClips();
            }
        }

        if (hadRecovery)
            recovery.replaceWithText(previousRecovery);
        else
            recovery.deleteFile();

        std::cout << (fullCapacityPass ? "[PASS] " : "[FAIL] ")
                  << "reopen 35 tracks x 15 scenes -> scene 15 -> 35 real mixer inserts" << std::endl;
        ok = ok && fullCapacityPass;

        fixture.deleteFile();
    }

    return ok ? 0 : 1;
}
