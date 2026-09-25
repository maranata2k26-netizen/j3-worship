#pragma once
#include <filesystem>
#include <string>
namespace j3 {
struct SessionState { std::string name{"Untitled"}; std::string currentSong{}; bool liveMode{false}; bool recording{false}; };
class SessionStore {
public:
    explicit SessionStore(std::filesystem::path root):root_(std::move(root)){}
    bool saveAtomic(const SessionState& s,std::string& error) const;
    bool load(SessionState& s,std::string& error) const;
private: std::filesystem::path root_;
};
}
