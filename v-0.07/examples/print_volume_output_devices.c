/**
 * @file print_volume_output_devices.c
 * @brief This file contains the main function to demonstrate the retrieval
 * of volume % of each individual audio channel of an output device with the PulseAudio API.
 *
 * volume-demo.c lists the available audio sinks, their ALSA IDs, sample rates, and
 * channel volumes. This file assumes the presence of a system_query.h header
 * file and related implementations for interacting with the PulseAudio system.
 */

#include "../system_query.h"
#include <stdio.h>

/**
 * @brief Main function which queries and displays audio device information.
 *
 * The function initializes necessary structures for PulseAudio (done in system_query.c),
 * retrieves the count of available devices, and  iterates through each device
 * to display its details, including the ALSA ID, sample rate, and channel volumes.
 *
 * @return int Returns 0 on successful execution, 1 on failure (e.g., no sinks available).
 */
int main() {
    // Initialize any necessary PulseAudio structures

    // Get the count of available devices
    uint32_t device_count = get_output_device_count();
    printf("Total devices: %u\n", device_count);

    // Get all available sinks
    pa_sink_info **sinks = get_available_output_devices();
    if (sinks == NULL) {
        printf("No sinks available.\n");
        return 1;
    }


    // Iterate through each sink
    for (uint32_t i = 0; i < device_count; ++i) {
        if (sinks[i] == NULL) {
            continue;
        }

        char **channel_names = NULL;

        printf("Device %u: %s\n", i, sinks[i]->name);

        // Get the ALSA ID for the sink
        const char *alsa_id = get_alsa_output_id(sinks[i]->name);
        printf("\tALSA ID: %s\n", alsa_id);

        // Get the sample rate for the sink
        int sample_rate = get_output_sample_rate(alsa_id, sinks[i]);
        printf("\tSample Rate: %d Hz\n", sample_rate);

        channel_names = get_output_channel_names(sinks[i]->name, sinks[i]->channel_map.channels);

        // Iterate through each channel
        for (uint8_t ch = 0; ch < sinks[i]->channel_map.channels; ++ch) {
            pa_volume_t volume = get_channel_volume(sinks[i], ch);
            float volume_percent = (float)volume / PA_VOLUME_NORM * 100;
            printf("\tChannel %u name: %s, volume: %.2f%%\n", (ch + 1), channel_names[ch], volume_percent);
            free(channel_names[ch]);
        }
        free(channel_names);
    }

    // Cleanup
    delete_output_devices(sinks);

    return 0;
}
