// we are the music makers, and we are the dreamers of dreams

#ifndef SFXR_SOUNDEFFECTS_H
#define SFXR_SOUNDEFFECTS_H
#include <stdio.h>

#define INCLUDE_SAMPLES 1
#define INCLUDE_WAV_EXPORT 1

#ifdef __cplusplus
extern "C" {
#endif

float MidiKeyToFrequency(int key);
int MidiKeyFromFrequency(float freq);
int MidiKeyFromString(const char * key);
// returns pointer to static thread local buffer
const char * StringFromMidiKey(int key);

void UnitTestMidiHelpers();

typedef struct sfxr_Settings sfxr_Settings;
typedef struct sfxr_Model sfxr_Model;
typedef struct sfxr_Data sfxr_Data;

#if INCLUDE_SAMPLES
	int sfxr_Mutate(sfxr_Settings * dst, sfxr_Settings const* src);
	int sfxr_Coin(sfxr_Settings * dst);
	int sfxr_Laser(sfxr_Settings * dst);
	int sfxr_Explosion(sfxr_Settings * dst);
	int sfxr_Powerup(sfxr_Settings * dst);
	int sfxr_Hit(sfxr_Settings * dst);
	int sfxr_Jump(sfxr_Settings * dst);
	int sfxr_Blip(sfxr_Settings * dst);

	int sfxr_Randomize(sfxr_Settings * dst);
	void sfxr_UnitTestTranslationFunctions();
#endif

#if INCLUDE_WAV_EXPORT
//wav freq can't actually change based on the original code
// currently wav_bits must be 8,16, or 32 and sample rate must be <= 44100
// for playback use 16/44100
// for audio mixing/editing use 32/44100
	int sfxr_ExportWAV(sfxr_Settings const*, int wav_bits, int sample_rate, const char* filename);
// sprintf filename convenience function
	int sfxr_ExportWAV_F(sfxr_Settings const*, int wav_bits, int sample_rate, const char* filename_format, ...);
#endif
	
// debug function used to view current state of the settings
// i will not add a settings from json function.
int sfxr_SettingsToJson(FILE *, sfxr_Settings * data);
	
// crush down to desired bit rate (creates PCM wave audio, so the uint8 isn't 2's compliment)
// assumes dst is big enough
int sfxr_Quantize8(unsigned char * dst, float* src, int src_length);
int sfxr_Quantize16(unsigned short * dst, float* src, int src_length);

// returns samples written (or negative if there was a problem)
int sfxr_Downsample(float * dst, int dst_length, float* src, int src_length, int dst_sample_rate, int src_sample_rate);

int sfxr_Init(sfxr_Settings * dst);
int sfxr_ModelInit(sfxr_Model * model, sfxr_Settings const* settings);
int sfxr_DataInit(sfxr_Data * data, sfxr_Model const* model);

// the library this is forked from always uses a sample rate of 44100
// ergo divide by 44100 to get time in seconds.
int sfxr_ComputeRemainingSamples(sfxr_Data const* data);

// use one of buffer or short buffer to get samples out
// 12 bits per sample is the limit of human hearing, but for mixing/editing etc you want full 32 bit floating samples.
// for those purposes you should also use 192khz though; but this library can't do more than 44.1khz (the limit of human hearing is 40khz)
int sfxr_DataSynthSample(sfxr_Data * data, int length, float* buffer, unsigned short * short_buffer);

	
enum sfxr_WaveType
{
	sfxr_Square,
	sfxr_Sawtooth,
	sfxr_Sine,
	sfxr_Noise
};
	

struct sfxr_Settings
{
	enum sfxr_WaveType wave_type;
	
// length of the sound effect is essentially attack + sustain + decay
// but it can be cut off early by the frequency decaying below the frequency.limitHz
// this is further affected by arpeggation which is further affected by retrigger
// basically when making music change the sustainSec to make the note longer/shorter
	struct {
		float attackSec; // Attack is the beginning of the sound, longer attack means a smoother start.
		float sustainSec; // Sustain is how long the volume is held constant before fading out.
		float decaySec; // Decay is the fade-out time.
		float punchPercent; // percentage
	} envelope;

	struct
	{
		float baseHz;  // Start frequency, has a large impact on the overall sound.
		float limitHz; //  represents a cutoff that stops all sound if it's passed during a downward slide.
		float slideOctaves_s; // Slide sets the speed at which the frequency should be swept (up or down).
		float slideOctaves_s2; // Delta slide is the "slide of slide", or rate of change in the slide speed.
	} frequency;

	struct // Vibrato depth/speed makes for an oscillating frequency effect at various strengths and rates.
	{
		float strengthPercent;
		float speedHz;
		float delaySec;
	} vibrato;

	struct
	{
		float frequencySemitones; // pitch change (up or down)
		float speedSec;  // Speed indicates time to wait before changing the pitch.
	} arpeggiation;

// two parameters specific to the squarewave waveform
	struct
	{
// The duty cycle of a square describes its shape in terms of how large the positive vs negative sections are.
		float cyclePercent;
		float sweepPercent_sec;
	} duty;

	struct
	{
		// Repeat speed, when not zero, causes the frequency and duty parameters to be reset at regular
		// intervals while the envelope and filter continue unhindered.
		// This can make for some interesting pulsating effects.
		float rateHz;
	} retrigger;

	struct
	{
		// Flanger offset overlays a delayed copy of the audio stream on top of itself
		// resulting in a kind of tight reverb or sci-fi effect.
		// This parameter can also be swept like many others.

		float offsetMs_sec;
		float sweepMs_sec2;
	} flanger;

// control post processing effects
	struct
	{
		float cutoffFrequencyHz;
		float cuttofSweep_sec;
		float resonancePercent;
	} lowPassFilter;

	struct
	{
		float cutoffFrequencyHz;
		float cuttofSweep_sec;
	} highPassFilter;
};

/*
 * This is stuff that is used to make the sample but never changes during sound production
 * You can use one model to make multiple samples but i don't know why you would want to.
 * But the Data depends on this, so make sure it stays alive while the data exists.
 *
 * The reason this is split out was because i thought maybe it could be used as a uniform buffer (computing the sample on the GPU)
 * The sample generation turns out to be too depdendent on the prior step to do this
 *
 * Or at least i couldn't work out how!
 */
struct sfxr_Model
{
	struct { float base, limit, slide; } frequency;
	struct { float punch; } envelope;
	struct { float frequency; } highPassFilter, lowPassFilter;
	struct { float offset; } flanger;
	struct { float cycle; } duty;
	struct { float speed; } arpeggiation;

	int env_length[3];
	int rep_limit;
	int wave_type;
	float square_duty;
	float square_slide;
	float fdphase;
	float fltw_d;
	float fltdmp;
	float flthp_d;
	float vib_speed;
	float vib_amp;
	double arp_mod;
	double fmaxperiod;
	double fdslide;
};

/*
 * This is what is actually operated on to generate samples!
 */
struct sfxr_Data
{
	sfxr_Model const* model;

	int playing_sample;
	int arp_limit;
	int period;
	int phase;
	int rep_time;
	int arp_time;
	int ipp;
	int iphase;
	int env_stage;
	int env_time;
	float fltw;
	float fltdp;
	float fltp;
	float fltphp;
	float vib_phase;
	float square_duty;
	float fphase;
	float flthp;
	float env_vol;
	double fperiod;
	double fslide;
	float noise_buffer[32];
	float phaser_buffer[1024];
};


#ifdef __cplusplus
}
#endif

#endif // SFXR_SOUNDEFFECTS_H
