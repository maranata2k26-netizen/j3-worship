#include "j3/ClickEngine.h"
namespace j3 {
void ClickEngine::setTempo(double bpm){ if(bpm<20||bpm>400) throw std::invalid_argument("BPM out of range"); bpm_=bpm; }
void ClickEngine::setTimeSignature(int n,int d){ if(n<1||n>16||(d!=2&&d!=4&&d!=8&&d!=16)) throw std::invalid_argument("Invalid signature"); numerator_=n; denominator_=d; }
}
