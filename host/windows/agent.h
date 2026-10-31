// The display agent: deskstrate-host.exe --agent, started by the service as SYSTEM in the
// console session, so it can reach the sign-in and lock screens as well as the user's
// desktop. It reads one command per line on stdin (an anonymous pipe from the service)
// and runs it with displays::. It exits when the pipe closes.
#pragma once

#include <cstdint>

namespace agent {

int run();  // agent side: --agent

// Service side. Starts an agent in the active console session if none is running there
// (first use, after it died, or after a session change), then hands it the command.
void sleep_displays();
void wake_displays();
bool set_topology(uint8_t topo);  // true once handed over: the agent logs the outcome
void stop();                      // ends the agent, e.g. when the console session changes

}  // namespace agent
