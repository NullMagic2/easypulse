/**
 * @file get-card-profiles-pulseaudio_api.c
 * @brief This program queries and displays information about available sound devices in a computer.
 *
 * It uses the PulseAudio library to interact with the sound system and retrieve information about
 * available sound devices, their properties, and ALSA (Advanced Linux Sound Architecture) related data.
 */

#include <stdio.h>
#include "../easypulse_core.h"

int main(void) {
    // Query the total number of PulseAudio devices
    uint32_t total_devices = get_output_device_count();

    printf("Total sound devices in this computer:%lu\n", (unsigned long) total_devices);
    printf("Available PulseAudio sound devices:\n");

    pa_sink_info **sink_info = get_available_output_devices();
    if (!sink_info) {
        fprintf(stderr, "Error: Could not retrieve sound device information.\n");
        return 1;
    }

    for (uint32_t i = 0; i < total_devices; i++) {
        if (!sink_info[i]) {
            fprintf(stderr, "Error: sound device information at index %u is NULL.\n", i);
            continue;
        }

        // Display sink name
        printf("\n- Sound device name: %s\n", sink_info[i]->name ? sink_info[i]->name : "NULL");
        printf("\n- Sound device description: %s\n", sink_info[i]->description ? sink_info[i]->description : "NULL");

        // Query and display the number of profiles
        uint32_t profile_count = get_profile_count(i);
        printf("  - Number of profiles: %u\n", profile_count);

        // Get and display the ALSA name
        const char *alsa_name = get_alsa_output_name(sink_info[i]->name);
        const char *alsa_id = get_alsa_id(sink_info[i]->name);
        int sample_rate = get_output_sample_rate(alsa_id, sink_info[i]);

        printf("  - Sample rate: %i\n", sample_rate);
        printf("\n- Alsa ID is: %s\n", alsa_id ? alsa_id : "NULL");

        if (alsa_name) {
            printf("  - ALSA name: %s\n", alsa_name);
        } else {
            printf("  - No corresponding ALSA name found.\n");
        }
        // Get and display the minimum and maximum channels
        int min_channels = get_min_output_channels(alsa_id, sink_info[i]);
        int max_channels = get_max_output_channels(alsa_id, sink_info[i]);

        if(min_channels > 0) printf("    - Minimum channels: %d\n", min_channels);
        if(max_channels > 0) printf("    - Maximum channels: %d\n", max_channels);
        free((char *)alsa_name);  // Free the memory allocated for alsa_name
    }

    delete_output_devices(sink_info);
    return 0;
}
