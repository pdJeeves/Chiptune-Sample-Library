// we are the music makers, and we are the dreamers of dreams

#ifndef SFXR_SOUNDEFFECTS_H
#define SFXR_SOUNDEFFECTS_H

#include <cmath>
#include <cstdint>
#define INCLUDE_SAMPLES 1
#define INCLUDE_WAV_EXPORT 1


extern "C"
{

float MidiKeyToFrequency(int key);
int MidiKeyFromFrequency(float freq);
int MidiKeyFromString(const char * key);
// returns pointer to static thread local buffer 
const char * StringFromMidiKey(int key);

void UnitTestMidiHelpers();
};


namespace sfxr
{

struct Settings
{
#if INCLUDE_SAMPLES
	static Settings Mutate(Settings const& input);
	static Settings Coin();
	static Settings Laser();
	static Settings Explosion();
	static Settings Powerup();
	static Settings Hit();
	static Settings Jump();
	static Settings Blip();

	static Settings Randomize();
#endif
#if INCLUDE_WAV_EXPORT
//wav freq can't actually change based on the original code, see below	
	bool ExportWAV(const char* filename, int wav_bits=16, int wav_freq=44100) const;
#endif

	int wave_type;
// so because converting to hz incldues the sample rate in the equation its not really changable
	static constexpr double internalToHz =  8.0 * 44100.0 /  100.0;

	float get_frequency_baseHz() const { float v = frequency.base; return internalToHz * (v * v + 0.001);  }
	void  set_frequency_baseHz(float v) { frequency.base = std::sqrt( v / internalToHz - 0.001);  }

		struct {
		float attack{}, // Attack is the beginning of the sound, longer attack means a smoother start.
		sustain{0.6641}, // (1 sec) Sustain is how long the volume is held constant before fading out.
		decay{0.4}, // Decay is the fade-out time.
		punch{};
	} envelope;

	struct
	{
		float base{0.35173364},  // 440 Hz // Start frequency, has a large impact on the overall sound.
		limit{}, //  represents a cutoff that stops all sound if it's passed during a downward slide.
		slide{}, // Slide sets the speed at which the frequency should be swept (up or down).
		deltaSlide{}; // Delta slide is the "slide of slide", or rate of change in the slide speed.
	} frequency;

	struct // Vibrato depth/speed makes for an oscillating frequency effect at various strengths and rates.
	{
		float strength{}, speed{}, delay{};
	} vibrato;

	struct
	{
		// Repeat speed, when not zero, causes the frequency and duty parameters to be reset at regular
		// intervals while the envelope and filter continue unhindered.
		// This can make for some interesting pulsating effects.
		float speed{};
	} retrigger;

	struct
	{
		float frequency{}, // pitch change (up or down)
		speed{};  // Speed indicates time to wait before changing the pitch.
	} arpeggiation;

// two parameters specific to the squarewave waveform
	struct
	{
// The duty cycle of a square describes its shape in terms of how large the positive vs negative sections are.
		float cycle{},
		sweep{};
	} duty;

	struct
	{
		// Flanger offset overlays a delayed copy of the audio stream on top of itself
		// resulting in a kind of tight reverb or sci-fi effect.
		// This parameter can also be swept like many others.

		float offset{}, sweep{};
	} flanger;

// control post processing effects
	struct
	{
		float frequency{}, ramp{};
	} highPassFilter;

	struct
	{
		float resonance{}, frequency{1.f}, ramp{};
	} lowPassFilter;
};

 // this remains constant during sound generation
struct Model
{
	Model(Settings const&, int key = -999); // key in MIDI format -999 to not alter from settings

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
  
// this is stuff that changes during sound generation
// i was hoping sepearting it out would lead to a way to make a compute shader but i don't super understand the algorithms yet.
struct Data
{
	Data(Model const&);

	Model const& model;

	void Reset();
	int SynthSample(int length, float* buffer);
	int SynthSample(int length, int16_t* buffer);

	bool playing_sample=true;
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

}

#endif // SFXR_SOUNDEFFECTS_H
