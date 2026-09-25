#pragma once
#include "j3/Mixer.h"
#include <string>
namespace j3 {
struct WorshipSetupRequest { int drumMics{0}; int bass{0}; int guitars{0}; int stereoKeys{0}; int leadVocals{0}; int bgv{0}; int tracks{0}; };
class AutoSetup { public: static Mixer create(const WorshipSetupRequest& request); };
}
