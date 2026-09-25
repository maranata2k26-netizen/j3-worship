#pragma once
namespace j3 {
struct RuntimeSafetyState { bool tracksRunning{false}; bool pluginsEnabled{true}; bool liveInputsEnabled{true}; bool recording{false}; };
class PanicController { public: static void engage(RuntimeSafetyState& s) noexcept { s.tracksRunning=false; s.pluginsEnabled=false; s.liveInputsEnabled=true; } };
}
