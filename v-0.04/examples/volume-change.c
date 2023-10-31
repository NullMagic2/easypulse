/**
 * @file volume_app.c
 * @brief PulseAudio Volume Control Application
 *
 * This application provides a simple interface to interact with PulseAudio.
 * It displays the current active device, its name, and the master volume.
 * The user can then input a new volume value, which the application attempts
 * to set. The result of the operation (success or failure) is displayed to the user.
 *
 * The application leverages the EasyPulse library to communicate with PulseAudio.
 *
 *
 * @date 10-08-2023 (creation date)
 */

#include <stdint.h>
#include <stdio.h>
#include "../easypulse_core.h"

int main() {
    // Initialize the pulseaudio manager
    pulseaudio_manager *manager = new_manager();
    pulseaudio_manager *self = manager;
    pulseaudio_device *active_device = self->active_device;

    if (!manager) {
        fprintf(stderr, "Failed to initialize PulseAudioManager.\n");
        manager->destroy(self);
        return 1;
    }

#if 0
    // Display the channels of the active profile
    uint32_t total_channels = manager->get_active_profile_channels(active_device, active_device->index);

    printf("[DEBUG] The active device is: %s\n", active_device->code);
    printf("[DEBUG] The following profiles were found:\n");

    for (uint32_t i = 0; i < active_device->profile_count; i++) {
        if (active_device->profiles[i].name) {
            printf("[DEBUG] Profile name: %s\n", active_device->profiles[i].name);
        } else {
            printf("[DEBUG] Profile name is NULL or invalid.\n");
        }
        printf("[DEBUG] This profile has %d channels.\n", active_device->profiles[i].channels);
    }


    printf("Current device: %s\n", manager->active_device->code);

    // Debug: Print the volume of individual channels of the active device
    for (uint32_t channel = 0; channel < total_channels; channel++) {
        printf("Channel %d volume before change: %f%%\n",
            channel,
            (100.0 * manager->active_device->volume.values[channel] / PA_VOLUME_NORM));
    }

    printf("Master Volume: %f%%\n",
           (100.0 * pa_cvolume_avg(&manager->active_device->volume) / PA_VOLUME_NORM));


    // Ask the user for a new volume value
    float new_volume;
    printf("Enter a new volume value (0.0 - 100.0): ");
    scanf("%f", &new_volume);

    // Set the new volume
    manager->set_volume(manager, manager->active_device->index, new_volume);


    // Debug: Print the volume of individual channels of the active device
    for (uint32_t channel = 0; channel < total_channels; channel++) {
        printf("Channel %d volume after change: %f%%\n",
            channel,
            (100.0 * manager->active_device->volume.values[channel] / PA_VOLUME_NORM));
    }
#endif
    // Cleanup and exit
    manager->destroy(self);
    return 0;

}
