/**
 * Demo Code: PulseAudio Sink Switcher
 *
 * This demonstration code showcases the functionality of switching audio sinks
 * using the PulseAudio API. It initializes the PulseAudio manager, loads available
 * audio sinks, prompts the user to select a sink, and then switches to the chosen sink.
 *
 * Note: Ensure the PulseAudio server is running and in a good state before executing.
 */
#include "../easypulse_core.h"
#include <stdio.h>
#include <stdlib.h>

#if 0
int main() {
    // Initialize the PulseAudio Manager
    pulseaudio_manager *manager = new_manager();
    pulseaudio_manager *self = manager;

    if (!manager) {
        fprintf(stderr, "Failed to initialize PulseAudioManager.\n");
        manager->destroy(self);
        return 1;
    }

    // Display available devices to the user
    printf("Available Sinks:\n");
    for (uint32_t i = 0; i < manager->device_count; i++) {
        printf("%d. %s - %s\n", i + 1, manager->devices[i].code, manager->devices[i].description);
    }

    // Prompt the user to select a device
    printf("Enter the number of the sink you want to switch to: ");
    uint32_t choice;
    scanf("%d", &choice);


    // Validate the user's choice
    if (choice < 1 || choice > manager->device_count) {
        fprintf(stderr, "Invalid choice.\n");
        manager->destroy(self);
        return 1;
    }


    // Switch to the selected device
    if (manager->switch_device(manager, choice - 1)) {
        printf("Successfully switched to the selected device.\n");

        // Debug code to print the default device after the switch
        fprintf(stderr, "[DEBUG]: Default device after switch: %s\n", manager->active_device->code);
    }
    else {
        fprintf(stderr, "Failed to switch to the selected device.\n");
        return 1;
    }

    // Cleanup
    manager->destroy(self);

    return 0;
}
#endif

int main(void) {
    return 0;
}
