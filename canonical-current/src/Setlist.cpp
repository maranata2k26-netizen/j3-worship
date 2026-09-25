#include "j3/Setlist.h"
namespace j3 {
bool Setlist::move(std::size_t from,std::size_t to){if(from>=songs_.size()||to>=songs_.size())return false;auto s=std::move(songs_[from]);songs_.erase(songs_.begin()+static_cast<long>(from));songs_.insert(songs_.begin()+static_cast<long>(to),std::move(s));return true;}
const SongRef* Setlist::current() const noexcept{return index_<songs_.size()?&songs_[index_]:nullptr;} const SongRef* Setlist::next() const noexcept{return index_+1<songs_.size()?&songs_[index_+1]:nullptr;} bool Setlist::advance() noexcept{if(index_+1>=songs_.size())return false;++index_;return true;}
}
