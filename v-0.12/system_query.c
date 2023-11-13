#include "system_query.h"
#include <pulse/mainloop-api.h>
#include <pulse/mainloop.h>
#include <pulse/operation.h>
#include <pulse/pulseaudio.h>
#include <pulse/thread-mainloop.h>
#include <alsa/asoundlib.h>
#include <stdint.h>
#include <stdio.h>
#include <ctype.h> // Include this for the isdigit() function
#include <stdbool.h>
#include <pwd.h>

#define DAEMON_CONF "/etc/pulse/daemon.conf"
#define MAX_LINE_LENGTH 1024

#define MUTED 1
#define UNMUTED 0

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
    char *alsa_name;
    char *alsa_id;
} _shared_data_2;

static _shared_data_2 shared_data_2 = {.alsa_name = NULL};


// Structure to share data between get_available_output_devices and its callback.
typedef struct {
    pa_sink_info **sinks;  // An array of pointers to pa_sink_info
    uint32_t count;
} _shared_data_3;

static _shared_data_3 shared_data_3;

// Structure to share data between get_profiles and its callback.
typedef struct {
    pa_card_profile_info *profiles;
    int num_profiles;
} _shared_data_4;

_shared_data_4 shared_data_4 = {NULL, 0};

// Structure to share data between get_available_input_devices and its callback.
static struct {
    pa_source_info **sources; // Array of pointers to pa_source_info structures
    uint32_t count;           // Count of available sources
    uint32_t allocated;       // Allocated size of the sources array
} shared_data_sources = {NULL, 0, 0};

// Strcture to share data between get_channel_mute_state and its callback.
typedef struct {
    uint32_t channel_index;
    bool mute_state;
} _shared_data_5;




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
        //fprintf(stderr, "Unable to open PCM device: %s, error: %s\n", alsa_id, snd_strerror(err));
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
        fprintf(stderr, "[get_max_output_channels()] Invalid parameters provided.\n");
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
char* get_alsa_input_name(const char *source_name) {
    if (!source_name) {
        fprintf(stderr, "[get_alsa_input_name()] Invalid source name.\n");
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
char* get_alsa_output_name(const char *sink_name) {
    pa_operation *name_op;

    shared_data_2.alsa_name = NULL;  // Ensure alsa_name is initialized to NULL

    if (!is_pulse_initialized() && !initialize_pulse()) {
        fprintf(stderr, "[get_alsa_output_name()] PulseAudio initialization failed.\n");
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
char* get_alsa_input_id(const char *source_name) {
    // Operation object for asynchronous PulseAudio calls
    pa_operation *op;

    // Check if PulseAudio is initialized
    if (!is_pulse_initialized()) {
        // Attempt to initialize PulseAudio if it's not already initialized
        if (!initialize_pulse()) {
            fprintf(stderr, "[get_alsa_input_id()] PulseAudio initialization failed.\n");
            return NULL;
        }
    }

    // Duplicate the source name to ensure it remains valid throughout the operation
    char *source_name_copy = strdup(source_name);
    if (!source_name_copy) {
        fprintf(stderr, "[get_alsa_input_id()] Failed to allocate memory for source name.\n");
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
char* get_alsa_output_id(const char *sink_name) {
    pa_operation *op;

    // Check if PulseAudio is initialized
    if (!is_pulse_initialized()) {
        // Attempt to initialize PulseAudio if it's not already initialized
        if (!initialize_pulse()) {
            fprintf(stderr, "[get_alsa_output_id()] PulseAudio initialization failed.\n");
            return NULL;
        }
    }

    // Make a copy of the sink name to ensure it remains valid
    char *sink_name_copy = strdup(sink_name);
    if (!sink_name_copy) {
        fprintf(stderr, "[get_alsa_output_id()] Failed to allocate memory for sink name.\n");
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


/**
 * @brief Retrieves the sample rate of the specified PulseAudio source by ALSA identifier.
 *
 * Attempts to open the ALSA device corresponding to the given PulseAudio source
 * to query the sample rate. If the ALSA query fails, the function falls back to
 * returning the sample rate as reported by the PulseAudio source information.
 *
 * @param alsa_id The ALSA device identifier corresponding to the PulseAudio source.
 * @param source_info The PulseAudio source information structure.
 * @return The sample rate of the source in Hz on success, or -1 on error.
 */
int get_input_sample_rate(const char *alsa_id, pa_source_info *source_info) {
    snd_pcm_t *handle;
    snd_pcm_hw_params_t *params;
    unsigned int sample_rate = 0; // Default to a known value
    int err;

    // Output debug information to stderr
    //fprintf(stderr, "[DEBUG, get_input_sample_rate()] called with alsa_id: '%s'\n", alsa_id);

    // If alsa_id is NULL, return the PulseAudio rate
    if (!alsa_id && source_info) {
        //fprintf(stderr, "alsa_id is NULL, returning PulseAudio sample rate: %u Hz\n", source_info->sample_spec.rate);
        return source_info->sample_spec.rate;
    }

    // Validate parameters
    if (!alsa_id || !source_info) {
        //fprintf(stderr, "Invalid parameters provided to get_input_sample_rate.\n");
        return -1;
    }

    // Attempt to open the ALSA device
    //fprintf(stderr, "[DEBUG, get_input_sample_rate()] Attempting to open ALSA device: '%s'\n", alsa_id);

    if ((err = snd_pcm_open(&handle, alsa_id, SND_PCM_STREAM_CAPTURE, SND_PCM_NONBLOCK)) < 0) {
        fprintf(stderr, "Unable to open PCM device: '%s', error: %s\n", alsa_id, snd_strerror(err));
        return source_info->sample_spec.rate;
    }

    // ALSA device successfully opened
    //fprintf(stderr, "ALSA device: '%s' successfully opened.\n", alsa_id);
    snd_pcm_hw_params_alloca(&params);

    // Initialize hardware parameters
    if ((err = snd_pcm_hw_params_any(handle, params)) < 0) {
        fprintf(stderr, "Cannot initialize hardware parameter structure: %s\n", snd_strerror(err));
        snd_pcm_close(handle);
        return -1;
    }

    // Set the access type for the hardware parameters
    if ((err = snd_pcm_hw_params_set_access(handle, params, SND_PCM_ACCESS_RW_INTERLEAVED)) < 0) {
        fprintf(stderr, "Cannot set access type: %s\n", snd_strerror(err));
        snd_pcm_close(handle);
        return -1;
    }

    // Set the sample format for the hardware parameters
    if ((err = snd_pcm_hw_params_set_format(handle, params, SND_PCM_FORMAT_S16_LE)) < 0) {
        fprintf(stderr, "Cannot set sample format: %s\n", snd_strerror(err));
        snd_pcm_close(handle);
        return -1;
    }

    // Set the channel count for the hardware parameters
    if ((err = snd_pcm_hw_params_set_channels(handle, params, source_info->sample_spec.channels)) < 0) {
        fprintf(stderr, "Cannot set channel count: %s\n", snd_strerror(err));
        snd_pcm_close(handle);
        return -1;
    }

    // Apply the hardware parameters to the device
    if ((err = snd_pcm_hw_params(handle, params)) < 0) {
        fprintf(stderr, "Cannot set hardware parameters: %s\n", snd_strerror(err));
        snd_pcm_close(handle);
        return -1;
    }

    // Now try to get the sample rate
    if ((err = snd_pcm_hw_params_get_rate(params, &sample_rate, 0)) < 0) {
        fprintf(stderr, "Error getting sample rate for device: '%s', error: %s\n", alsa_id, snd_strerror(err));
        snd_pcm_close(handle);
        return -1;
    }

    // Sample rate successfully obtained
    //fprintf(stderr, "Sample rate for ALSA device: '%s' is %u Hz\n", alsa_id, sample_rate);
    snd_pcm_close(handle);
    return sample_rate; // Successfully obtained sample rate
}


/**
 * @brief Retrieves the sample rate of the specified PulseAudio sink by ALSA identifier.
 *
 * Attempts to open the ALSA device corresponding to the given PulseAudio sink
 * to query the sample rate. If the ALSA query fails, the function falls back to
 * returning the sample rate as reported by the PulseAudio sink information.
 *
 * @param alsa_id The ALSA device identifier corresponding to the PulseAudio sink.
 * @param sink_info The PulseAudio sink information structure.
 * @return The sample rate of the sink in Hz on success, or -1 on error.
 */
int get_output_sample_rate(const char *alsa_id, const pa_sink_info *sink_info) {
    snd_pcm_t *handle;
    snd_pcm_hw_params_t *params;
    unsigned int sample_rate = 0; // Default to a known value
    int err;

    //fprintf(stderr, "[DEBUG, get_output_sample_rate()] called with alsa_id: '%s'\n", alsa_id);

    if (!alsa_id && sink_info) {
        //fprintf(stderr, "alsa_id is NULL, returning PulseAudio sample rate: %u Hz\n", sink_info->sample_spec.rate);
        return sink_info->sample_spec.rate;
    }

    if (!alsa_id || !sink_info) {
        //fprintf(stderr, "Invalid parameters provided to get_output_sample_rate.\n");
        return -1;
    }

    //fprintf(stderr, "Attempting to open ALSA device: '%s'\n", alsa_id);

    if ((err = snd_pcm_open(&handle, alsa_id, SND_PCM_STREAM_PLAYBACK, 0)) < 0) {
        fprintf(stderr, "Unable to open PCM device: '%s', error: %s\n", alsa_id, snd_strerror(err));
        return sink_info->sample_spec.rate;
    }

    //fprintf(stderr, "ALSA device: '%s' successfully opened.\n", alsa_id);
    snd_pcm_hw_params_alloca(&params);
    if ((err = snd_pcm_hw_params_any(handle, params)) < 0) {
        fprintf(stderr, "Cannot initialize hardware parameter structure: %s\n", snd_strerror(err));
        snd_pcm_close(handle);
        return -1;
    }

    // Set the access type for the hardware parameters
    if ((err = snd_pcm_hw_params_set_access(handle, params, SND_PCM_ACCESS_RW_INTERLEAVED)) < 0) {
        fprintf(stderr, "Cannot set access type: %s\n", snd_strerror(err));
        snd_pcm_close(handle);
        return -1;
    }

    // Set the sample format for the hardware parameters
    if ((err = snd_pcm_hw_params_set_format(handle, params, SND_PCM_FORMAT_S16_LE)) < 0) {
        fprintf(stderr, "Cannot set sample format: %s\n", snd_strerror(err));
        snd_pcm_close(handle);
        return -1;
    }

    // Set the channel count for the hardware parameters
    if ((err = snd_pcm_hw_params_set_channels(handle, params, sink_info->sample_spec.channels)) < 0) {
        fprintf(stderr, "Cannot set channel count: %s\n", snd_strerror(err));
        snd_pcm_close(handle);
        return -1;
    }

    // Apply the hardware parameters to the device
    if ((err = snd_pcm_hw_params(handle, params)) < 0) {
        fprintf(stderr, "Cannot set hardware parameters: %s\n", snd_strerror(err));
        snd_pcm_close(handle);
        return -1;
    }

    // Now try to get the sample rate
    if ((err = snd_pcm_hw_params_get_rate(params, &sample_rate, 0)) < 0) {
        fprintf(stderr, "Error getting sample rate for device: '%s', error: %s\n", alsa_id, snd_strerror(err));
        snd_pcm_close(handle);
        return -1;
    }

    //fprintf(stderr, "Sample rate for ALSA device: '%s' is %u Hz\n", alsa_id, sample_rate);
    snd_pcm_close(handle);
    return sample_rate; // Successfully obtained sample rate
}


/**
 * @brief Callback function for retrieving source information to get ports.
 *
 * This function is called by the PulseAudio context as a callback during the
 * operation initiated by `pa_context_get_source_info_list()`. It processes
 * each `pa_source_info` structure provided by PulseAudio, storing the relevant
 * data (name and description) of each source port in a `pa_source_info_list`.
 * The function also handles the end-of-list (EOL) signal from PulseAudio to
 * mark completion of the data retrieval process.
 *
 * @param c The PulseAudio context.
 * @param i The source information structure provided by PulseAudio.
 * @param eol End-of-list flag. A positive value indicates the end of data from PulseAudio.
 * @param userdata A pointer to user-provided data, expected to be of type `pa_source_info_list`.
 */
void get_source_port_info_cb(pa_context *c, const pa_source_info *i, int eol, void *userdata) {
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

/**
 * @brief Callback function for retrieving active source port information.
 *
 * This function is a callback for `pa_context_get_source_info_by_name()`. It is
 * used to determine which of the previously listed source ports is currently active.
 * It updates the `is_active` flag in the corresponding `pa_port_info` structure
 * within the `pa_source_info_list` if a match is found with the active port name.
 *
 * @param c The PulseAudio context.
 * @param i The source information structure, including the active port details.
 * @param eol End-of-list flag. A positive value indicates the end of data from PulseAudio.
 * @param userdata A pointer to user-provided data, expected to be of type `pa_source_info_list`.
 */
void get_source_port_info_cb2(pa_context *c, const pa_source_info *i, int eol, void *userdata) {
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

/**
 * @brief Retrieves a list of source port information from PulseAudio.
 *
 * This function queries PulseAudio for the list of available source ports
 * (such as microphone inputs, line-ins, etc.) and retrieves detailed information
 * for each source. It initializes PulseAudio if not already initialized, then
 * allocates and populates a `pa_source_info_list` structure with the source port
 * information. Each entry in the list contains details about a specific source port.
 *
 * @note The function attempts to initialize PulseAudio if it is not already initialized.
 *
 * @return A pointer to a `pa_source_info_list` structure containing the list of source
 *         ports and their information. Returns NULL if PulseAudio cannot be initialized,
 *         if memory allocation fails, or if the query to PulseAudio fails.
 */
pa_source_info_list* get_source_port_info() {

    // Check if PulseAudio is initialized.
    if (!is_pulse_initialized()) {
        // Attempt to initialize PulseAudio if it's not already initialized.
        if (!initialize_pulse()) {
            fprintf(stderr, "[get_source_port_info()] Failed to initialize pulseaudio.\n");
            return NULL;  // Return error if initialization fails.
        }
    }

    pa_source_info_list* info_list = malloc(sizeof(pa_source_info_list));
    if (!info_list) {
        // Handle malloc failure
        return NULL;
    }
    memset(info_list, 0, sizeof(pa_source_info_list));

    // Call the function to get the list of sources
    pa_operation *op = pa_context_get_source_info_list(shared_data_1.context, get_source_port_info_cb, info_list);
    iterate(op);

    // Now iterate over the collected sources and get detailed info for each one
    for (int i = 0; i < info_list->num_ports; ++i) {
        op = pa_context_get_source_info_by_name(shared_data_1.context, info_list->ports[i].name, get_source_port_info_cb2, info_list);

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

    // Check if PulseAudio is initialized.
    if (!is_pulse_initialized()) {
        // Attempt to initialize PulseAudio if it's not already initialized.
        if (!initialize_pulse()) {
            fprintf(stderr, "get_channel_volume(): failed to initialize pulseaudio.\n");
            return UINT32_MAX;  // Return error if initialization fails.
        }
    }

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
    (void)c; // Unused parameter
    (void)userdata; // Unused parameter

    // Error or end of list
    if (eol < 0) {
        fprintf(stderr, "Error occurred while getting source info.\n");
        pa_threaded_mainloop_signal(shared_data_1.mainloop, 0);
        return;
    }

    if (eol > 0) {
        shared_data_sources.sources[shared_data_sources.count] = NULL; // Sentinel value
        pa_threaded_mainloop_signal(shared_data_1.mainloop, 0);
        return;
    }

    if (shared_data_sources.count >= shared_data_sources.allocated) {
        size_t new_alloc = shared_data_sources.allocated + 8;
        void *temp = realloc(shared_data_sources.sources, new_alloc * sizeof(pa_source_info *));
        if (!temp) {
            fprintf(stderr, "Out of memory when reallocating sources array.\n");
            pa_threaded_mainloop_signal(shared_data_1.mainloop, 0);
            return;
        }
        shared_data_sources.sources = temp;
        shared_data_sources.allocated = new_alloc;
    }

    shared_data_sources.sources[shared_data_sources.count] = malloc(sizeof(pa_source_info));
    if (!shared_data_sources.sources[shared_data_sources.count]) {
        fprintf(stderr, "Out of memory when allocating source info.\n");
        pa_threaded_mainloop_signal(shared_data_1.mainloop, 0);
        return;
    }

    *(shared_data_sources.sources[shared_data_sources.count]) = *i;

    if (i->name) {
        shared_data_sources.sources[shared_data_sources.count]->name = strdup(i->name);
        if (!shared_data_sources.sources[shared_data_sources.count]->name) {
            fprintf(stderr, "Out of memory when duplicating source name.\n");
        }
    }
    if (i->description) {
        shared_data_sources.sources[shared_data_sources.count]->description = strdup(i->description);
        if (!shared_data_sources.sources[shared_data_sources.count]->description) {
            fprintf(stderr, "Out of memory when duplicating source description.\n");
        }
    }

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
    // Check if PulseAudio is initialized
    if (!is_pulse_initialized()) {
        // Attempt to initialize PulseAudio if it's not already initialized
        if (!initialize_pulse()) {
            fprintf(stderr, "[get_available_input_devices()] Failed to initialize PulseAudio.\n");
            return NULL;
        }
    }

    // Initialize the data structure for storing the sources
    shared_data_sources.sources = NULL;
    shared_data_sources.count = 0;
    shared_data_sources.allocated = 0;

    // Start the operation to get available input devices
    pa_operation *op = pa_context_get_source_info_list(shared_data_1.context, get_available_input_devices_cb, NULL);
    if (op) {
        // iterate handles locking, waiting, and cleanup
        iterate(op);
    } else {
        fprintf(stderr, "Failed to create the operation to get source info.\n");
        return NULL;
    }

    // Allocate one extra pointer to NULL at the end as a sentinel
    shared_data_sources.sources = realloc(shared_data_sources.sources, (shared_data_sources.count + 1) * sizeof(pa_source_info *));
    if (shared_data_sources.sources) {
        shared_data_sources.sources[shared_data_sources.count] = NULL; // Set the sentinel value
    } else {
        fprintf(stderr, "Out of memory while allocating sources array.\n");
    }

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
            fprintf(stderr, "[get_input_device_count()] Failed to initialize pulseaudio.\n");
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
char** get_output_channel_names(const char *pulse_id, int num_channels) {

    // Check if PulseAudio is initialized.
    if (!is_pulse_initialized()) {
        // Attempt to initialize PulseAudio if it's not already initialized.
        if (!initialize_pulse()) {
            fprintf(stderr, "[get_output_channel_names()] Failed to initialize pulseaudio.\n");
            return NULL;  // Return error if initialization fails.
        }
    }

    // Validate input parameters
    if (!pulse_id) {
        return NULL; // Return NULL if the sink name is not provided
    }

    // Retrieve the sink information for the specified sink name
    pa_sink_info *device_info = get_output_device_by_name(pulse_id);

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
char** get_input_channel_names(const char *pulse_code, int num_channels) {

    // Check if PulseAudio is initialized.
    if (!is_pulse_initialized()) {
        // Attempt to initialize PulseAudio if it's not already initialized.
        if (!initialize_pulse()) {
            fprintf(stderr, "[get_input_channel_names()] Failed to initialize pulseaudio.\n");
            return NULL;  // Return error if initialization fails.
        }
    }

    // Validate input parameters
    if (!pulse_code) {
        return NULL; // Return NULL if the source name is not provided
    }

    // Retrieve the source information for the specified source name
    pa_source_info *device_info = get_input_device_by_name(pulse_code);

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

    // Check if PulseAudio is initialized.
    if (!is_pulse_initialized()) {
        // Attempt to initialize PulseAudio if it's not already initialized.
        if (!initialize_pulse()) {
            fprintf(stderr, "[get_input_device_by_name()] Failed to initialize pulseaudio.\n");
            return NULL;  // Return error if initialization fails.
        }
    }

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

    // Check if PulseAudio is initialized.
    if (!is_pulse_initialized()) {
        // Attempt to initialize PulseAudio if it's not already initialized.
        if (!initialize_pulse()) {
            fprintf(stderr, "[get_output_device_by_name()] Failed to initialize pulseaudio.\n");
            return NULL;  // Return error if initialization fails.
        }
    }

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

/**
 * @brief Callback for handling the result of the sink information fetch operation.
 *
 * This callback is called by the PulseAudio library when sink information is ready to be
 * retrieved, or when the iteration over sinks has finished. The function will copy the sink
 * information to the provided user data structure if available, or signal the main loop to
 * continue if the end of the list is reached or if an error occurs.
 *
 * @param c The PulseAudio context.
 * @param i The sink information structure provided by PulseAudio.
 * @param eol End of list indicator. If positive, indicates the end of the list; if negative,
 *            indicates failure to retrieve sink information.
 * @param userdata User data pointer provided to the pa_context_get_sink_info_by_index function,
 *                 expected to be a pointer to a pa_sink_info structure.
 */
static void get_output_device_by_index_cb(pa_context *c, const pa_sink_info *i, int eol, void *userdata) {
    (void) c;

    pa_sink_info *sink_info = userdata;

    if (eol < 0 || eol > 0 || !i) {
        // Either an error occurred, or we've reached the end of the list without finding the sink
        // Signal main loop to continue in case of end of list
        if (eol > 0) {
            pa_threaded_mainloop_signal(shared_data_1.mainloop, 0);
        }
        return;
    }

    // Copy the sink information to the allocated structure
    *sink_info = *i; // Shallow copy first

    // Now duplicate any strings
    if (i->name) {
        sink_info->name = strdup(i->name);
    }
    if (i->description) {
        sink_info->description = strdup(i->description);
    }

    // Signal the main loop that the data is ready
    pa_threaded_mainloop_signal(shared_data_1.mainloop, 0);
}

/**
 * @brief Retrieve the sink information for a given output device by its index.
 *
 * This function initiates an asynchronous operation to fetch the sink information
 * for the specified device index. The operation is handled synchronously within this
 * function using a threaded mainloop to wait for completion.
 *
 * @param index The index of the sink for which information is to be retrieved.
 * @param sink_info A pointer to a pa_sink_info structure where the sink information will be stored.
 * @return int Returns 1 on success or 0 if the operation fails.
 *
 */
pa_sink_info* get_output_device_by_index(uint32_t index) {
    // Check if PulseAudio is initialized.
    if (!is_pulse_initialized()) {
        // Attempt to initialize PulseAudio if it's not already initialized.
        if (!initialize_pulse()) {
            fprintf(stderr, "[get_output_device_by_index] Failed to initialize pulseaudio.\n");
            return NULL;  // Return error if initialization fails.
        }
    }

    if (!shared_data_1.context || !shared_data_1.mainloop) {
        fprintf(stderr, "Invalid shared data context or mainloop.\n");
        return NULL;
    }

    // Allocate memory for sink_info
    pa_sink_info *sink_info = malloc(sizeof(pa_sink_info));
    if (!sink_info) {
        fprintf(stderr, "Memory allocation for sink_info failed.\n");
        return NULL;
    }

    // Start the operation to get the sink information
    pa_operation *op = pa_context_get_sink_info_by_index(shared_data_1.context, index, get_output_device_by_index_cb, sink_info);
    iterate(op);

    // Check if the operation was successful
    if (sink_info->name == NULL) {
        // The operation was not successful, free the allocated memory.
        free(sink_info);
        return NULL;
    }

    return sink_info; // Return the allocated sink_info
}

/**
 * @brief Callback for retrieving information about a specific audio input source by index.
 *
 * This function is the callback used by `pa_context_get_source_info_by_index` within
 * the `get_input_device_by_index` function to handle the response from PulseAudio.
 * It is called by the PulseAudio main loop when the source information is available or
 * when an error or end-of-list condition is signaled.
 *
 * @param c Pointer to the PulseAudio context, not used in this callback.
 * @param i Pointer to the source information structure containing the details of the source.
 * @param eol End-of-list flag that is positive if there is no more data to process, negative
 *            if an error occurred during the iteration.
 * @param userdata User data provided when initiating the operation; expected to be a pointer
 *                 to a `pa_source_info` structure where the source information will be stored.
 *
 */
static void get_input_device_by_index_cb(pa_context *c, const pa_source_info *i, int eol, void *userdata) {
    (void) c;

    pa_source_info *source_info = userdata;

    if (eol < 0 || eol > 0 || !i) {
        // Either an error occurred, or we've reached the end of the list without finding the source
        if (eol != 0) {
            // Signal main loop to continue
            pa_threaded_mainloop_signal(shared_data_1.mainloop, 0);
        }
        return;
    }

    // Copy the source information to the allocated structure
    *source_info = *i; // Shallow copy first

    // Now duplicate any strings
    if (i->name) {
        source_info->name = strdup(i->name);
    }
    if (i->description) {
        source_info->description = strdup(i->description);
    }

    // Signal the main loop that the data is ready
    pa_threaded_mainloop_signal(shared_data_1.mainloop, 0);
}

/**
 * @brief Retrieves the information of an audio input device (source) by its index.
 *
 * This function attempts to allocate memory for a `pa_source_info` structure and retrieve
 * the information for the specified source index using PulseAudio's API. It blocks until
 * the asynchronous operation to fetch the source information is complete or an error occurs.
 *
 * @param index The index of the input device (source) as recognized by PulseAudio.
 * @return A pointer to the allocated `pa_source_info` structure containing the source
 *         information, or NULL if the operation failed or the specified index was not valid.
 *         The caller is responsible for freeing the allocated structure and any associated
 *         strings when they are no longer needed.
 *
 */
pa_source_info* get_input_device_by_index(uint32_t index) {

    // Check if PulseAudio is initialized.
    if (!is_pulse_initialized()) {
        // Attempt to initialize PulseAudio if it's not already initialized.
        if (!initialize_pulse()) {
            fprintf(stderr, "[get_input_device_by_index()] Failed to initialize pulseaudio.\n");
            return NULL;  // Return error if initialization fails.
        }
    }

    if (!shared_data_1.context || !shared_data_1.mainloop) {
        fprintf(stderr, "Invalid shared data context or mainloop.\n");
        return NULL;
    }

    // Allocate memory for source_info
    pa_source_info *source_info = malloc(sizeof(pa_source_info));
    if (!source_info) {
        fprintf(stderr, "Memory allocation for source_info failed.\n");
        return NULL;
    }

    // Start the operation to get the source information
    pa_operation *op = pa_context_get_source_info_by_index(shared_data_1.context, index, get_input_device_by_index_cb, source_info);
    iterate(op);

    // Check if the operation was successful
    if (source_info->name == NULL) {
        // The operation was not successful, free the allocated memory.
        free(source_info);
        return NULL;
    }

    return source_info; // Return the allocated source_info
}

/**
 * @brief Callback function for getting the default output device from the PulseAudio server.
 *
 * @param c Pointer to the PulseAudio context.
 * @param i Pointer to the server information structure.
 * @param userdata The user data passed to the callback function, which is a pointer to a char*.
 */
static void get_default_output_cb(pa_context *c, const pa_server_info *i, void *userdata) {
    //fprintf(stderr, "[DEBUG, get_default_output()] Callback reached.\n");

    (void)c; // Unused parameter

    char **default_sink_name = (char**)userdata;

    // Always signal the mainloop to unblock the iterate function, even if i is NULL
    if (!i) {
        fprintf(stderr, "Failed to get default sink information.\n");
    } else if (i->default_sink_name) {
        // Duplicate the name string to our output variable
        *default_sink_name = strdup(i->default_sink_name);
    }

    // Signal the mainloop to unblock the iterate function, regardless of the outcome
    pa_threaded_mainloop_signal(shared_data_1.mainloop, 0);
}


/**
 * @brief Retrieves the name of the default sink (output device) in the system.
 *
 * This function checks if PulseAudio is initialized and if not, tries to initialize it.
 * Then, it queries the PulseAudio server for the default output device and waits for
 * the operation to complete.
 *
 * @param mainloop A pointer to the mainloop structure.
 * @param context A pointer to the PulseAudio context.
 * @return A dynamically allocated string containing the default sink name, or NULL on error.
 *         The caller is responsible for freeing this string.
 */
char* get_default_output(pa_context *context) {

    //fprintf(stderr,"[DEBUG, get_default_output()] Function reached.\n");

    // Check if PulseAudio is initialized, and if not, initialize it
    if (!is_pulse_initialized()) {
        if (!initialize_pulse()) {
            fprintf(stderr, "Failed to initialize PulseAudio.\n");
            return NULL;
        }
    }

    char *default_sink_name = NULL;

    // Start the operation to get the default sink
    pa_operation *op = pa_context_get_server_info(context, get_default_output_cb, &default_sink_name);

    if (op) {
        // Wait for the operation to complete using the iterate function
        iterate(op); // This function should handle the waiting and signaling
        // pa_operation_unref(op); is called inside iterate, no need to call here
    } else {
        fprintf(stderr, "Failed to create the operation to get server info.\n");
    }

    return default_sink_name; // Caller must free this string
}
/**
 * @brief Callback function for getting the default input device from the PulseAudio server.
 *
 * @param c Pointer to the PulseAudio context.
 * @param i Pointer to the server information structure.
 * @param userdata The user data passed to the callback function, which is a pointer to a char*.
 */
static void get_default_input_cb(pa_context *c, const pa_server_info *i, void *userdata) {
    (void)c; // Unused parameter

    char **default_source_name = (char**)userdata;

    //fprintf(stderr, "[DEBUG, get_default_input_cb()] callback reached.\n");

    if (!i) {
        fprintf(stderr, "Failed to get default source information.\n");
        pa_threaded_mainloop_signal(shared_data_1.mainloop, 0);
        return;
    }

    if (i->default_source_name) {
        // Duplicate the name string to our output variable
        *default_source_name = strdup(i->default_source_name);
        pa_threaded_mainloop_signal(shared_data_1.mainloop, 0); // Signal to stop the loop
    }
}


/**
 * @brief Retrieves the name of the default source (input device) in the system.
 *
 * This function queries the PulseAudio server for all available input devices
 * and iterates through them to find the one that is in the RUNNING state,
 * which typically indicates that it is the default source being used by the system.
 * The name of the default source is then returned.
 *
 * @note The caller is responsible for freeing the memory allocated for the
 * returned source name using the standard free() function to avoid memory leaks.
 *
 * @return A pointer to a dynamically allocated string containing the name of
 * the default source. If no active default source is found or in case of an error,
 * NULL is returned.
 */
char* get_default_input(pa_context *context) {

    //fprintf(stderr, "[DEBUG, get_default_input()] Function reached.\n");

    // Check if PulseAudio is initialized, and if not, initialize it
    if (!is_pulse_initialized() && !initialize_pulse()) {
        fprintf(stderr, "[get_default_onput()] Failed to initialize PulseAudio.\n");
        return NULL;
    }

    char *default_source_name = NULL;

    // Start the operation to get the default source
    pa_operation *op = pa_context_get_server_info(context, get_default_input_cb, &default_source_name);
    iterate(op);

    return default_source_name; // Caller must free this string
}

/**
 * @brief Fetches all profiles associated with a given sound card.
 *
 * This function queries the PulseAudio server for all profiles associated with the sound card
 * specified by the card_index. It blocks until all profiles are fetched or an error occurs.
 *
 * @param pa_ctx A pointer to the initialized pa_context representing the connection to the PulseAudio server.
 * @param card_index The index of the sound card for which to fetch profiles.
 * @param num_profiles A pointer to an integer where the number of fetched profiles will be stored.
 * @return A pointer to an array of pa_card_profile_info2 structures containing the profile info.
 *         This pointer must be freed by the caller. NULL is returned if an error occurs.
 */
void get_profiles_cb(pa_context *c, const pa_card_info *i, int eol, void *userdata) {
    (void) c;
    (void) userdata;

    if (eol < 0) {
        fprintf(stderr, "Failed to fetch profiles.\n");
        pa_threaded_mainloop_signal(shared_data_1.mainloop, 0);
        return;
    }

    if (eol > 0) {
        // All profiles have been fetched, the operation is complete
        pa_threaded_mainloop_signal(shared_data_1.mainloop, 0);
        return;
    }

    // Reallocate memory for profiles array to add new profiles
    shared_data_4.profiles = realloc(shared_data_4.profiles, sizeof(pa_card_profile_info2) * (shared_data_4.num_profiles + i->n_profiles));

    // Now copy the profiles from the PulseAudio provided array
    for (unsigned int j = 0; j < i->n_profiles; ++j) {
        shared_data_4.profiles[shared_data_4.num_profiles + j] = i->profiles[j];
    }

    // Update the number of profiles fetched
    shared_data_4.num_profiles += i->n_profiles;
}



/**
 * @brief Fetches all profiles associated with a given sound card.
 *
 * This function queries the PulseAudio server for all profiles associated with the sound card
 * specified by the card_index. It blocks until all profiles are fetched or an error occurs.
 *
 * @param pa_ctx A pointer to the initialized pa_context representing the connection to the PulseAudio server.
 * @param card_index The index of the sound card for which to fetch profiles.
 * @param num_profiles A pointer to an integer where the number of fetched profiles will be stored.
 * @return A pointer to an array of pa_card_profile_info2 structures containing the profile info.
 *         This pointer must be freed by the caller. NULL is returned if an error occurs.
 */
pa_card_profile_info *get_profiles(pa_context *pa_ctx, uint32_t card_index) {

    // Check if PulseAudio is initialized.
    if (!is_pulse_initialized()) {
        // Attempt to initialize PulseAudio if it's not already initialized.
        if (!initialize_pulse()) {
            fprintf(stderr, "[get_profiles()] Failed to initialize pulseaudio.\n");
            return NULL;  // Return error if initialization fails.
        }
    }

    // Reset the static global variable before use
    free(shared_data_4.profiles);
    shared_data_4.profiles = NULL;
    shared_data_4.num_profiles = 0;

    // Start the operation to fetch the profiles
    pa_operation *op = pa_context_get_card_info_by_index(pa_ctx, card_index, get_profiles_cb, NULL);

    // Wait for the operation to complete
    iterate(op);

    return shared_data_4.profiles;  // Return the static global profiles array
}

/**
 * Callback function for retrieving the mute status of a sink.
 *
 * This callback is provided to the PulseAudio context as part of a request
 * to obtain information about a particular sink. It will be called by the
 * PulseAudio main loop when the sink information is available. The end of list
 * (eol) parameter indicates whether the data received is the last in the list.
 *
 * @param c A pointer to the PulseAudio context.
 * @param i A pointer to the sink information structure.
 * @param eol An end-of-list flag that is positive if there is no more data to process.
 * @param userdata A pointer to user data, expected to be a pointer to an integer that
 *                 will be set to the mute status of the sink.
 *
 * @note The function sets the integer pointed to by `userdata` to the mute state
 *       of the sink. The mute state is non-zero when the sink is muted and zero
 *       when it is not muted. This function is not intended to be called directly
 *       by the user but as a callback from the PulseAudio API when
 *       pa_context_get_sink_info_by_name() is called.
 */
static void get_muted_output_status_cb(pa_context *c, const pa_sink_info *i, int eol, void *userdata) {
    (void) c;

    //fprintf(stderr, "[DEBUG, get_muted_output_status_cb]: inside callback.\n");

    // If eol is set to a positive number, you're at the end of the list
    if (eol > 0) {
        pa_threaded_mainloop_signal(shared_data_1.mainloop, 0);
        //fprintf(stderr, "[DEBUG, get_muted_output_status_cb]: leaving callback.\n");
        return;
    }

    // If eol is negative, an error occurred
    if (eol < 0) {
        int err = pa_context_errno(c); // Retrieve the error number from the context
        fprintf(stderr, "[ERROR, get_muted_output_status_cb]: Error occurred during iteration - %s\n", pa_strerror(err));
        pa_threaded_mainloop_signal(shared_data_1.mainloop, 0);
        return;
    }

    // Cast the userdata to a boolean pointer
    int *is_muted = (int *)userdata;

    // If the sink information is valid, set is_muted to the sink's mute state
    if (i) {
        *is_muted = i->mute;
    }
}



/**
 * Queries the mute status of a specified output sink.
 *
 * This function initiates an asynchronous operation to retrieve the mute status
 * of the sink specified by `sink_name`. It requires a valid `pulseaudio_manager`
 * instance that has been previously initialized with a mainloop and context.
 * The function blocks until the operation is complete or an error occurs.
 *
 * @param self A pointer to the initialized `pulseaudio_manager` instance.
 * @param sink_name The name of the sink whose mute status is being queried.
 *
 * @return Returns 1 if the sink is muted, 0 if not muted, and -1 if an error
 *         occurred or the sink was not found. In the case of an error, an
 *         appropriate message will be printed to standard error.
 *
 * @note The function uses `iterate` to block and process the mainloop until
 *       the operation is complete. It is assumed that `iterate` and
 *       `get_muted_output_status_cb` are implemented elsewhere and are
 *       responsible for iterating the mainloop and handling the callback
 *       from the sink information operation, respectively.
 */
int get_muted_output_status(const char *sink_name) {

    //fprintf(stderr,"[DEBUG, get_muted_output_status()] sink_name is %s\n", sink_name);

    // Check if PulseAudio is initialized.
    if (!is_pulse_initialized()) {
        // Attempt to initialize PulseAudio if it's not already initialized.
        if (!initialize_pulse()) {
            fprintf(stderr, "[get_muted_output_status()] Failed to initialize pulseaudio.\n");
            return UINT32_MAX;  // Return error if initialization fails.
        }
    }

    if (!shared_data_1.mainloop || !shared_data_1.context || !sink_name) {
        fprintf(stderr, "Invalid arguments provided.\n");
        return -1;
    }

    pa_operation *op = NULL;
    int is_muted = -1; // Default to -1 in case of error or sink not found

    // Start a PulseAudio operation to get information about the sink
    op = pa_context_get_sink_info_by_name(shared_data_1.context, sink_name, get_muted_output_status_cb, &is_muted);
    iterate(op);

    // -1 will be returned if the sink was not found or another error occurred
    return is_muted;
}

/**
 * @brief Callback function for retrieving the mute status of an audio input source.
 *
 * This callback is invoked by the PulseAudio main loop when the source information
 * becomes available. It is used as part of an asynchronous operation initiated by
 * `get_muted_input_status` to obtain the mute status of a specified audio source.
 * The `eol` parameter indicates if the data received is the last in the list or if
 * an error has occurred during the iteration.
 *
 * @param c Pointer to the PulseAudio context.
 * @param i Pointer to the source information structure containing details of the source.
 * @param eol End-of-list flag that is positive if there is no more data to process,
 *            negative if an error occurred during the iteration.
 * @param userdata User data provided when initiating the operation; expected to be a
 *                 pointer to an integer that will be set to the mute status of the source.
 *
 */
static void get_muted_input_status_cb(pa_context *c, const pa_source_info *i, int eol, void *userdata) {
    //fprintf(stderr, "[DEBUG, get_muted_input_status_cb]: inside callback.\n");

    // If eol is set to a positive number, you're at the end of the list
    if (eol > 0) {
        pa_threaded_mainloop_signal(shared_data_1.mainloop, 0);
        //fprintf(stderr, "[DEBUG, get_muted_input_status_cb]: leaving callback.\n");
        return;
    }

    // If eol is negative, an error occurred
    if (eol < 0) {
        int err = pa_context_errno(c); // Retrieve the error number from the context
        fprintf(stderr, "[ERROR, get_muted_input_status_cb]: Error occurred during iteration - %s\n", pa_strerror(err));
        pa_threaded_mainloop_signal(shared_data_1.mainloop, 0);
        return;
    }

    // Cast the userdata to a boolean pointer
    int *is_muted = (int *)userdata;

    // If the source information is valid, set is_muted to the source's mute state
    if (i) {
        *is_muted = i->mute;
    }
}


/**
 * @brief Queries the mute status of a specified audio input (source).
 *
 * This function initiates an asynchronous operation to retrieve the mute status
 * of the source specified by `source_name`. It requires a valid PulseAudio mainloop
 * and context to have been previously initialized and stored in shared_data_1.
 * The function blocks until the operation is complete or an error occurs.
 *
 * @param source_name The name of the source whose mute status is being queried.
 *                    This should be the exact name as recognized by PulseAudio.
 *
 * @return int Returns 1 if the source is muted, 0 if not muted, and -1 if an error
 *         occurred or the source was not found. In the case of an error, an
 *         appropriate message will be printed to standard error.
 *
 */
int get_muted_input_status(const char *source_name) {
    //fprintf(stderr,"[DEBUG, get_muted_input_status()] source_name is %s\n", source_name);

    if (!shared_data_1.mainloop || !shared_data_1.context || !source_name) {
        fprintf(stderr, "Invalid arguments provided.\n");
        return -1;
    }

    pa_operation *op = NULL;
    int is_muted = -1; // Default to -1 in case of error or source not found

    // Start a PulseAudio operation to get information about the source
    op = pa_context_get_source_info_by_name(shared_data_1.context, source_name, get_muted_input_status_cb, &is_muted);
    iterate(op);

    // -1 will be returned if the source was not found or another error occurred
    return is_muted;
}


/**
 * @brief Callback function for handling sink information response.
 *
 * This function is called by the PulseAudio main loop when the information about a sink
 * is available. It processes the sink information and stores the index of the sink in the
 * provided userdata pointer.
 *
 * @param c Pointer to the PulseAudio context.
 * @param info Pointer to the sink information structure.
 * @param eol End-of-list flag indicating if there are more entries to process.
 * @param userdata Pointer to user data where the sink index will be stored.
 */
static void get_output_device_index_by_code_cb(pa_context *c, const pa_sink_info *info, int eol, void *userdata) {
    (void) c;

    uint32_t *index_ptr = (uint32_t *) userdata;

    if (eol < 0) {
        fprintf(stderr, "Error occurred in sink_info_cb.\n");
        return;
    }

    if (!eol && info) {
        *index_ptr = info->index;
    }
    pa_threaded_mainloop_signal(shared_data_1.mainloop, 0);
}

/**
 * @brief Callback function for handling source information response.
 *
 * This function is called by the PulseAudio main loop when the information about a source
 * is available. It processes the source information and stores the index of the source in the
 * provided userdata pointer.
 *
 * @param c Pointer to the PulseAudio context.
 * @param info Pointer to the source information structure.
 * @param eol End-of-list flag indicating if there are more entries to process.
 * @param userdata Pointer to user data where the source index will be stored.
 */
static void get_input_device_index_by_code_cb(pa_context *c, const pa_source_info *info, int eol, void *userdata) {
    (void) c;

    uint32_t *index_ptr = (uint32_t *) userdata;

    if (eol < 0) {
        fprintf(stderr, "Error occurred in source_info_cb.\n");
        return;
    }

    if (!eol && info) {
        *index_ptr = info->index;
    }
    pa_threaded_mainloop_signal(shared_data_1.mainloop, 0);
}

/**
 * @brief Retrieves the index of an input device based on its code (PulseAudio name).
 *
 * This function initiates an asynchronous operation to get information about an input device (source)
 * based on its PulseAudio name. The function requires a valid PulseAudio context and a callback to
 * process the source information once it's received. It waits for the completion of the operation
 * and returns the index of the source.
 *
 * @param context Pointer to the initialized PulseAudio context.
 * @param device_code The pulseaudio code of the source whose index is to be retrieved.
 * @return The index of the input device if found, or UINT32_MAX if not found or in case of error.
 */
uint32_t get_input_device_index_by_code(pa_context *context, const char *device_code) {

    // Initializing the result variable.
    uint32_t index = 0;

    // Check if PulseAudio is initialized.
    if (!is_pulse_initialized()) {
        // Attempt to initialize PulseAudio if it's not already initialized.
        if (!initialize_pulse()) {
            fprintf(stderr, "[get_input_device_index_by_code()] Failed to initialize pulseaudio.\n");
            return UINT32_MAX;  // Return error if initialization fails.
        }
    }

    if (!context || !device_code) {
        fprintf(stderr, "Invalid arguments.\n");
        return UINT32_MAX;
    }

    pa_operation *op = pa_context_get_source_info_by_name(context, device_code, get_input_device_index_by_code_cb, &index);
    iterate(op);

    return index;
}

/**
 * @brief Retrieves the index of an output device based on its code (PulseAudio name).
 *
 * This function initiates an asynchronous operation to get information about an output device (sink)
 * based on its PulseAudio name. The function requires a valid PulseAudio context and a callback to
 * process the sink information once it's received. It waits for the completion of the operation
 * and returns the index of the sink.
 *
 * @param context Pointer to the initialized PulseAudio context.
 * @param device_code The pulseaudio code of the sink whose index is to be retrieved.
 * @return The index of the output device if found, or UINT32_MAX if not found or in case of error.
 */
uint32_t get_output_device_index_by_code(pa_context *context, const char *device_code) {

    //Initializing the result variable.
    uint32_t index = 0;

    // Check if PulseAudio is initialized.
    if (!is_pulse_initialized()) {
        // Attempt to initialize PulseAudio if it's not already initialized.
        if (!initialize_pulse()) {
            fprintf(stderr, "get_output_device_index_by_code()] Failed to initialize pulseaudio.\n");
            return UINT32_MAX;  // Return error if initialization fails.
        }
    }

    if (!context || !device_code) {
        fprintf(stderr, "Invalid arguments.\n");
        return UINT32_MAX;
    }

    pa_operation *op = pa_context_get_sink_info_by_name(context, device_code, get_output_device_index_by_code_cb, &index);
    iterate(op);

    return index;
}

/**
 * @brief Callback function used by get_sink_name_by_code to process information about each sink.
 *
 * This function is called by the PulseAudio context for each sink (output device).
 * It compares the name of each sink with a provided PulseAudio code. If a match is found,
 * it dynamically allocates memory and copies the sink description to the result.
 *
 * @param c The PulseAudio context.
 * @param i Information about the current sink being processed.
 * @param eol End-of-list flag, non-zero if this is the last sink in the list.
 * @param userdata Pointer to a string (char pointer) where the result will be stored.
 */
void get_output_name_by_code_cb(pa_context *c, const pa_sink_info *i, int eol, void *userdata) {
    (void) c;

    if (eol > 0) {
        pa_threaded_mainloop_signal(shared_data_1.mainloop, 0);
        return;
    }

    char **result = (char **)userdata;

    if (strcmp(i->name, *result) == 0) {
        *result = strdup(i->description); // Dynamically allocate and copy the sink description
    }
}

/**
 * @brief Finds and returns the sink name (description) corresponding to a given PulseAudio code (name).
 *
 * This function searches for a PulseAudio sink (output device) with a given code (name)
 * and returns its description. It interacts directly with the PulseAudio server, querying
 * the list of sinks and processing each one using the sink_info_callback function.
 *
 * The function dynamically allocates memory for the sink description, which must be freed
 * by the caller.
 *
 * @param pa_ctx A valid and connected PulseAudio context.
 * @param code The PulseAudio code (name) of the sink to search for.
 * @return char* Dynamically allocated string containing the sink description, or NULL if not found.
 *               The caller is responsible for freeing this memory.
 *
 */
char* get_output_name_by_code(pa_context *pa_ctx, const char *code) {
    if (pa_ctx == NULL || code == NULL) {
        return NULL;
    }

    // Check if PulseAudio is initialized.
    if (!is_pulse_initialized()) {
        // Attempt to initialize PulseAudio if it's not already initialized.
        if (!initialize_pulse()) {
            fprintf(stderr, "[get_output_name_by_code()] Failed to initialize pulseaudio.\n");
            return NULL;  // Return error if initialization fails.
        }
    }

    char *result = strdup(code); // Copy the code to result for the callback function

    // Querying the list of sinks
    pa_operation *op = pa_context_get_sink_info_list(pa_ctx, get_output_name_by_code_cb, &result);

    if (op != NULL) iterate(op);

    if (result != NULL && strcmp(result, code) != 0) {
        return result; // Return the dynamically allocated sink name
    }

    // If no matching device is found or if the result was not updated
    free(result);

    return NULL;
}

/**
 * @brief Callback function used by get_source_name_by_code to process information about each source.
 *
 * This function is called by the PulseAudio context for each source (input device).
 * It compares the name of each source with a provided PulseAudio code. If a match is found,
 * it dynamically allocates memory and copies the source description to the result.
 *
 * @param c The PulseAudio context.
 * @param i Information about the current source being processed.
 * @param eol End-of-list flag, non-zero if this is the last source in the list.
 * @param userdata Pointer to a string (char pointer) where the result will be stored.
 */
void get_input_name_by_code_cb(pa_context *c, const pa_source_info *i, int eol, void *userdata) {
    (void) c;

    if (eol > 0) {
        pa_threaded_mainloop_signal(shared_data_1.mainloop, 0);
        return;
    }

    char **result = (char **)userdata;

    if (strcmp(i->name, *result) == 0) {
        *result = strdup(i->description); // Dynamically allocate and copy the source description
    }
}


/**
 * @brief Finds and returns the source name (description) corresponding to a given PulseAudio code (name).
 *
 * This function searches for a PulseAudio source (input device) with a given code (name)
 * and returns its description. It interacts directly with the PulseAudio server, querying
 * the list of sources and processing each one using the get_source_name_by_code_cb function.
 *
 * The function dynamically allocates memory for the source description, which must be freed
 * by the caller.
 *
 * @param pa_ctx A valid and connected PulseAudio context.
 * @param code The PulseAudio code (name) of the source to search for.
 * @return char* Dynamically allocated string containing the source description, or NULL if not found.
 *               The caller is responsible for freeing this memory.
 *
 */
char* get_input_name_by_code(pa_context *pa_ctx, const char *code) {
    if (pa_ctx == NULL || code == NULL) {
        return NULL;
    }

    // Check if PulseAudio is initialized.
    if (!is_pulse_initialized()) {
        // Attempt to initialize PulseAudio if it's not already initialized.
        if (!initialize_pulse()) {
            fprintf(stderr, "[get_input_name_by_code] Failed to initialize pulseaudio.\n");
            return NULL;  // Return error if initialization fails.
        }
    }

    char *result = strdup(code); // Copy the code to result for the callback function

    // Querying the list of sources
    pa_operation *op = pa_context_get_source_info_list(pa_ctx, get_input_name_by_code_cb, &result);

    if (op != NULL) iterate(op);

    if (result != NULL && strcmp(result, code) != 0) {
        return result; // Return the dynamically allocated source name
    }

    // If no matching device is found or if the result was not updated
    free(result);

    return NULL;
}

/**
 * @brief Retrieves the global default playback sample rate from the PulseAudio configuration.
 *
 * This function reads the PulseAudio daemon configuration file to find the value of the
 * `default-sample-rate` setting, which determines the default sample rate for playback streams.
 * The function can optionally accept a custom path to a PulseAudio configuration file. If no
 * custom path is provided, it defaults to using the standard PulseAudio configuration file
 * located at '/etc/pulse/daemon.conf'.
 *
 * @param custom_config_path Optional path to a custom PulseAudio configuration file. If NULL,
 *                           the function uses the default PulseAudio configuration file path.
 * @return The default sample rate as an integer. Returns -1 if the function fails to open the
 *         configuration file or if the `default-sample-rate` setting is not found.
 */

int get_pulseaudio_global_playback_rate(const char* custom_config_path) {
    FILE* file = NULL;
    struct passwd *pw = getpwuid(getuid());
    const char *homedir = pw->pw_dir;

    char local_config_path[MAX_LINE_LENGTH];
    snprintf(local_config_path, sizeof(local_config_path), "%s/.config/pulse/daemon.conf", homedir);

    if (custom_config_path != NULL) {
        file = fopen(custom_config_path, "r");
    }

    if (!file) {
        file = fopen(local_config_path, "r");
    }

    if (!file) {
        file = fopen(DAEMON_CONF, "r");
    }

    if (!file) {
        perror("Failed to open PulseAudio configuration file");
        return -1;
    }

    char line[MAX_LINE_LENGTH];
    int sample_rate = -1;

    while (fgets(line, sizeof(line), file)) {
        char* p = line;
        // Skip leading whitespace
        while (*p && isspace((unsigned char)*p)) {
            p++;
        }

        // Skip comment lines
        if (*p == ';' || *p == '#') {
            continue;
        }

        // Check if the line contains the required setting
        if (strncmp(p, "default-sample-rate", 19) == 0) {
            char* value_str = strchr(p, '=');
            if (value_str) {
                value_str++;
                sample_rate = atoi(value_str);
                break;
            }
        }
    }
    fclose(file);

    return sample_rate;
}

/**
 * @brief Callback function for handling sink information.
 *
 * This function is used as a callback to process information about a PulseAudio sink.
 * It retrieves the mute state of a specific channel from the sink's volume information.
 *
 * @param c Pointer to the PulseAudio context.
 * @param info Pointer to the structure containing the sink information.
 * @param eol End-of-list flag. If non-zero, indicates no more data.
 * @param userdata Pointer to user data, expected to be of type _shared_data_5.
 *
 * @details This function checks if the channel index specified in the user data (_shared_data_5)
 *          is within the range of available channels in the sink. If it is, the function then
 *          accesses the volume of the specified channel directly and determines if it is muted.
 *          The mute state is stored in the user data.
 *
 * @note The function returns immediately if the eol parameter is greater than zero, indicating
 *       the end of the list of sink information.
 */
static void get_output_channel_mute_state_cb(pa_context *c, const pa_sink_info *info, int eol, void *userdata) {
    (void) c;

    if (eol > 0) {
        pa_threaded_mainloop_signal(shared_data_1.mainloop, 0);
        return;
    }

    _shared_data_5 *data = (_shared_data_5 *)userdata;

    // Ensure that the channel index is within the range of available channels
    if (info && data->channel_index < info->volume.channels) {
        // Directly accessing the volume of the specified channel
        data->mute_state = (info->volume.values[data->channel_index] == PA_VOLUME_MUTED);
    }
}

/**
 * @brief Retrieves the mute state of a specific channel of a given sink.
 *
 * This function checks if the specified channel in a given sink is muted. It utilizes
 * the PulseAudio API to query sink information and then checks the volume level of
 * the specified channel, considering it muted if the volume is set to PA_VOLUME_MUTED.
 *
 * @param context Pointer to the PulseAudio context. It should be a valid and initialized context.
 * @param mainloop Pointer to a PulseAudio threaded mainloop. This mainloop should be running for the operation.
 * @param sink_index The index of the sink whose channel mute state is to be checked.
 * @param channel_index The index of the channel within the sink to check for mute state.
 *
 * @return Returns true if the specified channel is muted, false if not muted or in case of an error.
 *
 * @note The function will return false if the context or mainloop pointers are null, or if the sink or channel
 *       indices are invalid.
 */
bool get_output_channel_mute_state(pa_context *context, pa_threaded_mainloop *mainloop,
uint32_t sink_index, uint32_t channel_index) {

    if (!context || !mainloop) {
        fprintf(stderr, "Invalid PulseAudio context or mainloop.\n");
        return false;
    }

    _shared_data_5 data = {channel_index, false};
    // Requesting information about the specified sink
    pa_operation *op = pa_context_get_sink_info_by_index(context, sink_index, get_output_channel_mute_state_cb, &data);
    iterate(op);

    return data.mute_state;
}

/**
 * @brief Callback function for handling input device information.
 *
 * This function is used as a callback to process information about a PulseAudio input device.
 * It retrieves the mute state of a specific channel from the input device volume information.
 *
 * @param c Pointer to the PulseAudio context.
 * @param info Pointer to the structure containing the input device information.
 * @param eol End-of-list flag. If non-zero, indicates no more data.
 * @param userdata Pointer to user data, expected to be of type _shared_data_5.
 *
 * @details This function checks if the channel index specified in the user data (_shared_data_5)
 *          is within the range of available channels in the input device. If it is, the function then
 *          accesses the volume of the specified channel directly and determines if it is muted.
 *          The mute state is stored in the user data.
 *
 * @note The function returns immediately if the eol parameter is greater than zero, indicating
 *       the end of the list of input device information.
 */
static void get_input_channel_mute_state_cb(pa_context *c, const pa_source_info *info, int eol, void *userdata) {
    (void) c;

    if (eol > 0) {
        pa_threaded_mainloop_signal(shared_data_1.mainloop, 0);
        return;
    }

    _shared_data_5 *data = (_shared_data_5 *)userdata;

    // Ensure that the channel index is within the range of available channels
    if (info && data->channel_index < info->volume.channels) {
        // Directly accessing the volume of the specified channel
        data->mute_state = (info->volume.values[data->channel_index] == PA_VOLUME_MUTED);
    }
}

/**
 * @brief Retrieves the mute state of a specific channel of a given input device.
 *
 * This function checks if the specified channel in a given input device is muted. It utilizes
 * the PulseAudio API to query input device information and then checks the volume level of
 * the specified channel, considering it muted if the volume is set to PA_VOLUME_MUTED.
 *
 * @param context Pointer to the PulseAudio context. It should be a valid and initialized context.
 * @param mainloop Pointer to a PulseAudio threaded mainloop. This mainloop should be running for the operation.
 * @param source_index The index of the input device whose channel mute state is to be checked.
 * @param channel_index The index of the channel within the input device to check for mute state.
 *
 * @return Returns true if the specified channel is muted, false if not muted or in case of an error.
 *
 * @note The function will return false if the context or mainloop pointers are null, or if the input device or channel
 *       indices are invalid.
 */
bool get_input_channel_mute_state(pa_context *context, pa_threaded_mainloop *mainloop,
uint32_t source_index, uint32_t channel_index) {
    if (!context || !mainloop) {
        fprintf(stderr, "Invalid PulseAudio context or mainloop.\n");
        return false;
    }

    _shared_data_5 data = {channel_index, false};
    // Requesting information about the specified input device
    pa_operation *op = pa_context_get_source_info_by_index(context, source_index, get_input_channel_mute_state_cb, &data);
    iterate(op);

    return data.mute_state;
}
