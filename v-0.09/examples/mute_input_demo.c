/**
 * @file mute_input_demo.c
 * @brief Demonstration program using PulseAudio to list input devices, toggle mute state.
 *
 * This program lists all available input devices managed by PulseAudio, checks
 * if they are currently muted, and allows the user to select a device to toggle
 * its mute state. The program uses the `easypulse_core` and `system_query`
 * libraries to interact with the PulseAudio server.
 *
 * Usage:
 * Run the program, and it will display a list of available input devices along
 * with their mute status. The user can then input the index of the device they
 * wish to toggle. The program will then change the mute state of the selected device.
 *
 * Example Output:
 * ```
 * Available input devices:
 * 0: Device 1 (muted: yes)
 * 1: Device 2 (muted: no)
 * Enter the index of the device you want to toggle the mute state for:
 * ```
 *
 * @author Mbyte2
 * @date November 11, 2023
 *
 * @note This program is a simple demonstration and does not handle all edge cases
 *       and errors that could arise in a full-featured application.
 */

#include <stdio.h>
#include <stdlib.h>
#include "../easypulse_core.h"
#include "../system_query.h"


int main() {
    // Initialize the PulseAudio manager
    pulseaudio_manager *manager = manager_create();
    if (!manager) {
        fprintf(stderr, "Failed to create the PulseAudio manager.\n");
        return 1;
    }

    // Display available input devices
    printf("\n***TOGGLING MUTE / UNMUTE FOR INPUT DEVICES DEMO***\n\nAvailable input devices:\n");
    for (uint32_t i = 0; i < manager->input_count; i++) {
        const char *device_name = manager->inputs[i].name;
        int is_muted = get_muted_input_status(manager->inputs[i].code);
        printf("%d: %s (muted: %s)\n", (i+1), device_name, is_muted == 1 ? "yes" : "no");
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
    if (index > manager->input_count) {
        fprintf(stderr, "Index out of range.\n");
        manager_cleanup(manager);
        return 1;
    }

    // Get the current mute state
    int current_mute_state = get_muted_input_status(manager->inputs[index-1].code);
    if (current_mute_state == -1) {
        fprintf(stderr, "Error getting the current mute state.\n");
        manager_cleanup(manager);
        return 1;
    }

    // Toggle the mute state of the selected source
    int new_mute_state = !current_mute_state;
    if (manager_toggle_input_mute(manager, (index-1), new_mute_state) != 0) {
        fprintf(stderr, "Failed to toggle the mute state.\n");
        manager_cleanup(manager);
        return 1;
    }

    printf("The mute state of '%s' has been %s.\n", manager->inputs[index-1].name, new_mute_state ? "muted" : "unmuted");

    // Clean up
    manager_cleanup(manager);
    return 0;
}
