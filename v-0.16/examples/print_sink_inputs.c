/**
 * @file print_sink_inputs.c
 * @brief This file demonstrates the usage of the EasyPulse library to interact with PulseAudio.
 *
 * The program initializes a PulseAudio manager, checks its readiness, and retrieves a list
 * of all output streams. It then iterates over each output device to list the streams associated
 * with it. This serves as an example of how to use the EasyPulse library to interact with PulseAudio
 * for querying and managing audio streams and devices.
 *
 * Dependencies:
 * - easypulse_core.h: Provides the core functionalities and structures for the EasyPulse library.
 * - pulse/introspect.h: PulseAudio API for introspection functionalities.
 * - stdint.h: Standard integer types.
 * - stdio.h: Standard input/output library for C.
 * - unistd.h: Provides access to the POSIX operating system API.
 *
 * Functions:
 * - manager_create(): Initializes and returns a new pulseaudio_manager instance.
 * - manager_cleanup(): Cleans up and deallocates a pulseaudio_manager instance.
 * - get_output_streams(): Retrieves a list of all output streams from the PulseAudio context.
 * - output_streams_cleanup(): Cleans up and deallocates an output_stream_list instance.
 *
 * The program demonstrates error handling, stream listing, and cleanup procedures in a PulseAudio context.
 *
 * @author Mbyte2
 * @date November 23, 2023
 *
 */

#include "../easypulse_core.h"
#include <pulse/introspect.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

int main(void) {
    const char *key;
    void *proplist_state = NULL;

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
                printf("\tStream [%u] name: %s\n", streams->inputs[j].index, streams->inputs[j].name);
                printf("\tOwner: %lu\n", (unsigned long) streams->inputs[j].owner_module);
                printf("\tParent index: %lu\n", (unsigned long) streams->inputs[j].parent_index);
                printf("\tVolume channels: %u\n", (unsigned) streams->inputs[j].volume.channels);
                printf("\tChannel map channels: %u\n", (unsigned) streams->inputs[j].channel_map.channels);
                printf("\tSink Input Format Encoding: %u\n", streams->inputs[j].format->encoding);
                printf("\tProperties:\n");

                while ((key = pa_proplist_iterate(streams->inputs[j].proplist, &proplist_state))) {
                    const char *value = pa_proplist_gets(streams->inputs[j].proplist, key);
                    if (value) {
                        printf("\t%s = %s\n", key, value);
                    } else {
                        printf("\t%s = <non-string value or not present>\n", key);
                    }
                }
                printf("\t***\n");
            }
        }
    }

    output_streams_cleanup(streams);
    manager_cleanup(manager);

    return 0;
}
