/**
 * @file easypulse_core.c
 * @brief Implementation of the easypulse core functions.
 *
 * This file provides the core functionality to interact with PulseAudio,
 * allowing operations like setting the default sink and adjusting volume.
 */

#include "easypulse_core.h"
#include <stdio.h>
#define INITIAL_ALLOCATION_SIZE 5
#include <stdlib.h>
#include <string.h>

/**
 * @brief Callback function for retrieving sink information.
 * @param c The PulseAudio context.
 * @param i The sink information.
 * @param eol End of list flag.
 * @param userdata User-provided data (expected to be a pointer to PulseAudioManager).
 */
static void sink_cb(pa_context *c, const pa_sink_info *i, int eol, void *userdata) {
    PulseAudioManager *manager = (PulseAudioManager *)userdata;

    // Check if we need to resize the sinks array
    if (manager->sink_count % INITIAL_ALLOCATION_SIZE == 0) {  // Resize every time we hit a multiple of INITIAL_ALLOCATION_SIZE
        size_t newSize = (manager->sink_count + INITIAL_ALLOCATION_SIZE) * sizeof(PulseAudioSink);
        PulseAudioSink *new_sinks = realloc(manager->sinks, newSize);

        if (!new_sinks) {
            fprintf(stderr, "[ERROR]: Failed to resize the sinks array.\n");
            free(manager->sinks);  // Free old memory
            manager->sinks = NULL;
            manager->sink_count = 0;  // Reset sink_count to prevent out-of-bounds access
            pa_threaded_mainloop_signal(manager->mainloop, 0);
            return;
        }
        manager->sinks = new_sinks;
    }

    if (eol < 0) {
        if (pa_context_errno(c) != PA_ERR_NOENTITY)
            fprintf(stderr, "Sink callback failure\\n");
        pa_threaded_mainloop_signal(manager->mainloop, 0);
        return;
    }

    if (eol > 0) {
        manager->sinks_loaded = 1;
        pa_threaded_mainloop_signal(manager->mainloop, 0);
        return;
    }

    // Store the sink's information
    PulseAudioSink sink;
    sink.index = i->index;
    sink.name = strdup(i->name);
    sink.description = strdup(i->description);
    sink.volume = i->volume;
    sink.channel_map = i->channel_map;
    sink.mute = i->mute;

    // Add the sink to the manager's list
    manager->sinks[manager->sink_count++] = sink;
}


/**
 * @brief Callback function to check the state of the PulseAudio context.
 * @param c The PulseAudio context.
 * @param userdata User-provided data (expected to be a pointer to an int indicating readiness).
 */
static void context_state_cb(pa_context *c, void *userdata) {
    pa_context_state_t state;
    PulseAudioManager *manager = (PulseAudioManager *) userdata;
    int *pa_ready = &(manager->pa_ready);
    pa_threaded_mainloop *m = manager->mainloop;

    state = pa_context_get_state(c);
    switch  (state) {
        // There are other states you can handle, but these are the important ones
        case PA_CONTEXT_CONNECTING:
        case PA_CONTEXT_AUTHORIZING:
        case PA_CONTEXT_SETTING_NAME:
        default:
            break;
        case PA_CONTEXT_READY:
            *pa_ready = 1;
            pa_threaded_mainloop_signal(m, 0);
            break;
        case PA_CONTEXT_FAILED:
        case PA_CONTEXT_TERMINATED:
            *pa_ready = 2;
            pa_threaded_mainloop_signal(m, 0);
            break;
    }
}

/**
 * @brief Initializes the PulseAudioManager.
 *
 * @param self Pointer to the PulseAudioManager instance.
 * @return Boolean indicating success or failure.
 */
bool initialize(PulseAudioManager *self) {
    // 1. Create the threaded mainloop
    self->mainloop = pa_threaded_mainloop_new();
    if (!self->mainloop) {
        return false;
    }

    // Get the mainloop API
    pa_mainloop_api *mlapi = pa_threaded_mainloop_get_api(self->mainloop);

    // Create the context
    self->context = pa_context_new(mlapi, "PulseAudio Manager");
    if (!self->context) {
        pa_threaded_mainloop_free(self->mainloop);
        return false;
    }

    // Set the state callback
    pa_context_set_state_callback(self->context, context_state_cb, self);

    // Lock the mainloop and connect the context
    pa_threaded_mainloop_lock(self->mainloop);

    if (pa_context_connect(self->context, NULL, PA_CONTEXT_NOFLAGS, NULL) < 0) {
        pa_threaded_mainloop_unlock(self->mainloop);
        pa_threaded_mainloop_free(self->mainloop);
        return false;
    }

    // 2. Start the threaded mainloop
    pa_threaded_mainloop_start(self->mainloop);

    // 4. Wait for the context to be ready
    while (self->pa_ready == 0) {
        pa_threaded_mainloop_wait(self->mainloop);
    }

    pa_threaded_mainloop_unlock(self->mainloop);

    if (self->pa_ready == 2) {
        return false;
    }

    return true;
}


/**
 * @brief Cleans up the PulseAudioManager.
 *
 * @param self Pointer to the PulseAudioManager instance.
 * @return Boolean indicating success or failure.
 */
bool cleanup(PulseAudioManager *self) {
    // Lock the mainloop before making changes to the context
    pa_threaded_mainloop_lock(self->mainloop);

    // Disconnect the context
    pa_context_disconnect(self->context);

    // Unlock the mainloop before stopping it
    pa_threaded_mainloop_unlock(self->mainloop);

    // Stop the threaded mainloop
    pa_threaded_mainloop_stop(self->mainloop);

    // Unreference the context
    pa_context_unref(self->context);

    // Free the threaded mainloop
    pa_threaded_mainloop_free(self->mainloop);

    return true;
}

/**
 * @brief Load available sound cards (sinks).
 * @param self The PulseAudioManager instance.
 * @return true on success, false otherwise.
 */
bool loadSinks(PulseAudioManager *self) {
    pa_operation *op;
    op = pa_context_get_sink_info_list(self->context, sink_cb, self);

    if (!op) {
        return false;
    }

    self->iterate(self, op);
    return true;
}

/**
 * @brief Callback function handling the completion of the "unmute" operation.
 *
 * @param c The PulseAudio context.
 * @param success A flag indicating the success or failure of the operation.
 * @param userdata User-provided data, expected to be a pointer to a PulseAudioManager instance.
 */
static void operation_complete_unmute_cb(pa_context *c, int success, void *userdata) {
    (void)c; // Suppress unused parameter warning

    PulseAudioManager* manager = (PulseAudioManager*) userdata;
    manager->operations_pending--;

    if (!success) {
        fprintf(stderr, "Failed to unmute the sink input.\n");
    }

    // Signal the mainloop to resume any waiting threads.
    pa_threaded_mainloop_signal(manager->mainloop, 0);
}

/**
 * @brief Callback function handling the completion of the "move" operation.
 *
 * @param c The PulseAudio context.
 * @param success A flag indicating the success or failure of the operation.
 * @param userdata User-provided data, expected to be a pointer to a PulseAudioManager instance.
 */
static void operation_complete_move_cb(pa_context *c, int success, void *userdata) {
    PulseAudioManager* manager = (PulseAudioManager*) userdata;

    if (success) {
        pa_operation* unmute_op = pa_context_set_sink_input_mute(c, manager->current_sink_index, 0, operation_complete_unmute_cb, manager);
        pa_operation_unref(unmute_op);
    } else {
        // Handle error...
        pa_threaded_mainloop_signal(manager->mainloop, 0);
    }
}

/**
 * @brief Callback function handling the completion of the "mute" operation.
 *
 * @param c The PulseAudio context.
 * @param success A flag indicating the success or failure of the operation.
 * @param userdata User-provided data, expected to be a pointer to a PulseAudioManager instance.
 */
static void operation_complete_mute_cb(pa_context *c, int success, void *userdata) {
    PulseAudioManager* manager = (PulseAudioManager*) userdata;
    uint32_t target_sink_index = manager->current_sink_index;

    if (success) {
        pa_operation* move_op = pa_context_move_sink_input_by_index(c, manager->current_sink_index, target_sink_index, operation_complete_move_cb, manager);
        pa_operation_unref(move_op);
    } else {
        // Handle error...
        pa_threaded_mainloop_signal(manager->mainloop, 0);
    }
}

/**
 * @brief Callback function to handle each sink input.
 * @param c The PulseAudio context.
 * @param i The sink input information.
 * @param eol End of list flag.
 * @param userdata User-provided data (expected to be a pointer to the target sink index).
 */
static void switch_sink_cb(pa_context *c, const pa_sink_input_info *i, int eol, void *userdata) {
    PulseAudioManager* manager = (PulseAudioManager*) userdata;

    if (!eol && i) {
        // Move this sink input to the desired sink
        pa_operation* move_op = pa_context_move_sink_input_by_index(c, i->index, manager->sinks[manager->current_sink_index].index, NULL, NULL);
        pa_operation_unref(move_op);
    }

    if (eol) {
        pa_threaded_mainloop_signal(manager->mainloop, 0);
    }
}

/**
 * @brief Switches the sink (audio source) for the PulseAudioManager.
 *
 * @param self Pointer to the PulseAudioManager instance.
 * @param sink_index Index of the sink to switch to.
 * @return Boolean indicating success or failure.
 */
bool switchSink(PulseAudioManager *self, uint32_t sink_index) {
    // Ensure the context is valid
    if (!self || !self->context) {
        return false;
    }

    // Check if sink_index is out of bounds
    if (sink_index >= self->sink_count) {
        fprintf(stderr, "[ERROR]: sink_index out of bounds.\n");
        return false;
    }

    self->current_sink_index = sink_index;

    // Set the desired sink as the default sink
    /*fprintf(stderr, "[DEBUG]: self->context = %p\n", self->context);
    fprintf(stderr, "[DEBUG]: self->sinks = %p\n", self->sinks);
    fprintf(stderr, "[DEBUG]: sink_index = %d\n", sink_index);*/

    if (self->sinks) {
        // Check if the name attribute is NULL
        if (!self->sinks[sink_index].name) {
            fprintf(stderr, "[ERROR]: Sink's name is NULL.\n");
            return false;
        }
        //fprintf(stderr, "[DEBUG]: self->sinks[sink_index].name = %s\n", self->sinks[sink_index].name);
    }
    pa_operation* set_default_op = pa_context_set_default_sink(self->context, self->sinks[sink_index].name, NULL, NULL);
    pa_operation_unref(set_default_op);

    // Use the introspect API to get a list of all sink inputs
    pa_operation *op = pa_context_get_sink_input_info_list(self->context, switch_sink_cb, self);

    if (!op) {
        return false;
    }

    while (pa_operation_get_state(op) == PA_OPERATION_RUNNING) {
        self->iterate(self, op);
    }

    pa_operation_unref(op);

    return true;
}
/**
 * @brief Callback function to check the volume of the PulseAudio context.
 *
 * @param c Pointer to the PulseAudio context.
 * @param i Information about the sink.
 * @param eol End of list flag.
 * @param userdata User-provided data.
 */
void volume_check_cb(pa_context *c, const pa_sink_info *i, int eol, void *userdata) {
    PulseAudioManager *self = (PulseAudioManager *)userdata;

    if (eol > 0) {
        printf("[DEBUG, volume_check_cb()]: End-of-list reached.\n");
        self->operations_pending--;
        return;
    }

    if (!i) {
        fprintf(stderr, "[ERROR, volume_check_cb()]: Null pointer for pa_sink_info.\n");
        self->operations_pending--;
        return;
    }

    if (!i->name) {
        fprintf(stderr, "[ERROR, volume_check_cb()]: Null pointer for sink name.\n");
        self->operations_pending--;
        return;
    }

    printf("[DEBUG, volume_check_cb()]: Processing sink info for sink: %s\n", i->name);

    // Update the volume values in the manager's sinks structure
    self->sinks[self->active_sink_index].volume = i->volume;

    // Print the volume for each channel
    for (int channel = 0; channel < i->volume.channels; channel++) {
        //float volume_percentage = (float)i->volume.values[channel] / PA_VOLUME_NORM * 100.0;
        //printf("Channel %d Volume: %.2f%%\n", channel, volume_percentage);
    }

    //Signaling to continue.
    pa_threaded_mainloop_signal(self->mainloop, 0);
}

// Completion callback for volume set operation
static void volume_set_complete_cb(pa_context *c, int success, void *userdata) {
    (void)c;
    PulseAudioManager* manager = (PulseAudioManager*) userdata;

    //printf("[DEBUG, volume_set_complete_cb()]: inside volume_set_complete_cb()\n");
    //printf("[DEBUG, volume_set_complete_cb()]: sink name is, %s\n", manager->active_sink_name);

    if (!success) {
        fprintf(stderr, "[ERROR]: Failed to set volume. Reason: %s\n", pa_strerror(pa_context_errno(c)));
    }

    // Debug: Print cvolume values for each channel as percentages
    pa_cvolume cvolume = manager->sinks[manager->active_sink_index].volume;

    /*for (int i = 0; i < cvolume.channels; i++) {
        float percentage = (cvolume.values[i] / (float)PA_VOLUME_NORM) * 100;
        //printf("[DEBUG, volume_set_complete_cb()]: Channel %d cvolume value: %u (%.2f%%)\n", i, cvolume.values[i], percentage);
    }
    //printf("[DEBUG, volume_set_complete_cb invoked. Success: %d\n", success);*/

    // Decrease the operations count and potentially signal the condition variable.
    manager->operations_pending--;

    // Signal the main loop to continue
    pa_threaded_mainloop_signal(manager->mainloop, 0);
}


/**
 * @brief Set the volume for a specified sink.
 *
 * This function allows setting the volume for a specific sink based on the given percentage.
 * The function performs various checks to ensure valid inputs and that the PulseAudio system is ready.
 * It will adjust the volume for all channels of the sink to the desired level.
 *
 * @param self Pointer to the PulseAudioManager instance.
 * @param sink_index Index of the sink to set the volume for.
 * @param percentage Desired volume level as a percentage.
 * @return Boolean indicating success or failure.
 */
bool setVolume(PulseAudioManager *self, uint32_t sink_index, float percentage) {
    if (!self || percentage < 0.0f || percentage > 100.0f || sink_index >= self->sink_count) {
        return false;
    }

    // Convert percentage to volume
    pa_volume_t volume = (percentage / 100.0) * PA_VOLUME_NORM;
    if (volume >= PA_VOLUME_NORM) {
        volume = PA_VOLUME_NORM - 1;
    }

    // Debug: show index and desired volume.
    //printf("[DEBUG, setVolume()] Index is: %i\n", sink_index);
    //printf("[DEBUG, setVolume()] Desired volume: %f%% (value: %u)\n", percentage, volume);

    // Ensure PulseAudio is ready and sinks are loaded
    if (self->pa_ready != 1 || self->sinks_loaded != 1) {
        return false;
    }

    // Debug: Show channel volumes before the change.
    /*for (int channel = 0; channel < self->sinks[sink_index].channel_map.channels; channel++) {
        printf("[DEBUG, setVolume()]: Channel %d Before volume: %f%%\n",
               channel,
               100.0 * self->sinks[sink_index].volume.values[channel] / PA_VOLUME_NORM);
    }*/

    // Create a pa_cvolume structure and set the volume for all channels
    pa_cvolume cvolume;
    pa_cvolume_init(&cvolume);
    cvolume.channels = self->sinks[sink_index].channel_map.channels;  // Manually set channels
    pa_cvolume_set(&cvolume, cvolume.channels, volume);

    printf("[DEBUG, setVolume()] channels: %d\n", cvolume.channels);

    // Apply the volume change to the specific sink by index and wait for the operation to complete
    const char *sink_name_to_change = self->sinks[sink_index].name;
    self->operations_pending++;
    pa_operation *op = pa_context_set_sink_volume_by_name(self->context, sink_name_to_change, &cvolume, volume_set_complete_cb, self);
    self->iterate(self, op);

    // Fetch the updated volume for the sink and wait for the operation to complete
    pa_operation *op2 = pa_context_get_sink_info_by_name(self->context, sink_name_to_change, volume_check_cb, self);
    self->iterate(self, op2);

    return true;
}


/**
 * @brief Iterates through operations in the PulseAudioManager.
 *
 * @param manager Pointer to the PulseAudioManager instance.
 * @param op Pointer to the pa_operation instance.
 */
void iterate(PulseAudioManager *manager, pa_operation *op) {
    if (!op) {
        return;
    }

    const int MAX_WAIT_CYCLES = 100;  // For example, wait for 100 cycles
    int wait_cycles = 0;

    pa_threaded_mainloop_lock(manager->mainloop);

    // Wait for the operation to complete, but not indefinitely
    while (pa_operation_get_state(op) == PA_OPERATION_RUNNING) {
        if (wait_cycles >= MAX_WAIT_CYCLES) {
            fprintf(stderr, "Error: Operation timeout. Exiting loop.\\n");
            break;  // Exit the loop if we've waited for too long
        }

        pa_threaded_mainloop_wait(manager->mainloop);
        wait_cycles++;
    }

    // pa_operation_unref(op);  // Commented out to prevent early unreference
    pa_threaded_mainloop_unlock(manager->mainloop);
}


/**
 * @brief Create a new PulseAudioManager instance.
 * @return A pointer to the newly created PulseAudioManager instance.
 */
PulseAudioManager* newPulseAudioManager() {
    PulseAudioManager *manager = (PulseAudioManager *)malloc(sizeof(PulseAudioManager));
    if (!manager) return NULL;

    manager->sinks = (PulseAudioSink *)malloc(5 * sizeof(PulseAudioSink));
    manager->sink_count = 0;
    manager->sinks_loaded = 0;  // Initialize to 0
    manager->initialize = initialize;
    manager->cleanup = cleanup;
    manager->loadSinks = loadSinks;
    manager->switchSink = switchSink;
    manager->setVolume = setVolume;
    manager->getActiveSink = getActiveSink;
    manager->iterate = iterate;


    return manager;
}

/**
 * @brief Request the default sink name from the PulseAudio server and determine the active sink.
 *
 * This function initiates a request to retrieve the default sink name (active sink) from the PulseAudio server.
 * Once the server provides this information, the server_info_cb callback is triggered to process the response.
 *
 * @param manager Pointer to the PulseAudioManager instance.
 */
void getActiveSink(PulseAudioManager *manager) {
    // Request server information to get the default sink name
    pa_context_get_server_info(manager->context, server_info_cb, manager);

    // Wait for the active sink name to be set
    int timeout = 50;  // Number of iterations or timeout value

    while (!manager->active_sink_name && timeout-- > 0) {
        manager->iterate(manager, NULL);
        //fprintf(stderr, "[DEBUG, getActiveSink]: Iterating to get active sink info.\n");
        usleep(1000);  // Sleep for 1ms
    }
}


/**
 * @brief Callback function to process the PulseAudio server information.
 *
 * This callback is invoked once the server information is available. It retrieves the default sink name
 * from the server response and determines the active sink by matching the name with the available sinks
 * in the manager's list.
 *
 * @param c      Pointer to the PulseAudio context.
 * @param info   Pointer to the pa_server_info structure containing the server's information.
 * @param userdata User-provided data (expected to be a pointer to PulseAudioManager).
 */
void server_info_cb(pa_context *c, const pa_server_info *info, void *userdata) {
    if (!info || !info->default_sink_name) {
        fprintf(stderr, "[ERROR]: Null pointer in server_info_cb.\n");
        return;
    }
    (void) c;
    PulseAudioManager *manager = (PulseAudioManager *)userdata;

    // Get the default sink name from the server information
    const char *default_sink_name = info->default_sink_name;
    //fprintf(stderr, "[DEBUG]: Default sink name from server: %s\n", default_sink_name);

    // Iterate over the available sinks to find the active one
    for (int i = 0; i < manager->sink_count; i++) {
        //fprintf(stderr, "[DEBUG]: Comparing with sink %d: %s\n", i, manager->sinks[i].name);
        if (strcmp(manager->sinks[i].name, default_sink_name) == 0) {
            // Set the active sink index when a match is found
            manager->active_sink_index = i;
            manager->active_sink_name = manager->sinks[i].name;  // Set the active sink name here
            break;
        }
    }
    //printf("[DEBUG, server_info_cb()]: Active sink index is, %i\n", manager->active_sink_index);
    //printf("[DEBUG, server_info_cb()]: Active sink name is, %s\n", manager->active_sink_name);
}


/**
 * @brief Deletes and cleans up a PulseAudioManager instance.
 *
 * @param manager Pointer to the PulseAudioManager instance to be deleted.
 */
void deletePulseAudioManager(PulseAudioManager *manager) {
    if (manager) {
        for (int i = 0; i < manager->sink_count; i++) {
            free(manager->sinks[i].name);
            free(manager->sinks[i].description);
        }
        free(manager->sinks);
        free(manager);
    }
}
