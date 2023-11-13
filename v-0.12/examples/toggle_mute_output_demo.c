/**
 * @file toggle_channels.c
 * @brief Program to toggle mute state of specified channels on a selected PulseAudio output device.
 *
 * This program uses the EasyPulse library to interface with PulseAudio. It lists all available
 * output devices and allows the user to select one. After a device is selected, the program displays
 * the current mute state of each channel of that device. The user can then specify which channels'
 * mute state they want to toggle. The program will only change the mute state of the channels specified
 * by the user.
 *
 * Usage:
 * 1. A list of available output devices is displayed.
 * 2. User selects a device by entering its corresponding number.
 * 3. The program displays the mute state of each channel of the selected device.
 * 4. User enters the channel numbers they wish to toggle, separated by spaces.
 * 5. The program toggles the mute state of the specified channels.
 *
 * @author Mbyte2
 * @date November 12, 2023
 */

#include "../easypulse_core.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main() {
    // Initialize PulseAudio manager
    pulseaudio_manager *self = manager_create();
    if (self == NULL) {
        fprintf(stderr, "Failed to initialize PulseAudio manager\n");
        return 1;
    }

    // List output devices
    for (uint32_t i = 0; i < self->output_count; i++) {
        printf("%d: %s\n", i, self->outputs[i].name);
    }

    // User selects a device
    uint32_t device_index;
    printf("Enter the number of the device you want to select: ");
    scanf("%d", &device_index);

    if (device_index >= self->output_count) {
        fprintf(stderr, "Invalid device index\n");
        manager_cleanup(self);
        return 1;
    }

    pulseaudio_device *selected_device = &self->outputs[device_index];

    // Display channels and their mute state
    printf("Channels and their current mute state:\n");

    for (int i = 0; i < selected_device->max_channels; i++) {
        bool mute_state = get_output_channel_mute_state(self->context, self->mainloop, selected_device->index, i);
        printf("Channel %d: %s\n", i, mute_state ? "Muted" : "Unmuted");
    }

    // Ask the user to specify channels to toggle
    printf("Enter the channel numbers to toggle, separated by spaces (e.g., 0 2 3): ");
    char input[1024];
    scanf(" %[^\n]", input);

    char *token = strtok(input, " ");

    while (token != NULL) {
        int channel = atoi(token);
        if (channel >= 0 && channel < selected_device->max_channels) {
            // Toggle the mute state of the specified channel
            bool new_mute_state = !get_output_channel_mute_state(self->context, self->mainloop, selected_device->index, channel);
            manager_set_output_mute_state(self, selected_device->index, channel, new_mute_state);
        } else {
            printf("Invalid channel number: %d\n", channel);
        }
        token = strtok(NULL, " ");
    }

    // Clean up and close PulseAudio connection
    manager_cleanup(self);

    return 0;
}
