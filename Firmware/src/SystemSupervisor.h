//System Supervisor

//Lifecycle housekeeping that is not access control (this is what was left of MachineStateController):
// * Reset if the network has been unavailable for a long time and nobody is using the device
// * Restart when unused / scheduled restart
// * Periodic status message timer
// * Tell the AccessManager when the network goes away or comes back

#pragma once

void runSystemSupervisorLoop(void *pvParameters);
bool anyChannelMatcheschannelState(String targetState); //Legacy helper, reads the mirrored channels