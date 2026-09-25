#pragma once
#include "j3/Diagnostics.h"
#include <filesystem>
#include <string>
#include <vector>
namespace j3 {
class DiagnosticReport { public: static bool write(const std::filesystem::path& path,const std::vector<DiagnosticItem>& items,const std::string& version,std::string& error); };
}
