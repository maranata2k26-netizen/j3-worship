#pragma once
#include "j3/Types.h"
#include <cstddef>
#include <string>
#include <vector>

namespace j3 {
struct Route { std::size_t from{}; std::size_t to{}; double gainDb{0.0}; bool enabled{true}; };

struct StereoOutputSelection {
    int left{-1};
    int right{-1};
    bool usedFallback{false};
    bool valid() const noexcept { return left >= 0 && right >= 0; }
};

StereoOutputSelection chooseActiveStereoOutputs(const std::vector<bool>& active,
                                                int preferredLeft,
                                                int preferredRight) noexcept;

class RoutingGraph {
public:
    std::size_t addNode(std::string name, ChannelRole role);
    bool connect(std::size_t from, std::size_t to, std::string& error);
    const std::vector<Route>& routes() const noexcept { return routes_; }
    std::string nodeName(std::size_t i) const;
private:
    struct Node { std::string name; ChannelRole role; };
    bool wouldCreateCycle(std::size_t from, std::size_t to) const;
    std::vector<Node> nodes_; std::vector<Route> routes_;
};
}
