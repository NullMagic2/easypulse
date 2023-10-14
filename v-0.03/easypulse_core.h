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
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>


// Forward declarations
typedef struct pulseaudio_manager pulseaudio_manager;
typedef struct pulseaudio_device pulseaudio_device;

/**
 * @brief Represents a PulseAudio devices.
 */
struct pulseaudio_device {
    uint32_t index;           ///< Index of the devices.
    char *name;               ///< Name of the devices.
    char *description;       ///< Description of the devices.
    pa_cvolume volume;       ///< Volume of the devices.
    pa_channel_map channel_map; ///< Channel map of the devices.
    int mute;                ///< Mute status of the devices (1 for muted, 0 for unmuted).
    int number_of_channels; //The number of channels of the devices.
};

/**
 * @brief Represents the main manager for PulseAudio operations.
 */
struct pulseaudio_manager {
    pa_threaded_mainloop *mainloop;     ///< Mainloop for PulseAudio operations.
    pa_context *context;                ///< PulseAudio context.
    pulseaudio_device *devices;       ///< Array of available devices.
    uint32_t device_count;             ///< Count of available devices.
    int pa_ready;                       ///< Indicates if PulseAudio is ready (1 for ready, 2 for error).
    int devices_loaded;                 ///< Indicates if devices are loaded (0 for not loaded, 1 for loaded successfully, 2 for error).
    int operations_pending;             // Counter for pending operations.
    int active_device_index;            // The active device index, i.e, the devices being used for playback.
    char *active_device_name;          // The name of the active device.
    uint32_t current_device_index;     // The devices being processed right now by the program. It's not necessarily the same as the playback device.


    bool (*initialize)(pulseaudio_manager *self);      ///< Function to initialize the manager.
    bool (*load_devices)(pulseaudio_manager *self);       ///< Function to load available devices.
    bool (*switch_device)(pulseaudio_manager *self, uint32_t devices_index); ///< Function to switch to a specified devics.
    bool (*set_volume)(pulseaudio_manager *self, uint32_t devices_index, float percentage); // Function to set the volume to a specified percentage.
    void (*get_active_device)(pulseaudio_manager *manager); // Function to set the volume to a specified percentage.
    void (*iterate)(pulseaudio_manager *manager, pa_operation *op); //Functions to go through every step of a threaded loop.
    void (*destroy)(pulseaudio_manager *manager);
    int (*get_device_channels) (const pulseaudio_device *device, int device_index); //Function to get the number of channels of a given device.
    pulseaudio_device (*get_device_list)(pulseaudio_manager *self); //Function to get a list of available device.

};

/**
 * @brief Create a new pulseaudio_manager instance.
 * @return A pointer to the newly created pulseaudio_manager instance.
 */
pulseaudio_manager* new_manager(void);

/**
 * @brief Free the memory associated with a pulseaudio_manager instance.
 * @param manager The pulseaudio_manager instance to delete.
 */
void get_active_device(pulseaudio_manager *manager);
bool set_volume(pulseaudio_manager *self, uint32_t devices_index, float percentage);
void iterate(pulseaudio_manager *manager, pa_operation *op);
int get_device_channels(const pulseaudio_device *devices, int devices_index);
void destroy(pulseaudio_manager *manager);
pulseaudio_device* get_device_list(pulseaudio_manager *self);

#endif // CORE_H
