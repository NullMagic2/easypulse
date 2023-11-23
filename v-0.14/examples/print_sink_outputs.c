#include "../easypulse_core.h"
#include <pulse/introspect.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

int main(void) {
    // Create and initialize the pulseaudio_manager
    pulseaudio_manager *manager = manager_create();
    if (!manager) {
        fprintf(stderr, "Failed to create PulseAudio manager.\n");
        return -1;
    }

    if (manager->pa_ready != 1) {
        fprintf(stderr, "PulseAudio manager is not ready.\n");
        manager_cleanup(manager);
        return -1;
    }

    // Get a list of all output streams
    output_stream_list *streams = get_output_streams(manager->context);
    if (!streams) {
        fprintf(stderr, "Failed to get output streams.\n");
        manager_cleanup(manager);
        return -1;
    }

    printf("*** Listing all output devices and streams *** \n");

    // Iterate over each output device
    for (uint32_t i = 0; i < manager->output_count; ++i) {

        // For each device, list the streams associated with it
        for (uint32_t j = 0; j < streams->num_inputs; ++j) {
            if (streams->inputs[j].parent_index == manager->outputs[i].index) {
                printf("\tStream %u: %s\n", streams->inputs[j].index, streams->inputs[j].name);
            }
        }
    }

    output_streams_cleanup(streams);
    manager_cleanup(manager);

    return 0;
}
