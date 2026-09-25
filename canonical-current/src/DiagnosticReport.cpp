#include "j3/DiagnosticReport.h"
#include <fstream>
namespace j3 {
bool DiagnosticReport::write(const std::filesystem::path&p,const std::vector<DiagnosticItem>&items,const std::string&version,std::string&error){try{if(!p.parent_path().empty())std::filesystem::create_directories(p.parent_path());std::ofstream o(p,std::ios::trunc);if(!o){error="Cannot create diagnostic report";return false;}o<<"J3 DIAGNOSTIC REPORT\nVersion: "<<version<<"\n\n";for(const auto&i:items){const char*s=i.health==Health::Ok?"OK":i.health==Health::Review?"REVIEW":"ERROR";o<<"["<<s<<"] "<<i.name<<" - "<<i.detail<<"\n";}o.flush();if(!o){error="Diagnostic report write failed";return false;}return true;}catch(const std::exception&e){error=e.what();return false;}}
}
