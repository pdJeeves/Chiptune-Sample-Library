#include "sfxr_soundeffects.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

enum
{
	SAMPLE_RATE = 44100,

};

// __restrict is not true
static int sfxr_InternalToReadable(struct sfxr_Settings * dst, struct sfxr_Settings const* src);
static int sfxr_ReadableToInternal(struct sfxr_Settings * dst, struct sfxr_Settings const* src);

#define nullptr 0L

#define rnd(n) (rand()%(n+1))
static float frnd(float range)
{
	return (float)rnd(10000)/10000*range;
}

int sfxr_ModelInit(sfxr_Model * model, sfxr_Settings const* settings)
{
	if(model == 0L || settings == 0L) return -1;

	sfxr_Settings maker;
	sfxr_ReadableToInternal(&maker, settings);

	memset(model, 0, sizeof(*model));

	model->wave_type					= maker.wave_type;
	model->frequency.base				= maker.frequency.baseHz;
	model->frequency.limit				= maker.frequency.limitHz;
	model->frequency.slide				= maker.frequency.slideOctaves_s;
	model->envelope.punch				= maker.envelope.punchPercent;
	model->highPassFilter.frequency		= maker.highPassFilter.cutoffFrequencyHz;
	model->lowPassFilter.frequency		= maker.lowPassFilter.cutoffFrequencyHz;
	model->flanger.offset				= maker.flanger.offsetMs_sec;
	model->duty.cycle					= maker.duty.cyclePercent;
	model->arpeggiation.speed			= maker.arpeggiation.speedSec;

	model->fmaxperiod= 100.0/(maker.frequency.limitHz*maker.frequency.limitHz+0.001);
	model->fdslide= -pow((double)maker.frequency.slideOctaves_s2, 3.0)*0.000001;
	model->square_duty= 0.5f-model->duty.cycle*0.5f;
	model->square_slide= -maker.duty.sweepPercent_sec*0.00005f;
	if(maker.arpeggiation.frequencySemitones>= 0.0f)
		model->arp_mod= 1.0-pow((double)maker.arpeggiation.frequencySemitones, 2.0)*0.9;
	else
		model->arp_mod= 1.0+pow((double)maker.arpeggiation.frequencySemitones, 2.0)*10.0;

	// reset filter
	float fltw= pow(model->lowPassFilter.frequency, 3.0f)*0.1f;
	model->fltw_d= 1.0f+maker.lowPassFilter.cuttofSweep_sec*0.0001f;
	model->fltdmp= 5.0f/(1.0f+pow(maker.lowPassFilter.resonancePercent, 2.0f)*20.0f)*(0.01f+fltw);
	if(model->fltdmp>0.8f) model->fltdmp= 0.8f;

	// reset vibrato
	model->vib_speed= pow(maker.vibrato.speedHz, 2.0f)*0.01f;
	model->vib_amp= maker.vibrato.strengthPercent*0.5f;
	// reset envelope
	model->env_length[0]= (int)(maker.envelope.attackSec*maker.envelope.attackSec*100000.0f);
	model->env_length[1]= (int)(maker.envelope.sustainSec*maker.envelope.sustainSec*100000.0f);
	model->env_length[2]= (int)(maker.envelope.decaySec*maker.envelope.decaySec*100000.0f);

	model->fdphase= pow(maker.flanger.sweepMs_sec2, 2.0f)*1.0f;
	if(maker.flanger.sweepMs_sec2<0.0f) model->fdphase= -model->fdphase;

	model->rep_limit= (int)(pow(1.0f-maker.retrigger.rateHz, 2.0f)*20000+32);
	if(maker.retrigger.rateHz== 0.0f)
		model->rep_limit= 0;

	return 0;
}

int sfxr_DataReset(sfxr_Data * data);

int sfxr_DataInit(sfxr_Data * data, sfxr_Model const* model)
{
	if(data == 0L || model == 0L) return -1;

	data->phase= 0;
	data->model = model;
	data->playing_sample = 1;

	sfxr_DataReset(data);

	// reset filter
	data->fltp= 0.0f;
	data->fltdp= 0.0f;
	data->fltw= pow(model->lowPassFilter.frequency, 3.0f)*0.1f;
	data->fltphp= 0.0f;
	data->flthp= pow(model->highPassFilter.frequency, 2.0f)*0.1f;
	// reset vibrato
	data->vib_phase= 0.0f;
	// reset envelope
	data->env_vol= 0.0f;
	data->env_stage= 0;
	data->env_time= 0;

	data->fphase= pow(model->flanger.offset, 2.0f)*1020.0f;
	if(model->flanger.offset<0.0f) data->fphase= -data->fphase;

	data->iphase= abs((int)data->fphase);
	data->ipp= 0;
	memset(data->phaser_buffer, 0, sizeof(data->phaser_buffer));

	for(int i= 0;i<32;i++)
		data->noise_buffer[i]= frnd(2.0f)-1.0f;

	data->rep_time= 0;

	return 0;
}


int sfxr_DataReset(sfxr_Data * data)
{
	if(data == 0L) return -1;

	data->fperiod= 100.0/(data->model->frequency.base*data->model->frequency.base+0.001);
	data->period= (int)data->fperiod;
	data->fslide= 1.0-pow((double)data->model->frequency.slide, 3.0)*0.01;
	data->square_duty= 0.5f-data->model->duty.cycle*0.5f;

	data->arp_time= 0;
	data->arp_limit= (int)(pow(1.0f-data->model->arpeggiation.speed, 2.0f)*20000+32);
	if(data->model->arpeggiation.speed== 1.0f)
		data->arp_limit= 0;

	return 0;
}

int sfxr_DataSynthSample(sfxr_Data * data, int length, float* buffer)
{
	if(data == 0L || data->model == 0L) return -1;
	sfxr_Model const* model = data->model;

	int i;
	for(i= 0;i<length;i++)
	{
		if(!data->playing_sample)
			break;

		data->rep_time++;
		if(model->rep_limit!= 0 && data->rep_time>= model->rep_limit)
		{
			data->rep_time= 0;
			sfxr_DataReset(data);
		}

		// frequency envelopes/arpeggios
		data->arp_time++;
		if(data->arp_limit!= 0 && data->arp_time>= data->arp_limit)
		{
			data->arp_limit= 0;
			data->fperiod*= model->arp_mod;
		}
		data->fslide+= model->fdslide;
		data->fperiod*= data->fslide;
		if(data->fperiod>model->fmaxperiod)
		{
			data->fperiod= model->fmaxperiod;
			if(model->frequency.limit>0.0f)
				data->playing_sample= 0;
		}
		float rfperiod= data->fperiod;
		if(model->vib_amp>0.0f)
		{
			data->vib_phase+= model->vib_speed;
			rfperiod= data->fperiod*(1.0+sin(data->vib_phase)*model->vib_amp);
		}
		data->period= (int)rfperiod;
		if(data->period<8) data->period= 8;
		data->square_duty+= model->square_slide;
		if(data->square_duty<0.0f) data->square_duty= 0.0f;
		if(data->square_duty>0.5f) data->square_duty= 0.5f;
		// volume envelope
		data->env_time++;
		if(data->env_time>model->env_length[data->env_stage])
		{
			data->env_time= 0;
			data->env_stage++;
			if(data->env_stage== 3)
				data->playing_sample= 0;
		}
		if(data->env_stage== 0)
			data->env_vol= (float)data->env_time/model->env_length[0];
		if(data->env_stage== 1)
			data->env_vol= 1.0f+pow(1.0f-(float)data->env_time/model->env_length[1], 1.0f)*2.0f*model->envelope.punch;
		if(data->env_stage== 2)
			data->env_vol= 1.0f-(float)data->env_time/model->env_length[2];

		// phaser step
		data->fphase+= model->fdphase;
		data->iphase= abs((int)data->fphase);
		if(data->iphase>1023) data->iphase= 1023;

		if(model->flthp_d!= 0.0f)
		{
			data->flthp*= model->flthp_d;
			if(data->flthp<0.00001f) data->flthp= 0.00001f;
			if(data->flthp>0.1f) data->flthp= 0.1f;
		}

		float ssample= 0.0f;
		for(int si= 0;si<8;si++) // 8x supersampling
		{
			float sample= 0.0f;
			data->phase++;
			if(data->phase>= data->period)
			{
//				phase= 0;
				data->phase%= data->period;
				if(model->wave_type== 3)
					for(int noise= 0;noise<32;noise++)
						data->noise_buffer[noise]= frnd(2.0f)-1.0f;
			}
			// base waveform
			float fp= (float)data->phase/data->period;
			switch(model->wave_type)
			{
			case sfxr_Square: // square
				if(fp<model->square_duty)
					sample= 0.5f;
				else
					sample= -0.5f;
				break;
			case sfxr_Sawtooth: // sawtooth
				sample= 1.0f-fp*2;
				break;
			case sfxr_Sine: // sine
				sample= (float)sin(fp*2*3.14159265358);
				break;
			default: // noise
				sample= data->noise_buffer[data->phase*32/data->period];
				break;
			}
			// lp filter
			float pp= data->fltp;
			data->fltw*= model->fltw_d;
			if(data->fltw<0.0f) data->fltw= 0.0f;
			if(data->fltw>0.1f) data->fltw= 0.1f;
			if(model->lowPassFilter.frequency!= 1.0f)
			{
				data->fltdp+= (sample-data->fltp)*data->fltw;
				data->fltdp-= data->fltdp*model->fltdmp;
			}
			else
			{
				data->fltp= sample;
				data->fltdp= 0.0f;
			}
			data->fltp+= data->fltdp;
			// hp filter
			data->fltphp+= data->fltp-pp;
			data->fltphp-= data->fltphp*data->flthp;
			sample= data->fltphp;
			// phaser
			data->phaser_buffer[data->ipp&1023]= sample;
			sample+= data->phaser_buffer[(data->ipp-data->iphase+1024)&1023];
			data->ipp= (data->ipp+1)&1023;
			// final accumulation and envelope application
			ssample+= sample*data->env_vol;
		}
		ssample= ssample * 0.125f;

		if(buffer!= nullptr)
		{
			if(ssample>1.0f) ssample= 1.0f;
			if(ssample<-1.0f) ssample= -1.0f;
			*buffer++= ssample;
		}
	}

	return i;
}

int sfxr_ComputeNoSamples(sfxr_Data const*__restrict data)
{

	if(data == 0L || data->model == 0L) return -1;
	sfxr_Model const *__restrict model = data->model;

	int playing_sample = data->playing_sample;
	int arp_limit = data->arp_limit;
	int rep_time = data->rep_time;
	int arp_time = data->arp_time;
	int env_stage = data->env_stage;
	int env_time = data->env_time;
	double fperiod = data->fperiod;
	double fslide = data->fslide;

	int i = 0;
	do
	{
		rep_time++;
		if(model->rep_limit!= 0 && rep_time>= model->rep_limit)
		{
			rep_time= 0;

			fperiod= 100.0/(model->frequency.base*model->frequency.base+0.001);
			fslide= 1.0-pow((double)model->frequency.slide, 3.0)*0.01;

			arp_time= 0;
			arp_limit= (int)(pow(1.0f-model->arpeggiation.speed, 2.0f)*20000+32);
			if(model->arpeggiation.speed== 1.0f)
				arp_limit= 0;
		}

		arp_time++;
		if(arp_limit != 0 && arp_time >= arp_limit)
		{
			arp_limit = 0;
			fperiod *= model->arp_mod;
		}

		fslide += model->fdslide;
		fperiod *= fslide;
		if(fperiod > model->fmaxperiod)
		{
			fperiod = model->fmaxperiod;
			if(model->frequency.limit > 0.0f)
				playing_sample= 0;
		}

		env_time++;
		if(env_time > model->env_length[env_stage])
		{
			env_time= 0;
			env_stage++;
			if(env_stage== 3)
				playing_sample= 0;
		}
	} while(++i && playing_sample);

	return i;
}

#if INCLUDE_WAV_EXPORT
int sfxr_ExportWAV(sfxr_Settings const* s, const char* filename)
{
	enum
	{
		wav_freq = SAMPLE_RATE,
		wav_bits = 16,
	};

	if(s == NULL) return -1;

	int file_sampleswritten = 0;
	float filesample= 0.0f;
	int fileacc= 0;

	FILE* foutput= fopen(filename, "wb");
	if(!foutput)
		return -1;
	// write wav header
	unsigned int dword= 0;
	unsigned short word= 0;
	fwrite("RIFF", 4, 1, foutput); // "RIFF"
	dword= 0;
	fwrite(&dword, 1, 4, foutput); // remaining file size
	fwrite("WAVE", 4, 1, foutput); // "WAVE"

	fwrite("fmt ", 4, 1, foutput); // "fmt "
	dword= 16;
	fwrite(&dword, 1, 4, foutput); // chunk size
	word= 1;
	fwrite(&word, 1, 2, foutput); // compression code
	word= 1;
	fwrite(&word, 1, 2, foutput); // channels
	dword= wav_freq;
	fwrite(&dword, 1, 4, foutput); // sample rate
	dword= wav_freq*wav_bits/8;
	fwrite(&dword, 1, 4, foutput); // bytes/sec
	word= wav_bits/8;
	fwrite(&word, 1, 2, foutput); // block align
	word= wav_bits;
	fwrite(&word, 1, 2, foutput); // bits per sample

	fwrite("data", 4, 1, foutput); // "data"
	dword= 0;
	int foutstream_datasize= ftell(foutput);
	fwrite(&dword, 1, 4, foutput); // chunk size

	// write sample data
	sfxr_Model model;
	sfxr_Data  data;

	sfxr_ModelInit(&model, s);
	sfxr_DataInit(&data, &model);

	float buffer[256];
	while(data.playing_sample)
	{
		sfxr_DataSynthSample(&data, 256, buffer);

		for(int i = 0; i < 256; ++i)
		{
			float ssample = buffer[i];

			// quantize depending on format
			// accumulate/count to accomodate variable sample rate?
			ssample*= 4.0f; // arbitrary gain to get reasonable output volume...
			if(ssample>1.0f) ssample= 1.0f;
			if(ssample<-1.0f) ssample= -1.0f;
			filesample+= ssample;
			fileacc++;
			if(wav_freq== 44100 || fileacc== 2)
			{
				filesample/= fileacc;
				fileacc= 0;
				if(wav_bits== 16)
				{
					short isample= (short)(filesample*32000);
					fwrite(&isample, 1, 2, foutput);
				}
				else
				{
					unsigned char isample= (unsigned char)(filesample*127+128);
					fwrite(&isample, 1, 1, foutput);
				}
				filesample= 0.0f;
			}
			file_sampleswritten++;
		}
	}

	// seek back to header and write size info
	fseek(foutput, 4, SEEK_SET);
	dword= 0;
	dword= foutstream_datasize-4+file_sampleswritten*wav_bits/8;
	fwrite(&dword, 1, 4, foutput); // remaining file size
	fseek(foutput, foutstream_datasize, SEEK_SET);
	dword= file_sampleswritten*wav_bits/8;
	fwrite(&dword, 1, 4, foutput); // chunk size (data)
	fclose(foutput);

	return 0;
}

#endif

int sfxr_InitInternal(sfxr_Settings * dst)
{
	if(!dst) return -1;
	memset(dst, 0, sizeof(*dst));

	dst->envelope.sustainSec = 0.3;
	dst->envelope.decaySec = 0.4;

	dst->frequency.baseHz = 0.3;

	dst->lowPassFilter.cutoffFrequencyHz = 1.f;

	return 0;
}

int sfxr_Init(sfxr_Settings * dst)
{
	if(!dst) return -1;
	sfxr_InitInternal(dst);
	sfxr_InternalToReadable(dst, dst);
	return 0;
}

#if INCLUDE_SAMPLES


int sfxr_Mutate(sfxr_Settings * s, sfxr_Settings const* src)
{
	if(s == NULL || src == 0L) return -1;
	memcpy(s, src, sizeof(*s));

	sfxr_ReadableToInternal(s, s);

	if(rnd(1)) s->frequency.baseHz+= frnd(0.1f)-0.05f;
//		if(rnd(1)) s->frequency.limit+= frnd(0.1f)-0.05f;
	if(rnd(1)) s->frequency.slideOctaves_s+= frnd(0.1f)-0.05f;
	if(rnd(1)) s->frequency.slideOctaves_s2+= frnd(0.1f)-0.05f;
	if(rnd(1)) s->duty.cyclePercent+= frnd(0.1f)-0.05f;
	if(rnd(1)) s->duty.sweepPercent_sec+= frnd(0.1f)-0.05f;
	if(rnd(1)) s->vibrato.strengthPercent+= frnd(0.1f)-0.05f;
	if(rnd(1)) s->vibrato.speedHz+= frnd(0.1f)-0.05f;
	if(rnd(1)) s->vibrato.delaySec+= frnd(0.1f)-0.05f;
	if(rnd(1)) s->envelope.attackSec+= frnd(0.1f)-0.05f;
	if(rnd(1)) s->envelope.sustainSec+= frnd(0.1f)-0.05f;
	if(rnd(1)) s->envelope.decaySec+= frnd(0.1f)-0.05f;
	if(rnd(1)) s->envelope.punchPercent+= frnd(0.1f)-0.05f;
	if(rnd(1)) s->lowPassFilter.resonancePercent+= frnd(0.1f)-0.05f;
	if(rnd(1)) s->lowPassFilter.cutoffFrequencyHz+= frnd(0.1f)-0.05f;
	if(rnd(1)) s->highPassFilter.cuttofSweep_sec+= frnd(0.1f)-0.05f;
	if(rnd(1)) s->highPassFilter.cutoffFrequencyHz+= frnd(0.1f)-0.05f;
	if(rnd(1)) s->highPassFilter.cuttofSweep_sec+= frnd(0.1f)-0.05f;
	if(rnd(1)) s->flanger.offsetMs_sec+= frnd(0.1f)-0.05f;
	if(rnd(1)) s->flanger.sweepMs_sec2+= frnd(0.1f)-0.05f;
	if(rnd(1)) s->retrigger.rateHz+= frnd(0.1f)-0.05f;
	if(rnd(1)) s->arpeggiation.speedSec+= frnd(0.1f)-0.05f;
	if(rnd(1)) s->arpeggiation.frequencySemitones+= frnd(0.1f)-0.05f;

	sfxr_InternalToReadable(s, s);

	return 0;
}

int sfxr_Coin(sfxr_Settings * s)
{
	if(s == NULL) return -1;
	sfxr_InitInternal(s);

	s->frequency.baseHz				= 0.4f+frnd(0.5f);
	s->envelope.attackSec				= 0.0f;
	s->envelope.sustainSec				= frnd(0.1f);
	s->envelope.decaySec				= 0.1f+frnd(0.4f);
	s->envelope.punchPercent				= 0.3f+frnd(0.3f);
	if(rnd(1))
	{
		s->arpeggiation.speedSec		= 0.5f+frnd(0.2f);
		s->arpeggiation.frequencySemitones	= 0.2f+frnd(0.4f);
	}

	sfxr_InternalToReadable(s, s);

	return 0;
}

int sfxr_Laser(sfxr_Settings * s) {
	if(s == NULL) return -1;
	sfxr_InitInternal(s);

	s->wave_type				=  rnd(2);
	if (s->wave_type 	 ==  2 && rnd(1))
		s->wave_type 		=  rnd(1);
	s->frequency.baseHz 		=  0.5f + frnd(0.5f);
	s->frequency.limitHz 		=  s->frequency.baseHz - 0.2f - frnd(0.6f);
	if (s->frequency.limitHz < 0.2f)
		s->frequency.limitHz 		=  0.2f;
	s->frequency.slideOctaves_s 		=  -0.15f - frnd(0.2f);
	if (rnd(2) 	 ==  0) {
		s->frequency.baseHz 		=  0.3f + frnd(0.6f);
		s->frequency.limitHz 		=  frnd(0.1f);
		s->frequency.slideOctaves_s 		=  -0.35f - frnd(0.3f);
	}
	if (rnd(1)) {
		s->duty.cyclePercent 		=  frnd(0.5f);
		s->duty.sweepPercent_sec 		=  frnd(0.2f);
	} else {
		s->duty.cyclePercent 		=  0.4f + frnd(0.5f);
		s->duty.sweepPercent_sec 		=  -frnd(0.7f);
	}
	s->envelope.attackSec 		=  0.0f;
	s->envelope.sustainSec 		=  0.1f + frnd(0.2f);
	s->envelope.decaySec 		=  frnd(0.4f);
	if (rnd(1))
		s->envelope.punchPercent 		=  frnd(0.3f);
	if (rnd(2) 	 ==  0) {
		s->flanger.offsetMs_sec 		=  frnd(0.2f);
		s->flanger.sweepMs_sec2 		=  -frnd(0.2f);
	}
	if (rnd(1))
		s->highPassFilter.cutoffFrequencyHz 		=  frnd(0.3f);

	sfxr_InternalToReadable(s, s);

	return 0;
}

int sfxr_Explosion(sfxr_Settings * s) {
	if(s == NULL) return -1;
	sfxr_InitInternal(s);

	s->wave_type 		=  3;
	if (rnd(1)) {
		s->frequency.baseHz 		=  0.1f + frnd(0.4f);
		s->frequency.slideOctaves_s 		=  -0.1f + frnd(0.4f);
	} else {
		s->frequency.baseHz 		=  0.2f + frnd(0.7f);
		s->frequency.slideOctaves_s 		=  -0.2f - frnd(0.2f);
	}
	s->frequency.baseHz *=  s->frequency.baseHz;
	if (rnd(4) 	 ==  0)
		s->frequency.slideOctaves_s 		=  0.0f;
	if (rnd(2) 	 ==  0)
		s->retrigger.rateHz 		=  0.3f + frnd(0.5f);
	s->envelope.attackSec 		=  0.0f;
	s->envelope.sustainSec 		=  0.1f + frnd(0.3f);
	s->envelope.decaySec 		=  frnd(0.5f);
	if (rnd(1) 	 ==  0) {
		s->flanger.offsetMs_sec 		=  -0.3f + frnd(0.9f);
		s->flanger.sweepMs_sec2 		=  -frnd(0.3f);
	}
	s->envelope.punchPercent 		=  0.2f + frnd(0.6f);
	if (rnd(1)) {
		s->vibrato.strengthPercent 		=  frnd(0.7f);
		s->vibrato.speedHz 		=  frnd(0.6f);
	}
	if (rnd(2) 	 ==  0) {
		s->arpeggiation.speedSec 		=  0.6f + frnd(0.3f);
		s->arpeggiation.frequencySemitones 		=  0.8f - frnd(1.6f);
	}

	sfxr_InternalToReadable(s, s);

	return 0;
}

int sfxr_Powerup(sfxr_Settings* s) {
	if(s == NULL) return -1;
	sfxr_InitInternal(s);

	if (rnd(1))
		s->wave_type 		=  1;
	else
		s->duty.cyclePercent 		=  frnd(0.6f);
	if (rnd(1)) {
		s->frequency.baseHz 		=  0.2f + frnd(0.3f);
		s->frequency.slideOctaves_s 		=  0.1f + frnd(0.4f);
		s->retrigger.rateHz 		=  0.4f + frnd(0.4f);
	} else {
		s->frequency.baseHz 		=  0.2f + frnd(0.3f);
		s->frequency.slideOctaves_s 		=  0.05f + frnd(0.2f);
		if (rnd(1)) {
			s->vibrato.strengthPercent 		=  frnd(0.7f);
			s->vibrato.speedHz 		=  frnd(0.6f);
		}
	}
	s->envelope.attackSec 		=  0.0f;
	s->envelope.sustainSec 		=  frnd(0.4f);
	s->envelope.decaySec 		=  0.1f + frnd(0.4f);

	sfxr_InternalToReadable(s, s);

	return 0;
}

int sfxr_Hit(sfxr_Settings* s) {
	if(s == NULL) return -1;
	sfxr_InitInternal(s);

	s->wave_type 		=  rnd(2);
	if (s->wave_type 	 ==  2)
		s->wave_type 		=  3;
	if (s->wave_type 	 ==  0)
		s->duty.cyclePercent 		=  frnd(0.6f);
	s->frequency.baseHz 		=  0.2f + frnd(0.6f);
	s->frequency.slideOctaves_s 		=  -0.3f - frnd(0.4f);
	s->envelope.attackSec 		=  0.0f;
	s->envelope.sustainSec 		=  frnd(0.1f);
	s->envelope.decaySec 		=  0.1f + frnd(0.2f);
	if (rnd(1))
		s->highPassFilter.cutoffFrequencyHz 		=  frnd(0.3f);

	sfxr_InternalToReadable(s, s);

	return 0;
}

int sfxr_Jump(sfxr_Settings* s) {
	if(s == NULL) return -1;
	sfxr_InitInternal(s);

	s->wave_type 		=  0;
	s->duty.cyclePercent 		=  frnd(0.6f);
	s->frequency.baseHz 		=  0.3f + frnd(0.3f);
	s->frequency.slideOctaves_s 		=  0.1f + frnd(0.2f);
	s->envelope.attackSec 		=  0.0f;
	s->envelope.sustainSec 		=  0.1f + frnd(0.3f);
	s->envelope.decaySec 		=  0.1f + frnd(0.2f);
	if (rnd(1))
		s->highPassFilter.cutoffFrequencyHz 		=  frnd(0.3f);
	if (rnd(1))
		s->lowPassFilter.cutoffFrequencyHz 		=  1.0f - frnd(0.6f);

	sfxr_InternalToReadable(s, s);

	return 0;
}

int sfxr_Blip(sfxr_Settings* s) {
	if(s == NULL) return -1;
	sfxr_InitInternal(s);

	s->wave_type 		=  rnd(1);
	if (s->wave_type 	 ==  0)
		s->duty.cyclePercent 		=  frnd(0.6f);
	s->frequency.baseHz 		=  0.2f + frnd(0.4f);
	s->envelope.attackSec 		=  0.0f;
	s->envelope.sustainSec 		=  0.1f + frnd(0.1f);
	s->envelope.decaySec 		=  frnd(0.2f);
	s->highPassFilter.cutoffFrequencyHz 		=  0.1f;

	sfxr_InternalToReadable(s, s);

	return 0;
}

int sfxr_Randomize(sfxr_Settings * s)
{
	if(s == NULL) return -1;
	sfxr_InitInternal(s);

	s->frequency.baseHz		= pow(frnd(2.0f)-1.0f, 2.0f);

	if(rnd(1))
		s->frequency.baseHz	= pow(frnd(2.0f)-1.0f, 3.0f)+0.5f;

	s->frequency.limitHz		= 0.0f;
	s->frequency.slideOctaves_s		= pow(frnd(2.0f)-1.0f, 5.0f);

	if(s->frequency.baseHz>0.7f && s->frequency.slideOctaves_s>0.2f)
		s->frequency.slideOctaves_s		= -s->frequency.slideOctaves_s;

	if(s->frequency.baseHz<0.2f && s->frequency.slideOctaves_s<-0.05f)
		s->frequency.slideOctaves_s	= -s->frequency.slideOctaves_s;

	s->frequency.slideOctaves_s2	= pow(frnd(2.0f)-1.0f, 3.0f);
	s->duty.cyclePercent			= frnd(2.0f)-1.0f;
	s->duty.sweepPercent_sec			= pow(frnd(2.0f)-1.0f, 3.0f);
	s->vibrato.strengthPercent		= pow(frnd(2.0f)-1.0f, 3.0f);
	s->vibrato.speedHz			= frnd(2.0f)-1.0f;
	s->vibrato.delaySec			= frnd(2.0f)-1.0f;
	s->envelope.attackSec		= pow(frnd(2.0f)-1.0f, 3.0f);
	s->envelope.sustainSec		= pow(frnd(2.0f)-1.0f, 2.0f);
	s->envelope.decaySec		= frnd(2.0f)-1.0f;
	s->envelope.punchPercent		= pow(frnd(0.8f), 2.0f);

	if(s->envelope.attackSec+s->envelope.sustainSec+s->envelope.decaySec<0.2f)
	{
		s->envelope.sustainSec	+= 0.2f+frnd(0.3f);
		s->envelope.decaySec	+= 0.2f+frnd(0.3f);
	}

	s->lowPassFilter.resonancePercent	= frnd(2.0f)-1.0f;
	s->lowPassFilter.cutoffFrequencyHz	= 1.0f-pow(frnd(1.0f), 3.0f);
	s->highPassFilter.cuttofSweep_sec		= pow(frnd(2.0f)-1.0f, 3.0f);

	if(s->lowPassFilter.cutoffFrequencyHz<0.1f && s->highPassFilter.cuttofSweep_sec<-0.05f)
		s->highPassFilter.cuttofSweep_sec	= -s->highPassFilter.cuttofSweep_sec;

	s->highPassFilter.cutoffFrequencyHz	= pow(frnd(1.0f), 5.0f);
	s->highPassFilter.cuttofSweep_sec		= pow(frnd(2.0f)-1.0f, 5.0f);
	s->flanger.offsetMs_sec			= pow(frnd(2.0f)-1.0f, 3.0f);
	s->flanger.sweepMs_sec2				= pow(frnd(2.0f)-1.0f, 3.0f);
	s->retrigger.rateHz			= frnd(2.0f)-1.0f;
	s->arpeggiation.speedSec		= frnd(2.0f)-1.0f;
	s->arpeggiation.frequencySemitones	= frnd(2.0f)-1.0f;

	sfxr_InternalToReadable(s, s);

	return 0;
}

int sfxr_Delta(struct sfxr_Settings * dst, struct sfxr_Settings *const a, struct sfxr_Settings const* b)
{
	if(dst == nullptr || a == nullptr || b == nullptr) return -1;
	if(dst != a)
	{
		if((unsigned long long)labs((intptr_t)dst - (intptr_t)(a)) < sizeof(*dst))
			return -1;
		memcpy(dst, a, sizeof(*dst));
	}

	float * A = (float*)dst;
	float const* B = (float const*)b;

	enum { N = sizeof(*a) / sizeof(float) };
	for(int i = 1; i < N; ++i)
	{
		A[i] -= B[i];
	}

	return 0;
}

void sfxr_UnitTestTranslationFunctions()
{
	sfxr_Settings a, b, c;
	for(int i = 0; i < 10; ++i)
	{
		sfxr_Randomize(&a);
		sfxr_ReadableToInternal(&b, &a);
		sfxr_InternalToReadable(&c, &b);
		sfxr_ReadableToInternal(&a, &c);
		sfxr_Delta(&c, &a, &b);

		int break_point = 0;
		++break_point;
	}
}

#endif

static char GetKey(char c)
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
}

static const char * GetKeyName(int key_number)
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
}

enum
{
	C4 = 60,
	A4 = 69,

	MiddleC_RAW = 0 + 4*12,
	A4_Raw		= 9 + 4*12,
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
}

int MidiKeyFromFrequency(float freq)
{
	return round(log2(freq / 440.0)*12.0 - (int)MiddleC_FreqOffset);
}

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
	static __thread char buffer[32];
	snprintf(buffer, sizeof(buffer), "%s%d", GetKeyName(key % 12), key / 12);
	return buffer;
}

void UnitTestMidiHelpers()
{
	for(char c = 'a'; c <= 'g'; ++c)
	{
		int id = GetKey(c);
		assert(GetKeyName(id)[0] == c);
	}

	const char *  should_be_a4_str = StringFromMidiKey(A4);assert(strcmp(should_be_a4_str, "a4") == 0);
	const char * should_be_c4_str = StringFromMidiKey(C4);assert(strcmp(should_be_c4_str, "c4") == 0);

	int   should_be_a4  = MidiKeyFromString("a4");		assert(should_be_a4 == A4);
		  should_be_a4  = MidiKeyFromFrequency(440);	assert(should_be_a4 == A4);
	float should_be_440 = MidiKeyToFrequency(A4);		assert(should_be_440 == 440);

	int   should_be_c4  = MidiKeyFromString("c4");		assert(should_be_c4 == C4);
		  should_be_c4  = MidiKeyFromFrequency(262);	assert(should_be_c4 == C4);
	float should_be_262 = MidiKeyToFrequency(C4);		assert(fabs(should_be_262 - 261.5) < 10.f);
}

/*** conversions from slider values, internal, and units ***/

// convert from slider values to internal representation
// then display = units(sliders(internal_c))

int sfxr_SettingsToJson(FILE * file, sfxr_Settings * data)
{
	if(file == nullptr)	return -1;

	fprintf(file, "{\n");
	fprintf(file, "\t\"%s\": {\n", "envelope");
		fprintf(file, "\t\t\"%s\": %f,\n", "attack sec", data->envelope.attackSec);
		fprintf(file, "\t\t\"%s\": %f,\n", "sustain sec", data->envelope.sustainSec);
		fprintf(file, "\t\t\"%s\": %f,\n", "decay sec", data->envelope.decaySec);
		fprintf(file, "\t\t\"%s\": %f\n", "punch %", data->envelope.punchPercent);
	fprintf(file, "\t},\n");

	fprintf(file, "\t\"%s\": {\n", "frequency");
		fprintf(file, "\t\t\"%s\": %f,\n", "frequency Hz", data->frequency.baseHz);
		fprintf(file, "\t\t\"%s\": %f,\n", "min freq Hz", data->frequency.limitHz);
		fprintf(file, "\t\t\"%s\": %f,\n", "slide octave/sec", data->frequency.slideOctaves_s);
		fprintf(file, "\t\t\"%s\": %f\n", "delta slide octave/sec^2", data->frequency.slideOctaves_s2);
	fprintf(file, "\t},\n");

	fprintf(file, "\t\"%s\": {\n", "vibrato");
		fprintf(file, "\t\t\"%s\": %f,\n", "strength %", data->vibrato.strengthPercent);
		fprintf(file, "\t\t\"%s\": %f,\n", "speed Hz", data->vibrato.speedHz);
		fprintf(file, "\t\t\"%s\": %f\n", "delay sec", data->vibrato.delaySec);
	fprintf(file, "\t},\n");

	fprintf(file, "\t\"%s\": {\n", "arpeggiation");
		fprintf(file, "\t\t\"%s\": %f,\n", "frequency semitones", data->arpeggiation.frequencySemitones);
		fprintf(file, "\t\t\"%s\": %f\n", "speed sec", data->arpeggiation.speedSec);
	fprintf(file, "\t},\n");

	fprintf(file, "\t\"%s\": {\n", "duty cycle");
		fprintf(file, "\t\t\"%s\": %f,\n", "cycle %", data->duty.cyclePercent);
		fprintf(file, "\t\t\"%s\": %f\n", "sweep %/sec", data->duty.sweepPercent_sec);
	fprintf(file, "\t},\n");

	fprintf(file, "\t\"%s\": {\n", "retrigger");
		fprintf(file, "\t\t\"%s\": %f\n", "rate hz", data->retrigger.rateHz);
	fprintf(file, "\t},\n");

	fprintf(file, "\t\"%s\": {\n", "flanger");
		fprintf(file, "\t\t\"%s\": %f,\n", "offset ms/sec", data->flanger.offsetMs_sec);
		fprintf(file, "\t\t\"%s\": %f\n", "sweep ms/sec2", data->flanger.sweepMs_sec2);
	fprintf(file, "\t},\n");

	fprintf(file, "\t\"%s\": {\n", "low pass filter");
		fprintf(file, "\t\t\"%s\": %f,\n", "cutoff frequency hz", data->lowPassFilter.cutoffFrequencyHz);
		fprintf(file, "\t\t\"%s\": %f,\n", "cuttoff sweep ^sec", data->lowPassFilter.cuttofSweep_sec);
		fprintf(file, "\t\t\"%s\": %f\n", "resonance %", data->lowPassFilter.resonancePercent);
	fprintf(file, "\t},\n");

	fprintf(file, "\t\"%s\": {\n", "high pass filter");
		fprintf(file, "\t\t\"%s\": %f,\n", "cutoff frequency hz", data->highPassFilter.cutoffFrequencyHz);
		fprintf(file, "\t\t\"%s\": %f,\n", "cuttoff sweep ^sec", data->highPassFilter.cuttofSweep_sec);
	fprintf(file, "\t}\n}\n");

	return 0;
}


const double SAMPLES = SAMPLE_RATE;
const double SAMPLES1 = 44101;
static const double log_half = -0.30102999566;

static double InternalToSec(double v) { return v * v * 100000.0 / SAMPLE_RATE; }
static double InternalToHz(double v) { return 8 * SAMPLE_RATE * (v * v + 0.001) / 100; }
#define SIGN(v) ((v) < 0? -1 : 1)
#define SQUARE(v) ((v)*(v))
#define CUBE(v) ((v)*(v)*(v))
#define min(x, y) ((x) < (y) ? (x) : (y))
#define max(x, y) ((x) > (y) ? (x) : (y))

static double InternalFromSec(double v) { return sqrt(v * SAMPLE_RATE / 100000.0); }
static double InternalFromHz(double v) { return sqrt(100.0 / (8 * SAMPLE_RATE) * v - 0.001) ; }


int sfxr_ReadableToInternal(struct sfxr_Settings * dst, struct sfxr_Settings const* src)
{
	if(dst == nullptr || src == nullptr) return -1;
	if(dst != src)
	{
		if((unsigned long long)labs((intptr_t)dst - (intptr_t)(src)) < sizeof(*dst))
			return -1;
		memcpy(dst, src, sizeof(*dst));
	}

	double v;

	dst->envelope.attackSec		= InternalFromSec(dst->envelope.attackSec);
	dst->envelope.sustainSec	= InternalFromSec(dst->envelope.sustainSec);
	dst->envelope.decaySec		= InternalFromSec(dst->envelope.decaySec);
	dst->envelope.punchPercent	= dst->envelope.punchPercent / 100;

	dst->frequency.baseHz = InternalFromHz(dst->frequency.baseHz );
	dst->frequency.limitHz = InternalFromHz(dst->frequency.limitHz );

	v = exp(dst->frequency.slideOctaves_s * log_half / SAMPLES);
	dst->frequency.slideOctaves_s = cbrt((1.0 -v) * 100);

	v = dst->frequency.slideOctaves_s2 * exp2(-SAMPLES1/SAMPLES) / SAMPLES;
	dst->frequency.slideOctaves_s2 = cbrt(-v /  0.000001);

	v = dst->vibrato.speedHz / (SAMPLES * 10/64.0);
	dst->vibrato.speedHz = sqrt(v * 100.0);

	dst->vibrato.strengthPercent = dst->vibrato.strengthPercent / (0.5 * 100);

	dst->vibrato.delaySec = InternalFromSec(dst->vibrato.delaySec);

	v = exp2(dst->arpeggiation.frequencySemitones / 12.0);
	v = 1.f / max(v, 1e-5);
	dst->arpeggiation.frequencySemitones = v < 1? sqrt(fabs(1.0 - v) / 0.9) : -sqrt((v - 1.0) / 10.0) ;

	v = (dst->arpeggiation.speedSec * SAMPLES);
	dst->arpeggiation.speedSec =  (v == 0) ? 1.0 :	(1.0 - sqrt((v - (v < 100 ? 30 : 32)) / 20000));

	v = dst->duty.cyclePercent * 0.01;
	dst->duty.cyclePercent = (v - 0.5) * -2.0;

	v = (dst->duty.sweepPercent_sec) / (8 * SAMPLES );
	dst->duty.sweepPercent_sec = -v /  0.00005;

	v =  dst->retrigger.rateHz == 0? 0.0f : dst->retrigger.rateHz / SAMPLES;
	dst->retrigger.rateHz =  v == 0? 0.0f : 1.0 - sqrt(fabs(v - 32) / 20000);

	v =  dst->flanger.offsetMs_sec * SAMPLES / 1000;
	dst->flanger.offsetMs_sec = SIGN(v) * sqrt(fabs(v) / 1020);

	v = dst->flanger.sweepMs_sec2 / 100;
	dst->flanger.sweepMs_sec2 = SIGN(v) * sqrt(fabs(v));

	v = dst->lowPassFilter.cutoffFrequencyHz / (dst->lowPassFilter.cutoffFrequencyHz + (8 * SAMPLES));
	dst->lowPassFilter.cutoffFrequencyHz = cbrt(v * 10);

	v =  pow(dst->lowPassFilter.cuttofSweep_sec, 1.0 / SAMPLES);
	dst->lowPassFilter.cuttofSweep_sec =  (v - 1.0) / 0.0001;

	v =  (100 - dst->lowPassFilter.resonancePercent) / 11.0;
	dst->lowPassFilter.resonancePercent =  sqrt((1.0 / (v / 5.0) - 1) / 20);

	v = dst->highPassFilter.cutoffFrequencyHz / (dst->highPassFilter.cutoffFrequencyHz + (8 * SAMPLES));
	dst->highPassFilter.cutoffFrequencyHz = sqrt(v * 10.0);

	v = pow(dst->highPassFilter.cuttofSweep_sec, 1.0 / SAMPLES);
	dst->highPassFilter.cuttofSweep_sec =  (v - 1.0) / 0.0003;

	if(isnormal(dst->frequency.slideOctaves_s) == 0)
		dst->frequency.slideOctaves_s = 0;

	if(isnormal(dst->frequency.slideOctaves_s2) == 0)
		dst->frequency.slideOctaves_s2 = 0;

	return 0;
}

int sfxr_InternalToReadable(struct sfxr_Settings * dst, struct sfxr_Settings const* src)
{
	if(dst == nullptr || src == nullptr) return -1;
	if(dst != src)
	{
		if((unsigned long long)labs((intptr_t)dst - (intptr_t)(src)) < sizeof(*dst))
			return -1;
		memcpy(dst, src, sizeof(*dst));
	}

	dst->frequency.baseHz = InternalToHz(dst->frequency.baseHz );
	dst->frequency.limitHz = InternalToHz(dst->frequency.limitHz );

	dst->frequency.slideOctaves_s = 1.0 - pow(dst->frequency.slideOctaves_s, 3.0) * 0.01;
	dst->frequency.slideOctaves_s = log(dst->frequency.slideOctaves_s) * SAMPLES / log_half;

	dst->frequency.slideOctaves_s2 = -pow(dst->frequency.slideOctaves_s2, 3.0) * 0.000001;
	dst->frequency.slideOctaves_s2 = dst->frequency.slideOctaves_s2 * SAMPLES / exp2( -SAMPLES1/SAMPLES);


	dst->envelope.attackSec		= InternalToSec(dst->envelope.attackSec);
	dst->envelope.sustainSec	= InternalToSec(dst->envelope.sustainSec);
	dst->envelope.decaySec		= InternalToSec(dst->envelope.decaySec);
	dst->envelope.punchPercent	= dst->envelope.punchPercent * 100;


	dst->vibrato.strengthPercent = dst->vibrato.strengthPercent * 0.5 * 100;

	dst->vibrato.speedHz = (dst->vibrato.speedHz*dst->vibrato.speedHz) * 0.01;
	dst->vibrato.speedHz = (SAMPLES * 10/64.0 * dst->vibrato.speedHz);
	dst->vibrato.delaySec = InternalToSec(dst->vibrato.delaySec);


	dst->arpeggiation.frequencySemitones = dst->arpeggiation.frequencySemitones >= 0?
		1.0 - SQUARE(dst->arpeggiation.frequencySemitones) * 0.9 :
		1.0 + SQUARE(dst->arpeggiation.frequencySemitones) * 10;

	dst->arpeggiation.frequencySemitones = 1.f / max(dst->arpeggiation.frequencySemitones, 1e-5);
// finally convert pitch multiplier to semitones
	dst->arpeggiation.frequencySemitones = 12.0 * log2(dst->arpeggiation.frequencySemitones);

	dst->arpeggiation.speedSec = (pow(1.0 - dst->arpeggiation.speedSec, 2.0) * 20000 + 32);
	dst->arpeggiation.speedSec = (dst->arpeggiation.speedSec / SAMPLES);


	dst->duty.cyclePercent = 0.5 - dst->duty.cyclePercent * 0.5;
	dst->duty.cyclePercent = 100 * dst->duty.cyclePercent;

	dst->duty.sweepPercent_sec = -dst->duty.sweepPercent_sec *  0.00005;
	dst->duty.sweepPercent_sec = (8 * SAMPLES * dst->duty.sweepPercent_sec);

	dst->retrigger.rateHz =  dst->retrigger.rateHz == 0? 0.0f : (SQUARE(1 - dst->retrigger.rateHz) * 20000) + 32;
	dst->retrigger.rateHz =  dst->retrigger.rateHz == 0? 0.0f : SAMPLES / dst->retrigger.rateHz;

	dst->flanger.offsetMs_sec = SIGN(dst->flanger.offsetMs_sec) * SQUARE(dst->flanger.offsetMs_sec)*1020;
	dst->flanger.offsetMs_sec =  dst->flanger.offsetMs_sec * 1000 / SAMPLES;

	dst->flanger.sweepMs_sec2 = SIGN(dst->flanger.sweepMs_sec2) * SQUARE(dst->flanger.sweepMs_sec2);
	dst->flanger.sweepMs_sec2 =  100 * dst->flanger.sweepMs_sec2;

	dst->lowPassFilter.cutoffFrequencyHz = CUBE(dst->lowPassFilter.cutoffFrequencyHz) * 0.1;
	dst->lowPassFilter.cutoffFrequencyHz = 8 * SAMPLES * dst->lowPassFilter.cutoffFrequencyHz / (1-dst->lowPassFilter.cutoffFrequencyHz);

	dst->lowPassFilter.cuttofSweep_sec =  1.0 + dst->lowPassFilter.cuttofSweep_sec * 0.0001;;
	dst->lowPassFilter.cuttofSweep_sec =  pow(dst->lowPassFilter.cuttofSweep_sec, SAMPLES);

	dst->lowPassFilter.resonancePercent = 5.0 / (1.0 + SQUARE(dst->lowPassFilter.resonancePercent) * 20);
	dst->lowPassFilter.resonancePercent =  (100*(1-dst->lowPassFilter.resonancePercent*.11));

	dst->highPassFilter.cutoffFrequencyHz = SQUARE(dst->highPassFilter.cutoffFrequencyHz) * 0.1;
	dst->highPassFilter.cutoffFrequencyHz = 8 * SAMPLES * dst->highPassFilter.cutoffFrequencyHz / (1-dst->highPassFilter.cutoffFrequencyHz);

	dst->highPassFilter.cuttofSweep_sec = 1 + dst->highPassFilter.cuttofSweep_sec * 0.0003;
	dst->highPassFilter.cuttofSweep_sec = pow(dst->highPassFilter.cuttofSweep_sec, SAMPLES);

	return 0;
}
