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

int main() {
    // Initialize the PulseAudio Manager
    pulseaudio_manager *manager = new_manager();
    pulseaudio_manager *self = manager;

    if (!manager) {
        fprintf(stderr, "Failed to initialize PulseAudioManager.\n");
        manager->destroy(self);
        return 1;
    }

    if (!manager->load_sinks(manager)) {
        fprintf(stderr, "Failed to load sinks.\n");
        manager->destroy(self);
        return 1;
    }

    // Display available sinks to the user
    printf("Available Sinks:\n");
    for (int i = 0; i < manager->sink_count; i++) {
        printf("%d. %s - %s\n", i + 1, manager->sinks[i].name, manager->sinks[i].description);
    }

    // Prompt the user to select a sink
    printf("Enter the number of the sink you want to switch to: ");
    int choice;
    scanf("%d", &choice);


    // Validate the user's choice
    if (choice < 1 || choice > manager->sink_count) {
        fprintf(stderr, "Invalid choice.\n");
        manager->destroy(self);
        return 1;
    }


    // Switch to the selected sink
    if (manager->switch_sink(manager, choice - 1)) {
        printf("Successfully switched to the selected sink.\n");

        //Get active sink.
        manager->get_active_sink(manager);

        // Debug code to print the default sink after the switch
        fprintf(stderr, "[DEBUG]: Default sink after switch: %s\n", manager->active_sink_name);
    }
    else {
        fprintf(stderr, "Failed to switch to the selected sink.\n");
        return 1;
    }

    // Cleanup
    manager->destroy(self);

    return 0;
}
