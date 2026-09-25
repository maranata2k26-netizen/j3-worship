#include "j3/Setlist.h"
namespace j3 {
bool Setlist::move(std::size_t from,std::size_t to){if(from>=songs_.size()||to>=songs_.size())return false;auto s=std::move(songs_[from]);songs_.erase(songs_.begin()+static_cast<long>(from));songs_.insert(songs_.begin()+static_cast<long>(to),std::move(s));if(index_==from)index_=to;else if(from<index_&&to>=index_)--index_;else if(from>index_&&to<=index_)++index_;return true;}
bool Setlist::remove(std::size_t at){if(at>=songs_.size())return false;songs_.erase(songs_.begin()+static_cast<long>(at));if(songs_.empty())index_=0;else if(index_>=songs_.size())index_=songs_.size()-1;else if(at<index_)--index_;return true;}
bool Setlist::select(std::size_t at) noexcept {if(at>=songs_.size())return false;index_=at;return true;}
const SongRef* Setlist::current() const noexcept{return index_<songs_.size()?&songs_[index_]:nullptr;} const SongRef* Setlist::next() const noexcept{return index_+1<songs_.size()?&songs_[index_+1]:nullptr;} bool Setlist::advance() noexcept{if(index_+1>=songs_.size())return false;++index_;return true;}
}
