/**
 * @file main.c
 * @brief Demonstration program using PulseAudio to list output devices and toggle mute state.
 *
 * This program lists all available output devices (sinks) managed by PulseAudio, checks
 * if they are currently muted, and allows the user to select a device to toggle its mute state.
 * The program uses the `easypulse_core` library to interact with the PulseAudio server.
 *
 * Usage:
 * Run the program, and it will display a list of available output devices along with their
 * mute status. The user can then input the index of the device they wish to toggle. The program
 * will then change the mute state of the selected device.
 *
 * @note This program is a simple demonstration and does not handle all edge cases and errors
 *       that could arise in a full-featured application.
 *
 * Example Output:
 * ```
 * Available output devices:
 * 0: Device 1 (Muted: No)
 * 1: Device 2 (Muted: Yes)
 * Enter the index of the device you want to toggle the mute state for:
 * ```
 *
 * @author Mbyte2
 * @date November 11, 2023
 */
#include <stdio.h>
#include <stdlib.h>
#include "../easypulse_core.h"
#include "../system_query.h"

// Forward declaration of the toggle_output_mute function
int toggle_output_mute(pulseaudio_manager *manager, uint32_t index, int state);

int main() {
    // Initialize the PulseAudio manager
    pulseaudio_manager *manager = manager_create();
    if (!manager) {
        fprintf(stderr, "Failed to create the PulseAudio manager.\n");
        return 1;
    }

    // Display available output devices
    printf("\n***TOGGLING MUTE / UNMUTE DEMO***\n\nAvailable output devices:\n");
    for (uint32_t i = 0; i < manager->output_count; i++) {
        const char *device_name = manager->outputs[i].name;
        int is_muted = get_muted_output_status(manager->outputs[i].code);
        printf("%d: %s (muted: %s)\n", (i+1), device_name, is_muted == true ? "yes" : "no");
    }

    // Ask the user for the device index to toggle mute state
    printf("\nEnter the index of the device you want to toggle the mute state for: ");
    uint32_t index;
    if (scanf("%u", &index) != 1) {
        fprintf(stderr, "Invalid input.\n");
        manager_cleanup(manager);
        return 1;
    }

    // Check if the index is within range
    if ((index - 1) >= manager->output_count) {
        fprintf(stderr, "Index out of range.\n");
        manager_cleanup(manager);
        return 1;
    }

    // Get the current mute state
    int current_mute_state = get_muted_output_status(manager->outputs[index-1].code);
    if (current_mute_state == -1) {
        fprintf(stderr, "Error getting the current mute state.\n");
        manager_cleanup(manager);
        return 1;
    }

    // Toggle the mute state of the selected sink
    int new_mute_state = !current_mute_state;
    if (manager_toggle_output_mute(manager, (index - 1), new_mute_state) != 0) {
        fprintf(stderr, "Failed to toggle the mute state.\n");
        manager_cleanup(manager);
        return 1;
    }

    printf("The mute state of '%s' has been %s.\n", manager->outputs[index-1].name, new_mute_state ? "muted" : "unmuted");

    // Clean up
    manager_cleanup(manager);
    return 0;
}
