#include "j3/Diagnostics.h"
namespace j3 {
std::vector<DiagnosticItem> Diagnostics::runCoreChecks(const AudioEngine&e){
 std::vector<DiagnosticItem> out; const auto c=e.config();
 out.push_back({"Audio Engine",e.running()?Health::Ok:Health::Review,e.running()?"Running":"Stopped"});
 out.push_back({"Sample Rate",(c.sampleRate==44100||c.sampleRate==48000||c.sampleRate==96000)?Health::Ok:Health::Error,std::to_string(static_cast<int>(c.sampleRate))+" Hz"});
 out.push_back({"Buffer",c.bufferFrames<=512?Health::Ok:Health::Review,std::to_string(c.bufferFrames)+" frames"});
 out.push_back({"Estimated round-trip",estimatedRoundTripMs(c)<20?Health::Ok:Health::Review,std::to_string(estimatedRoundTripMs(c))+" ms (buffer-only estimate)"});
 out.push_back({"Dropouts",e.dropouts()==0?Health::Ok:Health::Review,std::to_string(e.dropouts())});
 return out;
}
}
