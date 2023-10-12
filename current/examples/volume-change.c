/**
 * @file volume_app.c
 * @brief PulseAudio Volume Control Application
 *
 * This application provides a simple interface to interact with PulseAudio.
 * It displays the current active sink, its name, and the master volume.
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

    if (!manager) {
        fprintf(stderr, "Failed to initialize PulseAudioManager.\n");
        manager->destroy(self);
        return 1;
    }

    // Load available sinks
    if (!manager->load_sinks(manager)) {
        fprintf(stderr, "Failed to load sinks.\n");
        manager->destroy(self);
        return 1;
    }

    // Get the active sink
    manager->get_active_sink(self);

    // Display the active sink and its master volume
    uint32_t active_sink_index = manager->active_sink_index;
    uint32_t total_channels = manager->get_sink_channels(self->sinks, self->active_sink_index);

    printf("DEBUG, total_channels is: %lu\n", (long unsigned int) total_channels);


    printf("Current Sink: %s\n", manager->active_sink_name);

    // Debug: Print the volume of individual channels of the active sink
    for (uint32_t channel = 0; channel < total_channels; channel++) {
        printf("Channel %d volume before change: %f%%\n",
            channel,
            (100.0 * manager->sinks[active_sink_index].volume.values[channel] / PA_VOLUME_NORM));
    }

    printf("Master Volume: %f%%\n",
           (100.0 * pa_cvolume_avg(&manager->sinks[active_sink_index].volume) / PA_VOLUME_NORM));


    // Ask the user for a new volume value
    float new_volume;
    printf("Enter a new volume value (0.0 - 100.0): ");
    scanf("%f", &new_volume);

    // Set the new volume
    manager->set_volume(manager, manager->active_sink_index, new_volume);


    // Debug: Print the volume of individual channels of the active sink
    for (uint32_t channel = 0; channel < total_channels; channel++) {
        printf("Channel %d volume after change: %f%%\n",
            channel,
            (100.0 * manager->sinks[active_sink_index].volume.values[channel] / PA_VOLUME_NORM));
    }

    // Cleanup and exit
    manager->destroy(self);
    return 0;
}
