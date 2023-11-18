/**
 * @file list_sinks.c
 * @brief PulseAudio Sink Listing Program
 *
 * This program demonstrates the use of the EasyPulse API to list all available
 * PulseAudio sink outputs (such as speakers or headphones) on the system.
 * It creates an instance of pulseaudio_manager, initializes it, and iterates
 * through all the available sink outputs, displaying their details.
 *
 * Usage:
 *   Compile and run this program to list all the PulseAudio sink outputs.
 *   Ensure that the PulseAudio development libraries are installed and linked
 *   correctly when compiling this program.
 *
 * Compilation (example):
 *   gcc -o list_sinks list_sinks.c -lpulse -lpulse-simple
 *
 * Author: Mbyte2
 * Date: November 18, 2023
 */

#include "../easypulse_core.h"
#include <stdio.h>

int main() {
    pa_card_profile_info *default_profile = NULL;

    // Create and initialize the pulseaudio_manager
    pulseaudio_manager *manager = manager_create();
    if (!manager) {
        fprintf(stderr, "Failed to create PulseAudio manager.\n");
        return -1;
    }

    if (manager->pa_ready != 1) {
        fprintf(stderr, "PulseAudio manager is not ready.\n");
        manager_cleanup(manager);
        return -1;
    }

    // Check if output devices are loaded
    if (!manager->outputs) {
        fprintf(stderr, "Output devices are not loaded.\n");
        manager_cleanup(manager);
        return -1;
    }

    printf("Listing available sink outputs:\n");

    for (uint32_t i = 0; i < manager->output_count; ++i) {
        default_profile = get_active_profile(manager->context, manager->outputs[i].index);
        printf("Sink Index: %u\n", manager->outputs[i].index);
        printf("Name: %s\n", manager->outputs[i].name);
        printf("Description: %s\n", manager->outputs[i].code);
        printf("Sample Rate: %d\n", manager->outputs[i].sample_rate);
        printf("Channels: Min %d, Max %d\n", manager->outputs[i].min_channels, manager->outputs[i].max_channels);

        if (default_profile) {
            printf("Active Profile: %s\n", default_profile->name ? default_profile->name : "Unknown");
        } else {
            printf("Active Profile: Unknown\n");
            printf("\n");
            //free(default_profile);
        }
    }

    // Cleanup
    manager_cleanup(manager);
    return 0;
}
