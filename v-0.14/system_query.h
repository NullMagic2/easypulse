/**
 * @file system_query.h
 * @brief Header file for querying sound card properties in a PulseAudio environment.
 *
 * This header file provides a collection of functions and structures to interact with and query
 * various properties of sound cards using PulseAudio and ALSA interfaces. It includes functions
 * to obtain information about output and input devices (sinks and sources), such as the number
 * of devices, device names, channel names, sample rates, and mute states. It also offers
 * capabilities to manipulate and retrieve detailed information about ALSA cards and ports.
 *
 * Structures:
 * - pa_port_info: Represents port information (e.g., line-in, microphone).
 * - pa_source_info_list: Holds a list of pa_port_info structures.
 *
 * This file serves as an essential component for applications that need to interact with
 * PulseAudio and ALSA for detailed audio device management and information retrieval.
 *
 * @author Mbyte2
 * @date November 13, 2023
 */
#ifndef SYSTEM_QUERY_H
#define SYSTEM_QUERY_H

#include <pulse/pulseaudio.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>

//Structures to get source port information (e.g, line in, microphone...)
//Used by get_source_port_info, get_active_port, and get_source_ports.
typedef struct pa_port_info {
    char *name;           // Port name
    char *description;    // Port description
    bool is_active;       // Is this the active port
} pa_port_info;

typedef struct pa_source_info_list {
    pa_port_info *ports;  // Array of ports
    int num_ports;        // Number of ports
    bool done;            // Indicates if the callback has been called
} pa_source_info_list;

typedef struct {
    uint32_t index;
    uint32_t parent_index; //Index of the corresponding output device (sink).
    char name[256]; // Adjust size as needed
    // Add other fields as needed, like volume, application name, etc.
} output_stream_info;


//Used by get_output_streams() to get a list of all Pulseaudio sink inputs.
typedef struct output_stream_list {
    output_stream_info *inputs;
    uint32_t num_inputs;
} output_stream_list;

//Used by get_input_streams() to get a list of all Pulseaudio sink inputs.
typedef struct input_stream_list {
    pa_source_output_info *inputs; // Note that the structure for source inputs is pa_source_output_info
    uint32_t num_inputs;
} input_stream_list;


void print_proplist(const pa_proplist *p);                                 // Utility function to print all properties in the proplist
uint32_t get_output_device_count(void);                                    //Gets the number of output devices in the system.
uint32_t get_input_device_count(void);                                     //Gets the number of input devices in the system.
uint32_t get_profile_count(uint32_t card_index);                           //Gets the number of profiles for a given soundcard.
pa_sink_info **get_available_output_devices();                             //Gets the total available sinks (output devices) for this system.
pa_source_info **get_available_input_devices();                            //Gets the total available sources (input devices) for this system.

char* get_alsa_input_name(const char *source_name);                        //Gets the corresponding alsa name of a pulseaudio source (input device).
char* get_alsa_output_name(const char *sink_name);                         //Gets the corresponding alsa name of a pulseaudio sink (output device).

pa_sink_info* get_output_device_by_index(uint32_t index);                  //Gets alsa name of a pulseaudio sink (output device) by its index.
pa_source_info* get_input_device_by_index(uint32_t index);                 //Gets alsa name of a pulseaudio source (output device) by its index.

pa_source_info_list* get_source_port_info();                               //Returns which ports in the source are available (mic, line in...).


char** get_input_channel_names(const char *pulse_id,
int num_channels);                                                         //Returns the channel names of an input device.
char** get_output_channel_names(const char *pulse_id,
int num_channels);                                                         //Returns the channel names of an output device.

int get_min_input_channels(const char *alsa_id,
const pa_source_info *source_info);                                        //Gets the maximum output channels an ALSA card supports.

int get_max_input_channels(const char *alsa_id,
const pa_source_info *source_info);                                        //Gets the maximum output channels an ALSA card supports.

int get_max_output_channels(const char *alsa_id,
const pa_sink_info *sink_info);                                            //Gets the maximum output channels an ALSA card supports.

int get_min_output_channels(const char *alsa_id,
const pa_sink_info *sink_info);                                            //Gets the maximum output channels an ALSA card supports.

char* get_alsa_input_id(const char *source_name);                          //Gets the alsa input id based on the pulseaudio channel name.

char* get_alsa_output_id(const char *sink_name);                           //Gets the alsa output id based on the pulseaudio channel name.

int get_output_sample_rate(const char *alsa_id,
const pa_sink_info *sink_info);                                            //Gets the sample rate of a pulseaudio sink (output device).

int get_input_sample_rate(const char *alsa_id,
pa_source_info *source_info);                                              //Gets the sample rate of a pulseaudio source (input device).

pa_source_info *get_input_device_by_name(const char *pulse_code);          //Gets alsa name of a pulseaudio source (input device) by its name.
pa_sink_info *get_output_device_by_name(const char *pulse_code);           //Gets alsa name of a pulseaudio sink (output device) by its name.

uint32_t get_output_device_index_by_code(pa_context *context,
const char *device_code);                                                  //Gets index of an output device by its code (pulseaudio name).

uint32_t get_input_device_index_by_code(pa_context *context,
const char *device_code);                                                  //Gets index of an output device by its code (pulseaudio name).

int get_muted_output_status(const char *sink_name);                        //Queries whether a given audio output (sink) is muted or not.

int get_muted_input_status(const char *source_name);                       //Queries whether a given audio input (source) is muted or not.

pa_volume_t get_channel_volume(const pa_sink_info *sink_info,
unsigned int channel_index);                                               //Retrieves the volume of a given channel.

char* get_default_output(pa_context *context);                             //Gets default output device (default sink).

char* get_default_input(pa_context *context);                              //Gets default input device (default source).

pa_card_profile_info *get_profiles(pa_context *pa_ctx,
uint32_t card_index);                                                      //Gets pulseaudio profiles.

char* get_input_name_by_code(pa_context *pa_ctx,
const char *code);                                                         //Gets input name (pulseaudio device description) by code.

char* get_output_name_by_code(pa_context *pa_ctx,
const char *code);                                                         //Gets output name (pulseaudio device description) by code.

void delete_output_devices(pa_sink_info **sinks);                          //Releases memory for allocated output devices.
void delete_input_devices(pa_source_info **sources);                       //Releases memory for allocated input devices.

int get_pulseaudio_global_playback_rate(const char* custom_config_path);   //Gets the global pulseaudio playback rate from pulseaudio.

bool get_output_channel_mute_state(pa_context *context,
pa_threaded_mainloop *mainloop,
uint32_t sink_index, uint32_t channel_index);                              //Gets mute state of single output channel (0 = unmuted, 1 = muted).

bool get_input_channel_mute_state(pa_context *context,
pa_threaded_mainloop *mainloop,
uint32_t source_index, uint32_t channel_index);                            //Gets mute state of single input channel (0 = unmuted, 1 = muted).

output_stream_list *get_output_streams(pa_context *context);               //Gets a list of output (sink) inputs. They must be freed after allocated.
void output_streams_cleanup(output_stream_list *list);                     //Cleans up allocated output streams.

input_stream_list *get_input_streams(pa_context *context);                 //Gets a list of input (source) inputs.

pa_card_profile_info *get_active_profile(pa_context *context,
char *output_name);                                                        //Gets the active profile of a card.

#endif
