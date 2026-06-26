#include "state_handlers.h"

// main.cpp is deliberately minimal: the entire state machine (application
// layer) lives in state_handlers.h/.cpp. setup()/loop() here are just the
// Arduino entry point delegating to StateMachine::begin()/tick().
void setup() {
    StateMachine::begin();
}

void loop() {
    StateMachine::tick();
}
