/**
 * @file core.h
 * @brief EasyPulse Library Header.
 *
 * EasyPulse is a library designed to provide pseudo-object oriented programming
 * functions to simplify access to PulseAudio.
 */

#ifndef EASPYPULSE_CORE_H
#define EASPYPULSE_CORE_H
#include <pulse/introspect.h>
#define DEBUG_MODE 0  // Debug mode flag

#include <pulse/pulseaudio.h>
#include "system_query.h"
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>

// Forward declarations
typedef struct pulseaudio_manager pulseaudio_manager;
typedef struct pulseaudio_device pulseaudio_device;
typedef struct pulseaudio_volume pulseaudio_volume;


typedef struct {
    char *name;          // Name of the profile
    char *description;   // Description of the profile
    uint32_t channels;   // Number of channels this profile has
} pulseaudio_profile;

//Internal volume information.
typedef struct _internal_volume {
    uint32_t index;
    char *code;             //Pulseaudio name of the volume.
    pa_cvolume *volume;     //Volume representation.
    pa_channel_map *cmap;   //Channel map representation.

} internal_volume;

/**
 * @brief Represents a PulseAudio device.
 */
struct pulseaudio_device {
    uint32_t index;                              // Index of the device.
    char *code;                                  // Pulseaudio name of the device.
    char *name;                                  // Pulseaudio description of the device.
    char *alsa_id;                               // Alsa ID of the device.
    int sample_rate;                             // Current sample rate of the device.
    pa_card_profile_info *active_profile;        // Active alsa profile of this device.
    char **channel_names;                        // Public channel names.
    int master_volume;                           // Average volume of all channels (in percentage).
    int *channel_volume;                         // Volume of each individual channel (in percentage).
    bool mute;                                   // Mute status of the devices (true for muted, false for unmuted).
    int min_channels;                            // The minimum number of channels of the device.
    int max_channels;                            // The maximum number of channels of the device.
    pa_card_profile_info *profiles;              // Array of available profiles for the device
    uint32_t profile_count;                      // Number of available profiles
};

/**
 * @brief Represents the main manager for PulseAudio operations.
 */
struct pulseaudio_manager {
    pa_threaded_mainloop *mainloop;            // Mainloop for PulseAudio operations.
    pa_context *context;                       // PulseAudio context.
    pulseaudio_device *outputs;                // Array of available output devices.
    pulseaudio_device *inputs;                 // Array of available input devices.
    int pa_ready;                              // Indicates if PulseAudio is ready (1 for ready, 2 for error).
    int devices_loaded;                        // Indicates if devices are loaded (0 for not loaded, 1 for loaded successfully, 2 for error).
    char *active_output_device;                // Pointer to active output device.
    char *active_input_device;                 // Pointer to active input device.
    uint32_t output_count;                     // Number of pulseaudio sinks (outputs).
    uint32_t input_count;                      // Number of pulseaudio sources (inputs).
};

pulseaudio_manager *manager_create(void);
void manager_cleanup(pulseaudio_manager *manager);                 //Cleans up the manager.

int manager_set_master_volume(pulseaudio_manager *manager,
uint32_t device_id, int volume);                                   //Sets the master volume of a given volume.

int manager_toggle_output_mute(pulseaudio_manager *manager,
uint32_t index, int state);                                        //Toggles the volume of output device to muted / unmuted.

int manager_toggle_input_mute(pulseaudio_manager *manager,
uint32_t index, int state);                                        //Toggles the volume of input device to muted / unmuted.

bool manager_switch_default_output(pulseaudio_manager *self,
uint32_t device_index);                                            //Changes the default sink.

#endif // CORE_H
