#include <stdio.h>
#include "../system_query.h"

int main() {

    char *alsa_name = NULL;
    int min_channels = 0;
    int max_channels = 0;
    char *alsa_id = NULL;

    // Get all available input devices
    pa_source_info **input_devices = get_available_input_devices();
    if (input_devices == NULL) {
        fprintf(stderr, "Failed to get input devices\n");
        // Normally we would clean up here, but no cleanup function is available
        return 1;
    }

    // Output the number of input devices
    uint32_t input_device_count = get_input_device_count();
    printf("Number of input devices: %u\n", input_device_count);

    // Iterate over each input device, print its name and sample rate
    for (uint32_t i = 0; input_devices[i] != NULL; ++i) {


        pa_source_info *source_info = input_devices[i];
        alsa_id = get_alsa_input_id(source_info->name);

        //printf("[DEBUG, main()] alsa_id is, %s\n", alsa_id);

        min_channels = get_min_input_channels(alsa_id, source_info);
        max_channels = get_max_input_channels(alsa_id, source_info);
        alsa_name = get_alsa_input_name(source_info->name);

        printf(" - Input Device %u:\n", (i +1));
        printf(" - Pulseaudio ID: %s\n", source_info->name);
        printf(" - Pulseaudio name: %s\n", source_info->description);

        uint32_t sample_rate = get_input_sample_rate(alsa_id, source_info);
        printf(" - Sample Rate: %u Hz\n", sample_rate);

        if ((alsa_name) && (alsa_id)) {
            printf(" - Alsa name: %s\n", alsa_name);
            printf(" - Alsa id: %s\n", alsa_id);
            free(alsa_name);
            free(alsa_id);
        }
        else {
            printf("   [!] Unable to find an alsa name and ID.\n");
            printf("   [!] This is probably a pulseaudio-only virtual device.\n");
        }

        if(min_channels > 0) printf(" - Minimum channels: %i\n", min_channels);
        if(max_channels > 0) printf(" - Maximum channels: %i\n\n", max_channels);

        //Reset channels for now.
        min_channels = 0;
        max_channels = 0;
    }

    // Clean up all input devices
    delete_input_devices(input_devices);


    #if 0
    // Using the get_source_port_info function to get port information
    pa_source_info_list* source_ports_info = get_source_port_info();

    if (source_ports_info == NULL) {
        fprintf(stderr, "Failed to get source port information\n");
        // Cleanup would go here
        return 1;
    }

    // Iterate over the source ports and print information about active and available ports
    printf("Available source ports:\n");
    for (int i = 0; i < source_ports_info->num_ports; ++i) {
        pa_port_info *port_info = &source_ports_info->ports[i];
        printf(" Port name: %s\n", port_info->name);
        printf(" Port description: %s\n", port_info->description);
        printf(" Port status: %s\n", port_info->is_active ? "active" : "inactive");
    }

    // Remember to free the source_ports_info structure after use
    // Note: This assumes that the 'ports' array and its contents are dynamically allocated
    free(source_ports_info->ports);
    free(source_ports_info);
    #endif

    return 0;
}
