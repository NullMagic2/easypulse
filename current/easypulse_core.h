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
typedef struct pulseaudio_sink pulseaudio_sink;

/**
 * @brief Represents a PulseAudio sink.
 */
struct pulseaudio_sink {
    uint32_t index;           ///< Index of the sink.
    char *name;               ///< Name of the sink.
    char *description;       ///< Description of the sink.
    pa_cvolume volume;       ///< Volume of the sink.
    pa_channel_map channel_map; ///< Channel map of the sink.
    int mute;                ///< Mute status of the sink (1 for muted, 0 for unmuted).
    int number_of_channels; //The number of channels of the sink.
};

/**
 * @brief Represents the main manager for PulseAudio operations.
 */
struct pulseaudio_manager {
    pa_threaded_mainloop *mainloop; ///< Mainloop for PulseAudio operations.
    pa_context *context;         ///< PulseAudio context.
    pulseaudio_sink *sinks;       ///< Array of available sinks.
    uint32_t sink_count;              ///< Count of available sinks.
    int pa_ready;                ///< Indicates if PulseAudio is ready (1 for ready, 2 for error).
    int sinks_loaded;            ///< Indicates if sinks are loaded (0 for not loaded, 1 for loaded successfully, 2 for error).
    int operations_pending;      // Counter for pending operations.
    int active_sink_index;       // The active sink index, i.e, the sink being used for playback.
    char *active_sink_name;      // The name of the active sink.
    uint32_t current_sink_index; // The sink being processed right now by the program. It's not necessarily the same as the playback sink.


    bool (*initialize)(pulseaudio_manager *self);      ///< Function to initialize the manager.
    bool (*load_sinks)(pulseaudio_manager *self);       ///< Function to load available sinks.
    bool (*switch_sink)(pulseaudio_manager *self, uint32_t sink_index); ///< Function to switch to a specified sink.
    bool (*set_volume)(pulseaudio_manager *self, uint32_t sink_index, float percentage); // Function to set the volume to a specified percentage.
    void (*get_active_sink)(pulseaudio_manager *manager); // Function to set the volume to a specified percentage.
    void (*iterate)(pulseaudio_manager *manager, pa_operation *op); //Functions to go through every step of a threaded loop.
    void (*destroy)(pulseaudio_manager *manager);
    int (*get_sink_channels) (const pulseaudio_sink *sinks, int sink_index); //Function to get the number of channels of a given sink.
    pulseaudio_sink (*get_sink_list)(pulseaudio_manager *self); //Function to get a list of available sinks.

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
void get_active_sink(pulseaudio_manager *manager);
bool set_volume(pulseaudio_manager *self, uint32_t sink_index, float percentage);
void iterate(pulseaudio_manager *manager, pa_operation *op);
int get_sink_channels(const pulseaudio_sink *sinks, int sink_index);
void destroy(pulseaudio_manager *manager);
pulseaudio_sink* get_sink_list(pulseaudio_manager *self);

#endif // CORE_H
