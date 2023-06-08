# Refactored-SFXR
A refactored version of the SFXR program for use as a library; made to be more human readable, commented, and modular.

Includes helper functions to set sound frequency based on midi key.

TODO: change settings to all human units and emulation-units in the model only
TODO: remove C++ make it C

Example of saving a file:

	sfxr::Settings::Coin().ExportWAV("coin.wav");
  
Example of creating a new sound with FMOD:

	FMOD::Sound * CreateSound(FMOD::System * system, sfxr::Settings const& settings)
	{
		FMOD::Sound * sound = nullptr;
		// the model is data used during sound generation that never changes, it can be used for multiple generators
		// you're free to change the settings once you have the model
		sfxr::Model model(settings);
		// the data is stuff that does change during sound generation, it can't be used among multiple instances
		// it maintains a reference to the model, so the model can't be deleted until all the data is also.
		sfxr::Data  data(model);
		
		std::vector<float> _samples;
		
		//make sample
		while(data.playing_sample)
		{
			auto size = _samples.size();
			_samples.resize(_samples.size() + 44100);
			auto written = data.SynthSample(44100, _samples.data());

			if(written < 44100)
			{
				_samples.resize(size+written); // make it a tight fit!
				break;
			}
		}
		
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
		
	// humans can't actually hear a difference between float and short data
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
