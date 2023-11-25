/**
 * @file move_sink_input_demo.c
 * @brief Demo Program for PulseAudio Sink Input Manipulation
 *
 * This file contains a demonstration program for the PulseAudio API developed
 * in 'easypulse_core.h' and 'system_query.h'. The main purpose of this program
 * is to illustrate how to move a sink input from one sink to another in a
 * PulseAudio environment.
 *
 * The program performs the following key operations:
 *  1. Initializes a connection with the PulseAudio server.
 *  2. Lists all available sinks and sink inputs in the current PulseAudio context.
 *  3. Prompts the user to select a sink input and a target sink.
 *  4. Moves the selected sink input to the chosen sink.
 *  5. Cleans up and terminates the connection with the PulseAudio server.
 *
 *
 * @author Mbyte2
 * @date November 24, 2023
 *
 */

#include "../easypulse_core.h"
#include "../system_query.h"
#include <stdio.h>
#include <stdbool.h>

//Checks if sink entry (output) exists.
bool is_sink_valid(pulseaudio_manager *manager, uint32_t sink_id) {
    bool valid_sink = false;

    for(uint32_t i = 0; i < manager->output_count; ++i) {
        //fprintf(stderr,"[DEBUG, is_sink_valid()] i is %i\n", i);
        if(sink_id == manager->outputs[i].index)
            valid_sink = true;
    }
    return valid_sink;
}

//Checks if sink input entry (output stream) exists.
bool is_sink_input_valid(output_stream_list *sink_inputs, uint32_t sink_id) {
    bool valid_sink = false;

    for(uint32_t j = 0; j < sink_inputs->num_inputs; ++j) {
        if(sink_id == sink_inputs->inputs[j].index)
            valid_sink = true;
    }
    return valid_sink;
}

int main(void) {

    bool is_valid_1 = false;
    bool is_valid_2 = false;

    // Initialize the EasyPulse manager
    pulseaudio_manager *manager = manager_create();
    if (!manager || manager->pa_ready != 1) {
        fprintf(stderr, "Failed to initialize PulseAudio manager\n");
        return 1;
    }

    // List all available sink inputs
    output_stream_list *sink_inputs = get_output_streams(manager->context);

    if (!sink_inputs) {
        fprintf(stderr, "Failed to list sink inputs\n");
        output_streams_cleanup(sink_inputs);
        manager_cleanup(manager);
        return 1;
    }

    // Display available sink inputs
    printf("Available Sink Inputs:\n");
    for (uint32_t i = 0; i < sink_inputs->num_inputs; ++i) {
        printf("ID: %u, Name: %s, Driver: %s\n",
               sink_inputs->inputs[i].index,
               sink_inputs->inputs[i].name,
               sink_inputs->inputs[i].driver);
    }

    printf("Available Sinks:\n");

    for (uint32_t i = 0; i < manager->output_count; ++i) {
        printf("ID: %u, Name: %s\n",
               manager->outputs[i].index,
               manager->outputs[i].name);
    }

    // User selects a sink and a target sink input
    uint32_t sink_id, target_sink_id;

    printf("Enter the ID of the sink input to move: ");
    scanf("%d", &sink_id);
    printf("Enter the ID of the target sink: ");
    scanf("%d", &target_sink_id);

    is_valid_1 =  is_sink_input_valid(sink_inputs, sink_id);
    is_valid_2 =  is_sink_valid(manager, target_sink_id);

    if(!is_valid_1 || !is_valid_2) {
        fprintf(stderr, "Invalid sinks specified.\n");
        output_streams_cleanup(sink_inputs);
        manager_cleanup(manager);
        return 1;
    }

    // Move the selected sink input to the target sink
    if (!manager_move_sink_input(manager, sink_id, target_sink_id)) {
        fprintf(stderr, "Failed to move sink input\n");
    } else {
        printf("Successfully moved sink input %d to sink %d\n", sink_id, target_sink_id);
    }

    // Cleanup
    output_streams_cleanup(sink_inputs);
    manager_cleanup(manager);

    return 0;
}

