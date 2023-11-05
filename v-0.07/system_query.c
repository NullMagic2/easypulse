#include "system_query.h"
#include <pulse/mainloop-api.h>
#include <pulse/mainloop.h>
#include <pulse/operation.h>
#include <pulse/thread-mainloop.h>
#include <alsa/asoundlib.h>
#include <stdint.h>
#include <stdio.h>
#include <ctype.h> // Include this for the isdigit() function

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


// Structure to share data between get_available_output_devices and its callback.
typedef struct {
    pa_sink_info **sinks;  // An array of pointers to pa_sink_info
    uint32_t count;
} _shared_data_3;

static _shared_data_3 shared_data_3;

// Structure to share data between get_available_input_devices and its callback.
static struct {
    pa_source_info **sources; // Array of pointers to pa_source_info structures
    uint32_t count;           // Count of available sources
    uint32_t allocated;       // Allocated size of the sources array
} shared_data_sources = {NULL, 0, 0};

#define MAX_CHANNELS 32 // You can adjust this number based on expected maximum channels

typedef struct channel_info_s {
    char **channel_names; // Pointer to an array of string pointers
    int num_channels;     // Counter for the number of channels
} channel_info_t;


static void pulse_cleanup(void);
static bool is_pulse_initialized(void);
static void get_output_device_count_cb(pa_context *c, const pa_card_info *i, int eol, void *userdata);
static void get_profile_count_cb(pa_context *c, const pa_card_info *i, int eol, void *userdata);

// Utility function to print all properties in the proplist
void print_proplist(const pa_proplist *p) {
    void *state = NULL;
    const char *key;
    while ((key = pa_proplist_iterate(p, &state))) {
        const char *value = pa_proplist_gets(p, key);
        if (value) {
            printf("%s = %s\n", key, value);
        } else {
            printf("%s = <non-string value or not present>\n", key);
        }
    }
}

/**
 * @brief Iterates through operations in the pulseaudio threaded loop.
 *
 * @param loop Pointer to the threaded loop instance.
 * @param op Pointer to the pa_operation instance.
 */
static void iterate(pa_operation *op) {
    //fprintf(stderr, "[DEBUG] Entering %s\n", __FUNCTION__); // Debug statement for entry

    //Leaves if operation is invalid.
    if (!op) {
        fprintf(stderr, "[DEBUG] Operation is NULL\n"); // Debug statement for NULL operation
        return;
    }

    bool is_in_mainloop_thread = pa_threaded_mainloop_in_thread(shared_data_1.mainloop);
    //fprintf(stderr, "[DEBUG] Is in mainloop thread: %d\n", is_in_mainloop_thread); // Debug statement for thread check

    // If we're not in the mainloop thread, lock it.
    if (!is_in_mainloop_thread) {
        pa_threaded_mainloop_lock(shared_data_1.mainloop);
        //fprintf(stderr, "[DEBUG] Mainloop locked\n"); // Debug statement for mainloop locked
    }

    //Wait for the operation to complete.
    //The signaling to continue is performed inside the callback operation (op).
    while (pa_operation_get_state(op) == PA_OPERATION_RUNNING) {
        pa_threaded_mainloop_wait(shared_data_1.mainloop);
        //fprintf(stderr, "[DEBUG] Waiting in mainloop...\n"); // Debug message while waiting
    }

    //Debug code if needed.
    #if 0
    // Check the operation state after waiting
    switch (pa_operation_get_state(op)) {
        case PA_OPERATION_DONE:
            fprintf(stderr, "[DEBUG] Operation completed successfully\n"); // Debug message for successful completion
            break;
        case PA_OPERATION_CANCELLED:
            fprintf(stderr, "[DEBUG] Operation was cancelled\n"); // Debug message for cancellation
            break;
        case PA_OPERATION_RUNNING: // This case should not be possible after the wait
        default:
            fprintf(stderr, "[DEBUG] Operation is in an unexpected state: %d\n", pa_operation_get_state(op)); // Debug message for unexpected state
            break;
    }
    #endif

    //Cleaning up.
    pa_operation_unref(op);
    //fprintf(stderr, "[DEBUG] Operation unreferenced and cleaned up\n"); // Debug statement for cleanup

    // If we locked the mainloop earlier, unlock it now.
    if (!is_in_mainloop_thread) {
        pa_threaded_mainloop_unlock(shared_data_1.mainloop);
        //fprintf(stderr, "[DEBUG] Mainloop unlocked\n"); // Debug statement for mainloop unlocked
    }

    //fprintf(stderr, "[DEBUG] Exiting %s\n", __FUNCTION__); // Debug statement for exit
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

/**
 * @brief Initializes the PulseAudio mainloop and context for querying audio information.
 *
 * This function sets up the necessary PulseAudio components for subsequent queries
 * to the audio subsystem. It creates a new threaded mainloop, obtains the mainloop API,
 * and creates a new context with a specified name. It also starts the mainloop and
 * connects the context to the PulseAudio server, waiting until the context is ready
 * or an error occurs.
 *
 * @note If this function fails at any point, it ensures that all allocated resources are
 *       cleaned up before returning.
 *
 * @return true if the PulseAudio components were initialized successfully, false otherwise.
 */
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
static void get_available_output_devices_cb(pa_context *c, const pa_sink_info *i, int eol, void *userdata) {
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
pa_sink_info **get_available_output_devices() {
    pa_operation *op = NULL;

    // Using get_output_device_count() to obtain the number of sinks
    uint32_t max_sinks = get_output_device_count();

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
            fprintf(stderr, "get_available_output_devices(): failed to initialize pulseaudio.\n");
            free(shared_data_3.sinks);
            shared_data_3.sinks = NULL;
            return NULL;
        }
    }


    // Query PulseAudio for the list of available sinks for the specified card
    op = pa_context_get_sink_info_list(shared_data_1.context, get_available_output_devices_cb, NULL);

    // Wait for the PulseAudio operation to complete
    iterate(op);

    return shared_data_3.sinks;
}

/**
 * @brief Frees the memory allocated for an array of output devices (sinks).
 *
 * This function iterates over an array of `pa_sink_info` pointers, freeing the memory for
 * each sink's name and description strings, followed by the sink structure itself. It concludes
 * by freeing the memory for the array of pointers.
 *
 * @param sinks A pointer to the first element in an array of `pa_sink_info` pointers, which
 *              must be terminated with a NULL pointer. If NULL is passed, the function does nothing.
 */
void delete_output_devices(pa_sink_info **sinks) {
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
 * @brief Frees the memory allocated for an array of input devices (sources).
 *
 * This function iterates over an array of `pa_source_info` pointers, freeing the memory for
 * each source's name and description strings, followed by the source structure itself. It concludes
 * by freeing the memory for the array of pointers.
 *
 * @param sources A pointer to the first element in an array of `pa_source_info` pointers, which
 *                must be terminated with a NULL pointer. If NULL is passed, the function does nothing.
 */
void delete_input_devices(pa_source_info **sources) {
    if (!sources) {
        return; // Nothing to do if the pointer is NULL
    }

    // Iterate through each source info and free its memory
    for (int i = 0; sources[i] != NULL; i++) {
        if (sources[i]->name) {
            free((char*)sources[i]->name);
        }
        if (sources[i]->description) {
            free((char*)sources[i]->description);
        }
        free(sources[i]);
    }

    // Free the array of pointers itself
    free(sources);
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
static void get_output_device_count_cb(pa_context *c, const pa_card_info *i, int eol, void *userdata) {
    (void) c;
    (void) i;

    uint32_t *device_count = (uint32_t *) userdata;

    if (eol < 0) {
        // Handle error
        //fprintf(stderr,"[DEBUG, get_output_device_count_cb()] Reached elo < 0. Signal triggered.\n");
        pa_threaded_mainloop_signal(shared_data_1.mainloop, 0);
        return;
    }
    if (eol > 0) {
        // End of list
        //fprintf(stderr,"[DEBUG, get_output_device_count_cb()] Reached elo > 0. Signal triggered.\n");
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
uint32_t get_output_device_count(void) {
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
    count_op = pa_context_get_card_info_list(shared_data_1.context, get_output_device_count_cb, &device_count);

    // Wait for the PulseAudio operation to complete.
    iterate(count_op);

    return device_count;  // Return the total count of devices.
}

/**
 * @brief Retrieves the minimum number of channels for the given ALSA device name.
 *
 * This function opens the specified ALSA device in capture mode, retrieves
 * its hardware parameters, and then queries for the minimum number of channels
 * supported by the device.
 *
 * @param alsa_name Name of the ALSA device.
 * @return Minimum number of channels supported by the device or -1 on error.
 */
int get_min_input_channels(const char *alsa_id, const pa_source_info *source_info) {
    snd_pcm_t *handle;
    snd_pcm_hw_params_t *params;
    unsigned int min_channels = 0;
    int err;

    if ((!alsa_id) && (!source_info)) {
        fprintf(stderr, "Invalid parameters provided.\n");
        return -1;
    }
    //The input device was found, but there's no alsa information about it.
    //Attempt to retrieve used channels instead.
    else if((!alsa_id) && (source_info)) {
        return source_info->sample_spec.channels;
    }

    if ((err = snd_pcm_open(&handle, alsa_id, SND_PCM_STREAM_CAPTURE, SND_PCM_NONBLOCK)) < 0) {
        fprintf(stderr, "Unable to open PCM device: %s, error: %s\n", alsa_id, snd_strerror(err));
        return source_info->sample_spec.channels;  // Return channels from PulseAudio if ALSA fails
    }

    snd_pcm_hw_params_alloca(&params);
    if ((err = snd_pcm_hw_params_any(handle, params)) < 0) {
        fprintf(stderr, "Cannot initialize hardware parameter structure: %s\n", snd_strerror(err));
        snd_pcm_close(handle);
        return source_info->sample_spec.channels;
    }

    if ((err = snd_pcm_hw_params_get_channels_min(params, &min_channels)) < 0) {
        fprintf(stderr, "Error getting min channels for device: %s, error: %s\n", alsa_id, snd_strerror(err));
        snd_pcm_close(handle);
        return source_info->sample_spec.channels;
    }

    snd_pcm_close(handle);
    return min_channels;
}


/**
 * @brief Retrieves the maximum number of channels for the given ALSA device name.
 *
 * This function opens the specified ALSA device in capture mode, retrieves
 * its hardware parameters, and then queries for the maximum number of channels
 * supported by the device. If ALSA fails, it will fall back to using the
 * information from PulseAudio.
 *
 * @param alsa_name Name of the ALSA device.
 * @param source_info Information about the PulseAudio source.
 * @return Maximum number of channels supported by the device or -1 on error.
 */
int get_max_input_channels(const char *alsa_id, const pa_source_info *source_info) {
    snd_pcm_t *handle;
    snd_pcm_hw_params_t *params;
    unsigned int max_channels = 0;
    int err;

    if ((!alsa_id) && (!source_info)) {
        fprintf(stderr, "Invalid parameters provided.\n");
        return -1;
    }
    //The input device was found, but there's no alsa information about it.
    //Attempt to retrieve used channels instead.
    else if((!alsa_id) && (source_info)) {
        return source_info->sample_spec.channels;
    }

    // Try to open the ALSA device in capture mode
    if ((err = snd_pcm_open(&handle, alsa_id, SND_PCM_STREAM_CAPTURE, SND_PCM_NONBLOCK)) < 0) {
        fprintf(stderr, "Unable to open PCM device: %s, error: %s\n", alsa_id, snd_strerror(err));
        // ALSA failed, fall back to PulseAudio information
        return source_info->sample_spec.channels;
    }

    // Allocate hardware parameters object
    snd_pcm_hw_params_alloca(&params);

    // Fill it in with default values
    if ((err = snd_pcm_hw_params_any(handle, params)) < 0) {
        fprintf(stderr, "Cannot initialize hardware parameter structure: %s\n", snd_strerror(err));
        snd_pcm_close(handle);
        return source_info->sample_spec.channels;
    }

    // Get the maximum number of channels
    if ((err = snd_pcm_hw_params_get_channels_max(params, &max_channels)) < 0) {
        fprintf(stderr, "Error getting max channels for device: %s, error: %s\n", alsa_id, snd_strerror(err));
        snd_pcm_close(handle);
        return source_info->sample_spec.channels;
    }

    // Close the device
    snd_pcm_close(handle);

    return max_channels;
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
int get_max_output_channels(const char *alsa_id, const pa_sink_info *sink_info) {
    snd_pcm_t *handle;
    snd_pcm_hw_params_t *params;
    unsigned int max_channels = 0;
    int err;

    if ((!alsa_id) && (!sink_info)) {
        fprintf(stderr, "Invalid parameters provided.\n");
        return -1;
    }
    //The input device was found, but there's no alsa information about it.
    //Attempt to retrieve used channels instead.
    else if((!alsa_id) && (sink_info)) {
        return sink_info->sample_spec.channels;
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
int get_min_output_channels(const char *alsa_id, const pa_sink_info *sink_info) {
    snd_pcm_t *handle;
    snd_pcm_hw_params_t *params;
    unsigned int min_channels = 0;
    int err;

    //fprintf(stderr, "[DEBUG, get_min_output_channels()] sink_info->sample_spec.channels is %i\n", sink_info->sample_spec.channels);

    if ((!alsa_id) && (!sink_info)) {
        fprintf(stderr, "Invalid parameters provided.\n");
        return -1;
    }
    //The input device was found, but there's no alsa information about it.
    //Attempt to retrieve used channels instead.
    else if((!alsa_id) && (sink_info)) {
        return sink_info->sample_spec.channels;
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
 * @brief Callback function for retrieving the ALSA card name of a PulseAudio source.
 *
 * This callback is called by the PulseAudio context during the operation to retrieve
 * information about each available source. It is registered with the
 * pa_context_get_source_info_by_name() function call.
 *
 * @param c The PulseAudio context.
 * @param i The source information structure containing details about the source.
 * @param eol End of list indicator. If non-zero, indicates no more data to process.
 * @param userdata User data provided when registering the callback. In this case, it is
 *                 expected to be the name of the source for which we want the ALSA card name.
 *
 * @note This function is intended to be used internally and should not be called directly.
 *
 * @warning This function uses global shared data (shared_data_2.alsa_name) to store the
 *          retrieved ALSA name, and signals the main loop to stop waiting. Ensure that
 *          the main loop and shared data are properly managed.
 */
static void get_alsa_input_name_cb(pa_context *c, const pa_source_info *i, int eol, void *userdata) {
    (void) c;

    if (eol < 0 || !i) {
        pa_threaded_mainloop_signal(shared_data_1.mainloop, 0);
        return; // Handle error or invalid source info
    }
    if (eol > 0) {
        pa_threaded_mainloop_signal(shared_data_1.mainloop, 0);
        return; // End of list
    }

    // Check if this source is the one we're interested in
    if (userdata && strcmp(i->name, (const char *)userdata) == 0) {
        const char *prop_alsa_card_name = pa_proplist_gets(i->proplist, "alsa.card_name");
        if (prop_alsa_card_name) {
            shared_data_2.alsa_name = strdup(prop_alsa_card_name);
            pa_threaded_mainloop_signal(shared_data_1.mainloop, 0); // Signal to stop the loop
        }
    }
}

/**
 * @brief Retrieves the ALSA name of a given PulseAudio source.
 *
 * @param source_name The name of the source for which to retrieve the ALSA name.
 * @return The ALSA name of the source or NULL if not found or on error.
 */
const char* get_alsa_input_name(const char *source_name) {
    if (!source_name) {
        fprintf(stderr, "get_alsa_input_name(): Invalid source name.\n");
        return NULL;
    }

    shared_data_2.alsa_name = NULL; // Ensure alsa_name is initialized to NULL

    if (!is_pulse_initialized() && !initialize_pulse()) {
        fprintf(stderr, "get_alsa_input_name(): PulseAudio initialization failed.\n");
        return NULL;
    }

    pa_operation *name_op = pa_context_get_source_info_by_name(shared_data_1.context, source_name, get_alsa_input_name_cb, (void*)source_name);
    iterate(name_op);

    return shared_data_2.alsa_name;
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
static void get_alsa_output_name_cb(pa_context *c, const pa_sink_info *i, int eol, void *userdata) {
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
const char* get_alsa_output_name(const char *sink_name) {
    pa_operation *name_op;

    shared_data_2.alsa_name = NULL;  // Ensure alsa_name is initialized to NULL


    if (!is_pulse_initialized() && !initialize_pulse()) {
        fprintf(stderr, "get_alsa_name(): PulseAudio initialization failed.\n");
        return NULL;
    }

    name_op = pa_context_get_sink_info_list(shared_data_1.context, get_alsa_output_name_cb, (void*)sink_name);
    iterate(name_op);

    return shared_data_2.alsa_name;
}

/**
 * @brief Callback function to retrieve the ALSA device string based on PulseAudio source information.
 *
 * This function is called for each available PulseAudio source. It checks if the source's name matches
 * the target source name provided in the userdata. If a match is found, it retrieves the "alsa.card" and
 * "alsa.device" properties from the source's proplist, constructs the ALSA device string in the format
 * "hw:<card>,<device>", and stores it in shared data for later retrieval.
 *
 * @param c Pointer to the PulseAudio context.
 * @param i Pointer to the source information structure.
 * @param eol End of list flag. If non-zero, indicates the end of the source list.
 * @param userdata User-defined data pointer. In this case, it points to the target source name string.
 */
static void get_alsa_input_id_cb(pa_context *c, const pa_source_info *i, int eol, void *userdata) {
    (void) c; // Unused parameter

    // Check for end of list
    if (eol) {
        // Signal the main loop to unblock the iterate function
        pa_threaded_mainloop_signal(shared_data_1.mainloop, 0);
        free(userdata);
        return;
    }

    // Retrieve the target source name from userdata
    const char *target_source_name = (const char *)userdata;

    // Skip this source if it does not match the specified target name
    if (target_source_name && strcmp(target_source_name, i->name) != 0) {
        return;
    }

    // Attempt to retrieve the "alsa.card" and "alsa.device" properties
    const char *alsa_card = pa_proplist_gets(i->proplist, "alsa.card");
    const char *alsa_device = pa_proplist_gets(i->proplist, "alsa.device");

    //print_proplist(i->proplist);
    //printf("[DEBUG, get_alsa_input_id_cb] alsa_card is %s\n", alsa_card);
    //printf("[DEBUG, get_alsa_input_id_cb] alsa_device is %s\n", alsa_device);

    // Construct the ALSA device string if both properties are available
    if (alsa_card && alsa_device && isdigit((unsigned char)alsa_device[0])) {
        char alsa_device_string[128];
        snprintf(alsa_device_string, sizeof(alsa_device_string), "hw:%s,%s", alsa_card, alsa_device);

        // Store the ALSA device string in shared data
        shared_data_2.alsa_id = strdup(alsa_device_string);
        if (!shared_data_2.alsa_id) {
            fprintf(stderr, "Failed to allocate memory for ALSA device id.\n");
        }
    } else {
        // Log an error if ALSA properties are not found or are invalid
        //fprintf(stderr, "  - ALSA properties not found or invalid for source.\n");
        shared_data_2.alsa_id = NULL;
    }

    // Signal the main loop to unblock the iterate function
    pa_threaded_mainloop_signal(shared_data_1.mainloop, 0);
}

/**
 * @brief Retrieves the ALSA device string for a given PulseAudio source name.
 *
 * This function initializes PulseAudio (if not already initialized), then queries PulseAudio for the
 * information of a specific source by its name. It waits for the retrieval operation to complete and then
 * returns the constructed ALSA device string based on the "alsa.card" and "alsa.device" properties of
 * the source.
 *
 * @param source_name The name of the PulseAudio source.
 * @return ALSA device string in the format "hw:<card>,<device>" or NULL if not found or on error.
 */
const char* get_alsa_input_id(const char *source_name) {
    // Operation object for asynchronous PulseAudio calls
    pa_operation *op;

    // Check if PulseAudio is initialized
    if (!is_pulse_initialized()) {
        // Attempt to initialize PulseAudio if it's not already initialized
        if (!initialize_pulse()) {
            fprintf(stderr, "get_alsa_input_id(): PulseAudio initialization failed.\n");
            return NULL;
        }
    }

    // Duplicate the source name to ensure it remains valid throughout the operation
    char *source_name_copy = strdup(source_name);
    if (!source_name_copy) {
        fprintf(stderr, "get_alsa_input_id(): Failed to allocate memory for source name.\n");
        return NULL;
    }

    // Start querying PulseAudio for the specified source information
    op = pa_context_get_source_info_by_name(shared_data_1.context, source_name_copy, get_alsa_input_id_cb, source_name_copy);

    // Block and wait for the operation to complete
    iterate(op);

    // After the callback has been called with the source information, the ALSA device ID will be stored
    // Return the stored ALSA device ID
    return shared_data_2.alsa_id;
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
static void get_alsa_output_id_cb(pa_context *c, const pa_sink_info *i, int eol, void *userdata) {
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
        shared_data_2.alsa_id = NULL;
        //fprintf(stderr, "  - ALSA properties not found or invalid for sink.\n");
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
const char* get_alsa_output_id(const char *sink_name) {
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
    op = pa_context_get_sink_info_by_name(shared_data_1.context, sink_name_copy, get_alsa_output_id_cb, sink_name_copy);

    // Wait for the PulseAudio operation to complete
    iterate(op);

    // Return the ALSA device string retrieved by the callback function
    return shared_data_2.alsa_id;
}

static void get_input_sample_rate_cb(pa_context *c, const pa_source_info *i, int eol, void *userdata) {
    (void) c;
    if (eol < 0) {
        // An error occurred
        pa_threaded_mainloop_signal(shared_data_1.mainloop, 0);
        return;
    }
    if (eol > 0) {
        // No more entries
        pa_threaded_mainloop_signal(shared_data_1.mainloop, 0);
        return;
    }

    // Extract the sample rate and store it in the user data
    uint32_t *sample_rate = userdata;
    *sample_rate = i->sample_spec.rate;
}

uint32_t get_input_sample_rate(const char *source_name) {
    if (!is_pulse_initialized() && !initialize_pulse()) {
        fprintf(stderr, "PulseAudio is not initialized.\n");
        return 0;
    }

    uint32_t sample_rate = 0;
    pa_operation *op = NULL;


    // Get the source info, passing the address of sample_rate as user data
    op = pa_context_get_source_info_by_name(shared_data_1.context, source_name, get_input_sample_rate_cb, &sample_rate);
    iterate(op);

    return sample_rate;
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
int get_output_sample_rate(const char *alsa_id, const pa_sink_info *sink_info) {
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

// Callback for source information to get ports
void get_source_ports(pa_context *c, const pa_source_info *i, int eol, void *userdata) {
    (void) c;

    pa_source_info_list *info_list = (pa_source_info_list *)userdata;
    if (eol > 0) {
        info_list->done = 1;
        pa_threaded_mainloop_signal(shared_data_1.mainloop, 0);
        return;
    }

    info_list->ports = realloc(info_list->ports, (info_list->num_ports + 1) * sizeof(pa_port_info));
    pa_port_info *port = &info_list->ports[info_list->num_ports];
    port->name = strdup(i->name);
    port->description = strdup(i->description);
    port->is_active = 0;  // Will be set in the active port callback

    info_list->num_ports++;
}

// Callback for source information to get the active port
void get_active_port(pa_context *c, const pa_source_info *i, int eol, void *userdata) {
    (void) c;
    pa_source_info_list *info_list = (pa_source_info_list *)userdata;
    if (eol > 0) {
        pa_threaded_mainloop_signal(shared_data_1.mainloop, 0);
        return;
    }

    if (i->active_port) {
        for (int j = 0; j < info_list->num_ports; ++j) {
            if (strcmp(info_list->ports[j].name, i->active_port->name) == 0) {
                info_list->ports[j].is_active = 1;
                break;
            }
        }
    }
}

// Function to collect source port information and return it
pa_source_info_list* get_source_port_info() {
    pa_source_info_list* info_list = malloc(sizeof(pa_source_info_list));
    if (!info_list) {
        // Handle malloc failure
        return NULL;
    }
    memset(info_list, 0, sizeof(pa_source_info_list));

    // Call the function to get the list of sources
    pa_operation *op = pa_context_get_source_info_list(shared_data_1.context, get_source_ports, info_list);
    iterate(op);

    // Now iterate over the collected sources and get detailed info for each one
    for (int i = 0; i < info_list->num_ports; ++i) {
        op = pa_context_get_source_info_by_name(shared_data_1.context, info_list->ports[i].name, get_active_port, info_list);

        iterate(op);
    }

    // The info_list now contains all the ports and their active status
    return info_list;
}


/**
 * @brief Retrieves the volume of a given channel from a PulseAudio sink.
 *
 * This function takes a pointer to a pa_sink_info structure and a channel index
 * and returns the volume of that channel. The volume is given as a pa_volume_t,
 * which is an unsigned 32-bit integer. The function checks if the channel index
 * is within the valid range for the sink.
 *
 * @param sink_info A pointer to a pa_sink_info structure containing the sink details.
 * @param channel_index The index of the channel for which to retrieve the volume.
 * @return The volume of the specified channel as a pa_volume_t, or PA_VOLUME_INVALID on error.
 */
pa_volume_t get_channel_volume(const pa_sink_info *sink_info, unsigned int channel_index) {
    // Check if the sink_info is NULL
    if (sink_info == NULL) {
        return PA_VOLUME_INVALID; // Return invalid volume if sink_info is NULL
    }

    // Check if the provided channel index is valid
    if (channel_index >= sink_info->channel_map.channels) {
        return PA_VOLUME_INVALID; // Return invalid volume if the channel_index is out of range
    }

    // Retrieve the volume of the given channel
    return sink_info->volume.values[channel_index];
}



/**
 * @brief Callback function for processing each available audio input device (source) found.
 *
 * This function is called by the PulseAudio context as part of the operation initiated by
 * `get_available_input_devices`. It is invoked for each source found, and is responsible for
 * storing the details of each source into a dynamically allocated array. The function handles
 * memory allocation for the array of `pa_source_info` structures, as well as for the strings
 * within them. It also handles error conditions and signals the mainloop to terminate the wait
 * when the end of the source list is reached or an error occurs.
 *
 * @param c The PulseAudio context.
 * @param i The `pa_source_info` structure containing details about the current source.
 * @param eol End-Of-List indicator. If positive, indicates the end of the list; if negative, indicates an error.
 * @param userdata User data pointer provided during the context operation setup; unused in this callback.
 */
static void get_available_input_devices_cb(pa_context *c, const pa_source_info *i, int eol, void *userdata) {
    (void)c;       // Unused parameter
    //fprintf(stderr, "[DEBUG] %s called with eol=%d\n", __FUNCTION__, eol);       // Debug statement for entry
    (void)userdata; // Unused parameter
    //fprintf(stderr, "[DEBUG] %s called with eol=%d\n", __FUNCTION__, eol); // Debug statement for entry

    // Error or end of list
    if (eol < 0) {
        //fprintf(stderr, "Failed to get source info.\n");
        //fprintf(stderr, "[DEBUG] Signaling main loop to continue.\n"); // Debug statement before signaling
        pa_threaded_mainloop_signal(shared_data_1.mainloop, 0);
        return;
    }

    // If we've reached the end of the list, signal the main loop to stop waiting
    if (eol > 0) {
        shared_data_sources.sources[shared_data_sources.count] = NULL; // Sentinel value
        //fprintf(stderr, "[DEBUG] Signaling main loop to continue.\n"); // Debug statement before signaling
        pa_threaded_mainloop_signal(shared_data_1.mainloop, 0);
        return;
    }

    // Allocate or resize the sources array as needed
    if (shared_data_sources.count >= shared_data_sources.allocated) {
        uint32_t new_size = (shared_data_sources.allocated + 8) * sizeof(pa_source_info *);
        pa_source_info **temp = realloc(shared_data_sources.sources, new_size);
        if (!temp) {
            //fprintf(stderr, "Out of memory.\n");
            //fprintf(stderr, "[DEBUG] Signaling main loop to continue.\n"); // Debug statement before signaling
            pa_threaded_mainloop_signal(shared_data_1.mainloop, 0);
            return;
        }
        shared_data_sources.sources = temp;
        shared_data_sources.allocated += 8;
    }

    // Allocate memory for a new pa_source_info structure
    shared_data_sources.sources[shared_data_sources.count] = malloc(sizeof(pa_source_info));
    if (!shared_data_sources.sources[shared_data_sources.count]) {
        fprintf(stderr, "Failed to allocate memory for source info.\n");
        fprintf(stderr, "[DEBUG] Signaling main loop to continue.\n"); // Debug statement before signaling
        pa_threaded_mainloop_signal(shared_data_1.mainloop, 0);
        return;
    }

    // Copy the information from the callback
    *(shared_data_sources.sources[shared_data_sources.count]) = *i;

    // Duplicate the strings to ensure they will remain valid
    if (i->name) {
        shared_data_sources.sources[shared_data_sources.count]->name = strdup(i->name);
    }
    if (i->description) {
        shared_data_sources.sources[shared_data_sources.count]->description = strdup(i->description);
    }

    // Increase the count of found sources
    shared_data_sources.count++;
}

/**
 * @brief Retrieves an array of available audio input devices (sources).
 *
 * This function queries the PulseAudio server for a list of all audio input devices
 * currently available. It ensures that PulseAudio is initialized before making the query
 * and locks the main loop to provide thread safety during the operation.
 *
 * Each call to this function should be followed by a call to `delete_input_devices`
 * to free the allocated memory for the returned array of `pa_source_info` pointers.
 *
 * @note The array is terminated with a NULL pointer as the last element.
 *
 * @return On success, a pointer to an array of `pa_source_info` pointers, each representing
 * an audio input device. On failure, or if PulseAudio is not initialized, NULL is returned.
 */
pa_source_info **get_available_input_devices() {
    if (!is_pulse_initialized() && !initialize_pulse()) {
        fprintf(stderr, "PulseAudio is not initialized.\\n");
        return NULL;
    }

    shared_data_sources.sources = NULL;
    shared_data_sources.count = 0;

    // Prepare the PulseAudio operation to list the sources
    pa_operation *op = pa_context_get_source_info_list(shared_data_1.context, get_available_input_devices_cb, NULL);

    // Wait for the operation to complete
    iterate(op);
    // Allocate one extra pointer to NULL at the end as a sentinel
    shared_data_sources.sources = realloc(shared_data_sources.sources, (shared_data_sources.count + 1) * sizeof(pa_source_info *));
    shared_data_sources.sources[shared_data_sources.count] = NULL;

    return shared_data_sources.sources;
}

/**
 * @brief Callback function used to count the available audio input devices (sources).
 *
 * This function is called for each audio input device found by PulseAudio when querying
 * for the list of sources. The function increments the device_count for each device.
 * When the list is exhausted or there's an error, it signals the mainloop to continue.
 *
 * @param c Pointer to the PulseAudio context.
 * @param i Pointer to the current audio input device information.
 * @param eol End-Of-List indicator. If positive, indicates the end of the list; if negative, indicates an error.
 * @param userdata Pointer to the user data, which in this case is expected to be a pointer to the device_count.
 */
static void get_input_device_count_cb(pa_context *c, const pa_source_info *i, int eol, void *userdata) {
    (void) c; // Suppress unused parameter warning
    (void) i; // Suppress unused parameter warning

    uint32_t *device_count = (uint32_t *) userdata;

    if (eol < 0) {
        // Handle error
        pa_threaded_mainloop_signal(shared_data_1.mainloop, 0);
        return;
    }
    if (eol > 0) {
        // End of list
        pa_threaded_mainloop_signal(shared_data_1.mainloop, 0);
        return;
    }

    ++(*device_count);
}


/**
 * @brief Retrieve the count of audio input devices in the system.
 *
 * This function queries PulseAudio to get a count of all available audio input devices (sources).
 * If PulseAudio is not initialized, the function attempts to initialize it. If the initialization
 * fails or there's an error in fetching the device count, it returns UINT32_MAX.
 *
 * @return Count of audio input devices or UINT32_MAX on error.
 */
uint32_t get_input_device_count(void) {
    uint32_t device_count = 0; // Initialize device count to zero
    pa_operation *count_op = NULL;

    // Check if PulseAudio is initialized
    if (!is_pulse_initialized()) {
        // Attempt to initialize PulseAudio if it's not already initialized
        if (!initialize_pulse()) {
            fprintf(stderr, "get_input_device_count(): failed to initialize pulseaudio.\n");
            return UINT32_MAX; // Return error if initialization fails
        }
    }

    // Check if context is valid after initialization attempt
    if (!shared_data_1.context) {
        fprintf(stderr, "Context is NULL in get_input_device_count.\n");
        return UINT32_MAX;
    }

    // Query PulseAudio for the list of audio input devices (sources)
    count_op = pa_context_get_source_info_list(shared_data_1.context, get_input_device_count_cb, &device_count);

    // Wait for the PulseAudio operation to complete
    iterate(count_op);

    return device_count; // Return the total count of input devices
}


/**
 * @brief Get the channel names for a specific sink identified by its name.
 *
 * This function retrieves the channel names for a given sink using its unique name.
 * It allocates an array of strings where each entry corresponds to a channel name
 * of the sink. The caller is responsible for freeing the memory allocated for the
 * array of channel names and the strings themselves.
 *
 * @param sink_name The name of the sink whose channel names are to be retrieved.
 * @param num_channels Number of channels of the sink.
 * @return A pointer to an array of strings containing the channel names, or NULL if
 *         the sink is not found or in case of an error.
 */
char** get_output_channel_names(const char *alsa_id, int num_channels) {
    // Validate input parameters
    if (!alsa_id) {
        return NULL; // Return NULL if the sink name is not provided
    }

    // Retrieve the sink information for the specified sink name
    pa_sink_info *device_info = get_output_device_by_name(alsa_id);

    // Check if the sink was found
    if (!device_info) {
        return NULL; // Return NULL if the sink is not found or in case of an error
    }

    char **channel_names = calloc(num_channels, sizeof(char*));

    if (!channel_names) {
        free((char *)device_info->description); // Free the description if allocated
        free(device_info); // Free the device_info structure
        return NULL; // Return NULL if memory allocation fails
    }

    // Copy the channel names into the array
    for (int i = 0; i < num_channels; ++i) {
        channel_names[i] = strdup(pa_channel_position_to_pretty_string(device_info->channel_map.map[i]));
        if (!channel_names[i]) {
            // Handle allocation failure for a channel name
            // Free all previously allocated names and the array itself
            while (i--) free(channel_names[i]);
            free(channel_names);
            free((char *)device_info->description); // Free the description if allocated
            free(device_info); // Free the device_info structure
            return NULL; // Return NULL if memory allocation fails
        }
    }

    // Free the sink_info structure and its description field
    if (device_info->description) {
        free((char *)device_info->description); // Corrected free statement
    }
    free(device_info);

    // Return the array of channel names
    return channel_names;
}

/**
 * @brief Get the channel names for a specific source identified by its name.
 *
 * This function retrieves the channel names for a given source using its unique name.
 * It allocates an array of strings where each entry corresponds to a channel name
 * of the source. The caller is responsible for freeing the memory allocated for the
 * array of channel names and the strings themselves.
 *
 * @param source_name The name of the source whose channel names are to be retrieved.
 * @param num_channels Number of channels of the source.
 * @return A pointer to an array of strings containing the channel names, or NULL if
 *         the source is not found or in case of an error.
 */
char** get_input_channel_names(const char *alsa_id, int num_channels) {
    // Validate input parameters
    if (!alsa_id) {
        return NULL; // Return NULL if the source name is not provided
    }

    // Retrieve the source information for the specified source name
    pa_source_info *device_info = get_input_device_by_name(alsa_id);

    // Check if the source was found
    if (!device_info) {
        return NULL; // Return NULL if the source is not found or in case of an error
    }

    char **channel_names = calloc(num_channels, sizeof(char*));
    if (!channel_names) {
        free((char *)device_info->description); // Free the description if allocated
        free(device_info); // Free the device_info structure
        return NULL; // Return NULL if memory allocation fails
    }

    // Copy the channel names into the array
    for (int i = 0; i < num_channels; ++i) {
        channel_names[i] = strdup(pa_channel_position_to_pretty_string(device_info->channel_map.map[i]));
        if (!channel_names[i]) {
            // Handle allocation failure for a channel name
            while (i--) free(channel_names[i]); // Free all previously allocated names
            free(channel_names); // Free the array itself
            free((char *)device_info->description); // Free the description if allocated
            free(device_info); // Free the device_info structure
            return NULL; // Return NULL if memory allocation fails
        }
    }

    // Free the source_info structure and its description field
    if (device_info->description) {
        free((char *)device_info->description);
    }
    free(device_info);

    // Return the array of channel names
    return channel_names;
}

/**
 * @brief Retrieve a source (input device) information by its name.
 *
 * This function searches for an audio source with the given name and returns its information
 * if found. The caller is responsible for freeing the memory allocated for the source info
 * and its description.
 *
 * @param source_name The name of the source to be retrieved.
 * @return A pointer to a pa_source_info structure containing the source information,
 *         or NULL if the source is not found or in case of an error.
 */
pa_source_info *get_input_device_by_name(const char *source_name) {
    if (!source_name) {
        return NULL; // Handle null pointer argument
    }

    // Retrieve the list of available input devices (sources)
    pa_source_info **available_sources = get_available_input_devices();
    if (!available_sources) {
        return NULL; // Handle error in retrieving sources
    }

    pa_source_info *input_device_info = NULL;

    // Iterate over the list of sources to find the one with the matching name
    for (int i = 0; available_sources[i] != NULL; ++i) {
        if (strcmp(available_sources[i]->name, source_name) == 0) {
            // Found the matching source, make a copy of the source_info structure
            input_device_info = malloc(sizeof(pa_source_info));
            if (input_device_info) {
                memcpy(input_device_info, available_sources[i], sizeof(pa_source_info));

                // If the source has a description, also copy that string
                if (available_sources[i]->description) {
                    input_device_info->description = strdup(available_sources[i]->description);
                }
            }
            break; // Exit the loop after finding the matching source
        }
    }

    // Clean up the source information now that we're done with it
    // Assuming there's a function to delete input devices similar to delete_output_devices
    delete_input_devices(available_sources);

    return input_device_info; // Return the found source or NULL if not found
}



/**
 * @brief Retrieve a copy of the sink information for a given sink name.
 *
 * This function searches through the available output devices and returns a copy of the
 * pa_sink_info structure for the sink that matches the provided name. It uses the
 * get_available_output_devices function to obtain the list of all sinks and then
 * iterates through them to find the sink with the given name.
 *
 * @param sink_name The name of the sink to search for. Must not be NULL.
 * @return A pointer to a newly allocated pa_sink_info structure containing the sink
 *         information, or NULL if the sink is not found or if an error occurs. The
 *         caller is responsible for freeing the returned structure and its description
 *         field (if not NULL) when no longer needed.
 *
 * @note The function allocates memory for the returned pa_sink_info structure and its
 *       description field. It is the responsibility of the caller to free this memory
 *       using free(). If the sink has other dynamically allocated fields, these must
 *       also be freed by the caller.
 *
 * @warning The function returns NULL if the sink_name argument is NULL, or if the
 *          get_available_output_devices function fails to retrieve the sinks.
 *
 * Usage Example:
 * @code
 * pa_sink_info *sink_info = get_sink_by_name("alsa_output.pci-0000_00_1b.0.analog-stereo");
 * if (sink_info) {
 *     // Use the sink information
 *     ...
 *     // Free the memory allocated for the description
 *     if (sink_info->description) {
 *         free(sink_info->description);
 *     }
 *     // Free the memory allocated for the sink_info structure
 *     free(sink_info);
 * }
 * @endcode
 */
pa_sink_info *get_output_device_by_name(const char *sink_name) {
    if (!sink_name) {
        return NULL; // Handle null pointer argument
    }

    // Retrieve the list of available output devices (sinks)
    pa_sink_info **available_sinks = get_available_output_devices();
    if (!available_sinks) {
        return NULL; // Handle error in retrieving sinks
    }

    pa_sink_info *output_device_to_return = NULL;

    // Iterate over the list of sinks to find the one with the matching name
    for (int i = 0; available_sinks[i] != NULL; ++i) {
        if (strcmp(available_sinks[i]->name, sink_name) == 0) {
            // Found the matching output_device, make a copy of the sink_info structure
            output_device_to_return = malloc(sizeof(pa_sink_info));
            if (output_device_to_return) {
                memcpy(output_device_to_return, available_sinks[i], sizeof(pa_sink_info));

                // If the sink has a description, also copy that string
                if (available_sinks[i]->description) {
                    output_device_to_return->description = strdup(available_sinks[i]->description);
                }
            }
            break; // Exit the loop after finding the matching sink
        }
    }

    // Clean up the sink information now that we're done with it
    delete_output_devices(available_sinks);

    return output_device_to_return; // Return the found sink or NULL if not found
}
