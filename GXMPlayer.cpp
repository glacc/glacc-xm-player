//Glacc XM Module Player
//Glacc 2024-07-13
//
//      2024-07-13  Updated coding style
//                  Added SFML/Audio support
//
//      2021-01-10  Fixed arpeggio note not update at last tick per row
//
//      2020-10-24  Changed name to "Glacc XM Player"
//
//      2020-07-21  Fixed SegmentFault caused by unsynced SDL_AudioSpec
//
//      2020-06-05  Rewrite mixer timing part
//
//      2020-05-17  Fixed borken Dxx effect algorithm
//                  Fixed delay algorithm
//
//      2020-05-04  Improved volume ramping
//
//      2020-05-01  Fixed Rxx effect don't work in some conditions
//
//      2020-04-30  Added GetActiveChannels()
//                  Added SetPanMode()
//                  Compatibility improvement of effect EDx
//                  Fixed F00 command bug and added SetIgnoreF00()
//                  Fixed Lx/Rx is using wrong parameter
//
//      2020-04-29  Less 64-bit and floating calculation in audio mixer
//
//      2020-04-28  Performance optimization
//                  Fixed wrong effect type number (Xxx) in ChkEffectRow
//
//      2020-04-26  Added GetExcuteTime()
//
//      2020-04-25  Changed audio library from winmm to SDL2
//
//      2020-04-24  Put all code into a namespace
//
//      2020-04-22  Playback compatibility improvement
//
//      2020-04-21  Full volume ramping
//                  FT2 square root panning law
//
//      2020-04-18  Auto vibrato
//
//      2020-04-17  Unroll bidi loop samples
//
//      2020-04-14  Performance optimization
//
//  Missing effects:
//      E3x Glissando control
//      E4x Vibrato control
//      E7x Tremolo control

//#include "xmPlayer.h"

#define _SDL2
//#define _SFML

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <malloc.h>
#include <math.h>
#include <algorithm>
#include <time.h>

#ifdef _SDL2
#include <SDL2/SDL.h>
#include <SDL2/SDL_stdinc.h>
#include <SDL2/SDL_audio.h>
#endif
#ifdef _SFML
#include <SFML/Audio.hpp>
#endif

#define SMP_RATE 44100
#define BUFFER_SIZE 4096
#define VOLRAMPING_SAMPLES 40
#define VOLRAMP_VOLSET_SAMPLES 20
#define SMP_CHANGE_RAMP 20
#define VOLRAMP_NEW_INSTR 20

#define TOINT_SCL 65536.0
#define TOINT_SCL_RAMPING 1048576.0
#define INT_ACC 16
#define INT_ACC_RAMPING 20
#define INT_ACC_INTERPOL 15
#define INT_MASK 0xFFFF

#define NOTE_SIZE_XM 5
#define ROW_SIZE_XM NOTE_SIZE_XM * numOfChannels

namespace GXMPlayer
{
#ifdef _SDL2
	SDL_AudioSpec AudioSpec, ActualSpec;
	SDL_AudioDeviceID DeviceID;
#endif

#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))

	static char songName[21];
	static char trackerName[21];
	static int16_t trackerVersion;

	static bool loop;
	static bool stereo;
	static double amplifier = 1;
	static bool ignoreF00 = false;
	static int8_t panMode = 0;

	static int bufferSize;
	static int sampleRate;
	static bool isPlaying = false;
	static bool interpolation;
	static uint8_t masterVolume;

	static bool useAmigaFreqTable;
	static int16_t numOfChannels;
	static int16_t numOfPatterns;
	static int16_t numOfInstruments;
	static uint8_t orderTable[256];

	static int16_t songLength;
	static int16_t rstPos;
	static int16_t defaultTempo;
	static int16_t defaultSpd;

	static uint8_t *songData;
	static uint8_t *patternData;
	static int8_t *sampleData;
	//static int16_t *sndBuffer[2];

	struct Instrument
	{
		int16_t sampleNum;
		int16_t fadeOut;
		int16_t sampleMap[96];
		int16_t volEnvelops[24];
		int16_t panEnvelops[24];
		char name[23];
		int8_t volPoints;
		int8_t panPoints;
		uint8_t volSustainPt;
		uint8_t volLoopStart;
		uint8_t volLoopEnd;
		uint8_t panSustainPt;
		uint8_t panLoopStart;
		uint8_t panLoopEnd;
		int8_t volType;
		int8_t panType;
		int8_t vibratoType;
		int8_t vibratoSweep;
		int8_t vibratoDepth;
		int8_t vibratoRate;
	};

	struct Sample
	{
		int32_t length;
		int32_t loopStart;
		int32_t loopLength;
		int8_t *data;
		int8_t volume;
		int8_t fineTune;
		int8_t type;
		int8_t relNote;
		uint8_t origInst;
		bool is16Bit;
		uint8_t pan;
		char name[23];
	};

	static int32_t totalPatSize;
	static int32_t totalInstSize;
	static int32_t totalSampleSize;
	static int32_t totalSampleNum;
	static int32_t patternAddr[256];
	static Instrument *instruments;
	static Sample *samples;
	static int16_t *sampleStartIndex;
	static int32_t *sampleHeaderAddr;

	static int16_t tick, curRow, curPos;
	static int16_t patBreak, patJump, patDelay;
	static int16_t patRepeat, repeatPos, repeatTo;
	static uint8_t speed, tempo, globalVol;
	static int32_t sampleToNextTick;
	static double timer, timePerSample, timePerTick, samplePerTick;
	static bool songLoaded = false;
	static double amplifierFinal;

	static long startTime, endTime, excuteTime;

	static uint8_t vibTab[32] =
	{
		  0, 24, 49, 74, 97,120,141,161,
		180,197,212,224,235,244,250,253,
		255,253,250,244,235,224,212,197,
		180,161,141,120, 97, 74, 49, 24
	};

	static uint16_t periodTab[105] =
	{
		907,900,894,887,881,875,868,862,856,850,844,838,832,826,820,814,
		808,802,796,791,785,779,774,768,762,757,752,746,741,736,730,725,
		720,715,709,704,699,694,689,684,678,675,670,665,660,655,651,646,
		640,636,632,628,623,619,614,610,604,601,597,592,588,584,580,575,
		570,567,563,559,555,551,547,543,538,535,532,528,524,520,516,513,
		508,505,502,498,494,491,487,484,480,477,474,470,467,463,460,457,
		453,450,447,445,442,439,436,433,428
	};

	static int8_t fineTuneTable[32] =
	{
		   0,  16,  32,  48,  64,  80,  96, 112,
		-128,-112, -96, -80, -64, -48, -32, -16,
		-128,-112, -96, -80, -64, -48, -32, -16,
		   0,  16,  32,  48,  64,  80,  96, 112
	};

	static int8_t RxxVolSlideTable[16] =
	{
		0, -1, -2, -4, -8, -16,  0,  0,
		0,  1,  2,  4,  8,  16,  0,  0
	};

	struct Channel
	{
		int8_t note;
		int8_t relNote;
		int8_t noteArpeggio;
		uint8_t lastNote;
		int8_t fineTune;

		uint8_t instrument;
		uint8_t nextInstrument;
		uint8_t lastInstrument;

		uint8_t volCmd;
		uint8_t volPara;
		int16_t volume;
		int16_t lastVol;
		int16_t samplePlaying;
		int16_t sample;
		int16_t pan;
		int16_t panFinal;
		int16_t volEnvelope;
		int16_t panEnvelope;
		int16_t delay;
		int16_t RxxCounter;
		int16_t fadeTick;
		uint8_t effect;
		uint8_t parameter;

		uint8_t vibratoPos;
		uint8_t vibratoAmp;
		uint8_t vibratoType;
		bool volVibrato;
		uint8_t tremorPos;
		uint8_t tremorAmp;
		uint8_t tremorType;

		uint8_t slideUpSpd;
		uint8_t slideDnSpd;
		uint8_t slideSpd;
		uint8_t vibratoPara;
		uint8_t tremoloPara;
		int8_t volSlideSpd;
		uint8_t fineProtaUp;
		uint8_t fineProtaDn;
		uint8_t fineVolUp;
		uint8_t fineVolDn;
		int8_t globalVolSlideSpd;
		int8_t panSlideSpd;
		uint8_t retrigPara;
		uint8_t tremorPara;
		uint8_t EFProtaUp;
		uint8_t EFProtaDn;

		int8_t tremorTick;
		bool tremorMute;

		uint8_t autoVibPos;
		uint8_t autoVibSweep;
		int8_t envFlags;

		bool instTrig;
		bool LxxEffect;
		bool active;
		bool fading;
		bool keyOff;

		//Current playing sample
		bool muted;
		int8_t loop;
		int8_t loopType;
		bool is16Bit;
		int16_t period;
		int16_t targetPeriod;
		int16_t periodOfs;
		int16_t startCount;
		int16_t endCount;
		int32_t smpLeng;
		int32_t loopStart;
		int32_t loopEnd;
		int32_t loopLeng;
		int32_t delta;
		float facL;
		float facR;
		int8_t *data;
		int8_t *dataPrev;
		int32_t pos;
		int32_t posL16;
		int32_t volTargetInst;
		int32_t volFinalInst;
		int32_t volTargetL;
		int32_t volTargetR;
		int32_t volFinalL;
		int32_t volFinalR;
		int32_t volRampSpdL;
		int32_t volRampSpdR;
		int32_t volRampSpdInst;
		int32_t prevSmp;
		int32_t endSmp;
	};

	struct Note
	{
		uint8_t note;
		uint8_t instrument;
		uint8_t volCmd;
		uint8_t effect;
		uint8_t parameter;
	};

	struct EnvInfo
	{
		uint8_t value;
		int16_t maxPoint;
	};

	static Channel *channels;

#define Ch channels[i]

	static void RecalcAmp()
	{
		amplifierFinal = amplifier * 0.5;// /(MIN(3, MAX(1, numOfChannels/10)))*.7;
	}

	static void UpdateTimer()
	{
		timePerTick = 2.5 / tempo;
		samplePerTick = timePerTick / timePerSample;
	}

	static void ResetChannels()
	{
		int i = 0;
		while (i < numOfChannels)
		{
			Ch.note = 0;
			Ch.relNote = 0;
			Ch.noteArpeggio = 0;
			Ch.lastNote = 0;
			Ch.pos = 0;
			Ch.delta = 0;
			Ch.period = 0;
			Ch.targetPeriod = 0;
			Ch.fineTune = 0;
			Ch.loop = 0;

			Ch.samplePlaying = -1;
			Ch.sample = -1;
			Ch.instrument = 0;
			Ch.nextInstrument = 0;
			Ch.lastInstrument = 0;

			Ch.volCmd = 0;
			Ch.volume = 0;
			Ch.lastVol = 0;
			Ch.volTargetInst = 0;
			Ch.volTargetL = 0;
			Ch.volTargetR = 0;
			Ch.volFinalL = 0;
			Ch.volFinalR = 0;
			Ch.volFinalInst = 0;
			Ch.volRampSpdL = 0;
			Ch.volRampSpdR = 0;
			Ch.pan = 128;
			Ch.panFinal = 128;

			Ch.effect = 0;
			Ch.parameter = 0;

			Ch.vibratoPos = 0;
			Ch.vibratoAmp = 0;
			Ch.vibratoType = 0;
			Ch.volVibrato = false;
			Ch.tremorPos = 0;
			Ch.tremorAmp = 0;
			Ch.tremorType = 0;

			Ch.slideUpSpd = 0;
			Ch.slideDnSpd = 0;
			Ch.slideSpd = 0;
			Ch.vibratoPara = 0;
			Ch.tremoloPara = 0;
			Ch.volSlideSpd = 0;
			Ch.fineProtaUp = 0;
			Ch.fineProtaDn = 0;
			Ch.fineVolUp = 0;
			Ch.fineVolDn = 0;
			Ch.globalVolSlideSpd = 0;
			Ch.panSlideSpd = 0;
			Ch.retrigPara = 0;
			Ch.EFProtaUp = 0;
			Ch.EFProtaDn = 0;

			Ch.delay = 0;
			Ch.RxxCounter = 0;
			Ch.tremorTick = 0;
			Ch.tremorMute = false;

			Ch.startCount = 0;
			Ch.endCount = SMP_CHANGE_RAMP;
			Ch.prevSmp = 0;
			Ch.endSmp = 0;

			Ch.autoVibPos = 0;
			Ch.autoVibSweep = 0;
			Ch.volEnvelope = 0;
			Ch.panEnvelope = 0;
			Ch.envFlags = 0;

			Ch.instTrig = false;
			Ch.active = false;
			Ch.fading = false;
			Ch.keyOff = false;
			Ch.fadeTick = 0;

			Ch.muted = false;

			i ++;
		}
	}

#ifdef _SFML
	static void FillBuffer(int16_t *buffer);

	class CustomSoundStream : public sf::SoundStream
	{
	private:
		sf::Int16 *buffer;

		bool onGetData(Chunk &data) override
		{
			data.samples = buffer;
			data.sampleCount = bufferSize * 2;

			FillBuffer(buffer);

			return isPlaying;
		}

		void onSeek(sf::Time timeOffset) override
		{ }

	public:
		CustomSoundStream()
		{
			initialize(2, sampleRate);
			buffer = (sf::Int16 *)malloc(sizeof(int16_t) * 2 * bufferSize);
		}

		~CustomSoundStream()
		{
			if (buffer != NULL)
				free(buffer);
		}
	};

	CustomSoundStream *customStream;
#endif

	static void ResetPatternEffects()
	{
		sampleToNextTick = 0;

		globalVol = 64;

		patBreak = -1;
		patJump = -1;
		patDelay = 0;

		patRepeat = 0;
		repeatPos = 0;
		repeatTo = -1;
	}

	void ResetModule()
	{
#ifdef _SDL2
		SDL_PauseAudioDevice(DeviceID, 0);
#endif
#ifdef _SFML
		if (customStream != NULL)
			customStream->stop();
#endif
		isPlaying = false;

		tempo = defaultTempo;
		speed = defaultSpd;
		tick = speed - 1;

		curRow = -1;
		curPos = 0;

		ResetPatternEffects();

		RecalcAmp();

		ResetChannels();

		UpdateTimer();
	}

	bool LoadModule(uint8_t *songDataOrig, uint32_t songDataLeng, bool useInterpolation = true, bool stereoEnabled = true, bool loopSong = true, int bufSize = BUFFER_SIZE, int smpRate = SMP_RATE)
	{
		loop = loopSong;
		stereo = stereoEnabled;
		interpolation = useInterpolation;

		panMode = 0;
		ignoreF00 = true;

		sampleRate = smpRate;
		bufferSize = bufSize;

		timePerSample = 1.0 / sampleRate;

		masterVolume = 255;

		int i, j, k;
		int32_t songDataOfs = 17;

		//Song data init
		if (songData != NULL) free(songData);
		songData = (uint8_t *)malloc(songDataLeng);
		if (songData == NULL) return false;

		memcpy(songData, songDataOrig, songDataLeng);

		//Song name
		i = 0;
		while (i < 20)
			songName[i++] = songData[songDataOfs++];

		//Tracker name & version
		songDataOfs = 38;
		i = 0;
		while (i < 20)
			trackerName[i++] = songData[songDataOfs++];

		trackerVersion = (((((uint16_t)songData[songDataOfs]) << 8) & 0xFF00) | songData[songDataOfs + 1]);

		songDataOfs = 60;

		//Song settings
		int32_t HeaderSize = *(int32_t *)(songData + songDataOfs) + 60;
		songLength = *(int16_t *)(songData + songDataOfs + 4);
		rstPos = *(int16_t *)(songData + songDataOfs + 6);
		numOfChannels = *(int16_t *)(songData + songDataOfs + 8);
		numOfPatterns = *(int16_t *)(songData + songDataOfs + 10);
		numOfInstruments = *(int16_t *)(songData + songDataOfs + 12);
		useAmigaFreqTable = !(songData[songDataOfs + 14] & 1);

		defaultSpd = *(int16_t *)(songData + songDataOfs + 16);
		defaultTempo = *(int16_t *)(songData + songDataOfs + 18);

		if (channels != NULL) free(channels);
		channels = (Channel *)malloc(sizeof(Channel) * numOfChannels);
		if (channels == NULL) return false;

		//Pattern order table
		songDataOfs = 80;
		i = 0;
		while (i < songLength)
			orderTable[i++] = songData[songDataOfs++];

		//Pattern data size calc
		memset(patternAddr, 0, 256 * 4);
		totalPatSize = 0;

		songDataOfs = HeaderSize;
		int32_t patternOrig = songDataOfs;
		i = 0;
		while (i < numOfPatterns)
		{
			int32_t patHeaderSize = *(int32_t *)(songData + songDataOfs);
			int16_t patternLeng = *(int16_t *)(songData + songDataOfs + 5);
			int16_t patternSize = *(int16_t *)(songData + songDataOfs + 7);

			patternAddr[i++] = totalPatSize;

			songDataOfs += patHeaderSize + patternSize;

			totalPatSize += 2 + patternLeng * ROW_SIZE_XM;
		}

		//Pattern data init
		if (patternData != NULL) free(patternData);
		patternData = (uint8_t *)malloc(totalPatSize);
		if (patternData == NULL) return false;
		memset(patternData, 0, totalPatSize);

		//Patter data
		songDataOfs = patternOrig;
		i = 0;
		while (i < numOfPatterns)
		{
			int32_t patHeaderSize = *(int32_t *)(songData + songDataOfs);
			int16_t patternLeng = *(int16_t *)(songData + songDataOfs + 5);
			int16_t patternSize = *(int16_t *)(songData + songDataOfs + 7);

			int32_t PDIndex = patternAddr[i];
			patternData[PDIndex++] = patternLeng & 0xFF;
			patternData[PDIndex++] = (int8_t)((patternLeng >> 8) & 0xFF);

			songDataOfs += patHeaderSize;

			if (patternSize > 0)
			{
				j = 0;
				while (j < patternLeng)
				{
					k = 0;
					while (k < numOfChannels)
					{
						int8_t SignByte = songData[songDataOfs++];

						patternData[PDIndex] = 0;
						patternData[PDIndex + 1] = 0;

						if (SignByte & 0x80)
						{
							if (SignByte & 0x01) patternData[PDIndex] = songData[songDataOfs++];
							if (SignByte & 0x02) patternData[PDIndex + 1] = songData[songDataOfs++];
							if (SignByte & 0x04) patternData[PDIndex + 2] = songData[songDataOfs++];
							if (SignByte & 0x08) patternData[PDIndex + 3] = songData[songDataOfs++];
							if (SignByte & 0x10) patternData[PDIndex + 4] = songData[songDataOfs++];
						}
						else
						{
							patternData[PDIndex] = SignByte;
							patternData[PDIndex + 1] = songData[songDataOfs++];
							patternData[PDIndex + 2] = songData[songDataOfs++];
							patternData[PDIndex + 3] = songData[songDataOfs++];
							patternData[PDIndex + 4] = songData[songDataOfs++];
						}
						PDIndex += 5;
						k ++;
					}
					j ++;
				}
			}
			i ++;
		}

		//instrument size calc
		if (sampleStartIndex != NULL) free(sampleStartIndex);
		sampleStartIndex = (int16_t *)malloc(numOfInstruments * 2);
		if (sampleStartIndex == NULL) return false;

		if (instruments != NULL) free(instruments);
		instruments = (Instrument *)malloc(numOfInstruments * sizeof(Instrument));
		if (instruments == NULL) return false;

		totalInstSize = totalSampleSize = totalSampleNum = 0;
		int instOrig = songDataOfs;
		i = 0;
		while (i < numOfInstruments)
		{
			int32_t instSize = *(int32_t *)(songData + songDataOfs);
			int16_t instSampleNum = *(int16_t *)(songData + songDataOfs + 27);
			instruments[i].sampleNum = instSampleNum;

			//name
			k = 0;
			while (k < 22)
			{
				instruments[i].name[k] = songData[songDataOfs + 4 + k];
				k ++;
			}

			if (instSampleNum > 0)
			{
				//note mapping
				j = 0;
				while (j < 96)
				{
					instruments[i].sampleMap[j] = totalSampleNum + songData[songDataOfs + 33 + j];
					j ++;
				}

				//Vol envelopes
				j = 0;
				while (j < 24)
				{
					instruments[i].volEnvelops[j] = *(int16_t *)(songData + songDataOfs + 129 + j * 2);
					j ++;
				}

				//pan envelopes
				j = 0;
				while (j < 24)
				{
					instruments[i].panEnvelops[j] = *(int16_t *)(songData + songDataOfs + 177 + j * 2);
					j ++;
				}

				instruments[i].volPoints = songData[songDataOfs + 225];
				instruments[i].panPoints = songData[songDataOfs + 226];
				instruments[i].volSustainPt = songData[songDataOfs + 227];
				instruments[i].volLoopStart = songData[songDataOfs + 228];
				instruments[i].volLoopEnd = songData[songDataOfs + 229];
				instruments[i].panSustainPt = songData[songDataOfs + 230];
				instruments[i].panLoopStart = songData[songDataOfs + 231];
				instruments[i].panLoopEnd = songData[songDataOfs + 232];
				instruments[i].volType = songData[songDataOfs + 233];
				instruments[i].panType = songData[songDataOfs + 234];
				instruments[i].vibratoType = songData[songDataOfs + 235];
				instruments[i].vibratoSweep = songData[songDataOfs + 236];
				instruments[i].vibratoDepth = songData[songDataOfs + 237];
				instruments[i].vibratoRate = songData[songDataOfs + 238];
				instruments[i].fadeOut = *(int16_t *)(songData + songDataOfs + 239);

				songDataOfs += instSize;

				int32_t subOfs = 0;
				int32_t sampleDataOfs = 0;
				j = 0;
				while (j < instSampleNum)
				{
					subOfs = j * 40;
					int32_t sampleLeng = *(int32_t *)(songData + songDataOfs + subOfs);
					int32_t loopStart = *(int32_t *)(songData + songDataOfs + subOfs + 4);
					int32_t loopLeng = *(int32_t *)(songData + songDataOfs + subOfs + 8);
					int8_t sampleType = songData[songDataOfs + subOfs + 14] & 0x03;

					sampleDataOfs += sampleLeng;

					if (sampleType == 1)
						sampleLeng = loopStart + loopLeng;
					if ((songData[songDataOfs + subOfs + 14] & 0x03) >= 2)
						sampleLeng = loopStart + (loopLeng + loopLeng);

					totalSampleSize += sampleLeng;
					j ++;
				}
				songDataOfs += instSampleNum * 40 + sampleDataOfs;
			}
			else songDataOfs += instSize;

			sampleStartIndex[i] = totalSampleNum;
			totalSampleNum += instSampleNum;

			i ++;
		}

		//sample convert
		if (sampleHeaderAddr != NULL) free(sampleHeaderAddr);
		sampleHeaderAddr = (int32_t *)malloc(totalSampleNum * 4);
		if (sampleHeaderAddr == NULL) return false;


		if (sampleData != NULL) free(sampleData);
		sampleData = (int8_t *)malloc(totalSampleSize);
		if (sampleData == NULL) return false;

		if (samples != NULL) free(samples);
		samples = (Sample *)malloc(totalSampleNum * sizeof(Sample));
		if (samples == NULL) return false;

		int16_t sampleNum = 0;
		int32_t sampleWriteOfs = 0;
		songDataOfs = instOrig;
		i = 0;
		while (i < numOfInstruments)
		{
			int32_t instSize = *(int32_t *)(songData + songDataOfs);
			int16_t instSampleNum = *(int16_t *)(songData + songDataOfs + 27);

			songDataOfs += instSize;

			if (instSampleNum > 0)
			{
				int32_t subOfs = 0;
				int32_t sampleDataOfs = 0;
				j = 0;
				while (j < instSampleNum)
				{
					sampleNum = sampleStartIndex[i] + j;
					subOfs = j * 40;
					sampleHeaderAddr[sampleNum] = songDataOfs + subOfs;
					int32_t sampleLeng = *(int32_t *)(songData + songDataOfs + subOfs);
					int32_t loopStart = *(int32_t *)(songData + songDataOfs + subOfs + 4);
					int32_t loopLeng = *(int32_t *)(songData + songDataOfs + subOfs + 8);
					int8_t sampleType = songData[songDataOfs + subOfs + 14];
					bool is16Bit = (sampleType & 0x10);

					int16_t sampleNum = sampleStartIndex[i] + j;

					samples[sampleNum].origInst = i + 1;
					samples[sampleNum].type = sampleType & 0x03;
					samples[sampleNum].is16Bit = is16Bit;

					samples[sampleNum].volume = songData[songDataOfs + subOfs + 12];
					samples[sampleNum].pan = songData[songDataOfs + subOfs + 15];
					samples[sampleNum].fineTune = *(int8_t *)(songData + songDataOfs + subOfs + 13);
					samples[sampleNum].relNote = *(int8_t *)(songData + songDataOfs + subOfs + 16);

					k = 0;
					while (k < 22)
					{
						samples[sampleNum].name[k] = songData[songDataOfs + subOfs + 18 + k];
						k ++;
					}

					samples[sampleNum].data = sampleData + sampleWriteOfs;
					subOfs = 40 * instSampleNum + sampleDataOfs;
					sampleDataOfs += sampleLeng;

					if (is16Bit)
					{
						samples[sampleNum].length = sampleLeng >> 1;
						samples[sampleNum].loopStart = loopStart >> 1;
						samples[sampleNum].loopLength = loopLeng >> 1;
					}
					else
					{
						samples[sampleNum].length = sampleLeng;
						samples[sampleNum].loopStart = loopStart;
						samples[sampleNum].loopLength = loopLeng;
					}

					int32_t reversePoint = -1;
					if (samples[sampleNum].type == 1)
						sampleLeng = loopStart + loopLeng;
					else if (samples[sampleNum].type >= 2)
					{
						reversePoint = loopStart + loopLeng;
						sampleLeng = reversePoint + loopLeng;
					}

					bool reverse = false;
					int32_t readOfs = 0;
					int16_t oldPt = 0;
					k = 0;
					if (is16Bit)
					{
						int16_t newPt;
						sampleLeng >>= 1;
						reversePoint >>= 1;
						while (k < sampleLeng)
						{
							//int16_t newPt = (int16_t)SONGSEEK16(songDataOfs + subOfs, songData) + oldPt;
							if (k == reversePoint)
							{
								reverse = true;
								readOfs -= 2;
							}
							if (reverse)
							{
								newPt = -*(int16_t *)(songData + songDataOfs + subOfs + readOfs) + oldPt;
								readOfs -= 2;
							}
							else
							{
								newPt = *(int16_t *)(songData + songDataOfs + subOfs + readOfs) + oldPt;
								readOfs += 2;
							}
							*(int16_t *)(sampleData + sampleWriteOfs) = newPt;
							sampleWriteOfs += 2;

							oldPt = newPt;
							k ++;
						}
					}
					else
					{
						int8_t newPt;
						while (k < sampleLeng)
						{
							if (k == reversePoint)
							{
								reverse = true;
								readOfs --;
							}
							if (reverse)
							{
								newPt = -songData[songDataOfs + subOfs + readOfs] + oldPt;
								readOfs --;
							}
							else
							{
								newPt = songData[songDataOfs + subOfs + readOfs] + oldPt;
								readOfs ++;
							}
							sampleData[sampleWriteOfs++] = newPt;

							oldPt = newPt;
							k ++;
						}
					}

					j ++;
					//sampleNum ++ ;
				}
				songDataOfs += 40 * instSampleNum + sampleDataOfs;
			}
			i ++;
		}

		if (sampleHeaderAddr != NULL) free(sampleHeaderAddr);
		if (sampleStartIndex != NULL) free(sampleStartIndex);

		if (songData != NULL) free(songData);

		ResetModule();
		songLoaded = true;

		return true;
	}

	static Note GetNote(uint8_t pos, uint8_t row, uint8_t col)
	{
		Note thisNote = *(Note *)(patternData + patternAddr[pos] + ROW_SIZE_XM * row + col * NOTE_SIZE_XM + 2);

		return thisNote;
	}

	static EnvInfo CalcEnvelope(Instrument inst, int16_t pos, bool calcPan)
	{
		EnvInfo retInfo;

		if (inst.sampleNum > 0)
		{
			int16_t *envData = calcPan ? inst.panEnvelops : inst.volEnvelops;
			int8_t numOfPoints = calcPan ? inst.panPoints : inst.volPoints;
			retInfo.maxPoint = envData[(numOfPoints - 1) * 2];

			if (pos == 0)
			{
				retInfo.value = envData[1];
				return retInfo;
			}

			int i = 0;
			while (i < numOfPoints - 1)
			{
				if (pos < envData[i * 2 + 2])
				{
					int16_t prevPos = envData[i * 2];
					int16_t nextPos = envData[i * 2 + 2];
					int16_t prevValue = envData[i * 2 + 1];
					int16_t nextValue = envData[i * 2 + 3];
					retInfo.value = (int8_t)((nextValue - prevValue) * (pos - prevPos) / (nextPos - prevPos) + prevValue);
					return retInfo;
				}
				i ++;
			}

			retInfo.value = envData[(numOfPoints - 1) * 2 + 1];
			return retInfo;
		}
		else
		{
			retInfo.value = 0;
			retInfo.maxPoint = 0;
		}

		return retInfo;
	}

#define PI 3.1415926535897932384626433832795

	static void CalcPeriod(uint8_t i)
	{
		int16_t realNote = (Ch.noteArpeggio <= 119 && Ch.noteArpeggio > 0 ? Ch.noteArpeggio : Ch.note) + Ch.relNote - 1;
		int8_t fineTune = Ch.fineTune;
		int16_t period;
		if (!useAmigaFreqTable)
			period = 7680 - realNote * 64 - fineTune / 2;
		else
		{
			//https://github.com/dodocloud/xmplayer/blob/master/src/xmlib/engine/utils.ts
			//function calcPeriod
			double fineTuneFrac = floor((double)fineTune / 16.0);
			uint16_t period1 = periodTab[8 + (realNote % 12) * 8 + (int16_t)fineTuneFrac];
			uint16_t period2 = periodTab[8 + (realNote % 12) * 8 + (int16_t)fineTuneFrac + 1];
			fineTuneFrac = ((double)fineTune / 16.0) - fineTuneFrac;
			period = (int16_t)round((1.0 - fineTuneFrac) * period1 + fineTuneFrac * period2) * (16.0 / pow(2, floor(realNote / 12) - 1));
		}

		Ch.period = MAX(Ch.period, 50);
		period = MAX(period, 50);

		if (Ch.noteArpeggio <= 119 && Ch.noteArpeggio > 0)
			Ch.period = period;
		else
			Ch.targetPeriod = period;
	}

	static void UpdateChannelInfo()
	{
		int i = 0;
		while (i < numOfChannels)
		{
			if (Ch.active && Ch.samplePlaying != -1)
			{
				//arpeggio
				if (Ch.parameter != 0 && Ch.effect == 0)
				{
					int8_t arpNote1 = Ch.parameter >> 4;
					int8_t arpNote2 = Ch.parameter & 0xF;
					int8_t arpeggio[3] = { 0, arpNote2, arpNote1 };
					if (useAmigaFreqTable) Ch.noteArpeggio = Ch.note + arpeggio[tick % 3];
					else Ch.periodOfs = -arpeggio[tick % 3] * 64;
					//Ch.period = Ch.targetPeriod;
				}

				//Auto vibrato
				int32_t autoVibFinal = 0;
				if (Ch.samplePlaying != -1)
				{
					Instrument smpOrigInst = instruments[samples[Ch.samplePlaying].origInst - 1];

					if (smpOrigInst.sampleNum > 0)
					{
						Ch.autoVibPos += smpOrigInst.vibratoRate;
						if (Ch.autoVibSweep < smpOrigInst.vibratoSweep) Ch.autoVibSweep ++;

						if (smpOrigInst.vibratoRate)
						{
							//https://github.com/milkytracker/MilkyTracker/blob/master/src/milkyplay/PlayerSTD.cpp
							//Line 568 - 599
							uint8_t vibPos = Ch.autoVibPos >> 2;
							uint8_t vibDepth = smpOrigInst.vibratoDepth;

							int32_t value = 0;
							switch (smpOrigInst.vibratoType) {
								// sine (we must invert the phase here)
							case 0:
								value = ~vibTab[vibPos & 31];
								break;
								// square
							case 1:
								value = 255;
								break;
								// ramp down (down being the period here - so ramp frequency up ;)
							case 2:
								value = ((vibPos & 31) * 539087) >> 16;
								if ((vibPos & 63) > 31) value = 255 - value;
								break;
								// ramp up (up being the period here - so ramp frequency down ;)
							case 3:
								value = ((vibPos & 31) * 539087) >> 16;
								if ((vibPos & 63) > 31) value = 255 - value;
								value = -value;
								break;
							}

							autoVibFinal = ((value * vibDepth) >> 1);
							if (smpOrigInst.vibratoSweep) {
								autoVibFinal *= ((int32_t)Ch.autoVibSweep << 8) / smpOrigInst.vibratoSweep;
								autoVibFinal >>= 8;
							}

							if ((vibPos & 63) > 31) autoVibFinal = -autoVibFinal;

							autoVibFinal >>= 7;
						}
					}
				}

				//delta calculation
				CalcPeriod(i);
				double freq;
				double realPeriod = MAX(Ch.period + Ch.periodOfs, 50) + Ch.vibratoAmp * sin((Ch.vibratoPos & 0x3F) * PI / 32) * 8 + autoVibFinal;
				if (!useAmigaFreqTable)
					freq = 8363 * pow(2, (4608 - realPeriod) / 768);
				else freq = 8363.0 * 1712 / realPeriod;

				Ch.delta = (uint32_t)(freq / sampleRate * TOINT_SCL);

				Instrument Inst = instruments[Ch.instrument - 1];

				//volume
				Ch.volume = MAX(MIN(Ch.volume, 64), 0);
				int16_t realVol = Ch.tremorMute ? 0 : (int8_t)MAX(MIN(Ch.volume + Ch.tremorAmp * sin((Ch.tremorPos & 0x3F) * PI / 32) * 4, 64), 0);
				int16_t volTarget = realVol * globalVol / 64;
				if (Inst.volType & 0x01)
				{
					EnvInfo volEnv = CalcEnvelope(Inst, Ch.volEnvelope, false);

					if (Inst.volType & 0x02)
					{
						if (Ch.volEnvelope != Inst.volEnvelops[Inst.volSustainPt * 2] || Ch.fading)
							Ch.volEnvelope ++;
					}
					else Ch.volEnvelope ++;

					if (Inst.volType & 0x04)
					{
						if (Ch.volEnvelope >= Inst.volEnvelops[Inst.volLoopEnd * 2])
							Ch.volEnvelope = Inst.volEnvelops[Inst.volLoopStart * 2];
					}

					if (Ch.volEnvelope >= volEnv.maxPoint)
						Ch.volEnvelope = volEnv.maxPoint;

					int16_t instFadeout = Inst.fadeOut;
					int32_t fadeOutVol;
					if (Ch.fading && instFadeout > 0)
					{
						int16_t FadeOutLeng = 32768 / instFadeout;
						if (Ch.fadeTick < FadeOutLeng) Ch.fadeTick ++;
						else Ch.active = false;
						fadeOutVol = 64 * (FadeOutLeng - Ch.fadeTick) / FadeOutLeng;
					}
					else fadeOutVol = 64;

					Ch.volTargetInst = volEnv.value;
					//Ch.volTarget = fadeOutVol*globalVol/64*realVol/64;
					//Ch.volTarget = fadeOutVol*volEnv.value/64*globalVol/64*realVol/64;
					volTarget = fadeOutVol * globalVol / 64 * realVol / 64;
				}
				else
				{
					Ch.volTargetInst = 64;
					if (Ch.fading) Ch.volume = 0;
				}
				Ch.volTargetInst <<= INT_ACC_RAMPING;

				volTarget = MAX(MIN(volTarget, 64), 0);

				//Panning
				Ch.pan = MAX(MIN(Ch.pan, 255), 0);
				if (Inst.panType & 0x01)
				{
					EnvInfo panEnv = CalcEnvelope(Inst, Ch.panEnvelope, true);
					if (Inst.panType & 0x02)
					{
						if (Ch.panEnvelope != Inst.panEnvelops[Inst.panSustainPt * 2] || Ch.fading)
							Ch.panEnvelope ++;
					}
					else Ch.panEnvelope ++;

					if (Inst.panType & 0x04)
					{
						if (Ch.panEnvelope >= Inst.panEnvelops[Inst.panLoopEnd * 2])
							Ch.panEnvelope = Inst.panEnvelops[Inst.panLoopStart * 2];
					}

					if (Ch.panEnvelope >= panEnv.maxPoint)
						Ch.panEnvelope = panEnv.maxPoint;

					Ch.panFinal = Ch.pan + (((panEnv.value - 32) * (128 - abs(Ch.pan - 128))) >> 5);
				}
				else Ch.panFinal = Ch.pan;
				Ch.panFinal = MAX(MIN(Ch.panFinal, 255), 0);

				//sample info
				Sample curSample = samples[Ch.samplePlaying];
				Ch.data = curSample.data;
				Ch.loopType = curSample.type;

				Ch.is16Bit = curSample.is16Bit;
				Ch.smpLeng = curSample.length;
				Ch.loopStart = curSample.loopStart;
				Ch.loopLeng = curSample.loopLength;
				Ch.loopEnd = Ch.loopStart + Ch.loopLeng;

				if (Ch.loopType >= 2)
				{
					Ch.loopEnd += Ch.loopLeng;
					Ch.loopLeng <<= 1;
				}
				if (!Ch.loopType) Ch.loop = 0;

				//Set volume ramping
				double volRampSmps = samplePerTick;
				double instVolRampSmps = samplePerTick;
				if (Ch.LxxEffect)
				{
					instVolRampSmps = VOLRAMP_NEW_INSTR;
					Ch.LxxEffect = false;
				}

				if (((Ch.volCmd & 0xF0) >= 0x10 &&
					(Ch.volCmd & 0xF0) <= 0x50) ||
					(Ch.volCmd & 0xF0) == 0xC0 ||
					Ch.effect == 12 || Ch.effect == 8 ||
					(Ch.effect == 0x14 && (Ch.parameter & 0xF0) == 0xC0))
					volRampSmps = VOLRAMP_VOLSET_SAMPLES;

				if (!(Inst.volType & 0x01) && Ch.fading)
				{
					volTarget = 0;
					Ch.fading = false;
					volRampSmps = VOLRAMP_VOLSET_SAMPLES;
				}

				if (Ch.instTrig)
				{
					//Ch.VolFinal = Ch.volTarget;
					//Ch.volFinalInst = Ch.volTargetInst;

					volRampSmps = VOLRAMP_VOLSET_SAMPLES;
					instVolRampSmps = VOLRAMP_NEW_INSTR;

					//Ch.volFinalL = Ch.volFinalR = 0;
					Ch.instTrig = false;
				}
				//else if (Ch.effect == 0xA)
				//    volRampSmps = samplePerTick;

				if (stereo)
				{
					if (!panMode)
					{
						//https://modarchive.org/forums/index.php?topic=3517.0
						//FT2 square root panning law
						Ch.volTargetL = (int32_t)(volTarget * sqrt((256 - Ch.panFinal) / 256.0) / .707) << INT_ACC_RAMPING;
						Ch.volTargetR = (int32_t)(volTarget * sqrt(Ch.panFinal / 256.0) / .707) << INT_ACC_RAMPING;
					}
					else
					{
						//Linear panning
						if (Ch.panFinal > 128)
						{
							Ch.volTargetL = volTarget * (256 - Ch.panFinal) / 128.0;
							Ch.volTargetR = volTarget;
						}
						else
						{
							Ch.volTargetL = volTarget;
							Ch.volTargetR = volTarget * (Ch.panFinal / 128.0);
						}
						Ch.volTargetL <<= INT_ACC_RAMPING;
						Ch.volTargetR <<= INT_ACC_RAMPING;
					}
				}
				else Ch.volTargetL = Ch.volTargetR = (volTarget << INT_ACC_RAMPING);

				Ch.volRampSpdL = (Ch.volTargetL - Ch.volFinalL) / volRampSmps;
				Ch.volRampSpdR = (Ch.volTargetR - Ch.volFinalR) / volRampSmps;
				Ch.volRampSpdInst = (Ch.volTargetInst - Ch.volFinalInst) / instVolRampSmps;

				if (Ch.volRampSpdL == 0) Ch.volFinalL = Ch.volTargetL;
				if (Ch.volRampSpdR == 0) Ch.volFinalR = Ch.volTargetR;
				if (Ch.volRampSpdInst == 0) Ch.volFinalInst = Ch.volTargetInst;
			}
			else Ch.volFinalL = Ch.volFinalR = 0;
			i ++;
		}
	}

	static void ChkEffectRow(Note thisNote, uint8_t i, bool byPassEffectCol, bool RxxRetrig = false)
	{
		uint8_t volCmd = thisNote.volCmd;
		uint8_t volPara = thisNote.volCmd & 0x0F;
		uint8_t effect = thisNote.effect;
		uint8_t para = thisNote.parameter;
		uint8_t subEffect = para & 0xF0;
		uint8_t subPara = para & 0x0F;

		//if ((effect != 0) || (para == 0))   //0XX
		//    Ch.NoteOfs = 0;

		if (!byPassEffectCol)
		{
			//effect column
			switch (effect)
			{
			default:
				break;
			case 1: //1xx
				if (para != 0) Ch.slideUpSpd = para;
				break;
			case 2: //2xx
				if (para != 0) Ch.slideDnSpd = para;
				break;
			case 3: //3xx
				if (para != 0) Ch.slideSpd = para;
				break;
			case 4: //4xx
				if ((para & 0xF) > 0)
					Ch.vibratoPara = (Ch.vibratoPara & 0xF0) + (para & 0xF);
				if (((para >> 4) & 0xF) > 0)
					Ch.vibratoPara = (Ch.vibratoPara & 0xF) + (para & 0xF0);
				break;
			case 7: //7xx
				if ((para & 0xF) > 0)
					Ch.tremoloPara = (Ch.tremoloPara & 0xF0) + (para & 0xF);
				if (((para >> 4) & 0xF) > 0)
					Ch.tremoloPara = (Ch.tremoloPara & 0xF) + (para & 0xF0);
				break;
			case 8: //8xx
				Ch.pan = para;
				break;
				//case 9: //9xx
				//    //if (Ch.active) Ch.pos = para*256;
				//    if (thisNote.note < 97) Ch.pos = para*256;
				//    break;
			case 5: //5xx
			case 6: //6xx
			case 10:    //Axx
				if (para != 0)
				{
					if (para & 0xF) Ch.volSlideSpd = -(para & 0xF);
					else if (para & 0xF0) Ch.volSlideSpd = (para >> 4) & 0xF;
				}
				break;
			case 11:    //Bxx
				patJump = para;
				break;
			case 12:    //Cxx
				Ch.volume = para;
				break;
			case 13:    //Dxx
				patBreak = (para >> 4) * 10 + (para & 0xF);
				break;
			case 14:    //Exx
				switch (subEffect)
				{
				case 0x10:  //E1x
					if (subPara != 0) Ch.fineProtaUp = subPara;
					if (Ch.period > 1) Ch.period -= Ch.fineProtaUp * 4;
					break;
				case 0x20:  //E2x
					if (subPara != 0) Ch.fineProtaDn = subPara;
					if (Ch.period < 7680) Ch.period += Ch.fineProtaDn * 4;
					break;
				case 0x30:  //E3x
					break;
				case 0x40:  //E4x
					break;
				case 0x50:  //E5x
					Ch.fineTune = fineTuneTable[(useAmigaFreqTable ? 0 : 16) + subPara];
					break;
				case 0x60:  //E6x
					if (subPara == 0 && repeatPos < curRow) {
						repeatPos = curRow;
						patRepeat = 0;
					}
					else if (patRepeat < subPara) {
						patRepeat ++;
						repeatTo = repeatPos;
					}
					break;
				case 0x70:  //E7x
					break;
				case 0x80:  //E8x
					break;
				case 0xA0:  //EAx
					if (subPara != 0) Ch.fineVolUp = subPara;
					Ch.volume += Ch.fineVolUp;
					break;
				case 0xB0:  //EBx
					if (subPara != 0) Ch.fineVolDn = subPara;
					Ch.volume -= Ch.fineVolDn;
					break;
				case 0x90:  //E9x
				case 0xC0:  //ECx
					Ch.delay = subPara - 1;
					break;
				case 0xD0:  //EDx
					Ch.delay = subPara;
					break;
				case 0xE0:
					patDelay = subPara;
					break;
				}
				break;
			case 15:    //Fxx
				if (!ignoreF00)
				{
					if (para < 32 && para != 0) speed = para;
					else tempo = para;
				}
				else if (para != 0)
				{
					if (para < 32) speed = para;
					else tempo = para;
				}
				break;
			case 16:    //Gxx
				globalVol = MAX(MIN(para, 64), 0);
				break;
			case 17:    //Hxx
				if (para != 0)
				{
					if (para & 0xF) Ch.globalVolSlideSpd = -(para & 0xF);
					else if (para & 0xF0) Ch.globalVolSlideSpd = (para >> 4) & 0xF;
				}
				break;
			case 20:    //Kxx
				if (para == 0) Ch.keyOff = Ch.fading = true;
				else Ch.delay = para;
				break;
			case 21:    //Lxx
				if (Ch.instrument && Ch.instrument <= numOfInstruments)
				{
					if (instruments[Ch.instrument - 1].volType & 0x02)
						Ch.panEnvelope = para;

					Ch.volEnvelope = para;
					Ch.LxxEffect = true;
					Ch.active = true;
				}
				break;
			case 25:    //Pxx
				if (para != 0)
				{
					if (para & 0xF) Ch.panSlideSpd = -(para & 0xF);
					else if (para & 0xF0) Ch.panSlideSpd = (para >> 4) & 0xF;
				}
				break;
			case 27:    //Rxx
				//Ch.RxxCounter = 1;
				//Ch.delay = subPara;
				//if ((para & 0xF0) != 0) Ch.retrigPara = (para >> 4) & 0xF;
				if (((para >> 4) & 0xF) > 0)
					Ch.retrigPara = (Ch.retrigPara & 0xF) + (para & 0xF0);
				/*
				if ((Ch.retrigPara < 6) || (Ch.retrigPara > 7 && Ch.retrigPara < 14))
					Ch.volume += RxxVolSlideTable[Ch.retrigPara];
				else
				{
					switch (Ch.retrigPara)
					{
						case 6:
							Ch.volume = (Ch.volume << 1) / 3;
							break;
						case 7:
							Ch.volume >>= 1;
							break;
						case 14:
							Ch.volume = (Ch.volume * 3) >> 1;
							break;
						case 15:
							Ch.volume <<= 1;
							break;
					}
				}
				*/
				break;
			case 29:    //Txx
				if (para != 0) Ch.tremorPara = para;
				break;
			case 33:    //Xxx
				if (subEffect == 0x10)  //X1x
				{
					if (subPara != 0) Ch.EFProtaUp = subPara;
					Ch.period -= Ch.EFProtaUp;
				}
				else if (subEffect == 0x20) //X2x
				{
					if (subPara != 0) Ch.EFProtaDn = subPara;
					Ch.period += Ch.EFProtaDn;
				}
				break;
			}
		}

		//volume column effects
		switch (volCmd & 0xF0)
		{
		case 0x80:  //Dx
			Ch.volume -= volPara;
			break;
		case 0x90:  //Ux
			Ch.volume += volPara;
			break;
		case 0xA0:  //Sx
			Ch.vibratoPara = (Ch.vibratoPara & 0xF) + ((volPara << 4) & 0xF0);
			break;
		case 0xB0:  //Vx
			if (volPara != 0)
				Ch.vibratoPara = (Ch.vibratoPara & 0xF0) + volPara;
			break;
		case 0xC0:  //Px
			Ch.pan = volPara * 17;
			break;
		case 0xF0:  //Mx
			if (volPara != 0) Ch.slideSpd = ((volPara << 4) & 0xF0) + volPara;
			break;
		default:    //Vxx
			if (volCmd >= 0x10 && volCmd <= 0x50 && !RxxRetrig)
				Ch.lastVol = Ch.volume = volCmd - 0x10;
			break;
		}
	}

	static void ChkNote(Note thisNote, uint8_t i, bool byPassDelayChk, bool RxxRetrig = false)
	{
		/*
		note thisNote;
		thisNote.note = Ch.lastNote;
		thisNote.instrument = Ch.lastInstrument;
		thisNote.volCmd = Ch.volCmd | Ch.volPara;
		thisNote.effect = Ch.effect;
		thisNote.parameter = Ch.parameter;
		*/

		//uint8_t note = thisNote.note;
		//if (thisNote.note > 96 || thisNote.note == 0) note = Ch.note;
		//Ch.note = note;

		Note thisNoteOrig = thisNote;
		if (byPassDelayChk)
		{
			thisNote.note = Ch.lastNote;
			thisNote.instrument = Ch.lastInstrument;
			thisNote.volCmd = Ch.volCmd;
			thisNote.effect = Ch.effect;
			thisNote.parameter = Ch.parameter;
		}

		if ((Ch.noteArpeggio <= 119 && Ch.noteArpeggio > 0) || Ch.periodOfs != 0/*&& (Ch.effect != 0 || Ch.parameter == 0)*/)
		{
			Ch.periodOfs = 0;
			Ch.noteArpeggio = 0;
			if (useAmigaFreqTable)
				Ch.period = Ch.targetPeriod;
		}

		bool porta = (thisNote.effect == 3 || thisNote.effect == 5 || ((thisNote.volCmd & 0xF0) == 0xF0));

		bool hasNoteDelay = (thisNote.effect == 14 && ((thisNote.parameter & 0xF0) >> 4) == 13 && (thisNote.parameter & 0x0F) != 0);

		if (thisNote.effect != 4 && thisNote.effect != 6 && !Ch.volVibrato) Ch.vibratoPos = 0;

		if (!hasNoteDelay || byPassDelayChk)
		{
			//Reference: https://github.com/milkytracker/MilkyTracker/blob/master/src/milkyplay/PlayerSTD.cpp - PlayerSTD::progressRow()

			Ch.delay = -1;

			int8_t noteNum = thisNote.note;
			uint8_t instNum = thisNote.instrument;
			//bool validNote = true;

			//if (instNum && instNum <= numOfInstruments)
			//{
			//    Ch.instrument = instNum;
			//}

			uint8_t oldInst = Ch.instrument;
			int16_t oldSamp = Ch.sample;

			bool invalidInstr = true;
			if (instNum && instNum <= numOfInstruments && noteNum < 97)
			{
				if (instruments[instNum - 1].sampleNum > 0)
				{
					Ch.nextInstrument = instNum;
					invalidInstr = false;
				}
			}

			if (instNum && invalidInstr) Ch.nextInstrument = 255;

			bool validNote = true;
			bool trigByNote = false;
			if (noteNum && noteNum < 97)
			{
				/*
				if (instNum && instNum <= numOfInstruments)
				{
					Ch.instrument = instNum;
				}
				else instNum = 0;

				bool invalidInstr = true;
				if (Ch.instrument && Ch.instrument <= numOfInstruments)
				{
					if (instruments[Ch.instrument - 1].sampleNum > 0)
						invalidInstr = false;
				}
				*/

				if (Ch.nextInstrument == 255)
				{
					instNum = 0;
					Ch.active = false;
					Ch.volume = 0;
					Ch.sample = -1;
					Ch.instrument = 0;
				}
				else Ch.instrument = Ch.nextInstrument;

				if (Ch.nextInstrument && Ch.nextInstrument <= numOfInstruments)
				{
					Ch.sample = instruments[Ch.nextInstrument - 1].sampleMap[noteNum - 1];

					if (Ch.sample != -1 && !porta)
					{
						Sample smp = samples[Ch.sample];
						int8_t relNote = smp.relNote;
						int8_t finalNote = noteNum + relNote;

						if (finalNote >= 1 && finalNote <= 119)
						{
							Ch.fineTune = smp.fineTune;
							Ch.relNote = relNote;
						}
						else
						{
							noteNum = Ch.note;
							validNote = false;
						}
					}

					if (validNote)
					{
						Ch.note = noteNum;

						CalcPeriod(i);

						if (!porta)
						{
							Ch.period = Ch.targetPeriod;
							Ch.samplePlaying = Ch.sample;
							Ch.autoVibPos = Ch.autoVibSweep = 0;
							Ch.loop = 0;

							Ch.startCount = 0;

							if (thisNote.effect == 9) Ch.pos = thisNote.parameter << 8;
							else Ch.pos = 0;

							if (Ch.pos > samples[Ch.samplePlaying].length) Ch.pos = samples[Ch.samplePlaying].length;

							//NoteTrig = true;
							if (!Ch.active && !instNum)
							{
								trigByNote = true;
								goto TrigInst;
							}
						}
					}
				}
			}

			//FT2 bug emulation
			if ((porta || !validNote) && instNum)
			{
				instNum = Ch.instrument = oldInst;
				Ch.sample = oldSamp;
			}

			if (instNum && Ch.sample != -1)
			{
			TrigInst:
				if (!RxxRetrig && thisNoteOrig.instrument)
				{
					//Ch.volFinalL = Ch.volFinalR = 0;
					Ch.volume = trigByNote ? Ch.lastVol : Ch.lastVol = samples[Ch.samplePlaying].volume;
					Ch.pan = samples[Ch.samplePlaying].pan;
					Ch.tremorMute = false;
					Ch.tremorTick = 0;
					//Ch.autoVibPos = Ch.autoVibSweep = 0;
					Ch.tremorPos = 0;
				}
				Ch.volVibrato = false;

				Ch.fadeTick = Ch.volEnvelope = Ch.panEnvelope = 0;

				Ch.instTrig = Ch.active = true;
				Ch.keyOff = Ch.fading = false;
			}

			if (noteNum == 97)
				Ch.fading = true;

			ChkEffectRow(thisNote, i, byPassDelayChk, RxxRetrig);
		}
		else
		{
			Ch.delay = Ch.parameter & 0x0F;
			if (porta)
			{
				Ch.period = Ch.targetPeriod;
				if (Ch.samplePlaying != -1) CalcPeriod(i);
			}
		}
	}

	static void ChkEffectTick(uint8_t i, Note thisNote)
	{
		uint8_t volCmd = thisNote.volCmd;
		uint8_t volPara = thisNote.volCmd & 0x0F;
		uint8_t effect = thisNote.effect;
		uint8_t para = thisNote.parameter;
		uint8_t subEffect = para & 0xF0;
		uint8_t subPara = para & 0x0F;

		if (effect != 4 && effect != 6 && !Ch.volVibrato) Ch.vibratoPos = 0;
		if (effect != 27) Ch.RxxCounter = 0;

		uint8_t onTime, offTime;

		//effect column
		switch (effect)
		{
		case 1: //1xx
			Ch.period -= Ch.slideUpSpd * 4;
			break;
		case 2: //2xx
			Ch.period += Ch.slideDnSpd * 4;
			break;
		case 3: //3xx
		PortaEffect:
			if (Ch.period > Ch.targetPeriod)
				Ch.period -= Ch.slideSpd * 4;
			else if (Ch.period < Ch.targetPeriod)
				Ch.period += Ch.slideSpd * 4;
			if (abs(Ch.period - Ch.targetPeriod) < Ch.slideSpd * 4)
				Ch.period = Ch.targetPeriod;
			break;
		case 4: //4xx
		VibratoEffect:
			Ch.vibratoAmp = Ch.vibratoPara & 0xF;
			Ch.vibratoPos += (Ch.vibratoPara >> 4) & 0x0F;
			break;
		case 5: //5xx
			Ch.volume += Ch.volSlideSpd;
			goto PortaEffect;
			break;
		case 6: //6xx
			Ch.volume += Ch.volSlideSpd;
			goto VibratoEffect;
			break;
		case 7: //7xx
			Ch.tremorAmp = Ch.tremoloPara & 0xF;
			Ch.tremorPos += (Ch.tremoloPara >> 4) & 0x0F;
			break;
		case 8: //8xx
			Ch.pan = para;
			break;
		case 10:    //Axx
			Ch.volume += Ch.volSlideSpd;
			break;
		case 12:    //Cxx
			Ch.volume = para;
			break;
		case 14:    //Exx
			switch (subEffect)
			{
			case 0x90:  //E9x
				if (Ch.delay <= 0 && subPara > 0)
				{
					ChkNote(thisNote, i, true);
					Ch.delay = subPara;
				}
				break;
			case 0xC0:  //ECx
				if (Ch.delay <= 0)
					Ch.volume = 0;
				break;
			case 0xD0:  //EDx
				if (Ch.delay <= 1 && Ch.delay != -1 && !Ch.keyOff) ChkNote(thisNote, i, true);
				break;
			}
			break;
		case 17:    //Hxx
			globalVol = MAX(MIN(globalVol + Ch.globalVolSlideSpd, 64), 0);
			break;
		case 20:    //Kxx
			if (Ch.delay <= 0)
			{
				Ch.keyOff = Ch.fading = true;
			}
			break;
		case 25:    //Pxx
			Ch.pan += Ch.panSlideSpd;
			break;
		case 27:    //Rxx
			Ch.RxxCounter ++;
			if (Ch.RxxCounter >= (Ch.retrigPara & 0x0F) - 1)
			{
				ChkNote(thisNote, i, true, true);
				if ((para & 0xF) > 0)
					Ch.retrigPara = (Ch.retrigPara & 0xF0) + (para & 0xF);

				uint8_t retrigVol = (Ch.retrigPara >> 4) & 0x0F;
				if ((retrigVol < 6) || (retrigVol > 7 && retrigVol < 14))
					Ch.volume += RxxVolSlideTable[retrigVol];
				else
				{
					switch (retrigVol)
					{
					case 6:
						Ch.volume = (Ch.volume + Ch.volume) / 3;
						break;
					case 7:
						Ch.volume >>= 1;
						break;
					case 14:
						Ch.volume = (Ch.volume * 3) >> 1;
						break;
					case 15:
						Ch.volume <<= 1;
						break;
					}
				}
				Ch.RxxCounter = 0;
			}
			break;
		case 29:    //Txx
			Ch.tremorTick ++;
			onTime = ((Ch.tremorPara >> 4) & 0xF) + 1;
			offTime = (Ch.tremorPara & 0xF) + 1;
			Ch.tremorMute = (Ch.tremorTick > onTime);
			if (Ch.tremorTick >= onTime + offTime) Ch.tremorTick = 0;
			break;
		}

		if (Ch.delay > -1) Ch.delay --;

		//volume column effects
		switch (volCmd & 0xF0)
		{
		case 0x60:  //Dx
			Ch.volume -= volPara;
			break;
		case 0x70:  //Ux
			Ch.volume += volPara;
			break;
		case 0xB0:  //Vx
			Ch.vibratoAmp = Ch.vibratoPara & 0xF;
			Ch.vibratoPos += (Ch.vibratoPara >> 4) & 0x0F;
			Ch.volVibrato = true;
			break;
		case 0xD0:  //Lx
			Ch.pan -= volPara;
			break;
		case 0xE0:  //Rx
			Ch.pan += volPara;
			break;
		case 0xF0:  //Mx
			if (Ch.period > Ch.targetPeriod)
				Ch.period -= Ch.slideSpd * 4;
			else if (Ch.period < Ch.targetPeriod)
				Ch.period += Ch.slideSpd * 4;
			if (abs(Ch.period - Ch.targetPeriod) < Ch.slideSpd * 4)
				Ch.period = Ch.targetPeriod;
			break;
		}
	}

	static void NextRow()
	{
		if (patDelay <= 0)
		{
			curRow ++;
			if (patBreak >= 0 && patJump >= 0)
			{
				curRow = patBreak;
				curPos = patJump;
				patRepeat = repeatPos = 0;
				repeatTo = patBreak = patJump = -1;
			}
			if (patBreak >= 0)
			{
				curRow = patBreak;
				patRepeat = repeatPos = 0;
				patBreak = repeatTo = -1;
				curPos ++;
			}
			if (patJump >= 0)
			{
				curPos = patJump;
				curRow = patRepeat = repeatPos = 0;
				repeatTo = patJump = -1;
			}
			if (repeatTo >= 0)
			{
				curRow = repeatTo;
				repeatTo = -1;
			}
			if (curRow >= *(int16_t *)(patternData + patternAddr[orderTable[curPos]]))
			{
				//Pattern loop bug emulation
				curRow = repeatPos > 0 ? repeatPos : 0;
				patRepeat = repeatPos = 0;
				repeatTo = -1;
				curPos ++;
			}
			if (curPos >= songLength)
			{
				if (loop)
					curPos = rstPos;
				else
				{
					ResetModule();
					return;
				}
			}

			int i = 0;
			while (i < numOfChannels)
			{
				Note ThisNote = GetNote(orderTable[curPos], curRow, i);

				if (ThisNote.note != 0) Ch.lastNote = ThisNote.note;
				if (ThisNote.instrument != 0) Ch.lastInstrument = ThisNote.instrument;
				Ch.volCmd = ThisNote.volCmd;
				Ch.volPara = ThisNote.volCmd & 0x0F;
				Ch.effect = ThisNote.effect;
				Ch.parameter = ThisNote.parameter;

				ChkNote(ThisNote, i, false);
				//if (Ch.delay > -1) Ch.delay -- ;

				i ++;
			}
		}
		else patDelay --;
	}

	static void NextTick()
	{
		int i = 0;
		tick ++;

		if (tick >= speed) {
			tick = 0;
			NextRow();
			return;
		}

		if (curRow >= 0)
		{
			while (i < numOfChannels)
			{
				Note ThisNote = GetNote(orderTable[curPos], curRow, i);

				ChkEffectTick(i, ThisNote);

				i ++;
			}
		}
	}

	/*
	static inline void MixAudioOld(int16_t *buffer, uint32_t pos)
	{
		int32_t outL = 0;
		int32_t outR = 0;
		double result = 0;

		int i = 0;
		while (i < numOfChannels)
		{
			if (Ch.active)
			{
				if (Ch.samplePlaying != -1 && Ch.samplePlaying < totalSampleNum)
				{
					int32_t chPos = Ch.pos;

					if (Ch.startCount >= SMP_CHANGE_RAMP)
					{
						Ch.posL16 += Ch.delta;
						chPos += Ch.posL16 >> INT_ACC;
						Ch.posL16 &= INT_MASK;
					}

					//volume ramping
					if (Ch.volFinalL != Ch.volTargetL)
					{
						if (abs(Ch.volFinalL - Ch.volTargetL) >= abs(Ch.volRampSpdL))
							Ch.volFinalL += Ch.volRampSpdL;
						else Ch.volFinalL = Ch.volTargetL;
					}

					if (Ch.volFinalR != Ch.volTargetR)
					{
						if (abs(Ch.volFinalR - Ch.volTargetR) >= abs(Ch.volRampSpdR))
							Ch.volFinalR += Ch.volRampSpdR;
						else Ch.volFinalR = Ch.volTargetR;
					}

					if (Ch.volFinalInst != Ch.volTargetInst)
					{
						if (abs(Ch.volFinalInst - Ch.volTargetInst) >= abs(Ch.volRampSpdInst))
							Ch.volFinalInst += Ch.volRampSpdInst;
						else Ch.volFinalInst = Ch.volTargetInst;
					}

					//Looping
					if (Ch.loopType)
					{
						if (chPos < Ch.loopStart)
							Ch.loop = 0;
						else if (chPos >= Ch.loopEnd)
						{
							chPos = Ch.loopStart + (chPos - Ch.loopStart) % Ch.loopLeng;
							Ch.loop = 1;
						}
					}
					else if (chPos >= Ch.smpLeng)
					{
						Ch.active = false;
						Ch.endSmp = Ch.is16Bit ? *(int16_t *)(Ch.data + ((Ch.smpLeng - 1) << 1)) : (int16_t)(Ch.data[Ch.smpLeng - 1] << 8);
						Ch.samplePlaying = -1;
						Ch.endCount = 0;
					}

					Ch.pos = chPos;

					//Don't mix when there is no sample playing or the channel is muted
					if (Ch.muted || Ch.samplePlaying == -1) goto Continue;

					if (Ch.startCount >= SMP_CHANGE_RAMP)
					{
						//interpolation
						if (interpolation)
						{
							int32_t prevPos = chPos;

							if (chPos > 0) prevPos -- ;

							if (Ch.loop == 1 && chPos <= Ch.loopStart)
								prevPos = Ch.loopEnd - 1;

							int16_t prevData;
							int32_t dy;

							uint16_t ix = Ch.posL16 >> 1;

							if (Ch.is16Bit)
							{
								prevData = *(int16_t *)(Ch.data + (prevPos << 1));
								dy = *(int16_t *)(Ch.data + (chPos << 1)) - prevData;
							}
							else
							{
								prevData = Ch.data[prevPos] << 8;
								dy = (Ch.data[chPos] << 8) - prevData;
							}

							result = (prevData + ((dy * ix) >> INT_ACC_INTERPOL));
						}
						else
						{
							if (Ch.is16Bit) result = *(int16_t *)(Ch.data + (chPos << 1));
							else result = (int16_t)(Ch.data[chPos] << 8);
						}

						if (Ch.startCount < SMP_CHANGE_RAMP + SMP_CHANGE_RAMP)
						{
							result = result * (Ch.startCount - SMP_CHANGE_RAMP) / SMP_CHANGE_RAMP;
							Ch.startCount ++ ;
						}
						Ch.prevSmp = result;
					}
					else
					{
						result = Ch.prevSmp * (SMP_CHANGE_RAMP - Ch.startCount) / SMP_CHANGE_RAMP;
						Ch.startCount ++ ;
					}

					result *= amplifierFinal * masterVolume * Ch.volFinalInst / (64.0 * TOINT_SCL_RAMPING);

					outL += result * (Ch.volFinalL >> INT_ACC_RAMPING);
					outR += result * (Ch.volFinalR >> INT_ACC_RAMPING);
				}
				else goto NotActived;
			}
			else
			{
				NotActived:

				Ch.pos = Ch.posL16 = 0;
				Ch.prevSmp = 0;
				Ch.startCount = 0;
			}

			Continue:

			if (Ch.endCount < SMP_CHANGE_RAMP)
			{
				result = Ch.endSmp * (SMP_CHANGE_RAMP - Ch.endCount) / SMP_CHANGE_RAMP;
				Ch.endCount ++ ;

				result *= amplifierFinal * masterVolume * Ch.volFinalInst / (64.0 * TOINT_SCL_RAMPING);

				outL += result * (Ch.volFinalL >> INT_ACC_RAMPING);
				outR += result * (Ch.volFinalR >> INT_ACC_RAMPING);
			}

			i ++ ;
		}

		buffer[pos << 1] = outL >> 16;
		buffer[(pos << 1) + 1] = outR >> 16;
	}
	*/

#define MIXPREFIX\
	int k = 0;\
	while (k < samples)\
	{\
		int32_t outL = 0;\
		int32_t outR = 0;\
		double result = 0;\
\
		if (Ch.active)\
		{\
			if (Ch.samplePlaying != -1 && Ch.samplePlaying < totalSampleNum)\
			{\
				int32_t chPos = Ch.pos;\
\
				if (Ch.startCount >= SMP_CHANGE_RAMP)\
				{\
					Ch.posL16 += Ch.delta;\
					chPos += Ch.posL16 >> INT_ACC;\
					Ch.posL16 &= INT_MASK;\
				}\
\
				if (Ch.volFinalL != Ch.volTargetL)\
				{\
					if (abs(Ch.volFinalL - Ch.volTargetL) >= abs(Ch.volRampSpdL))\
						Ch.volFinalL += Ch.volRampSpdL;\
					else Ch.volFinalL = Ch.volTargetL;\
				}\
\
				if (Ch.volFinalR != Ch.volTargetR)\
				{\
					if (abs(Ch.volFinalR - Ch.volTargetR) >= abs(Ch.volRampSpdR))\
						Ch.volFinalR += Ch.volRampSpdR;\
					else Ch.volFinalR = Ch.volTargetR;\
				}\
\
				if (Ch.volFinalInst != Ch.volTargetInst)\
				{\
					if (abs(Ch.volFinalInst - Ch.volTargetInst) >= abs(Ch.volRampSpdInst))\
						Ch.volFinalInst += Ch.volRampSpdInst;\
					else Ch.volFinalInst = Ch.volTargetInst;\
				}

#define MIXNOLOOP\
	if (chPos >= Ch.smpLeng)\
	{\
		Ch.active = false;\
		Ch.endSmp = Ch.is16Bit ? *(int16_t *)(Ch.data + ((Ch.smpLeng - 1) << 1)) : (int16_t)(Ch.data[Ch.smpLeng - 1] << 8);\
		Ch.samplePlaying = -1;\
		Ch.endCount = 0;\
	}\
	Ch.pos = chPos;

#define MIXLOOP\
	if (chPos < Ch.loopStart)\
		Ch.loop = 0;\
	else if (chPos >= Ch.loopEnd)\
	{\
		chPos = Ch.loopStart + (chPos - Ch.loopStart) % Ch.loopLeng;\
		Ch.loop = 1;\
	}\
	Ch.pos = chPos;

#define MIXPART1(A)\
	if (Ch.muted || Ch.samplePlaying == -1) goto A;\
\
	if (Ch.startCount >= SMP_CHANGE_RAMP)\
	{

#define MIXINTERPOLINIT\
	int32_t prevPos = chPos;\
\
	if (chPos > 0) prevPos -- ;\
\
	if (Ch.loop == 1 && chPos <= Ch.loopStart)\
		prevPos = Ch.loopEnd - 1;\
\
	int16_t prevData;\
	int32_t dy;\
\
	uint16_t ix = Ch.posL16 >> 1;

#define MIXINTERPOL16BIT\
	prevData = *(int16_t *)(Ch.data + (prevPos << 1));\
	dy = *(int16_t *)(Ch.data + (chPos << 1)) - prevData;\
	result = (prevData + ((dy * ix) >> INT_ACC_INTERPOL));

#define MIXINTERPOL8BIT\
	prevData = Ch.data[prevPos] << 8;\
	dy = (Ch.data[chPos] << 8) - prevData;\
	result = (prevData + ((dy * ix) >> INT_ACC_INTERPOL));

#define MIXNEAREST16BIT\
	result = *(int16_t *)(Ch.data + (chPos << 1));

#define MIXNEAREST8BIT\
	result = (int16_t)(Ch.data[chPos] << 8);

#define MIXSUFFIX(A,B)\
					if (Ch.startCount < SMP_CHANGE_RAMP + SMP_CHANGE_RAMP)\
					{\
						result = result * (Ch.startCount - SMP_CHANGE_RAMP) / SMP_CHANGE_RAMP;\
						Ch.startCount ++ ;\
					}\
					Ch.prevSmp = result;\
				}\
				else\
				{\
					result = Ch.prevSmp * (SMP_CHANGE_RAMP - Ch.startCount) / SMP_CHANGE_RAMP;\
					Ch.startCount ++ ;\
				}\
\
				result *= amplifierFinal * masterVolume * Ch.volFinalInst / (64.0 * TOINT_SCL_RAMPING);\
\
				outL = result * (Ch.volFinalL >> INT_ACC_RAMPING);\
				outR = result * (Ch.volFinalR >> INT_ACC_RAMPING);\
			}\
			else goto B;\
		}\
		else\
		{\
			B:\
\
			Ch.pos = Ch.posL16 = 0;\
			Ch.prevSmp = 0;\
			Ch.startCount = 0;\
		}\
\
		A:\
\
		if (Ch.endCount < SMP_CHANGE_RAMP)\
		{\
			result = Ch.endSmp * (SMP_CHANGE_RAMP - Ch.endCount) / SMP_CHANGE_RAMP;\
			Ch.endCount ++ ;\
\
			result *= amplifierFinal * masterVolume * Ch.volFinalInst / (64.0 * TOINT_SCL_RAMPING);\
\
			outL = result * (Ch.volFinalL >> INT_ACC_RAMPING);\
			outR = result * (Ch.volFinalR >> INT_ACC_RAMPING);\
		}\
		else if (!Ch.active) break;\
\
		buffer[posFinal++] += outL >> 16;\
		buffer[posFinal++] += outR >> 16;\
\
		k ++ ;\
	}


	static inline void MixAudio(int16_t *buffer, uint32_t pos, int32_t samples)
	{
		int i = 0;
		while (i < numOfChannels)
		{
			int32_t posFinal = pos << 1;

			if (!Ch.loopType)
			{
				if (Ch.is16Bit)
				{
					if (interpolation)
					{
						MIXPREFIX

							MIXNOLOOP

							MIXPART1(C1)

							MIXINTERPOLINIT

							MIXINTERPOL16BIT

							MIXSUFFIX(C1, N1)
					}
					else
					{
						MIXPREFIX

							MIXNOLOOP

							MIXPART1(C2)

							MIXNEAREST16BIT

							MIXSUFFIX(C2, N2)
					}
				}
				else
				{
					if (interpolation)
					{
						MIXPREFIX

							MIXNOLOOP

							MIXPART1(C3)

							MIXINTERPOLINIT

							MIXINTERPOL8BIT

							MIXSUFFIX(C3, N3)
					}
					else
					{
						MIXPREFIX

							MIXNOLOOP

							MIXPART1(C4)

							MIXNEAREST8BIT

							MIXSUFFIX(C4, N4)
					}
				}
			}
			else
			{
				if (Ch.is16Bit)
				{
					if (interpolation)
					{
						MIXPREFIX

							MIXLOOP

							MIXPART1(C5)

							MIXINTERPOLINIT

							MIXINTERPOL16BIT

							MIXSUFFIX(C5, N5)
					}
					else
					{
						MIXPREFIX

							MIXLOOP

							MIXPART1(C6)

							MIXNEAREST16BIT

							MIXSUFFIX(C6, N6)
					}
				}
				else
				{
					if (interpolation)
					{
						MIXPREFIX

							MIXLOOP

							MIXPART1(C7)

							MIXINTERPOLINIT

							MIXINTERPOL8BIT

							MIXSUFFIX(C7, N7)
					}
					else
					{
						MIXPREFIX

							MIXLOOP

							MIXPART1(C8)

							MIXNEAREST8BIT

							MIXSUFFIX(C8, N8)
					}
				}
			}
			i ++;
		}
	}

	/*
	static void FillBufferOld(int16_t *buffer)
	{
		int i = 0;

		startTime = clock();

		while (i < bufferSize)
		{
			if (isPlaying)
				timer += timePerSample;

			if (timer >= timePerTick)
			{
				NextTick();
				UpdateChannelInfo();
				timer = fmod(timer, timePerTick);
				//timer -= timePerTick;
				UpdateTimer();
			}

			MixAudioOld(buffer, i);

			i ++ ;
		}

		endTime = clock();
		excuteTime = endTime - startTime;
	}
	*/

	static void FillBuffer(int16_t *buffer)
	{
		int i = 0;

		startTime = clock();

		memset(buffer, 0, bufferSize << 2);
		while (i < bufferSize)
		{
			if (isPlaying)
			{
				int mixLength = bufferSize;

				if (i + sampleToNextTick >= bufferSize)
				{
					mixLength = bufferSize - i;
					sampleToNextTick -= bufferSize - i;
				}
				else
				{
					if (sampleToNextTick)
					{
						mixLength = sampleToNextTick;
						sampleToNextTick = 0;
					}
					else
					{
						NextTick();
						UpdateChannelInfo();
						timer = fmod(timer, timePerTick);
						UpdateTimer();

						sampleToNextTick = samplePerTick;
						mixLength = sampleToNextTick;
					}

					if (i + sampleToNextTick >= bufferSize)
					{
						mixLength = bufferSize - i;
						sampleToNextTick -= bufferSize - i;
					}
					else sampleToNextTick = 0;
				}

				MixAudio(buffer, i, mixLength);

				i += mixLength;
			}
			else break;
		}

		endTime = clock();
		excuteTime = endTime - startTime;
	}

	void SetLoop(bool loopSong = true)
	{
		loop = loopSong;
	}

	void SetPanMode(int8_t mode = 0)
	{
		panMode = mode;
	}

	void PlayPause(bool play)
	{
		isPlaying = play;
#ifdef _SDL2
		SDL_PauseAudioDevice(DeviceID, !Play);
#endif
#ifdef _SFML
		if (customStream != NULL)
		{
			if (isPlaying)
				customStream->play();
			else
				customStream->stop();
		}
#endif
	}

	bool IsPlaying()
	{
		return isPlaying;
	}

	void SetAmp(float Value)
	{
		if (Value >= 0.1 && Value <= 10)
		{
			amplifier = Value;
			RecalcAmp();
		}
	}

	void SetStereo(bool UseStereo = false)
	{
		stereo = UseStereo;
	}

	void SetInterpolation(bool useInterpolation = true)
	{
		interpolation = useInterpolation;
	}

	void SetPos(int16_t pos)
	{
		if (pos < 0) pos = 0;
		if (pos >= songLength) pos = songLength - 1;

		ResetChannels();
		ResetPatternEffects();

		tick = speed - 1;
		curRow = -1;
		curPos = pos;
	}

	void SetIgnoreF00(bool trueFalse)
	{
		ignoreF00 = trueFalse;
	}

	void SetVolume(uint8_t volume)
	{
		masterVolume = volume;
	}

	char *GetSongName() {
		return songName;
	}

	uint8_t *GetPatternOrder()
	{
		return orderTable;
	}

	int16_t GetSpd()
	{
		return (int16_t)((speed << 8) | tempo);
	}

	int32_t GetPos() {
		return (int32_t)(((curPos & 0xFF) << 24) | ((orderTable[curPos] & 0xFF) << 16) | (curRow & 0xFFFF));
	}

	int32_t GetSongInfo()
	{
		return (songLength | (numOfPatterns << 8) | (numOfChannels << 16) | (numOfInstruments << 24));
	}

	uint8_t GetActiveChannels()
	{
		if (!songLoaded) return 0;

		uint8_t result = 0;
		int i = 0;
		while (i < numOfChannels)
		{
			if (Ch.active && Ch.samplePlaying != -1 && Ch.samplePlaying < totalSampleNum) result ++;
			i ++;
		}

		return result;
	}

	long GetExcuteTime()
	{
		return excuteTime;
	}

	bool IsLoaded()
	{
		return songLoaded;
	}

	int16_t GetPatLen(uint8_t patNum)
	{
		if (patNum >= numOfPatterns)
		{
			return 0;
		}
		return *(int16_t *)(patternData + patternAddr[patNum]);
	}

	Note GetNotePat(int16_t pos, int16_t row, uint8_t col)
	{
		Note thisNote;
		if (pos < songLength)
		{
			while (row >= *(int16_t *)(patternData + patternAddr[orderTable[pos]]))
			{
				row -= *(int16_t *)(patternData + patternAddr[orderTable[pos]]);
				pos ++;
				if (pos >= songLength) goto End;
			}
			while (row < 0)
			{
				pos --;
				if (pos < 0) goto End;
				row += *(int16_t *)(patternData + patternAddr[orderTable[pos]]);
			}
		}
		else goto End;

		if (col < numOfChannels)
		{
			thisNote = *(Note *)(patternData + patternAddr[orderTable[pos]] + ROW_SIZE_XM * row + col * NOTE_SIZE_XM + 2);
		}
		else
		{
		End:
			thisNote.note = 255;
			thisNote.instrument = thisNote.volCmd = thisNote.effect = thisNote.parameter = 0;
		}

		return thisNote;
	}

	static void WriteBufferCallback(void *userData, uint8_t *buffer, int length)
	{
		FillBuffer((int16_t *)buffer);
	}

	bool PlayModule()
	{
		if (!songLoaded)
			return false;

		/*
		int i = 0;
		while (i < 2)
		{
			if (SndBuffer[i] != nullptr)
				free SndBuffer[i]);

			SndBuffer[i] = (int16_t *)malloc(bufferSize);
			if (SndBuffer[i] == nullptr) return false;

			i ++ ;
		}
		*/

#ifdef _SDL2
		SDL_InitSubSystem(SDL_INIT_AUDIO);

		SDL_zero(AudioSpec);

		AudioSpec.freq = sampleRate;
		AudioSpec.format = AUDIO_S16;
		AudioSpec.channels = 2;
		AudioSpec.samples = bufferSize;
		AudioSpec.callback = WriteBufferCallback;
		DeviceID = SDL_OpenAudioDevice(NULL, 0, &AudioSpec, &ActualSpec, 0);

		if (DeviceID != 0)
		{
			bufferSize = ActualSpec.samples;
			SDL_PauseAudioDevice(DeviceID, 0);
			isPlaying = true;
		}
#endif
#ifdef _SFML
		if (customStream == NULL)
			customStream = new CustomSoundStream();

		isPlaying = true;
		customStream->play();
#endif

		return true;
	}

	bool StopModule()
	{
#ifdef _SDL2
		SDL_CloseAudioDevice(DeviceID);
#endif
#ifdef _SFML
		if (customStream != NULL)
			customStream->stop();
#endif
		isPlaying = false;
		return true;
	}

	void CleanUp()
	{
		isPlaying = false;
		songLoaded = false;
		StopModule();

#ifdef _SFML
		if (customStream != NULL)
			delete customStream;
#endif

		if (channels != NULL)
			free(channels);

		if (patternData != NULL)
			free(patternData);

		if (instruments != NULL)
			free(instruments);

		if (samples != NULL)
			free(samples);

		/*
		int i = 0;
		while (i < 2)
		{
			if (SndBuffer[i] != nullptr)
				free SndBuffer[i]);

		}
		*/
	}
}
