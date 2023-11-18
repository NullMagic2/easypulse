/**
 * @file mute-channel-output-demo.c
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

int main(void) {
    // Initialize PulseAudio manager
    pulseaudio_manager *self = manager_create();
    bool keep_running = true;
    int c; //To clear the buffer

    if (self == NULL) {
        fprintf(stderr, "Failed to initialize PulseAudio manager\n");
        return 1;
    }

    // User selects a device
    char choice[100];
    uint32_t device_index;

    while(keep_running) {
        // List output devices
        for (uint32_t i = 0; i < self->output_count; i++) {
            printf("%d: %s\n", i, self->outputs[i].name);
        }

        printf("Enter the number of the device you want to select ('q' to quit): ");

        if (fgets(choice, sizeof(choice), stdin) != NULL) {
            // Remove newline character if present
            size_t len = strcspn(choice, "\n");
            if (choice[len] == '\n') {
                // Newline found, remove it
                choice[len] = '\0';
            } else {
                // Newline not found, buffer was too small, clear remaining characters
                while ((c = getchar()) != '\n' && c != EOF);
            }
        }

        // Remove newline character if present
        choice[strcspn(choice, "\n")] = 0;

        if(strcmp(choice, "q") == 0) {
            keep_running = false;
            break;
        }
        char *end;
        long val = strtol(choice, &end, 10);

        // Check for valid number and within range
        if (end != choice && *end == '\0' && val >= 0 && val < self->output_count) {
            device_index = (uint32_t)val;
        }
        else {
            printf("Invalid input. Please try again.\n");
        }

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

        if (fgets(choice, sizeof(choice), stdin) != NULL) {
            // Remove newline character if present
            size_t len = strcspn(choice, "\n");
            if (choice[len] == '\n') {
                // Newline found, remove it
                choice[len] = '\0';
            } else {
                // Newline not found, buffer was too small, clear remaining characters
                while ((c = getchar()) != '\n' && c != EOF);
            }
        }

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
    }

    // Clean up and close PulseAudio connection
    manager_cleanup(self);

    return 0;
}
