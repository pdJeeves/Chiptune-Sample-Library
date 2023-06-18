# Chiptune Sound Library: SFXR

This is a fork of the NES-style sound effect generator made by [Dr Petterr](http://www.drpetter.se). There's a similar [webapp version of Dr Petter's sound maker](https://sfxr.me/). If you would like to preview what types of sounds this library will make!
I have redesigned it to be a C library rather than a standalone application. So you can use it to create new sound effects as needed in your game! 

I included comments wherever i figured something out and tried to make it a lot clearer than the original; and included helper functions to set sound frequency based on midi key!

Example of saving a file:

	// the library never calls malloc, there is no memory management everything is stack based. 
	sfxr_Settings settings;
	sfxr_Coin(&settings)
	sfxr_ExportWAV(&settings, /* bit rate */ 16, /* sample rate */ 44100, "coin.wav");
  
Example of creating a new sound with FMOD using C++:

If you want to use unity then combine this example [with this page here](https://www.fmod.com/docs/2.02/unity/examples-video-playback.html)

	FMOD::Sound * CreateSound(FMOD::System * system, sfxr_Settings const& settings)
	{
		FMOD::Sound * sound = nullptr;
	// the model is data used during sound generation that never changes, it can be used for multiple generators
	// you're free to change the settings once you have the model
		sfxr_Model model;
		sfxr_ModelInit(&model, settings);
	// the data is stuff that does change during sound generation, it can't be used among multiple instances
	// it maintains a reference to the model, so the model can't be deleted until all the data is also.
		sfxr_Data  data;
		sfxr_DataInit(&data, &model);
		
		std::vector<float> _samples;
	// compute size of buffer we need 
		_samples.resize(sfxr_ComputeRemainingSamples(&data));
		
	// synth as float samples, this doesn't have to be all at once, the data is like an iterator in the sample!
		sfxr_DataSynthSample(&data, _samples.size(), _samples.data());
		
	// done with this library now we use FMOD!!
		
	// we don't really need to create the whole buffer to make an FMOD sound but thats the easiest way to do it.
	// it would be smarter to make a buffer about 1/2 a second long, or 20k samples and fill the lower half 
	// when we're in the upper half and vice versa

	// this creates an empty FMOD sound with no data!!!
		FMOD_CREATESOUNDEXINFO info;
		memset(&info, 0, sizeof(info));
		info.cbsize = sizeof(info);
		info.length = _samples.size() * sizeof(_samples[0]);
		info.numchannels = 1;
		info.defaultfrequency = 44100;
		
	// humans can't actually hear a difference between float and short data (limit of hearing is 12 bits so 16 is already padded)
		info.format = FMOD_SOUND_FORMAT_PCMFLOAT;  
		
		system->createSound(
			nullptr,
			FMOD_OPENUSER,
			&info,
			&sound);
		
		if(sound == nullptr) return nullptr;
	
	// now we actually copy the samples we generated into FMOD
	// because we're doing this all at once we had to pre-generate the buffer
	// (because otherwise we don't know how big the buffer needs to be!
		void * ptr1, * ptr2;
		uint32_t len1, len2;
		size_t offset = 0;
		
		while(offset < _samples.size())
		{
			sound->lock(offset, (_samples.size()-offset) * 4, &ptr1, &ptr2, &len1, &len2);
		
			assert(len1%4 == 0);
			assert(len2%4 == 0);
		
			memcpy(ptr1, &_samples[offset], len1); offset += len1 / 4;
			memcpy(ptr2, &_samples[offset], len2); offset += len2 / 4;
			sound->unlock(ptr1, ptr2, len1, len2);
		}
		
		return sound;
	}
