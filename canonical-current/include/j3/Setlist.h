#pragma once
#include <string>
#include <vector>
namespace j3 {
struct SongRef { std::string name; std::string artist; std::string key; double bpm{120}; int numerator{4}; int denominator{4}; };
class Setlist {
public:
    explicit Setlist(std::string name="Untitled") : name_(std::move(name)) {}
    void add(SongRef s){songs_.push_back(std::move(s));}
    bool move(std::size_t from,std::size_t to);
    bool remove(std::size_t at);
    bool select(std::size_t at) noexcept;
    void clear() noexcept { songs_.clear(); index_=0; }
    const SongRef* current() const noexcept; const SongRef* next() const noexcept;
    const SongRef* song(std::size_t at) const noexcept { return at<songs_.size()?&songs_[at]:nullptr; }
    bool advance() noexcept; void reset() noexcept { index_=0; }
    std::size_t currentIndex() const noexcept { return index_; }
    std::size_t size() const noexcept {return songs_.size();}
private: std::string name_; std::vector<SongRef> songs_; std::size_t index_{0};
};
}
