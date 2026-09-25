#include "j3/Updater.h"
#include <algorithm>
namespace j3 {
bool Updater::manifestLooksSafe(const UpdateManifest&m) noexcept{
 if(m.url.rfind("https://",0)!=0) return false;
 return m.sha256.size()==64 && std::all_of(m.sha256.begin(),m.sha256.end(),[](char c){return (c>='0'&&c<='9')||(c>='a'&&c<='f')||(c>='A'&&c<='F');});
}
}
