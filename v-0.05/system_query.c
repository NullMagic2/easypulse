#include "system_query.h"
#include <pulse/mainloop-api.h>
#include <pulse/mainloop.h>
#include <pulse/operation.h>
#include <pulse/thread-mainloop.h>
#include <alsa/asoundlib.h>
#include <stdint.h>
#include <stdio.h>

// Since pulseaudio uses callbacks, we need something that will allow us to share data
// between functions.
typedef struct {
    pa_threaded_mainloop *mainloop;
    pa_mainloop_api *mainloop_api;
    pa_context *context;
} _shared_data_1;

static _shared_data_1 shared_data_1;

// Structure to share data between get_alsa_name and its callback.
typedef struct {
    const char *alsa_name;
    const char *alsa_id;
} _shared_data_2;

static _shared_data_2 shared_data_2 = {.alsa_name = NULL};


// Structure to share data between get_available_sinks its callback.
typedef struct {
    pa_sink_info **sinks;  // An array of pointers to pa_sink_info
    uint32_t count;
} _shared_data_3;

static _shared_data_3 shared_data_3;



static void pulse_cleanup(void);
static bool is_pulse_initialized(void);
static void get_device_count_cb(pa_context *c, const pa_card_info *i, int eol, void *userdata);
static void get_profile_count_cb(pa_context *c, const pa_card_info *i, int eol, void *userdata);

/**
 * @brief Iterates through operations in the pulseaudio threaded loop.
 *
 * @param loop Pointer to the threaded loop instance.
 * @param op Pointer to the pa_operation instance.
 */
static void iterate(pa_operation *op) {
    //Leaves if operation is invalid.
    if (!op) return;

    bool is_in_mainloop_thread = pa_threaded_mainloop_in_thread(shared_data_1.mainloop);

    // If we're not in the mainloop thread, lock it.
    if (!is_in_mainloop_thread) {
        pa_threaded_mainloop_lock(shared_data_1.mainloop);
    }

    //Wait for the operation to complete.
    //The signaling to continue is performed inside the callback operation (op).
    pa_threaded_mainloop_wait(shared_data_1.mainloop);

    //Cleaning up.
    pa_operation_unref(op);

    // If we locked the mainloop earlier, unlock it now.
    if (!is_in_mainloop_thread) {
        pa_threaded_mainloop_unlock(shared_data_1.mainloop);
    }

}

/**
 * @brief Callback function to handle changes in PulseAudio context state.
 *
 * This function is triggered whenever the state of the PulseAudio context changes.
 * It signals the mainloop to continue execution whenever the context is in a READY, FAILED,
 * or TERMINATED state.
 *
 * @param c Pointer to the PulseAudio context.
 * @param userdata Pointer to the data shared with the main function, which contains the mainloop.
 */
static void context_state_cb(pa_context *c, void *userdata) {
    (void) userdata;
    pa_context_state_t state = pa_context_get_state(c);

    // If the context is in a READY, FAILED, or TERMINATED state, signal the mainloop to continue.
    if (state == PA_CONTEXT_READY || state == PA_CONTEXT_FAILED || state == PA_CONTEXT_TERMINATED) {
        pa_threaded_mainloop_signal(shared_data_1.mainloop, 0);
    }
}

/**
 * @brief Utility function to check if PulseAudio is initialized and in a READY state.
 *
 * This function checks the state of the PulseAudio context and other key PulseAudio resources
 * to determine if PulseAudio has been successfully initialized and is in a READY state.
 *
 * @return True if PulseAudio is initialized and in a READY state, false otherwise.
 */
static bool is_pulse_initialized(void) {


    // Check if the main components of PulseAudio (mainloop, mainloop_api, and context) are initialized.
    if (shared_data_1.mainloop && shared_data_1.mainloop_api && shared_data_1.context) {
        pa_context_state_t state = pa_context_get_state(shared_data_1.context);

        // If the context is in a READY state, PulseAudio is initialized.
        if (state == PA_CONTEXT_READY) {
            return true;
        }
    }
    return false;
}

bool initialize_pulse() {
    shared_data_1.mainloop = pa_threaded_mainloop_new();
    if (!shared_data_1.mainloop) {
        fprintf(stderr, "Failed to create mainloop.\n");
        return false;
    }

    shared_data_1.mainloop_api = pa_threaded_mainloop_get_api(shared_data_1.mainloop);
    if (!shared_data_1.mainloop_api) {
        fprintf(stderr, "Failed to get mainloop API.\n");
        pa_threaded_mainloop_free(shared_data_1.mainloop);
        return false;
    }

    shared_data_1.context = pa_context_new(shared_data_1.mainloop_api, "Easypulse query API");
    if (!shared_data_1.context) {
        fprintf(stderr, "Failed to create context.\n");
        pa_threaded_mainloop_free(shared_data_1.mainloop);
        return false;
    }


    pa_context_set_state_callback(shared_data_1.context, context_state_cb, shared_data_1.mainloop);

    // Start the threaded mainloop
    pa_threaded_mainloop_start(shared_data_1.mainloop);

    // Lock the mainloop and connect the context
    pa_threaded_mainloop_lock(shared_data_1.mainloop);
    if (pa_context_connect(shared_data_1.context, NULL, PA_CONTEXT_NOFLAGS, NULL) < 0) {
        pa_threaded_mainloop_unlock(shared_data_1.mainloop);
        pulse_cleanup();
        return false;
    }

    // Wait for the context to be ready
    while (true) {
        pa_context_state_t state = pa_context_get_state(shared_data_1.context);
        if (state == PA_CONTEXT_READY) {
            break;
        } else if (state == PA_CONTEXT_FAILED || state == PA_CONTEXT_TERMINATED) {
            pa_threaded_mainloop_unlock(shared_data_1.mainloop);
            pulse_cleanup();
            return false;
        }
        pa_threaded_mainloop_wait(shared_data_1.mainloop);
    }

    pa_threaded_mainloop_unlock(shared_data_1.mainloop);
    return true;
}


/**
 * @brief Cleanup function to properly disconnect and free PulseAudio resources.
 *
 * This function ensures that all the PulseAudio resources are properly disconnected,
 * dereferenced, and freed to avoid any resource leaks. It should be called whenever
 * PulseAudio operations are done and the program wishes to terminate or release PulseAudio.
 */
static void pulse_cleanup(void) {
    if (shared_data_1.context) {
        // Check if the context is in a state where it can be disconnected
        pa_context_state_t state = pa_context_get_state(shared_data_1.context);
        if (state == PA_CONTEXT_READY) {
            pa_context_disconnect(shared_data_1.context);
        }
        pa_context_unref(shared_data_1.context);
        shared_data_1.context = NULL;
    }
    if (shared_data_1.mainloop) {
        pa_threaded_mainloop_free(shared_data_1.mainloop);
        shared_data_1.mainloop = NULL;
    }
}

/**
 * @brief Callback function used to retrieve the count of audio profiles for a specific card.
 *
 * This function is called by PulseAudio when querying for the list of profiles for a given card index.
 * It sets the profile_count with the number of profiles available for the card.
 * If there's an error or the list is exhausted, the function signals the mainloop to continue
 * without further processing.
 *
 * @param c Pointer to the PulseAudio context.
 * @param i Pointer to the current card information.
 * @param eol Indicates the end of the list or an error.
 * @param userdata Pointer to the data shared with the main function, which contains the mainloop
 *                 and the profile_count to be set.
 */
static void get_profile_count_cb(pa_context *c, const pa_card_info *i, int eol, void *userdata) {
    (void) c;

    uint32_t *profile_count = (uint32_t *) userdata;

    // Handle errors in retrieving profile count.
    if (eol < 0) {
        fprintf(stderr, "Failed to get profile count.\n");
        pa_threaded_mainloop_signal(shared_data_1.mainloop, 0);
        return;
    }

    // If we've reached the end of the list of profiles, signal the mainloop to continue.
    if (eol > 0) {
        pa_threaded_mainloop_signal(shared_data_1.mainloop, 0);
        return;
    }

    // Set the profile_count with the number of profiles available for the card.
    *profile_count = i->n_profiles;
}


/**
 * @brief Retrieve the count of audio profiles for a specific card.
 *
 * This function queries PulseAudio to get a count of all available audio profiles
 * for a specified card index. If PulseAudio is not initialized, the function attempts
 * to initialize it. If there's an error in fetching the profile count or initializing
 * PulseAudio, it returns UINT32_MAX.
 *
 * @param card_index The index of the card for which to retrieve the profile count.
 * @return Count of audio profiles or UINT32_MAX on error.
 */
uint32_t get_profile_count(uint32_t card_index) {
    uint32_t profile_count = 0;  // Initialize profile count to zero.
    pa_operation *count_op = NULL;

    // Check if PulseAudio is initialized.
    if (!is_pulse_initialized()) {
        // Attempt to initialize PulseAudio if it's not already initialized.
        if (!initialize_pulse()) {
            fprintf(stderr, "get_profiles_count(): failed to initialize pulseaudio.\n");
            return UINT32_MAX;  // Return error if initialization fails.
        }
    }

    // Query PulseAudio for the list of audio profiles for the specified card.
    // The callback get_profile_count_cb will populate the profile_count variable.
    count_op = pa_context_get_card_info_by_index(shared_data_1.context, card_index, get_profile_count_cb, &profile_count);

    // Wait for the PulseAudio operation to complete.
    iterate(count_op);

    return profile_count;  // Return the total count of profiles.
}

/**
 * @brief Callback function used to populate the list of available audio sinks.
 *
 * This function is called for each audio sink found by PulseAudio when querying
 * for the list of sinks. The function populates the `shared_sink_data` structure
 * with `pa_sink_info` for each sink. When the list is exhausted or there's an error,
 * the function exits without further processing.
 *
 * @param c Pointer to the PulseAudio context.
 * @param i Pointer to the current audio sink information.
 * @param eol Indicates the end of the list or an error.
 * @param userdata Pointer to the data shared with the main function.
 */
static void get_available_sinks_cb(pa_context *c, const pa_sink_info *i, int eol, void *userdata) {
    (void) c;
    (void) userdata;

    uint32_t *count = &shared_data_3.count;

    // Handle errors in retrieving sink information.
    if (eol < 0) {
        fprintf(stderr, "Failed to get sink info.\n");
        pa_threaded_mainloop_signal(shared_data_1.mainloop, 0);
        return;
    }

    // If we've reached the end of the list of sinks, mark the end and exit the callback.
    if (eol > 0) {
        shared_data_3.sinks[*count] = NULL; // Set the last element to NULL as sentinel
        pa_threaded_mainloop_signal(shared_data_1.mainloop, 0);
        return;
    }

    // Allocate memory for the pa_sink_info structure.
    shared_data_3.sinks[*count] = malloc(sizeof(pa_sink_info));
    if (shared_data_3.sinks[*count] == NULL) {
        fprintf(stderr, "Failed to allocate memory for sink info.\n");
        pa_threaded_mainloop_signal(shared_data_1.mainloop, 0);
        return;
    }

    // Copy the pa_sink_info structure.
    *(shared_data_3.sinks[*count]) = *i;

    // Duplicate the strings to ensure they remain valid.
    if (i->name != NULL) {
        shared_data_3.sinks[*count]->name = strdup(i->name);
    }
    if (i->description != NULL) {
        shared_data_3.sinks[*count]->description = strdup(i->description);
    }

    (*count)++;
}

/**
 * @brief Retrieve the list of available audio sinks for a specific card.
 *
 * This function queries PulseAudio to get a list of all available audio sinks
 * for a specified card index. It dynamically allocates memory based on the count
 * of sinks to store the list of sinks.
 * If PulseAudio is not initialized, the function attempts to initialize it.
 *
 * @param card_index The index of the card for which to retrieve the sinks.
 * @return Pointer to the first sink in the list or NULL on error.
 */
pa_sink_info **get_available_sinks() {
    pa_operation *op = NULL;

    // Using get_device_count() to obtain the number of sinks
    uint32_t max_sinks = get_device_count();

    // Allocate memory for pointers
    shared_data_3.sinks = malloc((max_sinks + 1) * sizeof(pa_sink_info*));
    shared_data_3.count = 0; // Reset count

    // Check for successful memory allocation
    if (!shared_data_3.sinks) {
        fprintf(stderr, "Failed to allocate memory for sinks.\n");
        return NULL;
    }

    // Check if PulseAudio is initialized
    if (!is_pulse_initialized()) {
        // Attempt to initialize PulseAudio if it's not already initialized
        if (!initialize_pulse()) {
            fprintf(stderr, "get_available_sinks(): failed to initialize pulseaudio.\n");
            free(shared_data_3.sinks);
            shared_data_3.sinks = NULL;
            return NULL;
        }
    }


    // Query PulseAudio for the list of available sinks for the specified card
    op = pa_context_get_sink_info_list(shared_data_1.context, get_available_sinks_cb, NULL);

    // Wait for the PulseAudio operation to complete
    iterate(op);

    return shared_data_3.sinks;
}

void delete_devices(pa_sink_info **sinks) {
    if (sinks == NULL) {
        return;
    }

    for (int i = 0; sinks[i] != NULL; ++i) {
        if (sinks[i]->name) {
            free((char*)sinks[i]->name);
        }
        if (sinks[i]->description) {
            free((char*)sinks[i]->description);
        }
        free(sinks[i]);
    }

    free(sinks);
}


/**
 * @brief Callback function used to count the available audio devices (cards).
 *
 * This function is called for each audio device found by PulseAudio when querying
 * for the list of devices. The function increments the device_count for each device.
 * When the list is exhausted or there's an error, it signals the mainloop to continue.
 *
 * @param c Pointer to the PulseAudio context.
 * @param i Pointer to the current audio device information.
 * @param eol Indicates the end of the list or an error.
 * @param userdata Pointer to the data shared with the main function.
 */
static void get_device_count_cb(pa_context *c, const pa_card_info *i, int eol, void *userdata) {
    (void) c;
    (void) i;

    uint32_t *device_count = (uint32_t *) userdata;

    if (eol < 0) {
        // Handle error
        //fprintf(stderr,"[DEBUG, get_device_count_cb()] Reached elo < 0. Signal triggered.\n");
        pa_threaded_mainloop_signal(shared_data_1.mainloop, 0);
        return;
    }
    if (eol > 0) {
        // End of list
        //fprintf(stderr,"[DEBUG, get_device_count_cb()] Reached elo > 0. Signal triggered.\n");
        pa_threaded_mainloop_signal(shared_data_1.mainloop, 0);
        return;
    }

    ++(*device_count);
}



/**
 * @brief Retrieve the count of audio devices in the system.
 *
 * This function queries PulseAudio to get a count of all available audio devices (cards).
 * If PulseAudio is not initialized, the function attempts to initialize it. If the initialization
 * fails or there's an error in fetching the device count, it returns UINT32_MAX.
 *
 * @return Count of audio devices or UINT32_MAX on error.
 */
uint32_t get_device_count(void) {
    uint32_t device_count = 0;  // Initialize device count to zero.
    pa_operation *count_op = NULL;

    // Check if PulseAudio is initialized.
    if (!is_pulse_initialized()) {
        // Attempt to initialize PulseAudio if it's not already initialized.
        if (!initialize_pulse()) {
            fprintf(stderr, "get_device_count(): failed to initialize pulseaudio.\n");
            return UINT32_MAX;  // Return error if initialization fails.
        }
    }

    // Check if context is valid after initialization attempt.
    if (!shared_data_1.context) {
        fprintf(stderr, "Context is NULL in get_device_count.\n");
        return UINT32_MAX;
    }

    //fprintf(stderr,"[get_device_count()] context is, %p\n",&shared_data_1.context);
    //fprintf(stderr,"[get_device_count()] mainloop is, %p\n",&shared_data_1.mainloop);

    // Query PulseAudio for the list of audio devices (cards).
    // The callback get_device_count_cb will increment the device_count for each device found.
    count_op = pa_context_get_card_info_list(shared_data_1.context, get_device_count_cb, &device_count);

    // Wait for the PulseAudio operation to complete.
    iterate(count_op);

    return device_count;  // Return the total count of devices.
}

/**
 * @brief Retrieves the maximum number of channels for the given ALSA device name.
 *
 * This function opens the specified ALSA device in playback mode, retrieves
 * its hardware parameters, and then queries for the maximum number of channels
 * supported by the device.
 *
 * @param alsa_name Name of the ALSA device.
 * @return Maximum number of channels supported by the device or -1 on error.
 */
int get_max_channels(const char *alsa_id, const pa_sink_info *sink_info) {
    snd_pcm_t *handle;
    snd_pcm_hw_params_t *params;
    unsigned int max_channels = 0;
    int err;

    if (!alsa_id || !sink_info) {
        fprintf(stderr, "Invalid parameters provided.\n");
        return -1;
    }

    if ((err = snd_pcm_open(&handle, alsa_id, SND_PCM_STREAM_PLAYBACK, SND_PCM_NONBLOCK)) < 0) {
        //fprintf(stderr, "Unable to open PCM device: %s, error: %s\n", alsa_id, snd_strerror(err));
        return sink_info->sample_spec.channels;  // Return channels from PulseAudio if ALSA fails
    }

    snd_pcm_hw_params_alloca(&params);
    if ((err = snd_pcm_hw_params_any(handle, params)) < 0) {
        //fprintf(stderr, "Cannot initialize hardware parameter structure: %s\n", snd_strerror(err));
        snd_pcm_close(handle);
        return sink_info->sample_spec.channels;
    }

    if ((err = snd_pcm_hw_params_get_channels_max(params, &max_channels)) < 0) {
        //fprintf(stderr, "Error getting max channels for device: %s, error: %s\n", alsa_id, snd_strerror(err));
        snd_pcm_close(handle);
        //fprintf(stderr, "[DEBUG, get_max_channels()] sink_info->sample_spec.channels is %i\n", sink_info->sample_spec.channels);
        return sink_info->sample_spec.channels;
    }

    snd_pcm_close(handle);
    return max_channels;
}

/**
 * @brief Retrieves the minimum number of channels for the given ALSA device id.
 *
 * This function opens the specified ALSA device in playback mode, retrieves
 * its hardware parameters, and then queries for the minimum number of channels
 * supported by the device.
 *
 * @param alsa_name Name of the ALSA device.
 * @return Minimum number of channels supported by the device or -1 on error.
 */
int get_min_channels(const char *alsa_id, const pa_sink_info *sink_info) {
    snd_pcm_t *handle;
    snd_pcm_hw_params_t *params;
    unsigned int min_channels = 0;
    int err;

    //fprintf(stderr, "[DEBUG, get_min_channels()] sink_info->sample_spec.channels is %i\n", sink_info->sample_spec.channels);

    if (!alsa_id || !sink_info) {
        fprintf(stderr, "Invalid parameters provided.\n");
        return -1;
    }

    if ((err = snd_pcm_open(&handle, alsa_id, SND_PCM_STREAM_PLAYBACK, SND_PCM_NONBLOCK)) < 0) {
        //fprintf(stderr, "Unable to open PCM device: %s, error: %s\n", alsa_id, snd_strerror(err));
        return sink_info->sample_spec.channels;
    }

    snd_pcm_hw_params_alloca(&params);
    if ((err = snd_pcm_hw_params_any(handle, params)) < 0) {
        //fprintf(stderr, "Cannot initialize hardware parameter structure: %s\n", snd_strerror(err));
        snd_pcm_close(handle);
        return sink_info->sample_spec.channels;
    }

    if ((err = snd_pcm_hw_params_get_channels_min(params, &min_channels)) < 0) {
        //fprintf(stderr, "Error getting min channels for device: %s, error: %s\n", alsa_id, snd_strerror(err));
        snd_pcm_close(handle);
        return sink_info->sample_spec.channels;
    }

    snd_pcm_close(handle);
    return min_channels;
}


/**
 * @brief Callback function to retrieve the ALSA name from the card info.
 *
 * This callback checks for the "alsa.card_name" property in the card's
 * proplist. If the property is found, it assigns the property's value
 * to the shared data structure for further use.
 *
 * @param c Pointer to the PulseAudio context.
 * @param i Pointer to the card info structure.
 * @param eol Flag indicating end of list.
 * @param userdata User-defined data pointer (unused in this callback).
 */
static void get_alsa_name_cb(pa_context *c, const pa_sink_info *i, int eol, void *userdata) {
    (void) c;

    if (eol) {
        pa_threaded_mainloop_signal(shared_data_1.mainloop, 0);
        return;
    }

    const char *target_sink_name = userdata;
    if (target_sink_name && strcmp(target_sink_name, i->name) != 0) {
        return; // Skip this sink, as it does not match the specified name
    }

    const char *alsa_name = pa_proplist_gets(i->proplist, "alsa.card_name");
    if (alsa_name) {
        shared_data_2.alsa_name = strdup(alsa_name);
        if (!shared_data_2.alsa_name) {
            fprintf(stderr, "Failed to allocate memory for ALSA name.\n");
        }
        pa_threaded_mainloop_signal(shared_data_1.mainloop, 0);
    }
}



/**
 * @brief Retrieves the ALSA name of the first audio device encountered.
 *
 * This function initializes PulseAudio (if not already initialized), then
 * queries PulseAudio for the list of audio devices. It waits for the
 * retrieval operation to complete and then returns the ALSA name
 * of the first device it finds with the "alsa.card_name" property.
 *
 * @return ALSA name of the device or NULL if not found or on error.
 */
const char* get_alsa_name(const char *sink_name) {
    pa_operation *name_op;

    shared_data_2.alsa_name = NULL;  // Ensure alsa_name is initialized to NULL


    if (!is_pulse_initialized() && !initialize_pulse()) {
        fprintf(stderr, "get_alsa_name(): PulseAudio initialization failed.\n");
        return NULL;
    }

    name_op = pa_context_get_sink_info_list(shared_data_1.context, get_alsa_name_cb, (void*)sink_name);
    iterate(name_op);

    return shared_data_2.alsa_name;
}

/**
 * @brief Callback function to retrieve the ALSA device string based on the PulseAudio sink information.
 *
 * This function is called for each available PulseAudio sink. It checks if the sink's name matches the
 * target sink name provided in the userdata. If a match is found, it retrieves the "alsa.card" and
 * "alsa.device" properties from the sink's proplist, constructs the ALSA device string in the format
 * "hw:<card>,<device>", and stores it in shared data for later retrieval.
 *
 * @param c Pointer to the PulseAudio context.
 * @param i Pointer to the sink information structure.
 * @param eol End of list flag. If non-zero, indicates the end of the sink list.
 * @param userdata User-defined data pointer. In this case, it points to the target sink name string.
 */
#include <ctype.h> // Include this for the isdigit() function

static void get_alsa_id_cb(pa_context *c, const pa_sink_info *i, int eol, void *userdata) {
    (void) c; // Unused parameter

    // Check for end of list
    if (eol) {
        // Signal the main loop to unblock the iterate function
        //fprintf(stderr,"[DEBUG, get_alsa_id_cb()] End of function reached.\n");
        pa_threaded_mainloop_signal(shared_data_1.mainloop, 0);
        free(userdata);
        return;
    }

    // Retrieve the target sink name from userdata
    const char *target_sink_name = userdata;

    // If a target sink name is provided, check if it matches the current sink's name
    if (target_sink_name && strcmp(target_sink_name, i->name) != 0) {
        return; // Skip this sink, as it does not match the specified name
    }

    // Attempt to retrieve the "alsa.card" and "alsa.device" properties
    const char *alsa_card = pa_proplist_gets(i->proplist, "alsa.card");
    const char *alsa_device = pa_proplist_gets(i->proplist, "alsa.device");

    //fprintf(stderr, "[DEBUG, get_alsa_id_cb()], alsa.card is %s\n", alsa_card);
    //fprintf(stderr, "[DEBUG, get_alsa_id_cb()], alsa.device is %s\n", alsa_device);

    // Check if both properties are available and alsa.device is a digit
    if (alsa_card && alsa_device && isdigit((unsigned char)alsa_device[0])) {
        // Construct the ALSA device string
        char alsa_device_string[128];
        snprintf(alsa_device_string, sizeof(alsa_device_string), "hw:%s,%s", alsa_card, alsa_device);

        // Store the ALSA device string in shared data
        shared_data_2.alsa_id = strdup(alsa_device_string);
        if (!shared_data_2.alsa_id) {
            fprintf(stderr, "Failed to allocate memory for ALSA device id.\n");
        }
    } else {
        fprintf(stderr, "  - ALSA properties not found or invalid for sink.\n");
    }

    //fprintf(stderr, "[DEBUG, get_alsa_id_cb()] alsa ID is %s\n", shared_data_2.alsa_id);

    // Signal the main loop to unblock the iterate function
    pa_threaded_mainloop_signal(shared_data_1.mainloop, 0);
}


/**
 * @brief Retrieves the ALSA device string for a given PulseAudio sink name.
 *
 * This function initializes PulseAudio (if not already initialized), then queries PulseAudio for the
 * information of a specific sink by its name. It waits for the retrieval operation to complete and then
 * returns the constructed ALSA device string based on the "alsa.card" and "alsa.device" properties of
 * the sink.
 *
 * @param sink_name The name of the PulseAudio sink.
 * @return ALSA device string in the format "hw:<card>,<device>" or NULL if not found or on error.
 */
const char* get_alsa_id(const char *sink_name) {
    pa_operation *op;

    // Check if PulseAudio is initialized
    if (!is_pulse_initialized()) {
        // Attempt to initialize PulseAudio if it's not already initialized
        if (!initialize_pulse()) {
            fprintf(stderr, "get_alsa_id(): PulseAudio initialization failed.\n");
            return NULL;
        }
    }

    // Make a copy of the sink name to ensure it remains valid
    char *sink_name_copy = strdup(sink_name);
    if (!sink_name_copy) {
        fprintf(stderr, "get_alsa_id(): Failed to allocate memory for sink name.\n");
        return NULL;
    }

    // Query PulseAudio for the information of the specified sink
    //fprintf(stderr,"[DEBUG, get_alsa_id()] sink name is: %s\n", sink_name_copy);
    op = pa_context_get_sink_info_by_name(shared_data_1.context, sink_name_copy, get_alsa_id_cb, sink_name_copy);

    // Wait for the PulseAudio operation to complete
    iterate(op);

    // Return the ALSA device string retrieved by the callback function
    return shared_data_2.alsa_id;
}

/**
 * @brief Retrieves the sample rate of the given ALSA device.
 *
 * This function opens the specified ALSA device in playback mode, retrieves
 * its hardware parameters, and then queries for the sample rate.
 *
 * @param alsa_id Name of the ALSA device.
 * @param sink_info Pointer to a PulseAudio sink_info structure.
 * @return Sample rate of the device or -1 on error.
 */
int get_sample_rate(const char *alsa_id, const pa_sink_info *sink_info) {
    snd_pcm_t *handle;
    snd_pcm_hw_params_t *params;
    unsigned int sample_rate;
    int err;

    if (!alsa_id || !sink_info) {
        fprintf(stderr, "Invalid parameters provided.\n");
        return -1;
    }

    if ((err = snd_pcm_open(&handle, alsa_id, SND_PCM_STREAM_PLAYBACK, SND_PCM_NONBLOCK)) < 0) {
        fprintf(stderr, "Unable to open PCM device: %s, error: %s\n", alsa_id, snd_strerror(err));
        return sink_info->sample_spec.rate;  // Return sample rate from PulseAudio if ALSA fails
    }

    snd_pcm_hw_params_alloca(&params);
    if ((err = snd_pcm_hw_params_any(handle, params)) < 0) {
        fprintf(stderr, "Cannot initialize hardware parameter structure: %s\n", snd_strerror(err));
        snd_pcm_close(handle);
        return sink_info->sample_spec.rate;
    }

    if ((err = snd_pcm_hw_params_get_rate(params, &sample_rate, 0)) < 0) {
        //fprintf(stderr, "Error getting sample rate for device: %s, error: %s\n", alsa_id, snd_strerror(err));
        snd_pcm_close(handle);
        return sink_info->sample_spec.rate;
    }

    snd_pcm_close(handle);
    return sample_rate;
}



