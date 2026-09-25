#include "j3/PluginCatalog.h"
#include <algorithm>
#include <cctype>
namespace j3 {
static std::string lower(std::string s){std::transform(s.begin(),s.end(),s.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});return s;}
void PluginCatalog::scan(const std::vector<std::filesystem::path>& roots){plugins_.clear();for(const auto&root:roots){std::error_code ec;if(!std::filesystem::exists(root,ec))continue;for(std::filesystem::recursive_directory_iterator it(root,std::filesystem::directory_options::skip_permission_denied,ec),end;it!=end;it.increment(ec)){if(ec){ec.clear();continue;}const auto&p=it->path();if(lower(p.extension().string())==".vst3"){plugins_.push_back({p,p.stem().string()});if(it->is_directory(ec))it.disable_recursion_pending();}}}std::sort(plugins_.begin(),plugins_.end(),[](const auto&a,const auto&b){return a.name<b.name;});plugins_.erase(std::unique(plugins_.begin(),plugins_.end(),[](const auto&a,const auto&b){return a.path==b.path;}),plugins_.end());}
void PluginCatalog::quarantine(const std::filesystem::path&p){for(auto&x:plugins_)if(x.path==p)x.quarantined=true;}
std::vector<PluginRecord> PluginCatalog::search(std::string text)const{const auto q=lower(std::move(text));std::vector<PluginRecord> out;for(const auto&p:plugins_)if(lower(p.name).find(q)!=std::string::npos||lower(p.manufacturer).find(q)!=std::string::npos)out.push_back(p);return out;}
}
