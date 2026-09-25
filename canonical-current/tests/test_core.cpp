#include "j3/AudioDevice.h"
#include "j3/AmbientPad.h"
#include "j3/AutoSetup.h"
#include "j3/DiagnosticReport.h"
#include "j3/MonitorMix.h"
#include "j3/PluginCatalog.h"
#include "j3/AudioEngine.h"
#include "j3/ClickEngine.h"
#include "j3/ClickGenerator.h"
#include "j3/Diagnostics.h"
#include "j3/Dsp.h"
#include "j3/LiveEngine.h"
#include "j3/MidiMap.h"
#include "j3/Mixer.h"
#include "j3/Multitrack.h"
#include "j3/PadEngine.h"
#include "j3/Panic.h"
#include "j3/Recording.h"
#include "j3/Routing.h"
#include "j3/Scene.h"
#include "j3/Session.h"
#include "j3/Setlist.h"
#include "j3/SpscRing.h"
#include "j3/Updater.h"
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {
void check(bool condition, const char* what) {
    if (!condition) throw std::runtime_error(std::string("CHECK FAILED: ") + what);
}
std::uintmax_t fileSize(const std::filesystem::path& p) {
    std::error_code ec; const auto s=std::filesystem::file_size(p,ec); if(ec) return 0; return s;
}
}

int main() {
    using namespace j3;
    try {
        std::string e;

        MockAudioDeviceProvider provider;
        AudioDeviceManager dm(provider); dm.refresh();
        check(dm.devices().size() == 3, "device enumeration simulation");
        auto focusrite = dm.findById("mock-focusrite");
        check(focusrite.has_value(), "generic Focusrite-like ASIO device lookup");
        check(dm.supports(*focusrite, {48000,128,8,8}, e), "device capability acceptance");
        e.clear(); check(!dm.supports(*focusrite, {48000,128,64,8}, e), "device capability rejection");

        AudioEngine audio;
        e.clear(); check(audio.configure({48000,256,18,18},e), "audio configure");
        check(audio.start(e), "audio start");
        check(audio.running(), "audio running");
        audio.simulateDeviceDisconnect(); check(!audio.running(), "device disconnect stops engine");
        check(audio.reconnect(e), "device reconnect");
        audio.reportDropout(); check(audio.dropouts()==1, "dropout counter");

        RoutingGraph g;
        auto master=g.addNode("MASTER",ChannelRole::Master), click=g.addNode("CLICK",ChannelRole::Click), guide=g.addNode("GUIDE",ChannelRole::Guide), iem=g.addNode("IEM",ChannelRole::Aux), bus=g.addNode("BUS",ChannelRole::Bus);
        e.clear(); check(g.connect(click,iem,e), "click to IEM allowed");
        e.clear(); check(!g.connect(click,master,e), "click direct to PA blocked");
        e.clear(); check(!g.connect(guide,master,e), "guide direct to PA blocked");
        e.clear(); check(g.connect(iem,bus,e), "IEM to bus allowed for graph test");
        e.clear(); check(!g.connect(bus,iem,e), "routing cycle blocked");

        Mixer drums; drums.createDrumPreset(false);
        check(drums.size()==9, "drum preset channel count");
        check(drums.channel(0).name=="Kick", "drum preset names");
        const auto hatPanDrummer=drums.channel(2).pan;
        Mixer audience; audience.createDrumPreset(true);
        check(audience.channel(2).pan == -hatPanDrummer, "audience perspective flips stereo");
        Mixer vocals; vocals.createVocalPreset(1,3);
        check(vocals.size()==5, "vocal preset count");
        check(vocals.channel(0).pan==0.0, "lead vocal centered");
        check(std::abs(vocals.channel(1).pan)<=0.35 && std::abs(vocals.channel(3).pan)<=0.35, "BGV pans conservative");

        SceneManager scenes;
        vocals.channel(0).faderDb=-3; vocals.channel(1).mute=true;
        const auto scene=scenes.capture("WORSHIP",vocals);
        vocals.channel(0).faderDb=-20; vocals.channel(1).mute=false;
        scenes.recall(scene,vocals,{true,false,false,false,false});
        check(vocals.channel(0).faderDb==-20, "safe channel protected during scene recall");
        check(vocals.channel(1).mute, "scene recall restores non-safe channel");

        LiveEngine live; live.setTempo(120,4); live.start({"CHORUS",SectionKind::Chorus,4});
        live.request({"BRIDGE",SectionKind::Bridge,4},Quantize::EndOfSection);
        for(int i=0;i<15;++i) live.tickBeat(); check(live.now()=="CHORUS", "end-of-section transition does not cut early");
        live.tickBeat(); check(live.now()=="BRIDGE", "end-of-section transition lands on boundary");
        live.enterFreePad(); for(int i=0;i<4;++i) live.tickBeat(); check(live.now()=="FREE / PAD", "free/pad quantized entry");
        live.stop(); check(live.now()=="STOPPED" && live.next().empty(), "live transport stop clears current and queued section");

        ClickEngine c; c.setTempo(100); c.setTimeSignature(4,4);
        check(c.isAccent(0), "click accent on downbeat"); check(!c.isAccent(1), "click non-accent beat");
        check(std::abs(c.secondsPerBeat()-0.6)<1e-9, "click tempo timing");

        ClickGenerator clickGen; clickGen.prepare(48000); clickGen.setTempo(120); clickGen.setTimeSignature(4,4); clickGen.setSubdivision(1); clickGen.setLevel(0.4f); clickGen.setEnabled(true);
        std::vector<float> clickAudio(48000); const int beatEvents=clickGen.process(clickAudio.data(),static_cast<int>(clickAudio.size()));
        double clickEnergy=0.0; for(float v:clickAudio){check(std::isfinite(v),"click generator finite");clickEnergy+=std::abs(v);}
        check(beatEvents==2,"click generator beat timing at 120 BPM"); check(clickEnergy>1.0,"click generator produces audible pulses");
        clickGen.setEnabled(false); std::fill(clickAudio.begin(),clickAudio.end(),0.0f); check(clickGen.process(clickAudio.data(),static_cast<int>(clickAudio.size()))==0,"disabled click produces no beat events");

        ChannelDsp dsp; dsp.prepare(48000); dsp.setHpf(80); dsp.setLpf(16000); dsp.setEqBand(0,250,1.2,3); dsp.setGate(-70); dsp.setCompressor(-12,3); dsp.setDenoise(0.3,-55);
        double energy=0; for(int i=0;i<500000;++i){float x=0.2f*std::sin(2.0*3.141592653589793*440.0*i/48000.0); const float y=dsp.process(x); check(std::isfinite(y), "DSP finite output under stress"); energy += std::abs(y);} check(energy>1.0, "DSP produces audio");

        AmbientPad ambient; ambient.prepare(48000); ambient.setRootMidi(67); ambient.setMinor(false); ambient.setVolume(0.25f); ambient.setEnabled(true);
        std::vector<float> ambientL(48000), ambientR(48000); ambient.process(ambientL.data(),ambientR.data(),static_cast<int>(ambientL.size()));
        double ambientEnergy=0.0; for(std::size_t i=0;i<ambientL.size();++i){check(std::isfinite(ambientL[i])&&std::isfinite(ambientR[i]),"ambient pad finite");ambientEnergy+=std::abs(ambientL[i])+std::abs(ambientR[i]);}
        check(ambientEnergy>10.0,"ambient pad produces sustained audio");
        ambient.setEnabled(false); std::fill(ambientL.begin(),ambientL.end(),0.0f); std::fill(ambientR.begin(),ambientR.end(),0.0f);
        for(int i=0;i<48000*2;++i){float l=0,r=0;ambient.process(&l,&r,1);} // let the safety fade reach silence
        ambient.process(ambientL.data(),ambientR.data(),static_cast<int>(ambientL.size()));
        double tailEnergy=0.0; for(float v:ambientL) tailEnergy+=std::abs(v); check(tailEnergy<0.05,"ambient pad fades safely to silence");

                PadEngine pad; PadSample ps; ps.key="G"; ps.crossfadeSamples=64; ps.mono.resize(4096); for(std::size_t i=0;i<ps.mono.size();++i) ps.mono[i]=static_cast<float>(std::sin(2*3.141592653589793*110.0*i/48000.0));
        e.clear(); check(pad.load(std::move(ps),e), "pad load"); pad.play(); double padEnergy=0; for(int i=0;i<20000;++i){const float y=pad.next();check(std::isfinite(y),"pad finite looping");padEnergy+=std::abs(y);} check(padEnergy>10,"pad loops continuously");

        check(classifyStemName("01 CLICK.wav")==StemRole::Click, "stem click classification");
        check(classifyStemName("BGV 1.wav")==StemRole::Vocals, "stem vocal classification");
        check(classifyStemName("Electric Guitar L.wav")==StemRole::Guitar, "stem guitar classification");

        Setlist set("Sunday 20:00"); set.add({"Song A","Artist","G",72}); set.add({"Song B","Artist","A",76}); set.add({"Song C","Artist","B",80});
        check(set.current() && set.current()->name=="Song A", "setlist current"); check(set.next() && set.next()->name=="Song B", "setlist next");
        check(set.move(2,1), "setlist drag reorder model"); check(set.next() && set.next()->name=="Song C", "setlist reordered");
        check(set.select(1) && set.current() && set.current()->name=="Song C", "setlist direct select");
        check(set.remove(0) && set.size()==2, "setlist remove"); check(set.current() && set.current()->name=="Song C", "setlist index remains stable after earlier removal");
        check(set.advance(), "setlist advance");

        MidiMap midi; midi.assign({1,64,127,true},"CHORUS"); auto action=midi.actionFor({1,64,1,true}); check(action && *action=="CHORUS", "MIDI mapping ignores value for key ordering semantics");

        MonitorMixer monitors(12,8); monitors.setSend(0,0,-3.0); monitors.setSend(0,1,-8.0); check(monitors.copyMix(0,1), "copy IEM mix"); check(monitors.mix(1).sends[0].levelDb==-3.0, "copied IEM send levels"); check(monitors.mix(1).name=="IEM 2", "copy mix preserves destination name");

        auto setup=AutoSetup::create({8,1,1,1,1,3,2}); check(setup.size()>=18, "auto setup creates worship channel layout");

        RuntimeSafetyState safety{true,true,true,true}; PanicController::engage(safety); check(!safety.tracksRunning && !safety.pluginsEnabled && safety.liveInputsEnabled, "panic keeps live inputs and disables risky layers");

        SpscRing<int,8> ring; for(int i=0;i<7;++i)check(ring.push(i),"SPSC push"); check(!ring.push(8),"SPSC bounded overflow"); for(int i=0;i<7;++i){int v=-1;check(ring.pop(v),"SPSC pop");check(v==i,"SPSC order");}

        const auto tmp=std::filesystem::temp_directory_path()/"j3worship_test"; std::filesystem::remove_all(tmp);
        SessionStore ss(tmp); SessionState state{"Sunday 20:00","Song A",true,false}, loaded;
        e.clear(); check(ss.saveAtomic(state,e), "atomic recovery save"); check(ss.load(loaded,e), "recovery load"); check(loaded.name==state.name && loaded.currentSong==state.currentSong, "recovery content");

        {
            MultiTrackRecorder rec; std::vector<std::string> names={"Kick","Lead Vocal"};
            e.clear(); check(rec.start(tmp/"recording",48000,names,e), "multitrack recorder start");
            std::vector<float> a(256),b(256); for(int i=0;i<256;++i){a[i]=0.1f*std::sin(2*3.141592653589793*60*i/48000.0);b[i]=0.1f*std::sin(2*3.141592653589793*220*i/48000.0);} const float* ptrs[]={a.data(),b.data()};
            for(int block=0;block<100;++block){ while(!rec.submit(ptrs,2,256)) std::this_thread::yield(); }
            e.clear(); check(rec.stop(e), "multitrack recorder stop/finalize"); check(rec.blocksWritten()==100,"recording worker wrote all blocks");
            check(fileSize(tmp/"recording"/"Kick.wav")>44,"Kick WAV created"); check(fileSize(tmp/"recording"/"Lead Vocal.wav")>44,"Lead Vocal WAV created");
        }

        {
            const auto pluginRoot=tmp/"plugins"; std::filesystem::create_directories(pluginRoot/"TestVerb.vst3"); std::ofstream(pluginRoot/"Another.vst3").put('x');
            PluginCatalog catalog; catalog.scan({pluginRoot}); check(catalog.plugins().size()==2,"VST3 catalog discovers bundles/files"); auto found=catalog.search("verb"); check(found.size()==1,"VST3 catalog search"); catalog.quarantine(found[0].path); auto q=catalog.search("verb"); check(q[0].quarantined,"plugin quarantine state");
        }

        UpdateManifest um{{1,1,0},"https://updates.example.com/J3Worship-1.1.0.exe",std::string(64,'a'),"Fixes"};
        check(Updater::updateAvailable({1,0,0},um), "semantic update available"); check(Updater::manifestLooksSafe(um), "HTTPS/hash update manifest");
        const auto parsedV = Updater::parseVersion("v1.2.3");
        check(parsedV.has_value() && parsedV->major==1 && parsedV->minor==2 && parsedV->patch==3, "parse GitHub release semantic version");
        check(!Updater::parseVersion("v1.2").has_value(), "reject incomplete release version");
        check(!Updater::safeToInstall(true,false,false), "no update during live"); check(!Updater::safeToInstall(false,true,false), "no update during recording"); check(Updater::safeToInstall(false,false,false), "update safe while idle");

        const auto diag=Diagnostics::runCoreChecks(audio); check(!diag.empty(),"diagnostics produced results");
        e.clear(); check(DiagnosticReport::write(tmp/"J3-Diagnostic.txt",diag,"0.2.0",e),"diagnostic report write"); check(fileSize(tmp/"J3-Diagnostic.txt")>20,"diagnostic report content");
        std::filesystem::remove_all(tmp);
        std::cout << "All J3 core tests passed (Release checks active)\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << ex.what() << '\n'; return 1;
    }
}
