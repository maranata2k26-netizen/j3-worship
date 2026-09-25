#pragma once
#include "j3/Types.h"
#include <optional>
#include <string>
namespace j3 {
struct UpdateManifest { SemVer version; std::string url; std::string sha256; std::string notes; };
class Updater {
public:
    static bool updateAvailable(SemVer current,const UpdateManifest&m) noexcept {return m.version>current;}
    static bool safeToInstall(bool liveMode,bool recording,bool sessionActive) noexcept {return !liveMode&&!recording&&!sessionActive;}
    static bool manifestLooksSafe(const UpdateManifest&m) noexcept;
};
}
