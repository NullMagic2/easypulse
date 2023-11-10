#include "../easypulse_core.h"
#include "../system_query.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

int main() {
    // Initialize the PulseAudio Manager
    pulseaudio_manager *manager = manager_create();
    if (!manager) {
        fprintf(stderr, "Failed to initialize PulseAudioManager.\n");
        return 1;
    }

    // Display available output devices to the user
    printf("\n\n***OUTPUT SWITCHING DEMO***\n\nAvailable output devices:\n");
    for (uint32_t i = 0; i < manager->output_count; i++) {
        printf("%d. %s - %s\n", i + 1, manager->outputs[i].name, manager->outputs[i].code);
    }

    // Prompt the user to select a device
    printf("Enter the number of the output device you want to switch to: ");
    uint32_t choice;
    scanf("%u", &choice);


    // Validate the user's choice
    if ((choice - 1) > manager->output_count) {
        fprintf(stderr, "Invalid choice.\n");
        manager_cleanup(manager);
        return 1;
    }

    // Switch to the selected device
    if (manager_switch_default_output(manager, manager->outputs[choice - 1].index) == true) {
        printf("Successfully switched to the selected output device.\n");
    } else {
        fprintf(stderr, "Failed to switch to the selected output device.\n");
        manager_cleanup(manager);
        return 1;
    }

    // Cleanup
    manager_cleanup(manager);

    return 0;
}
