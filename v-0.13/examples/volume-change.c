/**
 * @file volume-change.c
 * @brief PulseAudio Manager demo program
 *
 * This program demonstrates the usage of the PulseAudio Manager API.
 * It creates a manager instance, lists the available output devices,
 * asks the user to select one of the output devices and to enter a master volume.
 * It then sets the master volume of the selected output device to the given value.
 *
 * @author Mbyte2
 * @date 11-07-2023
 */
#include <stdio.h>
#include "../easypulse_core.h"  // Assuming this is the header file where your API is defined

int main() {
    // Create a manager instance
    pulseaudio_manager *manager = manager_create();
    if (!manager) {
        fprintf(stderr, "Failed to create manager.\n");
        return 1;
    }

    // List the outputs
    printf("Available output devices:\n");
    for (uint32_t i = 0; i < get_output_device_count(); ++i) {
        printf("%d: %s\n", (i+1), manager->outputs[i].name);
    }

    // Ask the user to select one of the outputs
    uint32_t selected_output;
    printf("Please enter the number of the output device you want to use: ");
    scanf("%u", &selected_output);

    // Check if the selected output is valid
    if ((selected_output - 1) >= get_output_device_count()) {
        fprintf(stderr, "Invalid output device number.\n");
        manager_cleanup(manager);
        return 1;
    }

    // Ask the user to type a master volume
    int master_volume;
    printf("Please enter the master volume (0-100): ");
    scanf("%d", &master_volume);

    // Check if the master volume is valid
    if (master_volume < 0 || master_volume > 100) {
        fprintf(stderr, "Invalid master volume. It should be between 0 and 100.\n");
        manager_cleanup(manager);
        return 1;
    }

    // Set the master volume to the given value
    if (manager_set_master_volume(manager, (selected_output -1), master_volume) != 0) {
        fprintf(stderr, "Failed to set master volume.\n");
        manager_cleanup(manager);
        return 1;
    }

    printf("Master volume for output device '%s' has been set to %d.\n", manager->outputs[(selected_output-1)].name, master_volume);

    // Clean up
    manager_cleanup(manager);

    return 0;
}
