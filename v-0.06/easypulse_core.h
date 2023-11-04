/**
 * @file core.h
 * @brief EasyPulse Library Header.
 *
 * EasyPulse is a library designed to provide pseudo-object oriented programming
 * functions to simplify access to PulseAudio.
 */

#ifndef EASPYPULSE_CORE_H
#define EASPYPULSE_CORE_H
#define DEBUG_MODE 0  // Debug mode flag

#include <pulse/pulseaudio.h>
#include "system_query.h"
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    char *name;          // Name of the profile
    char *description;   // Description of the profile
    uint32_t channels;   // Number of channels this profile has
} pulseaudio_profile;


// Forward declarations
typedef struct pulseaudio_manager pulseaudio_manager;
typedef struct pulseaudio_device pulseaudio_device;

/**
 * @brief Represents a PulseAudio devices.
 */
struct pulseaudio_device {
    uint32_t index;                              // Index of the device.
    char *code;                                  // Name of the device.
    char *description;                           // Description of the device.
    pa_cvolume volume;                           // Volume of the device.
    /* pulseaudio_profile *active_profile;       // Active alsa profile of this device. */
    pa_channel_map channel_map;                  // Channel map of the devices.
    int mute;                                    // Mute status of the devices (1 for muted, 0 for unmuted).
    int min_play_channels;                       // The minimum number of playback channels of the device.
    int max_play_channels;                       // The maximum number of playback channels of the device.
    pulseaudio_profile *profiles;                // Array of available profiles for the device
    uint32_t profile_count;                      // Number of available profiles
};

/**
 * @brief Represents the main manager for PulseAudio operations.
 */
struct pulseaudio_manager {
    pa_threaded_mainloop *mainloop;     // Mainloop for PulseAudio operations.
    pa_context *context;                // PulseAudio context.
    pulseaudio_device *devices;         // Array of available devices.
    uint32_t device_count;              // Count of available devices.
    int pa_ready;                       // Indicates if PulseAudio is ready (1 for ready, 2 for error).
    int devices_loaded;                 // Indicates if devices are loaded (0 for not loaded, 1 for loaded successfully, 2 for error).
    int operations_pending;             // Counter for pending operations.
    pulseaudio_device *active_device;   // Pointer to active device.
    uint32_t current_device_index;      // The devices being processed right now by the program. It's not necessarily the same as the playback device.

    bool (*initialize)(pulseaudio_manager *self);                                               // Initializes the manager.
    void (*destroy)(pulseaudio_manager *manager);                                               // Destroys the manager.
    bool (*load_devices)(pulseaudio_manager *self);                                             // Loads available devices.
    bool (*switch_device)(pulseaudio_manager *self, uint32_t devices_index);                    // Switches to a specified device.
    bool (*set_volume)(pulseaudio_manager *self, uint32_t devices_index, float percentage);     // Sets the volume to a specified percentage.
    void (*get_active_device)(pulseaudio_manager *manager);                                     // Assures that a pulseaudio operation is not pending.
    void (*iterate)(pulseaudio_manager *manager, pa_operation *op);                             // Goes through every step of a threaded loop.
    int (*get_active_profile_channels) (const pulseaudio_device *device, int device_index);     // Gets the number of channels of an active profile.
    //void (*get_profiles_for_device)(pulseaudio_manager *manager, const char* device_code);    // Updates the profiles of a particular device.
    void (*get_profile_channels)(pulseaudio_manager *manager);                                  // Extracts profile channels by copying them to null sink.
    uint32_t (*get_profile_count)(uint32_t card_index);                                         // Gets the number of pulseaudio profiles in the system.
};

/**
 * @brief Create a new pulseaudio_manager instance.
 * @return A pointer to the newly created pulseaudio_manager instance.
 */
pulseaudio_manager* new_manager(void);

bool load_devices(pulseaudio_manager *self);
int get_active_profile_channels(const pulseaudio_device *devices, int device_index);
bool set_volume(pulseaudio_manager *self, uint32_t devices_index, float percentage);
void iterate(pulseaudio_manager *manager, pa_operation *op);
int get_device_channels(const pulseaudio_device *devices, int devices_index);
void destroy(pulseaudio_manager *manager);
//void get_profiles_for_device(pulseaudio_manager *manager, const char* device_code);
void get_active_device(pulseaudio_manager *manager);
void get_profile_channels(pulseaudio_manager *manager);

#endif // CORE_H
