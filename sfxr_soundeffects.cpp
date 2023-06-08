#include "sfxr_soundeffects.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <math.h>

#define rnd(n) (rand()%(n+1))
static float frnd(float range)
{
	return (float)rnd(10000)/10000*range;
}

sfxr::Model::Model(Settings const& maker, int key)
{
	memset(this, 0, sizeof(*this));

	wave_type					= maker.wave_type;
	frequency.base				= maker.frequency.base;
	frequency.limit				= maker.frequency.limit;
	frequency.slide				= maker.frequency.slide;
	envelope.punch				= maker.envelope.punch;
	highPassFilter.frequency	= maker.highPassFilter.frequency;
	lowPassFilter.frequency		= maker.lowPassFilter.frequency;
	flanger.offset				= maker.flanger.offset;
	duty.cycle					= maker.duty.cycle;
	arpeggiation.speed			= maker.arpeggiation.speed;

	if(key > -999)
	{
		float freq = MidiKeyToFrequency(key);
		frequency.base = std::sqrt( freq / Settings::internalToHz - 0.001);
	}


	fmaxperiod=100.0/(maker.frequency.limit*maker.frequency.limit+0.001);
	fdslide=-pow((double)maker.frequency.deltaSlide, 3.0)*0.000001;
	square_duty=0.5f-duty.cycle*0.5f;
	square_slide=-maker.duty.sweep*0.00005f;
	if(maker.arpeggiation.frequency>=0.0f)
		arp_mod=1.0-pow((double)maker.arpeggiation.frequency, 2.0)*0.9;
	else
		arp_mod=1.0+pow((double)maker.arpeggiation.frequency, 2.0)*10.0;

	// reset filter
	float fltw=pow(lowPassFilter.frequency, 3.0f)*0.1f;
	fltw_d=1.0f+maker.lowPassFilter.ramp*0.0001f;
	fltdmp=5.0f/(1.0f+pow(maker.lowPassFilter.resonance, 2.0f)*20.0f)*(0.01f+fltw);
	if(fltdmp>0.8f) fltdmp=0.8f;

	// reset vibrato
	vib_speed=pow(maker.vibrato.speed, 2.0f)*0.01f;
	vib_amp=maker.vibrato.strength*0.5f;
	// reset envelope
	env_length[0]=(int)(maker.envelope.attack*maker.envelope.attack*100000.0f);
	env_length[1]=(int)(maker.envelope.sustain*maker.envelope.sustain*100000.0f);
	env_length[2]=(int)(maker.envelope.decay*maker.envelope.decay*100000.0f);

	fdphase=pow(maker.flanger.sweep, 2.0f)*1.0f;
	if(maker.flanger.sweep<0.0f) fdphase=-fdphase;

	rep_limit=(int)(pow(1.0f-maker.retrigger.speed, 2.0f)*20000+32);
	if(maker.retrigger.speed==0.0f)
		rep_limit=0;
}


sfxr::Data::Data(Model const& model) :
	model(model)
{
	phase=0;

	Reset();

	// reset filter
	fltp=0.0f;
	fltdp=0.0f;
	fltw=pow(model.lowPassFilter.frequency, 3.0f)*0.1f;
	fltphp=0.0f;
	flthp=pow(model.highPassFilter.frequency, 2.0f)*0.1f;
	// reset vibrato
	vib_phase=0.0f;
	// reset envelope
	env_vol=0.0f;
	env_stage=0;
	env_time=0;

	fphase=pow(model.flanger.offset, 2.0f)*1020.0f;
	if(model.flanger.offset<0.0f) fphase=-fphase;

	iphase=abs((int)fphase);
	ipp=0;
	memset(phaser_buffer, 0, sizeof(phaser_buffer));

	for(int i=0;i<32;i++)
		noise_buffer[i]=frnd(2.0f)-1.0f;

	rep_time=0;
}


void sfxr::Data::Reset()
{
	fperiod=100.0/(model.frequency.base*model.frequency.base+0.001);
	period=(int)fperiod;
	fslide=1.0-pow((double)model.frequency.slide, 3.0)*0.01;
	square_duty=0.5f-model.duty.cycle*0.5f;

	arp_time=0;
	arp_limit=(int)(pow(1.0f-model.arpeggiation.speed, 2.0f)*20000+32);
	if(model.arpeggiation.speed==1.0f)
		arp_limit=0;
}


int sfxr::Data::SynthSample(int length, float* buffer)
{
	int i;
	for(i=0;i<length;i++)
	{
		if(!playing_sample)
			break;

		rep_time++;
		if(model.rep_limit!=0 && rep_time>=model.rep_limit)
		{
			rep_time=0;
			Reset();
		}

		// frequency envelopes/arpeggios
		arp_time++;
		if(arp_limit!=0 && arp_time>=arp_limit)
		{
			arp_limit=0;
			fperiod*=model.arp_mod;
		}
		fslide+=model.fdslide;
		fperiod*=fslide;
		if(fperiod>model.fmaxperiod)
		{
			fperiod=model.fmaxperiod;
			if(model.frequency.limit>0.0f)
				playing_sample=false;
		}
		float rfperiod=fperiod;
		if(model.vib_amp>0.0f)
		{
			vib_phase+=model.vib_speed;
			rfperiod=fperiod*(1.0+sin(vib_phase)*model.vib_amp);
		}
		period=(int)rfperiod;
		if(period<8) period=8;
		square_duty+=model.square_slide;
		if(square_duty<0.0f) square_duty=0.0f;
		if(square_duty>0.5f) square_duty=0.5f;
		// volume envelope
		env_time++;
		if(env_time>model.env_length[env_stage])
		{
			env_time=0;
			env_stage++;
			if(env_stage==3)
				playing_sample=false;
		}
		if(env_stage==0)
			env_vol=(float)env_time/model.env_length[0];
		if(env_stage==1)
			env_vol=1.0f+pow(1.0f-(float)env_time/model.env_length[1], 1.0f)*2.0f*model.envelope.punch;
		if(env_stage==2)
			env_vol=1.0f-(float)env_time/model.env_length[2];

		// phaser step
		fphase+=model.fdphase;
		iphase=abs((int)fphase);
		if(iphase>1023) iphase=1023;

		if(model.flthp_d!=0.0f)
		{
			flthp*=model.flthp_d;
			if(flthp<0.00001f) flthp=0.00001f;
			if(flthp>0.1f) flthp=0.1f;
		}

		float ssample=0.0f;
		for(int si=0;si<8;si++) // 8x supersampling
		{
			float sample=0.0f;
			phase++;
			if(phase>=period)
			{
//				phase=0;
				phase%=period;
				if(model.wave_type==3)
					for(int noise=0;noise<32;noise++)
						noise_buffer[noise]=frnd(2.0f)-1.0f;
			}
			// base waveform
			float fp=(float)phase/period;
			switch(model.wave_type)
			{
			case 0: // square
				if(fp<model.square_duty)
					sample=0.5f;
				else
					sample=-0.5f;
				break;
			case 1: // sawtooth
				sample=1.0f-fp*2;
				break;
			case 2: // sine
				sample=(float)sin(fp*2*M_PI);
				break;
			default: // noise
				sample=noise_buffer[phase*32/period];
				break;
			}
			// lp filter
			float pp=fltp;
			fltw*=model.fltw_d;
			if(fltw<0.0f) fltw=0.0f;
			if(fltw>0.1f) fltw=0.1f;
			if(model.lowPassFilter.frequency!=1.0f)
			{
				fltdp+=(sample-fltp)*fltw;
				fltdp-=fltdp*model.fltdmp;
			}
			else
			{
				fltp=sample;
				fltdp=0.0f;
			}
			fltp+=fltdp;
			// hp filter
			fltphp+=fltp-pp;
			fltphp-=fltphp*flthp;
			sample=fltphp;
			// phaser
			phaser_buffer[ipp&1023]=sample;
			sample+=phaser_buffer[(ipp-iphase+1024)&1023];
			ipp=(ipp+1)&1023;
			// final accumulation and envelope application
			ssample+=sample*env_vol;
		}
		ssample=ssample * 0.125f;

		if(buffer!=nullptr)
		{
			if(ssample>1.0f) ssample=1.0f;
			if(ssample<-1.0f) ssample=-1.0f;
			*buffer++=ssample;
		}
	}

	return i;
}

#if INCLUDE_WAV_EXPORT
bool sfxr::Settings::ExportWAV(const char* filename, int wav_bits, int wav_freq) const
{
	int file_sampleswritten = 0;
	float filesample=0.0f;
	int fileacc=0;

	FILE* foutput=fopen(filename, "wb");
	if(!foutput)
		return false;
	// write wav header
	unsigned int dword=0;
	unsigned short word=0;
	fwrite("RIFF", 4, 1, foutput); // "RIFF"
	dword=0;
	fwrite(&dword, 1, 4, foutput); // remaining file size
	fwrite("WAVE", 4, 1, foutput); // "WAVE"

	fwrite("fmt ", 4, 1, foutput); // "fmt "
	dword=16;
	fwrite(&dword, 1, 4, foutput); // chunk size
	word=1;
	fwrite(&word, 1, 2, foutput); // compression code
	word=1;
	fwrite(&word, 1, 2, foutput); // channels
	dword=wav_freq;
	fwrite(&dword, 1, 4, foutput); // sample rate
	dword=wav_freq*wav_bits/8;
	fwrite(&dword, 1, 4, foutput); // bytes/sec
	word=wav_bits/8;
	fwrite(&word, 1, 2, foutput); // block align
	word=wav_bits;
	fwrite(&word, 1, 2, foutput); // bits per sample

	fwrite("data", 4, 1, foutput); // "data"
	dword=0;
	int foutstream_datasize=ftell(foutput);
	fwrite(&dword, 1, 4, foutput); // chunk size

	// write sample data
	Model model(*this);
	Data  data(model);

	float buffer[256];
	while(data.playing_sample)
	{
		data.SynthSample(256, buffer);

		for(auto i = 0; i < 256; ++i)
		{
			float ssample = buffer[i];

			// quantize depending on format
			// accumulate/count to accomodate variable sample rate?
			ssample*=4.0f; // arbitrary gain to get reasonable output volume...
			if(ssample>1.0f) ssample=1.0f;
			if(ssample<-1.0f) ssample=-1.0f;
			filesample+=ssample;
			fileacc++;
			if(wav_freq==44100 || fileacc==2)
			{
				filesample/=fileacc;
				fileacc=0;
				if(wav_bits==16)
				{
					short isample=(short)(filesample*32000);
					fwrite(&isample, 1, 2, foutput);
				}
				else
				{
					unsigned char isample=(unsigned char)(filesample*127+128);
					fwrite(&isample, 1, 1, foutput);
				}
				filesample=0.0f;
			}
			file_sampleswritten++;
		}
	}

	// seek back to header and write size info
	fseek(foutput, 4, SEEK_SET);
	dword=0;
	dword=foutstream_datasize-4+file_sampleswritten*wav_bits/8;
	fwrite(&dword, 1, 4, foutput); // remaining file size
	fseek(foutput, foutstream_datasize, SEEK_SET);
	dword=file_sampleswritten*wav_bits/8;
	fwrite(&dword, 1, 4, foutput); // chunk size (data)
	fclose(foutput);

	return true;
}

#endif

#if INCLUDE_SAMPLES


sfxr::Settings sfxr::Settings::Mutate(Settings const& input)
{
	Settings s = input;


	if(rnd(1)) s.frequency.base+=frnd(0.1f)-0.05f;
//		if(rnd(1)) s.frequency.limit+=frnd(0.1f)-0.05f;
	if(rnd(1)) s.frequency.slide+=frnd(0.1f)-0.05f;
	if(rnd(1)) s.frequency.deltaSlide+=frnd(0.1f)-0.05f;
	if(rnd(1)) s.duty.cycle+=frnd(0.1f)-0.05f;
	if(rnd(1)) s.duty.sweep+=frnd(0.1f)-0.05f;
	if(rnd(1)) s.vibrato.strength+=frnd(0.1f)-0.05f;
	if(rnd(1)) s.vibrato.speed+=frnd(0.1f)-0.05f;
	if(rnd(1)) s.vibrato.delay+=frnd(0.1f)-0.05f;
	if(rnd(1)) s.envelope.attack+=frnd(0.1f)-0.05f;
	if(rnd(1)) s.envelope.sustain+=frnd(0.1f)-0.05f;
	if(rnd(1)) s.envelope.decay+=frnd(0.1f)-0.05f;
	if(rnd(1)) s.envelope.punch+=frnd(0.1f)-0.05f;
	if(rnd(1)) s.lowPassFilter.resonance+=frnd(0.1f)-0.05f;
	if(rnd(1)) s.lowPassFilter.frequency+=frnd(0.1f)-0.05f;
	if(rnd(1)) s.lowPassFilter.ramp+=frnd(0.1f)-0.05f;
	if(rnd(1)) s.highPassFilter.frequency+=frnd(0.1f)-0.05f;
	if(rnd(1)) s.highPassFilter.ramp+=frnd(0.1f)-0.05f;
	if(rnd(1)) s.flanger.offset+=frnd(0.1f)-0.05f;
	if(rnd(1)) s.flanger.sweep+=frnd(0.1f)-0.05f;
	if(rnd(1)) s.retrigger.speed+=frnd(0.1f)-0.05f;
	if(rnd(1)) s.arpeggiation.speed+=frnd(0.1f)-0.05f;
	if(rnd(1)) s.arpeggiation.frequency+=frnd(0.1f)-0.05f;

	return input;
}

sfxr::Settings sfxr::Settings::Coin()
{
	sfxr::Settings s;
	s.frequency.base				= 0.4f+frnd(0.5f);
	s.envelope.attack				= 0.0f;
	s.envelope.sustain				= frnd(0.1f);
	s.envelope.decay				= 0.1f+frnd(0.4f);
	s.envelope.punch				= 0.3f+frnd(0.3f);
	if(rnd(1))
	{
		s.arpeggiation.speed		= 0.5f+frnd(0.2f);
		s.arpeggiation.frequency	= 0.2f+frnd(0.4f);
	}

	return s;

}

sfxr::Settings sfxr::Settings::Laser() {
	sfxr::Settings s;

	s.wave_type				=  rnd(2);
	if (s.wave_type 	 ==  2 && rnd(1))
		s.wave_type 		=  rnd(1);
	s.frequency.base 		=  0.5f + frnd(0.5f);
	s.frequency.limit 		=  s.frequency.base - 0.2f - frnd(0.6f);
	if (s.frequency.limit < 0.2f)
		s.frequency.limit 		=  0.2f;
	s.frequency.slide 		=  -0.15f - frnd(0.2f);
	if (rnd(2) 	 ==  0) {
		s.frequency.base 		=  0.3f + frnd(0.6f);
		s.frequency.limit 		=  frnd(0.1f);
		s.frequency.slide 		=  -0.35f - frnd(0.3f);
	}
	if (rnd(1)) {
		s.duty.cycle 		=  frnd(0.5f);
		s.duty.sweep 		=  frnd(0.2f);
	} else {
		s.duty.cycle 		=  0.4f + frnd(0.5f);
		s.duty.sweep 		=  -frnd(0.7f);
	}
	s.envelope.attack 		=  0.0f;
	s.envelope.sustain 		=  0.1f + frnd(0.2f);
	s.envelope.decay 		=  frnd(0.4f);
	if (rnd(1))
		s.envelope.punch 		=  frnd(0.3f);
	if (rnd(2) 	 ==  0) {
		s.flanger.offset 		=  frnd(0.2f);
		s.flanger.sweep 		=  -frnd(0.2f);
	}
	if (rnd(1))
		s.highPassFilter.frequency 		=  frnd(0.3f);

	return s;
}

sfxr::Settings sfxr::Settings::Explosion() {
	sfxr::Settings s;
	s.wave_type 		=  3;
	if (rnd(1)) {
		s.frequency.base 		=  0.1f + frnd(0.4f);
		s.frequency.slide 		=  -0.1f + frnd(0.4f);
	} else {
		s.frequency.base 		=  0.2f + frnd(0.7f);
		s.frequency.slide 		=  -0.2f - frnd(0.2f);
	}
	s.frequency.base *=  s.frequency.base;
	if (rnd(4) 	 ==  0)
		s.frequency.slide 		=  0.0f;
	if (rnd(2) 	 ==  0)
		s.retrigger.speed 		=  0.3f + frnd(0.5f);
	s.envelope.attack 		=  0.0f;
	s.envelope.sustain 		=  0.1f + frnd(0.3f);
	s.envelope.decay 		=  frnd(0.5f);
	if (rnd(1) 	 ==  0) {
		s.flanger.offset 		=  -0.3f + frnd(0.9f);
		s.flanger.sweep 		=  -frnd(0.3f);
	}
	s.envelope.punch 		=  0.2f + frnd(0.6f);
	if (rnd(1)) {
		s.vibrato.strength 		=  frnd(0.7f);
		s.vibrato.speed 		=  frnd(0.6f);
	}
	if (rnd(2) 	 ==  0) {
		s.arpeggiation.speed 		=  0.6f + frnd(0.3f);
		s.arpeggiation.frequency 		=  0.8f - frnd(1.6f);
	}
	return s;
}

sfxr::Settings sfxr::Settings::Powerup() {
	sfxr::Settings s;
	if (rnd(1))
		s.wave_type 		=  1;
	else
		s.duty.cycle 		=  frnd(0.6f);
	if (rnd(1)) {
		s.frequency.base 		=  0.2f + frnd(0.3f);
		s.frequency.slide 		=  0.1f + frnd(0.4f);
		s.retrigger.speed 		=  0.4f + frnd(0.4f);
	} else {
		s.frequency.base 		=  0.2f + frnd(0.3f);
		s.frequency.slide 		=  0.05f + frnd(0.2f);
		if (rnd(1)) {
			s.vibrato.strength 		=  frnd(0.7f);
			s.vibrato.speed 		=  frnd(0.6f);
		}
	}
	s.envelope.attack 		=  0.0f;
	s.envelope.sustain 		=  frnd(0.4f);
	s.envelope.decay 		=  0.1f + frnd(0.4f);
	return s;
}

sfxr::Settings sfxr::Settings::Hit() {
	sfxr::Settings s;
	s.wave_type 		=  rnd(2);
	if (s.wave_type 	 ==  2)
		s.wave_type 		=  3;
	if (s.wave_type 	 ==  0)
		s.duty.cycle 		=  frnd(0.6f);
	s.frequency.base 		=  0.2f + frnd(0.6f);
	s.frequency.slide 		=  -0.3f - frnd(0.4f);
	s.envelope.attack 		=  0.0f;
	s.envelope.sustain 		=  frnd(0.1f);
	s.envelope.decay 		=  0.1f + frnd(0.2f);
	if (rnd(1))
		s.highPassFilter.frequency 		=  frnd(0.3f);
	return s;
}

sfxr::Settings sfxr::Settings::Jump() {
	sfxr::Settings s;
	s.wave_type 		=  0;
	s.duty.cycle 		=  frnd(0.6f);
	s.frequency.base 		=  0.3f + frnd(0.3f);
	s.frequency.slide 		=  0.1f + frnd(0.2f);
	s.envelope.attack 		=  0.0f;
	s.envelope.sustain 		=  0.1f + frnd(0.3f);
	s.envelope.decay 		=  0.1f + frnd(0.2f);
	if (rnd(1))
		s.highPassFilter.frequency 		=  frnd(0.3f);
	if (rnd(1))
		s.lowPassFilter.frequency 		=  1.0f - frnd(0.6f);
	return s;
}

sfxr::Settings sfxr::Settings::Blip() {
	sfxr::Settings s;
	s.wave_type 		=  rnd(1);
	if (s.wave_type 	 ==  0)
		s.duty.cycle 		=  frnd(0.6f);
	s.frequency.base 		=  0.2f + frnd(0.4f);
	s.envelope.attack 		=  0.0f;
	s.envelope.sustain 		=  0.1f + frnd(0.1f);
	s.envelope.decay 		=  frnd(0.2f);
	s.highPassFilter.frequency 		=  0.1f;
	return s;
}

sfxr::Settings  sfxr::Settings::Randomize()
{
	sfxr::Settings s;

	s.frequency.base		= pow(frnd(2.0f)-1.0f, 2.0f);

	if(rnd(1))
		s.frequency.base	= pow(frnd(2.0f)-1.0f, 3.0f)+0.5f;

	s.frequency.limit		= 0.0f;
	s.frequency.slide		= pow(frnd(2.0f)-1.0f, 5.0f);

	if(s.frequency.base>0.7f && s.frequency.slide>0.2f)
		s.frequency.slide		= -s.frequency.slide;

	if(s.frequency.base<0.2f && s.frequency.slide<-0.05f)
		s.frequency.slide	= -s.frequency.slide;

	s.frequency.deltaSlide	= pow(frnd(2.0f)-1.0f, 3.0f);
	s.duty.cycle			= frnd(2.0f)-1.0f;
	s.duty.sweep			= pow(frnd(2.0f)-1.0f, 3.0f);
	s.vibrato.strength		= pow(frnd(2.0f)-1.0f, 3.0f);
	s.vibrato.speed			= frnd(2.0f)-1.0f;
	s.vibrato.delay			= frnd(2.0f)-1.0f;
	s.envelope.attack		= pow(frnd(2.0f)-1.0f, 3.0f);
	s.envelope.sustain		= pow(frnd(2.0f)-1.0f, 2.0f);
	s.envelope.decay		= frnd(2.0f)-1.0f;
	s.envelope.punch		= pow(frnd(0.8f), 2.0f);

	if(s.envelope.attack+s.envelope.sustain+s.envelope.decay<0.2f)
	{
		s.envelope.sustain	+= 0.2f+frnd(0.3f);
		s.envelope.decay	+= 0.2f+frnd(0.3f);
	}

	s.lowPassFilter.resonance	= frnd(2.0f)-1.0f;
	s.lowPassFilter.frequency	= 1.0f-pow(frnd(1.0f), 3.0f);
	s.lowPassFilter.ramp		= pow(frnd(2.0f)-1.0f, 3.0f);

	if(s.lowPassFilter.frequency<0.1f && s.lowPassFilter.ramp<-0.05f)
		s.lowPassFilter.ramp	= -s.lowPassFilter.ramp;

	s.highPassFilter.frequency	= pow(frnd(1.0f), 5.0f);
	s.highPassFilter.ramp		= pow(frnd(2.0f)-1.0f, 5.0f);
	s.flanger.offset			= pow(frnd(2.0f)-1.0f, 3.0f);
	s.flanger.sweep				= pow(frnd(2.0f)-1.0f, 3.0f);
	s.retrigger.speed			= frnd(2.0f)-1.0f;
	s.arpeggiation.speed		= frnd(2.0f)-1.0f;
	s.arpeggiation.frequency	= frnd(2.0f)-1.0f;

	return s;
}
#endif

extern "C"
{

static constexpr int8_t GetKey(char c)
{
	switch(c | ('a' - 'A')) {
	case 'a': return 9;
	case 'b': return 11;
	case 'c': return 0;
	case 'd': return 2;
	case 'e': return 4;
	case 'f': return 5;
	case 'g': return 7;
	default: return -1;
	}
};

static constexpr const char * GetKeyName(int key_number)
{
	switch(key_number)
	{
	case 0:		return "c";
	case 1:		return "c#";
	case 2:		return "d";
	case 3:		return "d#";
	case 4:		return "e";
	case 5:		return "f";
	case 6:		return "f#";
	case 7:		return "g";
	case 8:		return "g#";
	case 9:		return "a";
	case 10:	return "a#";
	case 11:	return "b";

	default:    return "ab";
	}
};

enum
{
	C4 = 60,
	A4 = 69,

	MiddleC_RAW = GetKey('c') + 4*12,
	A4_Raw		= GetKey('a') + 4*12,
	MiddleC_Freq = MiddleC_RAW - A4_Raw,
	MiddleC_Midi = 60,
	MiddleC_MidiOffset = MiddleC_Midi - MiddleC_RAW,
	MiddleC_Offset = MiddleC_RAW - MiddleC_Freq,
	MiddleC_FreqOffset = MiddleC_Freq - MiddleC_Midi
};

float MidiKeyToFrequency(int key)
{
	key = key + MiddleC_FreqOffset;
	float base_frequency = 440.0; // hz of A4
	return base_frequency * pow(2.0, key / 12.0);
};

int MidiKeyFromFrequency(float freq)
{
	return std::round(log2(freq / 440.0)*12.0 - (int)MiddleC_FreqOffset);
};

int MidiKeyFromString(const char * key)
{
	int size = strlen(key);
	int key_number = size? GetKey(key[0]) : 4;
	int octave = 4;

	if(size > 1)
	{
		if(key[1] == '#')
		{
			key_number += 1;
			if(size > 2) octave = atoi(key+2);
		}
		else if(key[1] == 'b')
		{
			key_number += 1;
			if(size > 2) octave = atoi(key+2);
		}
		else
		{
			octave = atoi(key+1);
		}
	}

	return (key_number + (12 * octave) + MiddleC_MidiOffset);
}

// returns pointer to static thread local buffer
const char * StringFromMidiKey(int key)
{
// should be big enough to contain invalid integers so at least(2 + log2(INT_MAX/12)) chars long
	static thread_local char buffer[32];
	snprintf(buffer, sizeof(buffer), "%s%d", GetKeyName(key % 12), key / 12);
	return buffer;
}

void UnitTestMidiHelpers()
{
	for(auto c = 'a'; c <= 'g'; ++c)
	{
		auto id = GetKey(c);
		assert(GetKeyName(id)[0] == c);
	}

	auto should_be_a4_str = StringFromMidiKey(A4);assert(strcmp(should_be_a4_str, "a4") == 0);
	auto should_be_c4_str = StringFromMidiKey(C4);assert(strcmp(should_be_c4_str, "c4") == 0);

	auto should_be_a4  = MidiKeyFromString("a4");		assert(should_be_a4 == A4);
		 should_be_a4  = MidiKeyFromFrequency(440);		assert(should_be_a4 == A4);
	auto should_be_440 = MidiKeyToFrequency(A4);		assert(should_be_440 == 440);

	auto should_be_c4  = MidiKeyFromString("c4");		assert(should_be_c4 == C4);
		 should_be_c4  = MidiKeyFromFrequency(262);		assert(should_be_c4 == C4);
	auto should_be_262 = MidiKeyToFrequency(C4);		assert(std::fabs(should_be_262 - 261.5) < 10.f);
}


}
