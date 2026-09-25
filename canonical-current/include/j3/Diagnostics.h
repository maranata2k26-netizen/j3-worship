#pragma once
#include "j3/AudioEngine.h"
#include "j3/Types.h"
#include <string>
#include <vector>
namespace j3 {
struct DiagnosticItem { std::string name; Health health; std::string detail; };
class Diagnostics {
public: static std::vector<DiagnosticItem> runCoreChecks(const AudioEngine& engine);
};
}
