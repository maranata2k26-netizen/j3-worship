#include "j3/Session.h"
#include <fstream>
#include <sstream>
namespace j3 {
bool SessionStore::saveAtomic(const SessionState&s,std::string&error) const{
 try{std::filesystem::create_directories(root_); auto tmp=root_/"last_session.tmp"; auto dst=root_/"last_session.j3s"; {std::ofstream o(tmp,std::ios::trunc); if(!o){error="Cannot open temp session";return false;} o<<s.name<<'\n'<<s.currentSong<<'\n'<<s.liveMode<<'\n'<<s.recording<<'\n'; o.flush(); if(!o){error="Session write failed";return false;}} std::error_code ec; std::filesystem::remove(dst,ec); std::filesystem::rename(tmp,dst); return true;} catch(const std::exception&e){error=e.what();return false;}
}
bool SessionStore::load(SessionState&s,std::string&error) const{
 std::ifstream i(root_/"last_session.j3s"); if(!i){error="No recovery session";return false;} std::getline(i,s.name); std::getline(i,s.currentSong); i>>s.liveMode>>s.recording; if(!i){error="Corrupt recovery session";return false;} return true;
}
}
