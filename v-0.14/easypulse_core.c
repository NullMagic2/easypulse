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
#include <pulse/thread-mainloop.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <alsa/asoundlib.h>
#include <pwd.h>
#include <ctype.h>

#define DAEMON_CONF "/etc/pulse/daemon.conf"
#define MAX_LINE_LENGTH 1024


static bool manager_initialize(pulseaudio_manager *self);
static void iterate(pulseaudio_manager *manager, pa_operation *op);

static void manager_set_output_channel_mute_state_cb(pa_context *c, const pa_sink_info *info,
int eol, void *userdata);

static void manager_set_output_channel_mute_state_cb2(pa_context *c, int success, void *userdata);

//Shared data between manager_switch_default_output and its callbacks
typedef struct _shared_data_1 {
    pulseaudio_manager *manager;
    uint32_t new_index; //Index of the new default sink.

} _shared_data_1;

//Shared data between manager_set_output_channel_mute_state and its callbacks
typedef struct _shared_data_2 {
        pulseaudio_manager *manager;
        uint32_t channel_index;
        bool mute_state;
        pa_cvolume new_volume;
} _shared_data_2;

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


            char *alsa_id = get_alsa_output_id(output_devices[i]->name);

            //Do NOT attempt to duplicate the string if alsa_id is null, as the program can crash!
            if (alsa_id) {
                self->outputs[i].alsa_id = strdup(alsa_id);
            } else {
                self->outputs[i].alsa_id = NULL;
            }
            self->outputs[i].sample_rate = get_output_sample_rate(self->outputs[i].alsa_id, output_devices[i]);
            self->outputs[i].max_channels = get_max_output_channels(self->outputs[i].alsa_id, output_devices[i]);
            self->outputs[i].min_channels = get_min_output_channels(self->outputs[i].alsa_id, output_devices[i]);
            self->outputs[i].channel_names = get_output_channel_names(output_devices[i]->name, self->outputs[i].max_channels);

            free(alsa_id);
        }
        for (uint32_t i = 0; i < self->output_count; ++i) {
            if (output_devices[i]) {
                // Free other dynamically allocated fields within output_devices[i] if any
                free(output_devices[i]);
            }
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

            char *alsa_id = get_alsa_input_id(input_devices[i]->name);

            if (alsa_id) {
                self->inputs[i].alsa_id = strdup(alsa_id);
            } else {
                self->inputs[i].alsa_id = NULL;
            }

            self->inputs[i].sample_rate = get_input_sample_rate(self->inputs[i].alsa_id, input_devices[i]);
            self->inputs[i].max_channels = get_max_input_channels(self->inputs[i].alsa_id, input_devices[i]);
            self->inputs[i].min_channels = get_min_input_channels(self->inputs[i].alsa_id, input_devices[i]);
            self->inputs[i].channel_names = get_input_channel_names(input_devices[i]->name, self->inputs[i].max_channels);

            free(alsa_id);
        }
        for (uint32_t i = 0; i < self->input_count; ++i) {
            if (input_devices[i]) {
                // Free other dynamically allocated fields within output_devices[i] if any
                free(input_devices[i]);
            }
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

/**
 * @brief Callback for handling the completion of setting the default sink.
 *
 * This callback is invoked when the operation to set the default sink in PulseAudio
 * is completed. It signals the main loop to continue the execution flow.
 *
 * @param c The PulseAudio context.
 * @param success Indicates if the operation was successful.
 * @param userdata User-provided data, expected to be a pointer to a pulseaudio_manager instance.
 */
static void manager_switch_default_output_cb(pa_context *c, int success, void *userdata) {
    (void) c;

    pulseaudio_manager *manager = (pulseaudio_manager *)userdata;
    if (!success) {
        fprintf(stderr, "Failed to set default sink.\n");
    }
    pa_threaded_mainloop_signal(manager->mainloop, 0);
}

/**
 * @brief Callback for handling each sink input during the process of moving them to a new sink.
 *
 * This callback is invoked for each sink input (audio stream) currently active. It moves
 * each sink input to the new default sink specified in the shared_data.
 *
 * @param c The PulseAudio context.
 * @param i The sink input information.
 * @param eol End of list flag, indicating no more data.
 * @param userdata User-provided data, expected to be a pointer to shared_data_1 structure.
 */
static void manager_switch_default_output_cb_2(pa_context *c, const pa_sink_input_info *i, int eol, void *userdata) {

    _shared_data_1 *shared_data = (_shared_data_1 *) userdata;
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

/**
 * @brief Switches the default output device to the specified device.
 *
 * This function sets the specified output device as the default sink in PulseAudio.
 * It also moves all current sink inputs (audio streams) to the new default sink.
 *
 * @param self Pointer to the pulseaudio_manager instance.
 * @param device_index Index of the output device to be set as the default.
 * @return True if the operation was successful, False otherwise.
 */
bool manager_switch_default_output(pulseaudio_manager *self, uint32_t device_index) {
    //To be sent to the second callback.
    _shared_data_1 shared_data = {self, self->outputs[device_index].index};

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

/**
 * @brief Callback for handling the completion of setting the default source.
 *
 * This callback is invoked when the operation to set the default source in PulseAudio
 * is completed. It signals the main loop to continue the execution flow.
 *
 * @param c The PulseAudio context.
 * @param success Indicates if the operation was successful.
 * @param userdata User-provided data, expected to be a pointer to a pulseaudio_manager instance.
 */
static void manager_switch_default_input_cb(pa_context *c, int success, void *userdata) {
    (void) c;

    pulseaudio_manager *manager = (pulseaudio_manager *)userdata;
    if (!success) {
        fprintf(stderr, "Failed to set default source.\n");
    }
    pa_threaded_mainloop_signal(manager->mainloop, 0);
}


/**
 * @brief Switches the default input device to the specified device.
 *
 * This function sets the specified input device as the default source in PulseAudio.
 * It requires a valid PulseAudio context and uses the PulseAudio API to set the new default source.
 *
 * @param self Pointer to the pulseaudio_manager instance.
 * @param device_index Index of the input device to be set as the default.
 * @return True if the operation was successful, False otherwise.
 */
bool manager_switch_default_input(pulseaudio_manager *self, uint32_t device_index) {
    // Validate the arguments
    if (!self || !self->context || device_index >= self->input_count) {
        fprintf(stderr, "Invalid arguments provided.\n");
        return false;
    }

    // Retrieve the code (PulseAudio name) of the new default input device
    const char *new_source_name = self->inputs[device_index].code;
    if (!new_source_name) {
        fprintf(stderr, "Input device code is NULL.\n");
        return false;
    }

    // Lock the main loop to ensure thread safety during the operation
    pa_threaded_mainloop_lock(self->mainloop);

    // Initiate the operation to set the new default source
    pa_operation *op = pa_context_set_default_source(self->context, new_source_name, manager_switch_default_input_cb, self);
    if (op) {
        pa_operation_unref(op);
    } else {
        fprintf(stderr, "Failed to set default source.\n");
        pa_threaded_mainloop_unlock(self->mainloop);
        return false;
    }

    // Wait for the completion of the operation
    pa_threaded_mainloop_wait(self->mainloop);

    // Unlock the main loop after the operation is complete
    pa_threaded_mainloop_unlock(self->mainloop);

    return true;
}

/**
 * @brief Sets the global sample rate for PulseAudio.
 *
 * This function attempts to set the global sample rate for PulseAudio by modifying
 * the PulseAudio configuration files. It first tries to update the system-wide
 * configuration file (/etc/pulse/daemon.conf). If it does not have permission to
 * write to the system-wide file or the file does not exist, it then tries to
 * update the user's local configuration file (~/.config/pulse/daemon.conf).
 *
 * The function searches for the 'default-sample-rate' line in the configuration file.
 * If found, it updates this line with the new sample rate. If the line is not found,
 * it appends the setting to the end of the configuration file.
 *
 * @param sample_rate The new sample rate to set (in Hz).
 * @return Returns 0 on success, -1 on failure (e.g., if both configuration files
 *         cannot be opened for writing).
 */
int manager_set_pulseaudio_global_rate(int sample_rate) {

    //Delay for waiting to restarting pulseaudio (in seconds).
    const int restart_delay = 2;

    const char* system_conf = DAEMON_CONF;
    struct passwd *pw = getpwuid(getuid());
    const char* homedir = pw ? pw->pw_dir : NULL;
    char local_conf[MAX_LINE_LENGTH];
    if (homedir) {
        snprintf(local_conf, sizeof(local_conf), "%s/.config/pulse/daemon.conf", homedir);
    } else {
        strcpy(local_conf, DAEMON_CONF); // Use system config as fallback
    }

    const char* paths[] = { system_conf, local_conf };
    int operation_successful = 0;

    for (int i = 0; i < 2; ++i) {
        FILE* file = fopen(paths[i], "r+");
        if (!file && i == 1) {  // If local file doesn't exist, create it
            file = fopen(local_conf, "w+");
        }
        if (!file) {
            continue;
        }

        char new_config[MAX_LINE_LENGTH * 10] = "";
        char line[MAX_LINE_LENGTH];
        int found = 0;

        while (fgets(line, sizeof(line), file)) {
            char *trimmed_line = line;
            // Skip leading whitespace
            while (*trimmed_line && isspace((unsigned char)*trimmed_line)) {
                trimmed_line++;
            }

            if (strncmp(trimmed_line, "default-sample-rate", 19) == 0) {
                sprintf(line, "default-sample-rate = %d\n", sample_rate);
                found = 1;
            }
            strcat(new_config, line);
        }

        if (!found) {
            sprintf(new_config + strlen(new_config), "default-sample-rate = %d\n", sample_rate);
        }

        rewind(file); // Rewind to the beginning of the file for writing
        if (fputs(new_config, file) != EOF) {
            operation_successful = 1;
        }
        fclose(file);

        if (operation_successful) {
            break; // Exit loop if operation was successful
        }
    }

    if (!operation_successful) {
        fprintf(stderr, "Failed to update PulseAudio configuration file\n");
        return -1;
    }

    // Check if running as root
    if (getuid() == 0) {
        // Inform the user to manually restart PulseAudio
        printf("[WARNING] Pulseaudio cannot be restarted automatically as root.\n");
        printf("Please restart PulseAudio manually to apply changes.\n");
        return 0;
    }

    // Check if PulseAudio is running; if so, kill it.
    if (system("pulseaudio --check") == 0) {
        if(system("pulseaudio --kill") != 0) {
            perror("Failed to kill PulseAudio");
            return -1;  // Indicate an error in restarting PulseAudio
        }
        sleep(restart_delay);
    }

    // Restart PulseAudio to apply changes
    if (system("pulseaudio --start") != 0) {
        perror("Failed to restart PulseAudio");
        return -1;  // Indicate an error in restarting PulseAudio
    }

    return 0; // Configuration updated and PulseAudio restarted successfully
}


/**
 * @brief Callback function for setting the mute state of a channel in a PulseAudio sink.
 *
 * This callback function is triggered by `pa_context_get_sink_info_by_index` to process
 * information about a specific PulseAudio sink. It modifies the volume of a given channel
 * in the sink to either muted or unmuted state, as specified in the user data.
 *
 * @param c Pointer to the PulseAudio context.
 * @param info Pointer to the structure containing the sink information.
 * @param eol End-of-list flag. If non-zero, indicates no more data.
 * @param userdata Pointer to user data, expected to be of type `struct volume_update_data`.
 *
 */
static void manager_set_output_channel_mute_state_cb(pa_context *c, const pa_sink_info *info, int eol, void *userdata) {
    (void) c;
    _shared_data_2 *volume_data = (_shared_data_2 *) userdata;

    if (eol > 0) {
        pa_threaded_mainloop_signal(volume_data->manager->mainloop, 0);
        return;
    }

    struct _shared_data_2 *data = (struct _shared_data_2 *)userdata;

    if (info) {
        // Modify the volume of the specified channel
        data->new_volume = info->volume;
        data->new_volume.values[data->channel_index] = data->mute_state ? PA_VOLUME_MUTED : pa_cvolume_max(&info->volume);
    }
}

/**
 * @brief Callback function for confirming the volume set operation on a PulseAudio sink.
 *
 * This callback function is used to verify the success of a volume set operation
 * on a PulseAudio sink. It is called after attempting to set the volume of a specific
 * channel within a sink, indicating whether the operation was successful.
 *
 * @param c Pointer to the PulseAudio context.
 * @param success Integer indicating the success of the volume set operation. Non-zero if successful.
 * @param userdata Pointer to user data. This parameter is not used in this callback.
 *
 */
static void manager_set_output_channel_mute_state_cb2(pa_context *c, int success, void *userdata) {
    (void) c;
    _shared_data_2 *volume_data = (_shared_data_2 *) userdata;

    if (!success) {
        fprintf(stderr, "Failed to set output device volume.\n");
    }
    pa_threaded_mainloop_signal(volume_data->manager->mainloop, 0);
}

/**
 * @brief Sets the mute state for a single channel of an output device.
 *
 * This function controls the mute state of a specific channel for a given PulseAudio sink (output device).
 * It uses the EasyPulse API to interact with the PulseAudio server.
 *
 * @param sink_index Index of the sink (output device) whose channel mute state is to be set.
 * @param channel_index Index of the channel within the sink to be muted or unmuted.
 * @param mute_state Boolean value indicating the desired mute state. 'true' to mute, 'false' to unmute.
 *
 * @return Returns 0 on success, non-zero on failure.
 *
 */
int manager_set_output_mute_state(pulseaudio_manager *self, uint32_t sink_index,
uint32_t channel_index, bool mute_state) {

    if (!self->context) {
        fprintf(stderr, "Failed to get PulseAudio context.\n");
        return -1;
    }
    _shared_data_2 volume_data;

    volume_data.channel_index = channel_index;
    volume_data.mute_state = mute_state;
    volume_data.manager = self;

    // Requesting information about the specified sink
    pa_operation *op = pa_context_get_sink_info_by_index(self->context, sink_index,
    manager_set_output_channel_mute_state_cb, &volume_data);

    if (!op) {
        fprintf(stderr, "Failed to start output device information operation.\n");
        return -1;
    }

    // Wait for the operation to complete
    iterate(self, op);

    // Set the updated volume
    op = pa_context_set_sink_volume_by_index(self->context, sink_index,
    &(volume_data.new_volume), manager_set_output_channel_mute_state_cb2, &volume_data);

    iterate(self, op);

    return 0; // Success
}

/**
 * @brief Callback function for handling input device information.
 *
 * This function is called in response to a request for information about a specific PulseAudio input device.
 * It is used to modify the volume of a specified channel in the input device based on the mute state.
 *
 * @param c Pointer to the PulseAudio context.
 * @param info Pointer to the structure containing the source information.
 * @param eol End-of-list flag. If non-zero, indicates no more data.
 * @param userdata Pointer to user data, expected to be of type pulseaudio_manager.
 *
 */
static void manager_set_input_mute_state_cb(pa_context *c, const pa_source_info *info, int eol, void *userdata) {
    (void) c;
    _shared_data_2 *volume_data = (_shared_data_2 *) userdata;

    if (eol > 0) {
        pa_threaded_mainloop_signal(volume_data->manager->mainloop, 0);
        return;
    }

    //fprintf(stderr, "[DEBUG, manager_set_input_mute_state_cb()] info->description is, %s\n", info->description);

    if (info) {
        // Modify the volume of the specified channel
        volume_data->new_volume = info->volume;
        pa_volume_t new_channel_volume = volume_data->mute_state ? PA_VOLUME_MUTED : pa_cvolume_max(&info->volume);
        volume_data->new_volume.values[volume_data->channel_index] = new_channel_volume;
    }
}

/**
 * @brief Callback function for confirming the volume set operation on a PulseAudio input device.
 *
 * This callback function is used to verify the success of setting the volume of a specified channel in
 * a PulseAudio input device. It is called after an attempt to set the volume of a channel within an input device.
 *
 * @param c Pointer to the PulseAudio context.
 * @param success Integer indicating the success of the volume set operation. Non-zero if successful.
 * @param userdata Pointer to user data, expected to be of type pulseaudio_manager.
 *
 */
static void manager_set_input_mute_state_cb2(pa_context *c, int success, void *userdata) {
    (void) c;
    _shared_data_2 *volume_data = (_shared_data_2 *) userdata;

    if (!success) {
        fprintf(stderr, "Failed to set input device volume.\n");
    }

    pa_threaded_mainloop_signal(volume_data->manager->mainloop, 0);
}


/**
 * @brief Sets the mute state for a single channel of a PulseAudio input device.
 *
 * This function controls the mute state of a specified PulseAudio input device (source).
 * It mutes or unmutes all channels of the input device based on the provided mute state.
 *
 * @param self Pointer to the pulseaudio_manager structure, containing the necessary PulseAudio context.
 * @param input_index Index of the input device (source) whose mute state is to be set.
 * @param mute_state Boolean value indicating the desired mute state. 'true' to mute, 'false' to unmute.
 *
 * @return Returns 0 on success, -1 on failure.
 *
 */
int manager_set_input_mute_state(pulseaudio_manager *self, uint32_t input_index,
uint32_t channel_index, bool mute_state) {
    if (!self->context) {
        fprintf(stderr, "Failed to get PulseAudio context.\n");
        return -1;
    }

    _shared_data_2 volume_data;

    volume_data.channel_index = channel_index;
    volume_data.mute_state = mute_state;
    volume_data.manager = self;

    // Requesting information about the specified source
    pa_operation *op = pa_context_get_source_info_by_index(self->context, input_index, manager_set_input_mute_state_cb, &volume_data);

    if (!op) {
        fprintf(stderr, "Failed to start input device information operation.\n");
        return -1;
    }

    // Wait for the operation to complete
    iterate(self, op);

    // Set the updated volume for the specified channel (effectively muting or unmuting the channel)
    op = pa_context_set_source_volume_by_index(self->context, input_index,
    &(volume_data.new_volume), manager_set_input_mute_state_cb2, &volume_data);

    if (!op) {
        fprintf(stderr, "Failed to start source volume set operation.\n");
        return -1;
    }

    iterate(self, op);

    return 0;
}

/**
 * @brief Moves playback from one sink to another.
 *
 * This function moves all playback streams from one output device (sink) to another.
 * It is used to switch the audio output from one device to another, for example, from
 * speakers to headphones.
 *
 * @param manager Pointer to the pulseaudio_manager instance.
 * @param sink1_index Index of the current sink (output device).
 * @param sink2_index Index of the new sink (output device) to move streams to.
 * @return Returns 0 on success, -1 on failure.
 */
int manager_move_output_playback(pulseaudio_manager *self, uint32_t sink1_index, uint32_t sink2_index) {
    if (!self || sink1_index >= self->output_count || sink2_index >= self->output_count) {
        fprintf(stderr, "Invalid arguments provided.\n");
        return -1;
    }

    _shared_data_1 shared_data = { self, sink2_index };

    // Iterate over all sink inputs and move them to the new sink
    pa_operation *op = pa_context_get_sink_input_info_list(self->context, manager_switch_default_output_cb_2, &shared_data);

    if (!op) {
        fprintf(stderr, "Failed to start sink input info list operation.\n");
        return -1;
    }

    iterate(self, op);

    return 0; // Success
}
