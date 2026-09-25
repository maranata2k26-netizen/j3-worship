#pragma once
#include <string>
namespace j3 {
enum class StemRole { Click, Guide, Drums, Bass, Guitar, Keys, Pad, Strings, Synth, Fx, Vocals, Other };
StemRole classifyStemName(std::string name);
const char* stemRoleName(StemRole r) noexcept;
}
