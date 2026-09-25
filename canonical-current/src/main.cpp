#include "j3/AudioEngine.h"
#include "j3/Diagnostics.h"
#include "j3/LiveEngine.h"
#include "j3/Mixer.h"
#include "j3/Routing.h"
#include <iostream>

int main(){
 using namespace j3; std::string err; AudioEngine audio; audio.configure({48000,256,18,18},err); audio.start(err);
 Mixer mix; mix.createDrumPreset(); mix.createVocalPreset(1,3);
 RoutingGraph r; auto master=r.addNode("MASTER",ChannelRole::Master); auto click=r.addNode("CLICK",ChannelRole::Click); auto iem=r.addNode("IEM 1",ChannelRole::Aux);
 r.connect(click,iem,err); if(!r.connect(click,master,err)) std::cout<<"Safety: "<<err<<"\n";
 LiveEngine live; live.setTempo(72); live.start({"CHORUS",SectionKind::Chorus,4}); live.request({"BRIDGE",SectionKind::Bridge,4},Quantize::EndOfSection);
 for(int i=0;i<16;++i) live.tickBeat(); std::cout<<"NOW: "<<live.now()<<" NEXT: "<<live.next()<<"\n";
 for(const auto& d:Diagnostics::runCoreChecks(audio)) std::cout<<d.name<<": "<<(d.health==Health::Ok?"OK":d.health==Health::Review?"REVIEW":"ERROR")<<" - "<<d.detail<<"\n";
 std::cout<<"Channels: "<<mix.size()<<"\n"; return 0;
}
