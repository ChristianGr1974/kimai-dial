#pragma once

// Public interface of the application layer: main.cpp (setup()/loop())
// only knows these two functions, no longer the state machine, the global
// AppContext, or the carousel instance in detail (those remain internal
// implementation details in state_handlers.cpp, see the anonymous
// namespace there).
namespace StateMachine {

// One-time boot sequence (display/SPIFFS/settings init, WiFi connect, or
// starting setup AP directly if no credentials are stored).
void begin();

// One tick of the state machine - called from loop() on every Arduino
// loop iteration.
void tick();

} // namespace StateMachine
