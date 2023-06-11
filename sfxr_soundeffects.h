// we are the music makers, and we are the dreamers of dreams

#ifndef SFXR_SOUNDEFFECTS_H
#define SFXR_SOUNDEFFECTS_H
#include <stdint.h>
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

int sfxr_ModelInit(sfxr_Model * model, sfxr_Settings const* settings);
int sfxr_DataInit(sfxr_Data * data, sfxr_Model const* model);

// this isn't very fast, the library this is forked from always uses a sample rate of 44100
// ergo divide by 44100 to get time in seconds.
int sfxr_ComputeRemainingSamples(sfxr_Data const* data);

int sfxr_DataSynthSample(sfxr_Data * data, int length, float* buffer);
int sfxr_SettingsToJson(FILE *, sfxr_Settings * data);

int sfxr_Init(sfxr_Settings * dst);

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
// currently wav_bits must be 16 and wav freq must be 44100
	int sfxr_ExportWAV(sfxr_Settings const*, const char* filename);
#endif

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


//	float get_frequency_baseHz() const { float v = frequency.base; return internalToHz * (v * v + 0.001);  }
//	void  set_frequency_baseHz(float v) { frequency.base = std::sqrt( v / internalToHz - 0.001);  }

	struct {
		float attackSec, // Attack is the beginning of the sound, longer attack means a smoother start.
		sustainSec, // (0.6641 = 1 sec) Sustain is how long the volume is held constant before fading out.
		decaySec, // Decay is the fade-out time.
		punchPercent; // percentage
	} envelope;

	struct
	{
		float baseHz,  // 0.35173364 = 440 Hz // Start frequency, has a large impact on the overall sound.
		limitHz, //  represents a cutoff that stops all sound if it's passed during a downward slide.
		slideOctaves_s, // Slide sets the speed at which the frequency should be swept (up or down).
		slideOctaves_s2; // Delta slide is the "slide of slide", or rate of change in the slide speed.
	} frequency;

	struct // Vibrato depth/speed makes for an oscillating frequency effect at various strengths and rates.
	{
		float strengthPercent, speedHz, delaySec;
	} vibrato;

	struct
	{
		float frequencySemitones, // pitch change (up or down)
		speedSec;  // Speed indicates time to wait before changing the pitch.
	} arpeggiation;

// two parameters specific to the squarewave waveform
	struct
	{
// The duty cycle of a square describes its shape in terms of how large the positive vs negative sections are.
		float cyclePercent,
		sweepPercent_sec;
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

		float offsetMs_sec, sweepMs_sec2;
	} flanger;

	struct
	{
		float cutoffFrequencyHz, cuttofSweep_sec, resonancePercent;
	} lowPassFilter;

// control post processing effects
	struct
	{
		float cutoffFrequencyHz, cuttofSweep_sec;
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
