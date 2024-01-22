#include <aud.h>

void aud_mixer_init ( void );

void aud_mixer_mix_sources ( aud_source_t* sources, uint64_t count, aud_device_h device, double milliseconds );
