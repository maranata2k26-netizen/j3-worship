#include "j3/Updater.h"
#include <algorithm>
#include <charconv>
#include <cctype>
namespace j3 {
std::optional<SemVer> Updater::parseVersion(std::string_view text) noexcept{
 while(!text.empty() && std::isspace(static_cast<unsigned char>(text.front()))) text.remove_prefix(1);
 while(!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) text.remove_suffix(1);
 if(!text.empty() && (text.front()=='v' || text.front()=='V')) text.remove_prefix(1);
 SemVer v{};
 auto readPart=[&](int& out, bool requireDot)->bool{
   if(text.empty()) return false;
   const char* begin=text.data(); const char* end=begin+text.size();
   int value=0; const auto r=std::from_chars(begin,end,value);
   if(r.ec!=std::errc{} || r.ptr==begin || value<0) return false;
   out=value;
   const auto consumed=static_cast<std::size_t>(r.ptr-begin);
   text.remove_prefix(consumed);
   if(requireDot){ if(text.empty() || text.front()!='.') return false; text.remove_prefix(1); }
   return true;
 };
 if(!readPart(v.major,true) || !readPart(v.minor,true) || !readPart(v.patch,false)) return std::nullopt;
 if(!text.empty()) return std::nullopt;
 return v;
}
bool Updater::manifestLooksSafe(const UpdateManifest&m) noexcept{
 if(m.url.rfind("https://",0)!=0) return false;
 return m.sha256.size()==64 && std::all_of(m.sha256.begin(),m.sha256.end(),[](char c){return (c>='0'&&c<='9')||(c>='a'&&c<='f')||(c>='A'&&c<='F');});
}
}
