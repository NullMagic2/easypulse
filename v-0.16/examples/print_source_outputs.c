/**
 * @file print_source_outputs.c
 * @brief Demonstrates interaction with PulseAudio using the EasyPulse library for input streams.
 *
 * This program initializes a PulseAudio manager, verifies its readiness, and retrieves a list
 * of all input streams (source outputs). It iterates over each input stream, printing detailed
 * information about them. This example showcases the use of the EasyPulse library to interface
 * with PulseAudio for querying and managing audio input streams and their associated devices.
 *
 * Dependencies:
 * - easypulse_core.h: Provides core functionalities and structures for the EasyPulse library.
 * - pulse/introspect.h: PulseAudio API for introspection functionalities.
 * - stdint.h: Standard integer types.
 * - stdio.h: Standard input/output library for C.
 * - unistd.h: Access to the POSIX operating system API.
 *
 * Functions:
 * - manager_create(): Initializes and returns a new pulseaudio_manager instance.
 * - manager_cleanup(): Cleans up and deallocates a pulseaudio_manager instance.
 * - get_input_streams(): Retrieves a list of all input streams (source outputs) from the PulseAudio context.
 * - input_streams_cleanup(): Cleans up and deallocates an input_stream_list instance.
 *
 * The program demonstrates error handling, input stream listing, and cleanup procedures within a PulseAudio context.
 *
 * @author Mbyte2
 * @date November 23, 2023
 */

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

    // Get a list of all input streams (source outputs)
    input_stream_list *input_streams = get_input_streams(manager->context);
    if (!input_streams) {
        fprintf(stderr, "Failed to get input streams.\n");
        manager_cleanup(manager);
        return -1;
    }

    printf("*** Listing all input streams (source outputs) ***\n");

    // Iterate over each input stream
    for (uint32_t i = 0; i < input_streams->num_inputs; ++i) {
        printf("\tInput Stream [%u] name: %s\n", input_streams->outputs[i].index, input_streams->outputs[i].name);
        // Additional properties of the input stream can be printed here
        printf("\t***\n");
    }

    // Cleanup
    input_streams_cleanup(input_streams);  // Assuming a cleanup function for input streams
    manager_cleanup(manager);

    return 0;
}
