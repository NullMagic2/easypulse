/**
 * @file easypulse_core.c
 * @brief Implementation of the easypulse core functions.
 *
 * This file provides the core functionality to interact with PulseAudio,
 * allowing operations like setting the default device and adjusting volume.
 */

#include "easypulse_core.h"
#include "system_query.h"
#include <pulse/introspect.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static bool manager_initialize(pulseaudio_manager *self);
static void iterate(pulseaudio_manager *manager, pa_operation *op);

//Shared data between manager_switch_default_output and its callbacks
typedef struct _shared_data_1 {
    pulseaudio_manager *manager;
    uint32_t new_index; //Index of the new default sink.

} shared_data_1;

/**
 * @brief Creates a new pulseaudio_manager instance.
 *
 * This function allocates memory for a new pulseaudio_manager instance and initializes it.
 * It allocates memory for the output and input devices based on the current system state,
 * and initializes the PulseAudio context and mainloop. It also sets the active output and
 * input devices.
 *
 * If any memory allocation or initialization operation fails, the function cleans up any
 * resources that were successfully allocated or initialized, and returns NULL.
 *
 * @return A pointer to the newly created pulseaudio_manager instance, or NULL if the
 *         creation failed.
 */
pulseaudio_manager *manager_create(void) {
    pulseaudio_manager *self = malloc(sizeof(pulseaudio_manager));
    if (!self) {
        fprintf(stderr, "Failed to allocate memory for pulseaudio_manager.\n");
        return NULL;
    }

    // Zero-initialize the structure to set sensible defaults
    memset(self, 0, sizeof(pulseaudio_manager));

    // Initialize manager's PulseAudio main loop and context
    if (!manager_initialize(self)) {
        fprintf(stderr, "Failed to initialize pulseaudio_manager.\n");
        free(self);
        return NULL;
    }

    // Get the count of output and input devices
    self->output_count = get_output_device_count();
    self->input_count = get_input_device_count();

    // Allocate memory for outputs
    if (self->output_count > 0) {
        self->outputs = calloc(self->output_count, sizeof(pulseaudio_device));
        if (!self->outputs) {
            fprintf(stderr, "Failed to allocate memory for outputs.\n");
            manager_cleanup(self);
            return NULL;
        }
        // Retrieve and populate output devices
        pa_sink_info **output_devices = get_available_output_devices();

        for (uint32_t i = 0; i < self->output_count; ++i) {
            self->outputs[i].index = output_devices[i]->index;
            self->outputs[i].name = strdup(output_devices[i]->description);
            self->outputs[i].code = strdup(output_devices[i]->name);
            self->outputs[i].sample_rate = get_output_sample_rate(get_alsa_output_id(output_devices[i]->name), output_devices[i]);
            self->outputs[i].max_channels = get_max_output_channels(get_alsa_output_id(output_devices[i]->name), output_devices[i]);
            self->outputs[i].min_channels = get_min_output_channels(get_alsa_output_id(output_devices[i]->name), output_devices[i]);
            self->outputs[i].channel_names = get_output_channel_names(output_devices[i]->name, self->outputs[i].max_channels);
            // Add any additional fields to populate
        }
        free(output_devices);
    }

    // Allocate memory for inputs
    if (self->input_count > 0) {
        self->inputs = calloc(self->input_count, sizeof(pulseaudio_device));
        if (!self->inputs) {
            fprintf(stderr, "Failed to allocate memory for inputs.\n");
            manager_cleanup(self);
            return NULL;
        }
        // Retrieve and populate input devices
        pa_source_info **input_devices = get_available_input_devices();

        for (uint32_t i = 0; i < self->input_count; ++i) {
            self->inputs[i].index = input_devices[i]->index;
            self->inputs[i].name = strdup(input_devices[i]->description);
            self->inputs[i].code = strdup(input_devices[i]->name);
            self->inputs[i].sample_rate = get_input_sample_rate(get_alsa_input_id(input_devices[i]->name), input_devices[i]);
            self->inputs[i].max_channels = get_max_input_channels(get_alsa_input_id(input_devices[i]->name), input_devices[i]);
            self->inputs[i].min_channels = get_min_input_channels(get_alsa_input_id(input_devices[i]->name), input_devices[i]);
            self->inputs[i].channel_names = get_input_channel_names(input_devices[i]->name, self->inputs[i].max_channels);
            // Add any additional fields to populate
        }
        free(input_devices);
    }

    // Set the default output and input devices
    self->active_output_device = strdup(get_default_output(self->context));
    self->active_input_device = strdup(get_default_input(self->context));

    // Check that the active devices were set
    if (!self->active_output_device || !self->active_input_device) {
        fprintf(stderr, "Failed to set the active output or input device.\n");
        manager_cleanup(self);
        return NULL;
    }

    // Don't forget to clean up the temporary lists of devices after you're done with them

    return self;
}


/**
 * @brief Callback function for handling PulseAudio context state changes.
 *
 * This callback is invoked by the PulseAudio mainloop when the context state changes.
 * It updates the `pa_ready` flag in the pulseaudio_manager structure based on the
 * context's state. The `pa_ready` flag is set to 1 when the context is ready, and
 * to 2 when the context has failed or terminated. This callback will signal the
 * mainloop to continue its operations whenever the state changes to either READY,
 * FAILED, or TERMINATED.
 *
 * @param c Pointer to the PulseAudio context.
 * @param userdata User-provided pointer to the pulseaudio_manager structure.
 */
static void manager_initialize_cb(pa_context *c, void *userdata) {
    pa_context_state_t state;
    pulseaudio_manager *manager = (pulseaudio_manager *) userdata;
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
 * @brief Initializes the PulseAudio manager.
 *
 * This function sets up the PulseAudio threaded mainloop and context for the given manager.
 * It creates the mainloop, context, and connects to the PulseAudio server, then starts
 * the mainloop and waits for the context to be ready. It also sets up a state callback
 * to handle the context state changes.
 *
 * @param self Pointer to the pulseaudio_manager structure to be initialized.
 * @return Returns true if initialization is successful, false otherwise.
 *
 * @note The function will clean up allocated resources and return false if any step
 * of the initialization fails.
 */
static bool manager_initialize(pulseaudio_manager *self) {
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
    pa_context_set_state_callback(self->context, manager_initialize_cb, self);

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
 * Cleans up and frees all resources associated with a pulseaudio_manager object.
 * This function ensures that all memory allocated for output and input devices
 * within the manager is released. It includes freeing of all associated strings,
 * channel names, and profile data. Additionally, it shuts down and frees the
 * PulseAudio context and mainloop, if they have been initialized.
 *
 * @param manager A pointer to the pulseaudio_manager object to be cleaned up.
 *                If the pointer is NULL, the function does nothing.
 */
void manager_cleanup(pulseaudio_manager *manager) {
    if (manager) {
        // Free output devices
        if (manager->outputs) {
            for (uint32_t i = 0; i < manager->output_count; ++i) {
                free(manager->outputs[i].code);
                free(manager->outputs[i].name);
                free(manager->outputs[i].alsa_id);
                if (manager->outputs[i].channel_names) {
                    for (int j = 0; j < manager->outputs[i].max_channels; ++j) {
                        free(manager->outputs[i].channel_names[j]);
                    }
                    free(manager->outputs[i].channel_names);
                }
                if (manager->outputs[i].profiles) {
                    for (uint32_t j = 0; j < manager->outputs[i].profile_count; ++j) {
                        free((char*)manager->outputs[i].profiles[j].name);
                        free((char*)manager->outputs[i].profiles[j].description);
                    }
                    free(manager->outputs[i].profiles);
                }
            }
            free(manager->outputs); // Finally free the array itself
        }

        // Free input devices
        if (manager->inputs) {
            for (uint32_t i = 0; i < manager->input_count; ++i) {
                free(manager->inputs[i].code);
                free(manager->inputs[i].name);
                free(manager->inputs[i].alsa_id);
                if (manager->inputs[i].channel_names) {
                    for (int j = 0; j < manager->inputs[i].max_channels; ++j) {
                        free(manager->inputs[i].channel_names[j]);
                    }
                    free(manager->inputs[i].channel_names);
                }
                if (manager->inputs[i].profiles) {
                    for (uint32_t j = 0; j < manager->inputs[i].profile_count; ++j) {
                        free((char*)manager->inputs[i].profiles[j].name);
                        free((char*)manager->inputs[i].profiles[j].description);
                    }
                    free(manager->inputs[i].profiles);
                }
            }
            free(manager->inputs); // Finally free the array itself
        }

        // Free the names of active output and input devices
        free(manager->active_output_device);
        free(manager->active_input_device);

        // Disconnect and unreference the context if it's there
        if (manager->context) {
            // Check if the context is in a state that can be disconnected
            if (pa_context_get_state(manager->context) == PA_CONTEXT_READY) {
                pa_context_disconnect(manager->context);
            }
            pa_context_unref(manager->context);
        }

        // Stop and free the mainloop if it's there
        if (manager->mainloop) {
            pa_threaded_mainloop_stop(manager->mainloop);
            pa_threaded_mainloop_free(manager->mainloop);
        }

        // Free the manager itself
        free(manager);
    }
}




/**
 * @brief Iterates through operations in the pulseaudio_manager.
 *
 * @param manager Pointer to the pulseaudio_manager instance.
 * @param op Pointer to the pa_operation instance.
 */
static void iterate(pulseaudio_manager *manager, pa_operation *op) {
    //Leaves if operation is invalid.
    if (!op) return;

    bool is_in_mainloop_thread = pa_threaded_mainloop_in_thread(manager->mainloop);

    // If we're not in the mainloop thread, lock it.
    if (!is_in_mainloop_thread) {
        pa_threaded_mainloop_lock(manager->mainloop);
    }

    //Wait for the operation to complete.
    //The signaling to continue is performed inside the callback operation (op).
    pa_threaded_mainloop_wait(manager->mainloop);

    //Cleaning up.
    pa_operation_unref(op);

    // If we locked the mainloop earlier, unlock it now.
    if (!is_in_mainloop_thread) {
        pa_threaded_mainloop_unlock(manager->mainloop);
    }
}

/**
 * @brief Callback function for setting master volume on a device.
 *
 * This function is called when the asynchronous operation to set the volume
 * for a sink completes. It will signal the mainloop to stop waiting.
 *
 * @param c The PulseAudio context.
 * @param success Non-zero if the operation succeeded, zero if it failed.
 * @param userdata The userdata passed to the function, a pointer to the pulseaudio_manager.
 */
void manager_set_master_volume_cb(pa_context *c, int success, void *userdata) {
    (void) c;

    pulseaudio_manager *manager = (pulseaudio_manager *)userdata;

    // Check if the operation was successful
    if (success) {
        printf("Volume set successfully.\n");
    } else {
        printf("Failed to set volume.\n");
    }

    // Signal the mainloop to stop waiting
    pa_threaded_mainloop_signal(manager->mainloop, 0);
}

/**
 * @brief Sets the master volume of a given device.
 *
 * This function sets the master volume of a device specified by its PulseAudio ID.
 *
 * @param manager Pointer to the pulseaudio_manager instance.
 * @param device_id The PulseAudio ID of the device.
 * @param volume The new volume level to set. This should be a value between 0 (mute) and 100 (maximum volume).
 * @return 0 if the operation was successful, or a non-zero error code if the operation failed.
 */
/**
 * @brief Sets the master volume of a given device.
 *
 * This function sets the master volume of a device specified by its PulseAudio ID.
 *
 * @param manager Pointer to the pulseaudio_manager instance.
 * @param device_id The PulseAudio ID of the device.
 * @param volume The new volume level to set. This should be a value between 0 (mute) and 100 (maximum volume).
 * @return 0 if the operation was successful, or a non-zero error code if the operation failed.
 */
int manager_set_master_volume(pulseaudio_manager *manager, uint32_t device_id, int volume) {
    if (!manager) {
        fprintf(stderr, "Manager is NULL\n");
        return -1;
    }

    if(volume < 0 || volume > 100) {
        fprintf(stderr, "[manager_set_master_volume] The volume specified is out of range (0-100).\n");
        return -1;
    }

    // Fetch the sink information for the device ID
    const pa_sink_info *sink_info = get_output_device_by_index(device_id);
    if (!sink_info) {
        fprintf(stderr, "Could not retrieve sink info for device ID %u\n", device_id);
        return -1;
    }

    // Calculate the PA volume from the provided percentage
    pa_volume_t pa_volume = (pa_volume_t) ((double) volume / 100.0 * PA_VOLUME_NORM);

    // Initialize a pa_cvolume structure and set the volume for all channels
    pa_cvolume cvolume;
    pa_cvolume_set(&cvolume, sink_info->channel_map.channels, pa_volume);

    // Start the asynchronous operation to set the sink volume
    pa_operation *op = pa_context_set_sink_volume_by_index(manager->context, device_id, &cvolume, manager_set_master_volume_cb, manager);
    if (!op) {
        fprintf(stderr, "Failed to start volume set operation\n");
        return -1;
    }

    // Wait for the operation to complete
    iterate(manager, op);

    return 0;
}

/**
 * @brief Callback function for handling the completion of an output mute toggle operation.
 *
 * This function is invoked by the PulseAudio main loop upon the completion of an operation
 * to toggle the mute state of an output device (sink). It is used in conjunction with
 * `pa_context_set_sink_mute_by_index` as part of the `manager_toggle_output_mute` function.
 * The callback checks if the mute toggle operation was successful and signals the mainloop
 * to continue processing.
 *
 * @param c Pointer to the PulseAudio context, not used in this callback.
 * @param success An integer indicating the success of the mute toggle operation.
 *                A non-zero value indicates success, while zero indicates failure.
 * @param userdata User data provided when initiating the operation; expected to be a pointer
 *                 to a `pulseaudio_manager` instance.
 *
 */
static void manager_toggle_output_mute_cb(pa_context *c, int success, void *userdata) {
    (void) c;

    pulseaudio_manager *manager = (pulseaudio_manager *) userdata;
    if (!success) {
        fprintf(stderr, "Failed to toggle mute state.\n");
    }

    // Signal the mainloop to continue
    pa_threaded_mainloop_signal(manager->mainloop, 0);
}


/**
 * Toggle the mute state of a given output device.
 *
 * @param manager A pointer to the initialized pulseaudio_manager instance.
 * @param index The index of the output device to toggle mute state.
 * @param state The desired mute state (1 for ON/mute, 0 for OFF/unmute).
 * @return Returns 0 on success, -1 on failure.
 */
int manager_toggle_output_mute(pulseaudio_manager *manager, uint32_t index, int state) {

    if (!manager || !manager->context) {
        fprintf(stderr, "Invalid PulseAudio manager or context.\n");
        return -1;
    }

    if (index >= manager->output_count) {
        fprintf(stderr, "Output device index out of range.\n");
        return -1;
    }

    pa_operation *op = pa_context_set_sink_mute_by_index(manager->context,
        index, state, manager_toggle_output_mute_cb, manager);

    iterate(manager, op);

    return 0;
}

/**
 * @brief Callback function for handling the completion of an input mute toggle operation.
 *
 * This function is called by the PulseAudio main loop when the operation to toggle
 * the mute state of an input device (source) is completed. The function is used in
 * conjunction with `pa_context_set_source_mute_by_index` within the `manager_toggle_input_mute`
 * function. It checks if the operation was successful and signals the mainloop to continue.
 *
 * @param c Pointer to the PulseAudio context. This parameter is not used in this callback.
 * @param success An integer indicating the success of the mute toggle operation.
 *                Non-zero value indicates success, zero indicates failure.
 * @param userdata User data provided when initiating the operation; expected to be a pointer
 *                 to a `pulseaudio_manager` instance.
 *
 */
static void manager_toggle_input_mute_cb(pa_context *c, int success, void *userdata) {
    (void) c;

    pulseaudio_manager *manager = (pulseaudio_manager *) userdata;
    if (!success) {
        fprintf(stderr, "Failed to toggle input mute state.\n");
    }

    // Signal the mainloop to continue
    pa_threaded_mainloop_signal(manager->mainloop, 0);
}

/**
 * Toggle the mute state of a given input device.
 *
 * @param manager A pointer to the initialized pulseaudio_manager instance.
 * @param index The index of the input device to toggle mute state.
 * @param state The desired mute state (1 for ON/mute, 0 for OFF/unmute).
 * @return Returns 0 on success, -1 on failure.
 */
int manager_toggle_input_mute(pulseaudio_manager *manager, uint32_t index, int state) {

    if (!manager || !manager->context) {
        fprintf(stderr, "Invalid PulseAudio manager or context.\n");
        return -1;
    }

    if (index >= manager->input_count) {
        fprintf(stderr, "Input device index out of range.\n");
        return -1;
    }

    pa_operation *op = pa_context_set_source_mute_by_index(manager->context,
        index, state, manager_toggle_input_mute_cb, manager);

    iterate(manager, op);

    return 0;
}



// Callback for setting the default sink
static void manager_switch_default_output_cb(pa_context *c, int success, void *userdata) {
    (void) c;

    pulseaudio_manager *manager = (pulseaudio_manager *)userdata;
    if (!success) {
        fprintf(stderr, "Failed to set default sink.\n");
    }
    pa_threaded_mainloop_signal(manager->mainloop, 0);
}

// Callback for moving sink inputs
static void manager_switch_default_output_cb_2(pa_context *c, const pa_sink_input_info *i, int eol, void *userdata) {
    shared_data_1 *shared_data = (shared_data_1 *) userdata;
    pa_threaded_mainloop *mainloop = shared_data->manager->mainloop;

    if (eol < 0) {
        // Error occurred, signal the main loop to continue
        pa_threaded_mainloop_signal(mainloop, 0);
        return;
    }

    if (!eol && i) {
        // Move sink input to the new sink index stored in shared_data
        pa_operation *op_move = pa_context_move_sink_input_by_index(c, i->index, shared_data->new_index, NULL, NULL);
        if (op_move) {
            pa_operation_unref(op_move);
            pa_threaded_mainloop_signal(mainloop, 0);
        }
    }

    if (eol > 0) {
        // End of list, signal the main loop to continue
        pa_threaded_mainloop_signal(mainloop, 0);
    }
}

bool manager_switch_default_output(pulseaudio_manager *self, uint32_t device_index) {
    //To be sent to the second callback.
    shared_data_1 shared_data = {self, self->outputs[device_index].index};

    if (!self || !self->context || device_index >= self->output_count) {
        fprintf(stderr, "Invalid arguments provided.\n");
        return false;
    }

    const char *new_sink_name = self->outputs[device_index].code;
    if (!new_sink_name) {
        fprintf(stderr, "Output device code is NULL.\n");
        return false;
    }

    // Set the new default sink
    pa_operation *op = pa_context_set_default_sink(self->context, new_sink_name, manager_switch_default_output_cb, self);
    iterate(self, op);

    shared_data.new_index = get_output_device_index_by_code(self->context, self->outputs[device_index].code);
    //fprintf(stderr, "[DEBUG, manager_switch_default_output()] index is %lu\n", (unsigned long) shared_data.new_index);

    // Move all sink inputs to the new default sink
    op = pa_context_get_sink_input_info_list(self->context, manager_switch_default_output_cb_2, &shared_data);
    iterate(self, op);

    return true;
}



