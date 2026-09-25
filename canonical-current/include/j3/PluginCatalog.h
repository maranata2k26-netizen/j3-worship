#pragma once
#include <filesystem>
#include <string>
#include <vector>
namespace j3 {
struct PluginRecord { std::filesystem::path path; std::string name; std::string manufacturer{"Unknown"}; bool favorite{false}; bool quarantined{false}; int reportedLatencySamples{0}; };
class PluginCatalog {
public:
    void scan(const std::vector<std::filesystem::path>& roots);
    void quarantine(const std::filesystem::path& path);
    const std::vector<PluginRecord>& plugins() const noexcept{return plugins_;}
    std::vector<PluginRecord> search(std::string text) const;
private: std::vector<PluginRecord> plugins_;
};
}
