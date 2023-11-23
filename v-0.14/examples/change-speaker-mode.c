/**
 * @file sample_program.c
 * @brief PulseAudio Mode Selector
 *
 * This program is designed to manage the audio mode of the active PulseAudio sink.
 *
 * The program performs the following tasks:
 * - Initializes the PulseAudio manager.
 * - Detects and displays the current mode of the active device.
 * - Lists audio modes supported by the active device based on its number of channels.
 * - Prompts the user to select a desired mode from the list.
 * - Sets the active device to the mode selected by the user.
 *
 * The available modes include: mono, stereo, 4.0, 5.0, 5.1, and 7.1. The program lists
 * modes dynamically, ensuring that only those supported by the active device are presented.
 *
 * @date 10-15-2023 (creation date)
 */

#if 0
#include "../easypulse_core.h"
#include <stdio.h>

int main(void) {
    // Initialize the pulseaudio_manager
    pulseaudio_manager *manager = new_manager();
    //pulseaudio_manager *self = manager;

    if (!manager) {
        fprintf(stderr, "Failed to initialize PulseAudio manager.\n");
        return 1;
    }


    // Detect the current mode of the active device
    const char* current_mode = manager->get_device_mode_by_code(manager, manager->active_device->code);
    printf("Current mode of the active device (%s): %s\n", manager->active_device->code, current_mode);

    // List available modes based on the number of channels of the active device
    printf("\nAvailable modes:\n");
    printf("1. mono\n");
    printf("2. stereo\n");

    int max_choice = 2;  // To keep track of the maximum mode choice number
    uint32_t channels = manager->active_device->number_of_channels;

    if (channels >= 4) {
        printf("3. 4.0\n");
        max_choice = 3;
    }
    if (channels >= 5) {
        printf("4. 5.0\n");
        max_choice = 4;
    }
    if (channels >= 6) {
        printf("5. 5.1\n");
        max_choice = 5;
    }
    if (channels >= 8) {
        printf("6. 7.1\n");
        max_choice = 6;
    }

    // Prompt the user to select a mode
    printf("\nEnter the number corresponding to the desired mode: ");
    int choice;
    scanf("%d", &choice);

    if (choice < 1 || choice > max_choice) {
        printf("Invalid choice.\n");
        manager->destroy(self);
        return 1;
    }

    const char* mode_choices[] = {"mono", "stereo", "4.0", "5.0", "5.1", "7.1"};
    const char* mode_to_set = mode_choices[choice - 1];

    // Set the mode of the active device
    if (manager->set_device_mode_by_code(manager, manager->active_device->code, mode_to_set)) {
        printf("Mode set to %s successfully!\n", mode_to_set);
    } else {
        fprintf(stderr, "Failed to set the mode.\n");
    }

    // Cleanup
    manager->destroy(self);
    return 0;

}
#endif

int main(void) {
    return 0;
}
