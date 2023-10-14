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

#include <stdio.h>
#include "../easypulse_core.h"

int main() {
    // Initialize PulseAudioManager
    PulseAudioManager *manager = newPulseAudioManager();
    if (!manager->initialize(manager)) {
        fprintf(stderr, "Failed to initialize PulseAudioManager.\n");
        deletePulseAudioManager(manager);
        return 1;
    }

    // Load available sinks
    if (!manager->loadSinks(manager)) {
        fprintf(stderr, "Failed to load sinks.\n");
        manager->cleanup(manager);
        deletePulseAudioManager(manager);
        return 1;
    }

    // Get the active sink
    manager->getActiveSink(manager);

    // Display the active sink and its master volume
    int active_sink_index = manager->active_sink_index;

    printf("Current Sink: %s\n", manager->active_sink_name);

    // Debug: Print the volume of individual channels of the active sink
    for (int channel = 0; channel < manager->sinks[active_sink_index].channel_map.channels; channel++) {
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
    bool success = manager->setVolume(manager, manager->active_sink_index, new_volume);
    if (success) {
        printf("Successfully applied the new volume.\n");
    } else {
        printf("Failed to apply the new volume.\n");
    }

    // Debug: Print the volume of individual channels of the active sink
    for (int channel = 0; channel < manager->sinks[active_sink_index].channel_map.channels; channel++) {
        printf("Channel %d volume after change: %f%%\n",
            channel,
            (100.0 * manager->sinks[active_sink_index].volume.values[channel] / PA_VOLUME_NORM));
    }

    // Cleanup and exit
    manager->cleanup(manager);
    deletePulseAudioManager(manager);
    return 0;
}
