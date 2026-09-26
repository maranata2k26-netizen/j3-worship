#include "j3/Routing.h"
#include <functional>
#include <stdexcept>

namespace j3 {

StereoOutputSelection chooseActiveStereoOutputs(const std::vector<bool>& active,
                                                int preferredLeft,
                                                int preferredRight) noexcept {
    auto activeAt = [&active](int index) {
        return index >= 0
            && index < static_cast<int>(active.size())
            && active[static_cast<std::size_t>(index)];
    };

    if (activeAt(preferredLeft) && activeAt(preferredRight))
        return {preferredLeft, preferredRight, false};

    int first = -1;
    int second = -1;
    for (int i = 0; i < static_cast<int>(active.size()); ++i) {
        if (!active[static_cast<std::size_t>(i)]) continue;
        if (first < 0) first = i;
        else { second = i; break; }
    }

    if (first < 0)
        return {};
    if (second < 0)
        second = first;
    return {first, second, true};
}

std::size_t RoutingGraph::addNode(std::string name, ChannelRole role){ nodes_.push_back({std::move(name),role}); return nodes_.size()-1; }
std::string RoutingGraph::nodeName(std::size_t i) const { return nodes_.at(i).name; }
bool RoutingGraph::wouldCreateCycle(std::size_t from, std::size_t to) const {
    std::vector<bool> seen(nodes_.size(),false);
    std::function<bool(std::size_t)> dfs=[&](std::size_t n){ if(n==from) return true; if(seen[n]) return false; seen[n]=true; for(const auto&r:routes_) if(r.enabled&&r.from==n) if(dfs(r.to)) return true; return false; };
    return dfs(to);
}
bool RoutingGraph::connect(std::size_t from, std::size_t to, std::string& error){
    if(from>=nodes_.size()||to>=nodes_.size()){error="Invalid route endpoint";return false;}
    if(from==to){error="Self routing blocked";return false;}
    const auto src=nodes_[from].role, dst=nodes_[to].role;
    if((src==ChannelRole::Click||src==ChannelRole::Guide) && dst==ChannelRole::Master){ error="SAFE MODE: Click/Guide cannot be routed directly to PA Master"; return false; }
    if(wouldCreateCycle(from,to)){error="Routing loop blocked";return false;}
    routes_.push_back({from,to,0.0,true}); return true;
}
}
