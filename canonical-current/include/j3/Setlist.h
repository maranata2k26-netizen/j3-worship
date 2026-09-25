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
    const SongRef* current() const noexcept; const SongRef* next() const noexcept;
    bool advance() noexcept; void reset() noexcept { index_=0; }
    std::size_t size() const noexcept {return songs_.size();}
private: std::string name_; std::vector<SongRef> songs_; std::size_t index_{0};
};
}
