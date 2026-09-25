#include "j3/PadEngine.h"
namespace j3 {
bool PadEngine::load(PadSample s,std::string& error){ if(s.mono.size()<2){error="Pad audio is empty"; return false;} s.crossfadeSamples=std::min(s.crossfadeSamples,s.mono.size()/2); sample_=std::move(s); pos_=0; return true; }
float PadEngine::next() noexcept { if(!playing_||sample_.mono.empty()) return 0; const auto n=sample_.mono.size(); float y=sample_.mono[pos_]; const auto cf=sample_.crossfadeSamples; if(cf>0 && pos_>=n-cf){ const float t=static_cast<float>(pos_-(n-cf))/static_cast<float>(cf); const std::size_t head=pos_-(n-cf); y=(1.0f-t)*sample_.mono[pos_]+t*sample_.mono[head]; } ++pos_; if(pos_>=n) pos_=cf>0?cf:0; return y*volume_; }
}
