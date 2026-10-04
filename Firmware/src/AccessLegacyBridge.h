//Access Legacy Bridge

//Small adapter between the AccessManager and the rest of the firmware. It no longer mirrors any state.
//On each AccessManager change it refreshes the config frontend, flags the display for redraw, and turns the
//outbound queue into the mqttState request flags that MainController's message builders act on.
//Replace the mqttState flags with direct queue consumption in MainController when it is reworked.

#pragma once

void runAccessBridgeLoop(void *pvParameters);
