/**
 * @file switch-input.c
 * @brief Program to list and switch PulseAudio input devices.
 *
 * This program demonstrates how to use the EasyPulse library to interact with
 * PulseAudio input devices. It lists all available input devices (sources) and
 * allows the user to switch the default input device to any of the listed devices.
 *
 * The program performs the following steps:
 * 1. Initializes the PulseAudio manager using the EasyPulse library.
 * 2. Lists all available input devices with their names and internal codes.
 * 3. Prompts the user to select an input device by entering its associated number.
 * 4. Validates the user's choice to ensure it corresponds to an available device.
 * 5. Switches the default input device to the user-selected device.
 * 6. Cleans up the PulseAudio manager instance before program termination.
 *
 * Usage:
 * Run the program, and it will display a list of available input devices. Enter
 * the number corresponding to the desired input device to switch to it.
 *
 *
 * @author Mbyte2
 * @date November 10, 2023
 */

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

    // Display available input devices to the user
    printf("\n\n***INPUT SWITCHING DEMO***\n\n");

    //We will display human-friendly device name in the program
    char *device_name = get_input_name_by_code(manager->context, manager->active_input_device);

    if(!device_name) {
        fprintf(stderr, "[main()] Failed when trying to allocate memory for device name.\n");
        return 1;
    }

    printf("[Default device: %s]\n\n", device_name);
    printf("Available input devices:\n");

    for (uint32_t i = 0; i < manager->input_count; i++) {
        printf("%d. %s - %s\n", i + 1, manager->inputs[i].name, manager->inputs[i].code);
    }

    // Prompt the user to select a device
    printf("Enter the number of the input device you want to switch to: ");
    uint32_t choice;
    scanf("%u", &choice);

    // Validate the user's choice
    if ((choice - 1) >= manager->input_count) {
        fprintf(stderr, "Invalid choice.\n");
        manager_cleanup(manager);
        return 1;
    }

    // Switch to the selected device
    if (manager_switch_default_input(manager, manager->inputs[choice - 1].index) == true) {
        printf("Successfully switched to the selected input device.\n");
    } else {
        fprintf(stderr, "Failed to switch to the selected input device.\n");
        manager_cleanup(manager);
        return 1;
    }

    // Cleanup
    free(device_name);
    manager_cleanup(manager);

    return 0;
}
