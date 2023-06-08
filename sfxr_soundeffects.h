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
		float attack{},
		sustain{0.6641}, // 1 sec
		decay{0.4}, punch{};
	} envelope;

	struct
	{
		float base{0.35173364},  // 440 Hz
		limit{}, slide{}, deltaSlide{};
	} frequency;

	struct
	{
		float strength{}, speed{}, delay{};
	} vibrato;

	struct
	{
		float speed{};
	} retrigger;

	struct
	{
		float frequency{}, speed{};
	} arpeggiation;
	
// this is only used by square wave thigns
	struct
	{
		float cycle{}, sweep{};
	} duty;

	struct
	{
		float offset{}, sweep{};
	} flanger;

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
