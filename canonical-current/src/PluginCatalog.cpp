#include "j3/PluginCatalog.h"

#include <algorithm>
#include <cctype>

namespace j3 {
namespace {
std::string lower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::string inferManufacturer(const std::filesystem::path& path)
{
    const auto haystack = lower(path.generic_string());
    if (haystack.find("fabfilter") != std::string::npos) return "FabFilter";
    if (haystack.find("waves") != std::string::npos) return "Waves";
    if (haystack.find("universal audio") != std::string::npos
        || haystack.find("/uad") != std::string::npos
        || haystack.find("\\uad") != std::string::npos) return "Universal Audio";
    if (haystack.find("native instruments") != std::string::npos
        || haystack.find("kontakt") != std::string::npos) return "Native Instruments";
    if (haystack.find("valhalla") != std::string::npos) return "Valhalla";
    if (haystack.find("izotope") != std::string::npos) return "iZotope";
    if (haystack.find("soundtoys") != std::string::npos) return "Soundtoys";
    if (haystack.find("slate digital") != std::string::npos) return "Slate Digital";
    if (haystack.find("plugin alliance") != std::string::npos
        || haystack.find("brainworx") != std::string::npos) return "Plugin Alliance";
    if (haystack.find("j3") != std::string::npos) return "J3";

    auto parent = path.parent_path().filename().string();
    if (!parent.empty())
    {
        const auto p = lower(parent);
        if (p != "vst3" && p != "common files" && p != "program files")
            return parent;
    }
    return "Unknown";
}
}

void PluginCatalog::scan(const std::vector<std::filesystem::path>& roots)
{
    plugins_.clear();
    for (const auto& root : roots)
    {
        std::error_code ec;
        if (!std::filesystem::exists(root, ec))
            continue;

        for (std::filesystem::recursive_directory_iterator it(
                 root, std::filesystem::directory_options::skip_permission_denied, ec), end;
             it != end; it.increment(ec))
        {
            if (ec)
            {
                ec.clear();
                continue;
            }

            const auto& path = it->path();
            if (lower(path.extension().string()) != ".vst3")
                continue;

            plugins_.push_back({ path, path.stem().string(), inferManufacturer(path) });
            if (it->is_directory(ec))
                it.disable_recursion_pending();
        }
    }

    std::sort(plugins_.begin(), plugins_.end(),
        [](const auto& a, const auto& b)
        {
            const auto an = lower(a.name);
            const auto bn = lower(b.name);
            if (an == bn)
                return a.path.generic_string() < b.path.generic_string();
            return an < bn;
        });

    plugins_.erase(std::unique(plugins_.begin(), plugins_.end(),
        [](const auto& a, const auto& b) { return a.path == b.path; }),
        plugins_.end());
}

void PluginCatalog::quarantine(const std::filesystem::path& path)
{
    for (auto& plugin : plugins_)
        if (plugin.path == path)
            plugin.quarantined = true;
}

std::vector<PluginRecord> PluginCatalog::search(std::string text) const
{
    const auto query = lower(std::move(text));
    std::vector<PluginRecord> out;
    for (const auto& plugin : plugins_)
    {
        if (lower(plugin.name).find(query) != std::string::npos
            || lower(plugin.manufacturer).find(query) != std::string::npos)
            out.push_back(plugin);
    }
    return out;
}
}
