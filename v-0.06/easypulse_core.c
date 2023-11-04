/**
 * @file easypulse_core.c
 * @brief Implementation of the easypulse core functions.
 *
 * This file provides the core functionality to interact with PulseAudio,
 * allowing operations like setting the default device and adjusting volume.
 */

#include "easypulse_core.h"
#include <pulse/introspect.h>
#include <pulse/operation.h>
#include <pulse/thread-mainloop.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

//Variables shared for count_profile function and its callback.
typedef struct {
    pulseaudio_manager *manager;
    pulseaudio_device *device;
    uint32_t profile_count;
} _shared_vars_1;

static _shared_vars_1 shared_vars_1;

/**
 * @brief Callback function to check the state of the PulseAudio context.
 * @param c The PulseAudio context.
 * @param userdata User-provided data (expected to be a pointer to an int indicating readiness).
 */
static void context_state_cb(pa_context *c, void *userdata) {
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
 * @brief Initializes the pulseaudio_manager.
 *
 * @param self Pointer to the pulseaudio_manager instance.
 * @return Boolean indicating success or failure.
 */
bool initialize(pulseaudio_manager *self) {
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
 * @brief Cleans up the pulseaudio_manager.
 *
 * @param self Pointer to the pulseaudio_manager instance.
 * @return Boolean indicating success or failure.
 */
static bool cleanup(pulseaudio_manager *self) {
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
 * @brief Nested callback to load sound card profiles for a device.
 *
 * This function is triggered after invoking pa_context_get_card_info_by_index()
 * to fetch details about a specific card. It is responsible for allocating memory
 * for the profiles associated with the card and populating them with the relevant details.
 *
 * @param c Pointer to the PulseAudio context.
 * @param i Pointer to the pa_card_info structure containing details about the card.
 * @param eol Indicates the end of the list. If eol > 0, the list has ended or no more data is available.
 * @param userdata User-provided data, expected to be a pointer to the profiles field of a pulseaudio_device structure.
 */
static void load_devices_cb_cb(pa_context *c, const pa_card_info *i, int eol, void *userdata) {
    (void) c;

    // Cast userdata to a double pointer to pulseaudio_profile
    pulseaudio_profile **profiles_ptr = (pulseaudio_profile **) userdata;

    // If eol > 0, the list has ended or no more data is available
    if (eol > 0) {
        return;
    }

    // Allocate memory for device profiles based on the number of profiles available for the card
    *profiles_ptr = malloc(i->n_profiles * sizeof(pulseaudio_profile));
    if (!*profiles_ptr) {
        fprintf(stderr, "[ERROR]: Failed to allocate memory for profiles.\n");
        return;
    }

    // Populate the allocated memory with the profile details
    for (uint32_t j = 0; j < i->n_profiles; j++) {
        (*profiles_ptr)[j].name = strdup(i->profiles[j].name);
        (*profiles_ptr)[j].description = strdup(i->profiles[j].description);
    }
}

/**
 * @brief Callback function for retrieving device information.
 * @param c The PulseAudio context.
 * @param i The device information.
 * @param eol End of list flag.
 * @param userdata User-provided data (expected to be a pointer to pulseaudio_manager).
 */
static void load_devices_cb(pa_context *c, const pa_sink_info *i, int eol, void *userdata) {
    (void) c;
    (void) userdata;
#if 0
    //Variable to check if devices need to be reloaded.
    static bool reload_devices = false;
    pulseaudio_manager *manager = (pulseaudio_manager *)userdata;
    pulseaudio_device *devices = NULL;
    pulseaudio_profile *profiles = NULL;

    if(manager->devices) {
        devices = manager->devices;
        if(manager->devices->profiles) profiles = manager->devices->profiles;
    }

    if(reload_devices == true) {
        for(uint32_t i = 0; i < manager->device_count; ++i) {
            for(uint32_t j = 0 j > profiles->
        }
    }

    manager->devices = (pulseaudio_device *)malloc(manager->device_count * sizeof(pulseaudio_device));

    if (!manager->devices) {
        free(manager);
        return NULL;
    }
#endif

#if 0
    //TODO: rewrite this so that this is called every time load_devices has been called again.
    //We need to detect that.
    //size_t newSize = (manager->device_count) * sizeof(pulseaudio_device);
    //pulseaudio_device *new_devices = realloc(manager->devices, newSize);

    /*if (!new_devices) {
        fprintf(stderr, "[ERROR]: Failed to resize the devices array.\n");
        free(manager->devices);  // Free old memory
        manager->devices = NULL;
        manager->device_count = 0;  // Reset device_count to prevent out-of-bounds access
        pa_threaded_mainloop_signal(manager->mainloop, 0);
        return;
    }
    manager->devices = new_devices; */

    if (eol < 0) {
        if (pa_context_errno(c) != PA_ERR_NOENTITY)
            fprintf(stderr, "Sink callback failure\\n");
        pa_threaded_mainloop_signal(manager->mainloop, 0);
        return;
    }

    if (eol > 0) {
        manager->devices_loaded = 1;
        pa_threaded_mainloop_signal(manager->mainloop, 0);
        return;
    }

    // Store the device's information
    pulseaudio_device device;
    device.index = i->index;
    device.code = strdup(i->name);
    device.description = strdup(i->description);
    device.volume = i->volume;
    device.channel_map = i->channel_map;
    device.mute = i->mute;

    //Pulseaudio operation to fetch card profiles.
    //pa_operation *profile_op = pa_context_get_card_info_by_index(manager->context,device.index,load_devices_cb_cb,&i->profiles);
    //iterate(manager, profile_op);

    // Add the device to the manager's list
    manager->devices[manager->device_count++] = device;

    pa_threaded_mainloop_signal(manager->mainloop, 0);
    #endif
}

/**
 * @brief Load available sound cards (devices).
 * @param self The pulseaudio_manager instance.
 * @return true on success, false otherwise.
 */
bool load_devices(pulseaudio_manager *self) {
    pa_operation *data_op;

    data_op = pa_context_get_sink_info_list(self->context, load_devices_cb, self);
    iterate(self, data_op);

    #if 0
    pa_threaded_mainloop_lock(self->mainloop);
    data_op = pa_context_get_sink_info_list(self->context, load_devices_cb, self);
    pa_threaded_mainloop_wait(self->mainloop);

    // Once all devices are loaded, loads the active device.
    // get_active_device(self);

    pa_threaded_mainloop_unlock(self->mainloop);
    pa_operation_unref(data_op);
    #endif
    return true;
}


/**
 * @brief Callback function handling the completion of the "unmute" operation.
 *
 * @param c The PulseAudio context.
 * @param success A flag indicating the success or failure of the operation.
 * @param userdata User-provided data, expected to be a pointer to a pulseaudio_manager instance.
 */
static void operation_complete_unmute_cb(pa_context *c, int success, void *userdata) {
    (void)c; // Suppress unused parameter warning

    pulseaudio_manager* manager = (pulseaudio_manager*) userdata;
    manager->operations_pending--;

    if (!success) {
        fprintf(stderr, "Failed to unmute the device input.\n");
    }

    // Signal the mainloop to resume any waiting threads.
    pa_threaded_mainloop_signal(manager->mainloop, 0);
}

/**
 * @brief Callback function handling the completion of the "move" operation.
 *
 * @param c The PulseAudio context.
 * @param success A flag indicating the success or failure of the operation.
 * @param userdata User-provided data, expected to be a pointer to a pulseaudio_manager instance.
 */
static void operation_complete_move_cb(pa_context *c, int success, void *userdata) {
    pulseaudio_manager* manager = (pulseaudio_manager*) userdata;

    if (success) {
        pa_operation* unmute_op = pa_context_set_sink_input_mute(c, manager->current_device_index, 0, operation_complete_unmute_cb, manager);
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
 * @param userdata User-provided data, expected to be a pointer to a pulseaudio_manager instance.
 */
static void operation_complete_mute_cb(pa_context *c, int success, void *userdata) {
    pulseaudio_manager* manager = (pulseaudio_manager*) userdata;
    uint32_t target_device_index = manager->current_device_index;

    if (success) {
        pa_operation* move_op = pa_context_move_sink_input_by_index(c, manager->current_device_index, target_device_index, operation_complete_move_cb, manager);
        pa_operation_unref(move_op);
    } else {
        // Handle error...
        pa_threaded_mainloop_signal(manager->mainloop, 0);
    }
}

/**
 * @brief Callback function to handle each device input.
 * @param c The PulseAudio context.
 * @param i The device input information.
 * @param eol End of list flag.
 * @param userdata User-provided data (expected to be a pointer to the target device index).
 */
static void switch_device_cb(pa_context *c, const pa_sink_input_info *i, int eol, void *userdata) {
    pulseaudio_manager* manager = (pulseaudio_manager*) userdata;

    if (!eol && i) {
        // Move this device input to the desired device
        pa_operation* move_op = pa_context_move_sink_input_by_index(c, i->index, manager->devices[manager->current_device_index].index, NULL, NULL);
        pa_operation_unref(move_op);
    }

    if (eol) {
        pa_threaded_mainloop_signal(manager->mainloop, 0);
    }
}

/**
 * @brief Switches the device (audio source) for the pulseaudio_manager.
 *
 * @param self Pointer to the pulseaudio_manager instance.
 * @param device_index Index of the device to switch to.
 * @return Boolean indicating success or failure.
 */
bool switch_device(pulseaudio_manager *self, uint32_t device_index) {
    // Ensure the context is valid
    if (!self || !self->context) {
        return false;
    }

    // Check if device_index is out of bounds
    if (device_index >= self->device_count) {
        fprintf(stderr, "[ERROR]: device_index out of bounds.\n");
        return false;
    }

    self->current_device_index = device_index;

    // Set the desired device as the default device
    /*fprintf(stderr, "[DEBUG]: self->context = %p\n", self->context);
    fprintf(stderr, "[DEBUG]: self->devices = %p\n", self->devices);
    fprintf(stderr, "[DEBUG]: device_index = %d\n", device_index);*/

    if (self->devices) {
        // Check if the name attribute is NULL
        if (!self->devices[device_index].code) {
            fprintf(stderr, "[ERROR]: Sink's name is NULL.\n");
            return false;
        }
        //fprintf(stderr, "[DEBUG]: self->devices[device_index].code = %s\n", self->devices[device_index].code);
    }
    pa_operation* set_default_op = pa_context_set_default_sink(self->context, self->devices[device_index].code, NULL, NULL);
    pa_operation_unref(set_default_op);

    // Use the introspect API to get a list of all device inputs
    pa_operation *op = pa_context_get_sink_input_info_list(self->context, switch_device_cb, self);

    if (!op) {
        return false;
    }

    while (pa_operation_get_state(op) == PA_OPERATION_RUNNING) {
        iterate(self, op);
    }

    pa_operation_unref(op);

    return true;
}


/**
 * @brief Process PulseAudio profiles using a null sink.
 *
 * This function initializes a null sink, iterates through all `pulseaudio_profile`s,
 * retrieves the number of channels from the device's channel map, assigns it to the
 * profile's `channels` attribute, and finally unloads the null sink.
 *
 * @param manager A pointer to the `pulseaudio_manager` structure.
 *
 * @note This function uses the `iterate` method from `pulseaudio_manager` to wait for PulseAudio operations to complete.
 *
 * @warning Ensure that the `pulseaudio_manager` is properly initialized before calling this function.
 */
void get_profile_channels(pulseaudio_manager *manager) {
    uint32_t module_index = PA_INVALID_INDEX;

    // Static variable to track if the mainloop is locked
    static int is_mainloop_locked = 0;

    // Check if the threaded mainloop is locked
    if (!is_mainloop_locked) {
        pa_threaded_mainloop_lock(manager->mainloop);
        is_mainloop_locked = 1;
    }

    // Callback to capture the module index when loading the null sink module
    void load_module_callback(pa_context *c, uint32_t idx, void *userdata) {
        (void) c;
        (void) userdata;
        module_index = idx;
    }

    // 1. Initialize a Null Sink
    pa_operation *load_op = pa_context_load_module(manager->context, "module-null-sink", "sink_name=easy_pulse_null_sink", load_module_callback, NULL);
    if (!load_op) {
        fprintf(stderr, "Failed to load null sink module\n");
        if (is_mainloop_locked) {
            pa_threaded_mainloop_unlock(manager->mainloop);
            is_mainloop_locked = 0;
        }
        return;
    }

    // Wait for the operation to complete and for the callback to capture the module index
    iterate(manager, load_op);

    // 2. Iterate through Profiles and Extract Channel Map
    for (uint32_t i = 0; i < manager->device_count; i++) {
        pulseaudio_device *device = &(manager->devices[i]);
        for (uint32_t j = 0; j < device->profile_count; j++) {
            pulseaudio_profile *profile = &(device->profiles[j]);

            // Extract the number of channels from the device's channel map
            int num_channels = device->channel_map.channels;

            // Update the pulseaudio_profile with the number of channels
            profile->channels = num_channels;
        }
    }

    // 3. Unload the Null Sink using the captured module index
    if (module_index != PA_INVALID_INDEX) {
        pa_operation *unload_op = pa_context_unload_module(manager->context, module_index, NULL, NULL);
        if (!unload_op) {
            fprintf(stderr, "Failed to unload null sink module\n");
            if (is_mainloop_locked) {
                pa_threaded_mainloop_unlock(manager->mainloop);
                is_mainloop_locked = 0;
            }
            return;
        }

        // Wait for the operation to complete
        iterate(manager, unload_op);
    } else {
        fprintf(stderr, "Invalid module index for null sink\n");
    }

    if (is_mainloop_locked) {
        pa_threaded_mainloop_unlock(manager->mainloop);
        is_mainloop_locked = 0;
    }
}


/**
 * @brief Retrieves the number of channels for a specified device.
 *
 * @param devices Pointer to an array of PulseSink structures.
 * @param device_index Index of the device whose number of channels is to be retrieved.
 *
 * @return Number of channels for the specified device. Returns -1 on error.
 */
int get_active_profile_channels(const pulseaudio_device *devices, int device_index) {
    if (!devices || device_index < 0) {
        fprintf(stderr, "[ERROR]: Invalid devices array or device index in get_device_channels.\n");
        return -1;  // Return -1 or another indicator of failure
    }
    return devices[device_index].channel_map.channels;
}



/**
 * @brief Callback function belonging to set_volume. Triggers when audio volume is set.
 *
 *
 * @param c        Pointer to the PulseAudio context.
 * @param success  Indicates the success (1) or failure (0) of the volume setting operation.
 * @param userdata User data provided during the set_volume operation, expected to be of type `pulseaudio_manager`.
 *
 * @note On failure, an error message is printed to stderr with the reason for the failure.
 * @note After the operations are processed, the function signals the main loop to continue.
 */
static void set_volume_cb1(pa_context *c, int success, void *userdata) {
    (void) c;
    pulseaudio_manager* manager = (pulseaudio_manager*) userdata;


    if (!success) {
        fprintf(stderr, "[ERROR]: Failed to set volume. Reason: %s\n", pa_strerror(pa_context_errno(c)));
    }

    // Debug: Print cvolume values for each channel as percentages
    //pa_cvolume cvolume = manager->devices[manager->active_device_index].volume;

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
 * @brief Callback function belonging to set_volume.
 * Triggers after audio volume levels are updated.
 *
 * @param c        Pointer to the PulseAudio context.
 * @param success  Indicates the success (1) or failure (0) of the volume setting operation.
 * @param userdata User data provided during the set_volume operation, expected to be of type `pulseaudio_manager`.
 *
 * @note On failure, an error message is printed to stderr with the reason for the failure.
 * @note After the operations are processed, the function signals the main loop to continue.
 */
static void set_volume_cb2(pa_context *c, const pa_sink_info *i, int eol, void *userdata) {
    (void) c;
    pulseaudio_manager *self = (pulseaudio_manager *)userdata;

    if (eol > 0) {
        //printf("[DEBUG, volume_check_cb()]: End-of-list reached.\n");
        self->operations_pending--;
        return;
    }

    if (!i) {
        fprintf(stderr, "[ERROR, volume_check_cb()]: Null pointer for pa_sink_info.\n");
        self->operations_pending--;
        return;
    }

    if (!i->name) {
        fprintf(stderr, "[ERROR, volume_check_cb()]: Null pointer for device name.\n");
        self->operations_pending--;
        return;
    }

    printf("[DEBUG, volume_check_cb()]: Processing device info for device: %s\n", i->name);

    // Update the volume values in the manager's devices structure
    self->active_device->volume = i->volume;

    // Print the volume for each channel
    for (int channel = 0; channel < i->volume.channels; channel++) {
        //float volume_percentage = (float)i->volume.values[channel] / PA_VOLUME_NORM * 100.0;
        //printf("Channel %d Volume: %.2f%%\n", channel, volume_percentage);
    }

    //Signaling to continue.
    pa_threaded_mainloop_signal(self->mainloop, 0);
}


/**
 * @brief Set the volume for a specified device.
 *
 * This function allows setting the volume for a specific device based on the given percentage.
 * The function performs various checks to ensure valid inputs and that the PulseAudio system is ready.
 * It will adjust the volume for all channels of the device to the desired level.
 *
 * @param self Pointer to the pulseaudio_manager instance.
 * @param device_index Index of the device to set the volume for.
 * @param percentage Desired volume level as a percentage.
 * @return Boolean indicating success or failure.
 */
bool set_volume(pulseaudio_manager *self, uint32_t device_index, float percentage) {
    if (!self || percentage < 0.0f || percentage > 100.0f || device_index >= self->device_count) {
        return false;
    }

    // Convert percentage to volume
    pa_volume_t volume = (percentage / 100.0) * PA_VOLUME_NORM;
    if (volume >= PA_VOLUME_NORM) {
        volume = PA_VOLUME_NORM - 1;
    }

    // Debug: show index and desired volume.
    //printf("[DEBUG, set_volume()] Index is: %i\n", device_index);
    //printf("[DEBUG, set_volume()] Desired volume: %f%% (value: %u)\n", percentage, volume);

    // Ensure PulseAudio is ready and devices are loaded
    if (self->pa_ready != 1 || self->devices_loaded != 1) {
        return false;
    }

    // Debug: Show channel volumes before the change.
    /*for (int channel = 0; channel < self->devices[device_index].channel_map.channels; channel++) {
        printf("[DEBUG, set_volume()]: Channel %d Before volume: %f%%\n",
               channel,
               100.0 * self->devices[device_index].volume.values[channel] / PA_VOLUME_NORM);
    }*/

    // Create a pa_cvolume structure and set the volume for all channels
    pa_cvolume cvolume;
    pa_cvolume_init(&cvolume);
    cvolume.channels = self->devices[device_index].channel_map.channels;  // Manually set channels
    pa_cvolume_set(&cvolume, cvolume.channels, volume);

    printf("[DEBUG, set_volume()] channels: %d\n", cvolume.channels);

    // Apply the volume change to the specific device by index and wait for the operation to complete
    const char *device_name_to_change = self->devices[device_index].code;
    self->operations_pending++;
    pa_operation *op = pa_context_set_sink_volume_by_name(self->context, device_name_to_change, &cvolume, set_volume_cb1, self);
    iterate(self, op);

    // Fetch the updated volume for the device and wait for the operation to complete
    pa_operation *op2 = pa_context_get_sink_info_by_name(self->context, device_name_to_change, set_volume_cb2, self);
    iterate(self, op2);

    return true;
}


/**
 * @brief Iterates through operations in the pulseaudio_manager.
 *
 * @param manager Pointer to the pulseaudio_manager instance.
 * @param op Pointer to the pa_operation instance.
 */
void iterate(pulseaudio_manager *manager, pa_operation *op) {
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
 * @brief Create a new pulseaudio_manager instance.
 * @return Returns a pointer to a manager structure.
 */
pulseaudio_manager *new_manager(void) {
    pulseaudio_manager *manager = (pulseaudio_manager *)malloc(sizeof(pulseaudio_manager));
    if (!manager) return NULL;


    // Initialize pointers to 0 -- good practice.
    manager->device_count = 0;
    manager->devices_loaded = 0;  // Initialize to 0
    manager->load_devices = load_devices;
    manager->destroy = destroy;
    manager->switch_device = switch_device;
    manager->set_volume = set_volume;
    manager->get_active_device = get_active_device;
    manager->iterate = iterate;
    manager->get_active_profile_channels = get_active_profile_channels;
    //manager->get_profiles_for_device = get_profiles_for_device;
    manager->get_profile_channels = get_profile_channels;
    manager->get_profile_count = get_profile_count;

    if(!initialize(manager)) {
        free(manager->devices);
        free(manager);
        return NULL;
    }

    manager->device_count = get_output_device_count();
    manager->devices = malloc(manager->device_count * sizeof(pulseaudio_manager));

    if(!manager->devices) {
        fprintf(stderr, "[new_manager()] Could not allocate memory for the manager devices.\n");
        return NULL;
    }

#if 0
    if (!load_devices(manager)) {
        fprintf(stderr, "No audio devices were detected. Aborting.\n");
        destroy(manager);
        return NULL;
    }
#endif

    return manager;
}

/**
 * @brief Callback function to process the PulseAudio server information.
 *
 * This callback is invoked once the server information is available. It retrieves the default device name
 * from the server response and determines the active device by matching the name with the available devices
 * in the manager's list.
 *
 * @param c      Pointer to the PulseAudio context.
 * @param info   Pointer to the pa_server_info structure containing the server's information.
 * @param userdata User-provided data (expected to be a pointer to pulseaudio_manager).
 */
static void get_active_device_cb(pa_context *c, const pa_server_info *info, void *userdata) {
    (void) c;
    pulseaudio_manager *manager = (pulseaudio_manager *)userdata;

    if (!info || !info->default_sink_name) {
        fprintf(stderr, "[ERROR]: Null pointer in active_device_cb.\n");
        pa_threaded_mainloop_signal(manager->mainloop, 0);
        return;
    }

    // Get the default device name from the server information
    const char *default_device_name = info->default_sink_name;

    // Iterate over the available devices to find the active one
    for (uint32_t i = 0; i < manager->device_count; i++) {
        if (strcmp(manager->devices[i].code, default_device_name) == 0) {
            // Set the active device pointer when a match is found
            manager->active_device = &manager->devices[i];
            break;
        }
    }
    pa_threaded_mainloop_signal(manager->mainloop, 0);
}


/**
 * @brief Request the default device name from the PulseAudio server and determine the active device.
 *
 * This function initiates a request to retrieve the default device name (active device) from the PulseAudio server.
 * Once the server provides this information, the active_device_cb callback is triggered to process the response.
 *
 * @param manager Pointer to the pulseaudio_manager instance.
 */
//TODO: REWORK THE WAITING LOGIC.
void get_active_device(pulseaudio_manager *manager) {
    pa_threaded_mainloop_lock(manager->mainloop);

    // Request server information
    pa_context_get_server_info(manager->context, get_active_device_cb, manager);

    // Wait until the iterate function signals that it's done
    pa_threaded_mainloop_wait(manager->mainloop);

    pa_threaded_mainloop_unlock(manager->mainloop);

    // The original loop to wait for active_device_name to be set
    int timeout = 50;  // Number of iterations or timeout value
    while (!manager->active_device->code && timeout-- > 0) {
        manager->iterate(manager, NULL);
        pa_threaded_mainloop_signal(manager->mainloop, 0);
    }
}


/**
 * @brief Frees the memory of a pulseaudio_manager instance.
 *
 * @param manager Pointer to the pulseaudio_manager instance to be deleted.
 */
void destroy(pulseaudio_manager *self) {
    cleanup(self);

    if (self) {
        for (uint32_t i = 0; i < self->device_count; i++) {
            if (self->devices[i].code) {
                free(self->devices[i].code);
                self->devices[i].code = NULL;
            }
            if (self->devices[i].description) {
                free(self->devices[i].description);
                self->devices[i].description = NULL;
            }
        }
        // Now, free profiles for each device (if they exist)
        for (uint32_t i = 0; i < self->device_count; i++) {
            if (self->devices[i].profiles) free(self->devices[i].profiles);
        }
        if (self->devices) free(self->devices);
        free(self);
    }
}
