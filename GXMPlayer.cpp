//Glacc XM Module Player
//Glacc 2021-01-10
//
//  Change log:
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

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <malloc.h>
#include <math.h>
#include <algorithm>
#include <time.h>

#include <SDL2/SDL.h>
#include <SDL2/SDL_stdinc.h>
#include <SDL2/SDL_audio.h>

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
#define ROW_SIZE_XM NOTE_SIZE_XM*NumOfChannels

namespace GXMPlayer
{
    SDL_AudioSpec AudioSpec, ActualSpec;
    SDL_AudioDeviceID DeviceID;

    #define MIN(a, b) ((a) < (b) ? (a) : (b))
    #define MAX(a, b) ((a) > (b) ? (a) : (b))

    static char SongName[21];
    static char TrackerName[21];
    static int16_t TrackerVersion;

    static bool Loop;
    static bool Stereo;
    static double Amp = 1;
    static bool IgnoreF00 = false;
    static int8_t PanMode = 0;

    static int BufferSize;
    static int SamplingRate;
    static bool Playing = false;
    static bool Interpolation;
    static uint8_t MasterVol;

    static bool AmigaFreqTable;
    static int16_t NumOfChannels;
    static int16_t NumOfPatterns;
    static int16_t NumOfInstruments;
    static uint8_t OrderTable[256];

    static int16_t SongLength;
    static int16_t RstPos;
    static int16_t DefaultTempo;
    static int16_t DefaultSpd;

    static uint8_t *SongData;
    static uint8_t *PatternData;
    static int8_t *SampleData;
    //static int16_t *SndBuffer[2];

    struct Instrument
    {
        int16_t SampleNum;
        int16_t FadeOut;
        int16_t SampleMap[96];
        int16_t VolEnvelops[24];
        int16_t PanEnvelops[24];
        char Name[23];
        int8_t VolPoints;
        int8_t PanPoints;
        uint8_t VolSustainPt;
        uint8_t VolLoopStart;
        uint8_t VolLoopEnd;
        uint8_t PanSustainPt;
        uint8_t PanLoopStart;
        uint8_t PanLoopEnd;
        int8_t VolType;
        int8_t PanType;
        int8_t VibratoType;
        int8_t VibratoSweep;
        int8_t VibratoDepth;
        int8_t VibratoRate;
    };

    struct Sample
    {
        int32_t Length;
        int32_t LoopStart;
        int32_t LoopLength;
        int8_t *Data;
        int8_t Volume;
        int8_t FineTune;
        int8_t Type;
        int8_t RelNote;
        uint8_t OrigInst;
        bool Is16Bit;
        uint8_t Pan;
        char Name[23];
    };

    static int32_t TotalPatSize;
    static int32_t TotalInstSize;
    static int32_t TotalSampleSize;
    static int32_t TotalSampleNum;
    static int32_t PatternAddr[256];
    static Instrument *Instruments;
    static Sample *Samples;
    static int16_t *SampleStartIndex;
    static int32_t *SampleHeaderAddr;

    static int16_t Tick, CurRow, CurPos;
    static int16_t PatBreak, PatJump, PatDelay;
    static int16_t PatRepeat, RepeatPos, RepeatTo;
    static uint8_t Speed, Tempo, GlobalVol;
    static int32_t SampleToNextTick;
    static double Timer, TimePerSample, TimePerTick, SamplePerTick;
    static bool SongLoaded = false;
    static double AmpFinal;

    static long StartTime, EndTime, ExcuteTime;

    static uint8_t VibTab[32] =
    {
          0, 24, 49, 74, 97,120,141,161,
        180,197,212,224,235,244,250,253,
        255,253,250,244,235,224,212,197,
        180,161,141,120, 97, 74, 49, 24
    };

    static uint16_t PeriodTab[105] =
    {
        907,900,894,887,881,875,868,862,856,850,844,838,832,826,820,814,
        808,802,796,791,785,779,774,768,762,757,752,746,741,736,730,725,
        720,715,709,704,699,694,689,684,678,675,670,665,660,655,651,646,
        640,636,632,628,623,619,614,610,604,601,597,592,588,584,580,575,
        570,567,563,559,555,551,547,543,538,535,532,528,524,520,516,513,
        508,505,502,498,494,491,487,484,480,477,474,470,467,463,460,457,
        453,450,447,445,442,439,436,433,428
    };

    static int8_t FineTuneTable[32] =
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
        int8_t Note;
        int8_t RelNote;
        int8_t NoteArpeggio;
        uint8_t LastNote;
        int8_t FineTune;

        uint8_t Instrument;
        uint8_t NextInstrument;
        uint8_t LastInstrument;

        uint8_t VolCmd;
        uint8_t VolPara;
        int16_t Volume;
        int16_t LastVol;
        int16_t SmpPlaying;
        int16_t Sample;
        int16_t Pan;
        int16_t PanFinal;
        int16_t VolEnvelope;
        int16_t PanEnvelope;
        int16_t Delay;
        int16_t RxxCounter;
        int16_t FadeTick;
        uint8_t Effect;
        uint8_t Parameter;

        uint8_t VibratoPos;
        uint8_t VibratoAmp;
        uint8_t VibratoType;
        bool VolVibrato;
        uint8_t TremorPos;
        uint8_t TremorAmp;
        uint8_t TremorType;

        uint8_t SlideUpSpd;
        uint8_t SlideDnSpd;
        uint8_t SlideSpd;
        uint8_t VibratoPara;
        uint8_t TremoloPara;
        int8_t VolSlideSpd;
        uint8_t FineProtaUp;
        uint8_t FineProtaDn;
        uint8_t FineVolUp;
        uint8_t FineVolDn;
        int8_t GlobalVolSlideSpd;
        int8_t PanSlideSpd;
        uint8_t RetrigPara;
        uint8_t TremorPara;
        uint8_t EFProtaUp;
        uint8_t EFProtaDn;

        int8_t TremorTick;
        bool TremorMute;

        uint8_t AutoVibPos;
        uint8_t AutoVibSweep;
        int8_t EnvFlags;

        bool InstTrig;
        bool LxxEffect;
        bool Active;
        bool Fading;
        bool KeyOff;

        //Current playing sample
        bool Muted;
        int8_t Loop;
        int8_t LoopType;
        bool Is16Bit;
        int16_t Period;
        int16_t TargetPeriod;
        int16_t PeriodOfs;
        int16_t StartCount;
        int16_t EndCount;
        int32_t SmpLeng;
        int32_t LoopStart;
        int32_t LoopEnd;
        int32_t LoopLeng;
        int32_t Delta;
        float FacL;
        float FacR;
        int8_t *Data;
        int8_t *DataPrev;
        int32_t Pos;
        int32_t PosL16;
        int32_t VolTargetInst;
        int32_t VolFinalInst;
        int32_t VolTargetL;
        int32_t VolTargetR;
        int32_t VolFinalL;
        int32_t VolFinalR;
        int32_t VolRampSpdL;
        int32_t VolRampSpdR;
        int32_t VolRampSpdInst;
        int32_t PrevSmp;
        int32_t EndSmp;
    };

    struct Note
    {
        uint8_t Note;
        uint8_t Instrument;
        uint8_t VolCmd;
        uint8_t Effect;
        uint8_t Parameter;
    };

    struct EnvInfo
    {
        uint8_t Value;
        int16_t MaxPoint;
    };

    static Channel *Channels;

    #define Ch Channels[i]

    static void RecalcAmp()
    {
        AmpFinal = Amp * 0.5;// /(MIN(3, MAX(1, NumOfChannels/10)))*.7;
    }

    static void UpdateTimer()
    {
        TimePerTick = 2.5 / Tempo;
        SamplePerTick = TimePerTick / TimePerSample;
    }

    static void ResetChannels()
    {
        int i = 0;
        while (i < NumOfChannels)
        {
            Ch.Note = 0;
            Ch.RelNote = 0;
            Ch.NoteArpeggio = 0;
            Ch.LastNote = 0;
            Ch.Pos = 0;
            Ch.Delta = 0;
            Ch.Period = 0;
            Ch.TargetPeriod = 0;
            Ch.FineTune = 0;
            Ch.Loop = 0;

            Ch.SmpPlaying = -1;
            Ch.Sample = -1;
            Ch.Instrument = 0;
            Ch.NextInstrument = 0;
            Ch.LastInstrument = 0;

            Ch.VolCmd = 0;
            Ch.Volume = 0;
            Ch.LastVol = 0;
            Ch.VolTargetInst = 0;
            Ch.VolTargetL = 0;
            Ch.VolTargetR = 0;
            Ch.VolFinalL = 0;
            Ch.VolFinalR = 0;
            Ch.VolFinalInst = 0;
            Ch.VolRampSpdL = 0;
            Ch.VolRampSpdR = 0;
            Ch.Pan = 128;
            Ch.PanFinal = 128;

            Ch.Effect = 0;
            Ch.Parameter = 0;

            Ch.VibratoPos = 0;
            Ch.VibratoAmp = 0;
            Ch.VibratoType = 0;
            Ch.VolVibrato = false;
            Ch.TremorPos = 0;
            Ch.TremorAmp = 0;
            Ch.TremorType = 0;

            Ch.SlideUpSpd = 0;
            Ch.SlideDnSpd = 0;
            Ch.SlideSpd = 0;
            Ch.VibratoPara = 0;
            Ch.TremoloPara = 0;
            Ch.VolSlideSpd = 0;
            Ch.FineProtaUp = 0;
            Ch.FineProtaDn = 0;
            Ch.FineVolUp = 0;
            Ch.FineVolDn = 0;
            Ch.GlobalVolSlideSpd = 0;
            Ch.PanSlideSpd = 0;
            Ch.RetrigPara = 0;
            Ch.EFProtaUp = 0;
            Ch.EFProtaDn = 0;

            Ch.Delay = 0;
            Ch.RxxCounter = 0;
            Ch.TremorTick = 0;
            Ch.TremorMute = false;

            Ch.StartCount = 0;
            Ch.EndCount = SMP_CHANGE_RAMP;
            Ch.PrevSmp = 0;
            Ch.EndSmp = 0;

            Ch.AutoVibPos = 0;
            Ch.AutoVibSweep = 0;
            Ch.VolEnvelope = 0;
            Ch.PanEnvelope = 0;
            Ch.EnvFlags = 0;

            Ch.InstTrig = false;
            Ch.Active = false;
            Ch.Fading = false;
            Ch.KeyOff = false;
            Ch.FadeTick = 0;

            Ch.Muted = false;

            i ++ ;
        }
    }

    static void ResetPatternEffects()
    {
        SampleToNextTick = 0;

        GlobalVol = 64;

        PatBreak = -1;
        PatJump = -1;
        PatDelay = 0;

        PatRepeat = 0;
        RepeatPos = 0;
        RepeatTo = -1;
    }

    void ResetModule()
    {

        SDL_PauseAudioDevice(DeviceID, 0);
        Playing = true;

        Tempo = DefaultTempo;
        Speed = DefaultSpd;
        Tick = Speed - 1;

        CurRow = -1;
        CurPos = 0;

        ResetPatternEffects();

        RecalcAmp();

        ResetChannels();

        UpdateTimer();
    }

    bool LoadModule(uint8_t *SongDataOrig, uint32_t SongDataLeng, bool UsingInterpolation = true, bool UseStereo = true, bool LoopSong = true, int BufSize = BUFFER_SIZE, int SmpRate = SMP_RATE)
    {
        Loop = LoopSong;
        Stereo = UseStereo;
        Interpolation = UsingInterpolation;

        PanMode = 0;
        IgnoreF00 = true;

        SamplingRate = SmpRate;
        BufferSize = BufSize;

        TimePerSample = 1.0/SamplingRate;

        MasterVol = 255;

        int i, j, k;
        int32_t SongDataOfs = 17;

        //Song data init
        if (SongData != NULL) free(SongData);
        SongData = (uint8_t *)malloc(SongDataLeng);
        if (SongData == NULL) return false;

        memcpy(SongData, SongDataOrig, SongDataLeng);

        //Song name
        i = 0;
        while (i < 20)
            SongName[i++] = SongData[SongDataOfs++];

        //Tracker name & version
        SongDataOfs = 38;
        i = 0;
        while (i < 20)
            TrackerName[i++] = SongData[SongDataOfs++];

        TrackerVersion = (((((uint16_t)SongData[SongDataOfs])<<8)&0xFF00) | SongData[SongDataOfs+1]);

        SongDataOfs = 60;

        //Song settings
        int32_t HeaderSize = *(int32_t *)(SongData + SongDataOfs) + 60;
        SongLength = *(int16_t *)(SongData + SongDataOfs + 4);
        RstPos = *(int16_t *)(SongData + SongDataOfs + 6);
        NumOfChannels = *(int16_t *)(SongData + SongDataOfs + 8);
        NumOfPatterns = *(int16_t *)(SongData + SongDataOfs + 10);
        NumOfInstruments = *(int16_t *)(SongData + SongDataOfs + 12);
        AmigaFreqTable = !(SongData[SongDataOfs + 14] & 1);

        DefaultSpd = *(int16_t *)(SongData + SongDataOfs + 16);
        DefaultTempo = *(int16_t *)(SongData + SongDataOfs + 18);

        if (Channels != NULL) free(Channels);
        Channels = (Channel *)malloc(sizeof(Channel) * NumOfChannels);
        if (Channels == NULL) return false;

        //Pattern order table
        SongDataOfs = 80;
        i = 0;
        while (i < SongLength)
            OrderTable[i++] = SongData[SongDataOfs++];

        //Pattern data size calc
        memset(PatternAddr, 0, 256*4);
        TotalPatSize = 0;

        SongDataOfs = HeaderSize;
        int32_t PatternOrig = SongDataOfs;
        i = 0;
        while (i < NumOfPatterns)
        {
            int32_t PatHeaderSize = *(int32_t *)(SongData + SongDataOfs);
            int16_t PatternLeng = *(int16_t *)(SongData + SongDataOfs + 5);
            int16_t PatternSize = *(int16_t *)(SongData + SongDataOfs + 7);

            PatternAddr[i++] = TotalPatSize;

            SongDataOfs += PatHeaderSize + PatternSize;

            TotalPatSize += 2 + PatternLeng*ROW_SIZE_XM;
        }

        //Pattern data init
        if (PatternData != NULL) free(PatternData);
        PatternData = (uint8_t *)malloc(TotalPatSize);
        if (PatternData == NULL) return false;
        memset(PatternData, 0, TotalPatSize);

        //Patter data
        SongDataOfs = PatternOrig;
        i = 0;
        while (i < NumOfPatterns)
        {
            int32_t PatHeaderSize = *(int32_t *)(SongData + SongDataOfs);
            int16_t PatternLeng = *(int16_t *)(SongData + SongDataOfs + 5);
            int16_t PatternSize = *(int16_t *)(SongData + SongDataOfs + 7);

            int32_t PDIndex = PatternAddr[i];
            PatternData[PDIndex++] = PatternLeng & 0xFF;
            PatternData[PDIndex++] = (int8_t)((PatternLeng >> 8) & 0xFF);

            SongDataOfs += PatHeaderSize;

            if (PatternSize > 0)
            {
                j = 0;
                while (j < PatternLeng)
                {
                    k = 0;
                    while (k < NumOfChannels)
                    {
                        int8_t SignByte = SongData[SongDataOfs++];

                        PatternData[PDIndex] = 0;
                        PatternData[PDIndex + 1] = 0;

                        if (SignByte & 0x80)
                        {
                            if (SignByte & 0x01) PatternData[PDIndex] = SongData[SongDataOfs++];
                            if (SignByte & 0x02) PatternData[PDIndex + 1] = SongData[SongDataOfs++];
                            if (SignByte & 0x04) PatternData[PDIndex + 2] = SongData[SongDataOfs++];
                            if (SignByte & 0x08) PatternData[PDIndex + 3] = SongData[SongDataOfs++];
                            if (SignByte & 0x10) PatternData[PDIndex + 4] = SongData[SongDataOfs++];
                        }
                        else
                        {
                            PatternData[PDIndex] = SignByte;
                            PatternData[PDIndex + 1] = SongData[SongDataOfs++];
                            PatternData[PDIndex + 2] = SongData[SongDataOfs++];
                            PatternData[PDIndex + 3] = SongData[SongDataOfs++];
                            PatternData[PDIndex + 4] = SongData[SongDataOfs++];
                        }
                        PDIndex += 5;
                        k ++ ;
                    }
                    j ++ ;
                }
            }
            i ++ ;
        }

        //Instrument size calc
        if (SampleStartIndex != NULL) free(SampleStartIndex);
        SampleStartIndex = (int16_t *)malloc(NumOfInstruments*2);
        if (SampleStartIndex == NULL) return false;

        if (Instruments != NULL) free(Instruments);
        Instruments = (Instrument *)malloc(NumOfInstruments*sizeof(Instrument));
        if (Instruments == NULL) return false;

        TotalInstSize = TotalSampleSize = TotalSampleNum = 0;
        int InstOrig = SongDataOfs;
        i = 0;
        while (i < NumOfInstruments)
        {
            int32_t InstSize = *(int32_t *)(SongData + SongDataOfs);
            int16_t InstSampleNum = *(int16_t *)(SongData + SongDataOfs + 27);
            Instruments[i].SampleNum = InstSampleNum;

            //Name
            k = 0;
            while (k < 22)
            {
                Instruments[i].Name[k] = SongData[SongDataOfs + 4 + k];
                k ++ ;
            }

            if (InstSampleNum > 0)
            {
                //Note mapping
                j = 0;
                while (j < 96)
                {
                    Instruments[i].SampleMap[j] = TotalSampleNum + SongData[SongDataOfs + 33 + j];
                    j ++ ;
                }

                //Vol envelopes
                j = 0;
                while (j < 24)
                {
                    Instruments[i].VolEnvelops[j] = *(int16_t *)(SongData + SongDataOfs + 129 + j*2);
                    j ++ ;
                }

                //Pan envelopes
                j = 0;
                while (j < 24)
                {
                    Instruments[i].PanEnvelops[j] = *(int16_t *)(SongData + SongDataOfs + 177 + j*2);
                    j ++ ;
                }

                Instruments[i].VolPoints = SongData[SongDataOfs + 225];
                Instruments[i].PanPoints = SongData[SongDataOfs + 226];
                Instruments[i].VolSustainPt = SongData[SongDataOfs + 227];
                Instruments[i].VolLoopStart = SongData[SongDataOfs + 228];
                Instruments[i].VolLoopEnd = SongData[SongDataOfs + 229];
                Instruments[i].PanSustainPt = SongData[SongDataOfs + 230];
                Instruments[i].PanLoopStart = SongData[SongDataOfs + 231];
                Instruments[i].PanLoopEnd = SongData[SongDataOfs + 232];
                Instruments[i].VolType = SongData[SongDataOfs + 233];
                Instruments[i].PanType = SongData[SongDataOfs + 234];
                Instruments[i].VibratoType = SongData[SongDataOfs + 235];
                Instruments[i].VibratoSweep = SongData[SongDataOfs + 236];
                Instruments[i].VibratoDepth = SongData[SongDataOfs + 237];
                Instruments[i].VibratoRate = SongData[SongDataOfs + 238];
                Instruments[i].FadeOut = *(int16_t *)(SongData + SongDataOfs + 239);

                SongDataOfs += InstSize;

                int32_t SubOfs = 0;
                int32_t SampleDataOfs = 0;
                j = 0;
                while (j < InstSampleNum)
                {
                    SubOfs = j*40;
                    int32_t SampleLeng = *(int32_t *)(SongData + SongDataOfs + SubOfs);
                    int32_t LoopStart = *(int32_t *)(SongData + SongDataOfs + SubOfs + 4);
                    int32_t LoopLeng = *(int32_t *)(SongData + SongDataOfs + SubOfs + 8);
                    int8_t SampleType = SongData[SongDataOfs + SubOfs + 14] & 0x03;

                    SampleDataOfs += SampleLeng;

                    if (SampleType == 1)
                        SampleLeng = LoopStart + LoopLeng;
                    if ((SongData[SongDataOfs + SubOfs + 14] & 0x03) >= 2)
                        SampleLeng = LoopStart + (LoopLeng + LoopLeng);

                    TotalSampleSize += SampleLeng;
                    j ++ ;
                }
                SongDataOfs += InstSampleNum*40 + SampleDataOfs;
            }
            else SongDataOfs += InstSize;

            SampleStartIndex[i] = TotalSampleNum;
            TotalSampleNum += InstSampleNum;

            i ++ ;
        }

        //Sample convert
        if (SampleHeaderAddr != NULL) free(SampleHeaderAddr);
        SampleHeaderAddr = (int32_t *)malloc(TotalSampleNum*4);
        if (SampleHeaderAddr == NULL) return false;


        if (SampleData != NULL) free(SampleData);
        SampleData = (int8_t *)malloc(TotalSampleSize);
        if (SampleData == NULL) return false;

        if (Samples != NULL) free(Samples);
        Samples = (Sample *)malloc(TotalSampleNum*sizeof(Sample));
        if (Samples == NULL) return false;

        int16_t SampleNum = 0;
        int32_t SampleWriteOfs = 0;
        SongDataOfs = InstOrig;
        i = 0;
        while (i < NumOfInstruments)
        {
            int32_t InstSize = *(int32_t *)(SongData + SongDataOfs);
            int16_t InstSampleNum = *(int16_t *)(SongData + SongDataOfs + 27);

            SongDataOfs += InstSize;

            if (InstSampleNum > 0)
            {
                int32_t SubOfs = 0;
                int32_t SampleDataOfs = 0;
                j = 0;
                while (j < InstSampleNum)
                {
                    SampleNum = SampleStartIndex[i] + j;
                    SubOfs = j*40;
                    SampleHeaderAddr[SampleNum] = SongDataOfs + SubOfs;
                    int32_t SampleLeng = *(int32_t *)(SongData + SongDataOfs + SubOfs);
                    int32_t LoopStart = *(int32_t *)(SongData + SongDataOfs + SubOfs + 4);
                    int32_t LoopLeng = *(int32_t *)(SongData + SongDataOfs + SubOfs + 8);
                    int8_t SampleType = SongData[SongDataOfs + SubOfs + 14];
                    bool Is16Bit = (SampleType & 0x10);

                    int16_t SampleNum = SampleStartIndex[i] + j;

                    Samples[SampleNum].OrigInst = i + 1;
                    Samples[SampleNum].Type = SampleType & 0x03;
                    Samples[SampleNum].Is16Bit = Is16Bit;

                    Samples[SampleNum].Volume = SongData[SongDataOfs + SubOfs + 12];
                    Samples[SampleNum].Pan = SongData[SongDataOfs + SubOfs + 15];
                    Samples[SampleNum].FineTune = *(int8_t *)(SongData + SongDataOfs + SubOfs + 13);
                    Samples[SampleNum].RelNote = *(int8_t *)(SongData + SongDataOfs + SubOfs + 16);

                    k = 0;
                    while (k < 22)
                    {
                        Samples[SampleNum].Name[k] = SongData[SongDataOfs + SubOfs + 18 + k];
                        k ++ ;
                    }

                    Samples[SampleNum].Data = SampleData + SampleWriteOfs;
                    SubOfs = 40*InstSampleNum + SampleDataOfs;
                    SampleDataOfs += SampleLeng;

                    if (Is16Bit)
                    {
                        Samples[SampleNum].Length = SampleLeng >> 1;
                        Samples[SampleNum].LoopStart = LoopStart >> 1;
                        Samples[SampleNum].LoopLength = LoopLeng >> 1;
                    }
                    else
                    {
                        Samples[SampleNum].Length = SampleLeng;
                        Samples[SampleNum].LoopStart = LoopStart;
                        Samples[SampleNum].LoopLength = LoopLeng;
                    }

                    int32_t ReversePoint = -1;
                    if (Samples[SampleNum].Type == 1)
                        SampleLeng = LoopStart + LoopLeng;
                    else if (Samples[SampleNum].Type >= 2)
                    {
                        ReversePoint = LoopStart + LoopLeng;
                        SampleLeng = ReversePoint + LoopLeng;
                    }

                    bool Reverse = false;
                    int32_t ReadOfs = 0;
                    int16_t Old = 0;
                    k = 0;
                    if (Is16Bit)
                    {
                        int16_t New;
                        SampleLeng >>= 1;
                        ReversePoint >>= 1;
                        while (k < SampleLeng)
                        {
                            //int16_t New = (int16_t)SONGSEEK16(SongDataOfs + SubOfs, SongData) + Old;
                            if (k == ReversePoint)
                            {
                                Reverse = true;
                                ReadOfs -= 2;
                            }
                            if (Reverse)
                            {
                                New = -*(int16_t *)(SongData + SongDataOfs + SubOfs + ReadOfs) + Old;
                                ReadOfs -= 2;
                            }
                            else
                            {
                                New = *(int16_t *)(SongData + SongDataOfs + SubOfs + ReadOfs) + Old;
                                ReadOfs += 2;
                            }
                            *(int16_t *)(SampleData + SampleWriteOfs) = New;
                            SampleWriteOfs += 2;

                            Old = New;
                            k ++ ;
                        }
                    }
                    else
                    {
                        int8_t New;
                        while (k < SampleLeng)
                        {
                            if (k == ReversePoint)
                            {
                                Reverse = true;
                                ReadOfs -- ;
                            }
                            if (Reverse)
                            {
                                New = -SongData[SongDataOfs + SubOfs + ReadOfs] + Old;
                                ReadOfs -- ;
                            }
                            else
                            {
                                New = SongData[SongDataOfs + SubOfs + ReadOfs] + Old;
                                ReadOfs ++ ;
                            }
                            SampleData[SampleWriteOfs++] = New;

                            Old = New;
                            k ++ ;
                        }
                    }

                    j ++ ;
                    //SampleNum ++ ;
                }
                SongDataOfs += 40*InstSampleNum + SampleDataOfs;
            }
            i ++ ;
        }

        if (SampleHeaderAddr != NULL) free(SampleHeaderAddr);
        if (SampleStartIndex != NULL) free(SampleStartIndex);

        if (SongData != NULL) free(SongData);

        ResetModule();
        SongLoaded = true;

        return true;
    }

    static Note GetNote(uint8_t PatNum, uint8_t Row, uint8_t Col)
    {
        Note ThisNote = *(Note *)(PatternData + PatternAddr[PatNum] + ROW_SIZE_XM*Row + Col*NOTE_SIZE_XM + 2);

        return ThisNote;
    }

    static EnvInfo CalcEnvelope(Instrument Inst, int16_t Pos, bool CalcPan)
    {
        EnvInfo RetInfo;

        if (Inst.SampleNum > 0)
        {
            int16_t *EnvData = CalcPan ? Inst.PanEnvelops : Inst.VolEnvelops;
            int8_t NumOfPoints = CalcPan ? Inst.PanPoints : Inst.VolPoints;
            RetInfo.MaxPoint = EnvData[(NumOfPoints - 1)*2];

            if (Pos == 0)
            {
                RetInfo.Value = EnvData[1];
                return RetInfo;
            }

            int i = 0;
            while (i < NumOfPoints - 1)
            {
                if (Pos < EnvData[i*2 + 2])
                {
                    int16_t PrevPos = EnvData[i*2];
                    int16_t NextPos = EnvData[i*2 + 2];
                    int16_t PrevValue = EnvData[i*2 + 1];
                    int16_t NextValue = EnvData[i*2 + 3];
                    RetInfo.Value = (int8_t)((NextValue - PrevValue)*(Pos - PrevPos)/(NextPos - PrevPos) + PrevValue);
                    return RetInfo;
                }
                i ++ ;
            }

            RetInfo.Value = EnvData[(NumOfPoints - 1)*2 + 1];
            return RetInfo;
        }
        else
        {
            RetInfo.Value = 0;
            RetInfo.MaxPoint = 0;
        }

        return RetInfo;
    }

    #define PI 3.1415926535897932384626433832795

    static void CalcPeriod(uint8_t i)
    {
        int16_t RealNote = (Ch.NoteArpeggio <= 119 && Ch.NoteArpeggio > 0 ? Ch.NoteArpeggio : Ch.Note) + Ch.RelNote - 1;
        int8_t FineTune = Ch.FineTune;
        int16_t Period;
        if (!AmigaFreqTable)
            Period = 7680 - RealNote*64 - FineTune/2;
        else
        {
            //https://github.com/dodocloud/xmplayer/blob/master/src/xmlib/engine/utils.ts
            //function calcPeriod
            double FineTuneFrac = floor((double)FineTune / 16.0);
            uint16_t Period1 = PeriodTab[8 + (RealNote % 12)*8 + (int16_t)FineTuneFrac];
            uint16_t Period2 = PeriodTab[8 + (RealNote % 12)*8 + (int16_t)FineTuneFrac + 1];
            FineTuneFrac = ((double)FineTune / 16.0) - FineTuneFrac;
            Period = (int16_t)round((1.0 - FineTuneFrac)*Period1 + FineTuneFrac*Period2) * (16.0 / pow(2, floor(RealNote / 12) - 1));
        }

        Ch.Period = MAX(Ch.Period, 50);
        Period = MAX(Period, 50);

        if (Ch.NoteArpeggio <= 119 && Ch.NoteArpeggio > 0)
            Ch.Period = Period;
        else
            Ch.TargetPeriod = Period;
    }

    static void UpdateChannelInfo()
    {
        int i = 0;
        while (i < NumOfChannels)
        {
            if (Ch.Active && Ch.SmpPlaying != -1)
            {
                //Arpeggio
                if (Ch.Parameter != 0 && Ch.Effect == 0)
                {
                    int8_t ArpNote1 = Ch.Parameter >> 4;
                    int8_t ArpNote2 = Ch.Parameter & 0xF;
                    int8_t Arpeggio[3] = {0, ArpNote2, ArpNote1};
                    if (AmigaFreqTable) Ch.NoteArpeggio = Ch.Note + Arpeggio[Tick%3];
                    else Ch.PeriodOfs = -Arpeggio[Tick%3] * 64;
                    //Ch.Period = Ch.TargetPeriod;
                }

                //Auto vibrato
                int32_t AutoVibFinal = 0;
                if (Ch.SmpPlaying != -1)
                {
                    Instrument SmpOrigInst = Instruments[Samples[Ch.SmpPlaying].OrigInst - 1];

                    if (SmpOrigInst.SampleNum > 0)
                    {
                        Ch.AutoVibPos += SmpOrigInst.VibratoRate;
                        if (Ch.AutoVibSweep < SmpOrigInst.VibratoSweep) Ch.AutoVibSweep ++ ;

                        if (SmpOrigInst.VibratoRate)
                        {
                            //https://github.com/milkytracker/MilkyTracker/blob/master/src/milkyplay/PlayerSTD.cpp
                            //Line 568 - 599
                            uint8_t VibPos = Ch.AutoVibPos >> 2;
                            uint8_t VibDepth = SmpOrigInst.VibratoDepth;

                            int32_t Value = 0;
                            switch (SmpOrigInst.VibratoType) {
                                // sine (we must invert the phase here)
                                case 0:
                                    Value = ~VibTab[VibPos&31];
                                    break;
                                // square
                                case 1:
                                    Value = 255;
                                    break;
                                // ramp down (down being the period here - so ramp frequency up ;)
                                case 2:
                                    Value = ((VibPos & 31) *539087) >> 16;
                                    if ((VibPos & 63) > 31) Value = 255 - Value;
                                    break;
                                // ramp up (up being the period here - so ramp frequency down ;)
                                case 3:
                                    Value = ((VibPos & 31) * 539087) >> 16;
                                    if ((VibPos & 63) > 31) Value = 255 - Value;
                                    Value = -Value;
                                    break;
                            }

                            AutoVibFinal = ((Value * VibDepth) >> 1);
                            if (SmpOrigInst.VibratoSweep) {
                                AutoVibFinal *= ((int32_t)Ch.AutoVibSweep << 8) / SmpOrigInst.VibratoSweep;
                                AutoVibFinal >>= 8;
                            }

                            if ((VibPos & 63) > 31) AutoVibFinal = -AutoVibFinal;

                            AutoVibFinal >>= 7;
                        }
                    }
                }

                //Delta calculation
                CalcPeriod(i);
                double Freq;
                double RealPeriod = MAX(Ch.Period + Ch.PeriodOfs, 50) + Ch.VibratoAmp*sin((Ch.VibratoPos & 0x3F)*PI/32)*8 + AutoVibFinal;
                if (!AmigaFreqTable)
                    Freq = 8363*pow(2, (4608 - RealPeriod)/768);
                else Freq = 8363.0*1712/RealPeriod;

                Ch.Delta = (uint32_t)(Freq/SamplingRate*TOINT_SCL);

                Instrument Inst = Instruments[Ch.Instrument - 1];

                //Volume
                Ch.Volume = MAX(MIN(Ch.Volume, 64), 0);
                int16_t RealVol = Ch.TremorMute ? 0 : (int8_t)MAX(MIN(Ch.Volume + Ch.TremorAmp*sin((Ch.TremorPos & 0x3F)*PI/32)*4, 64), 0);
                int16_t VolTarget = RealVol*GlobalVol/64;
                if (Inst.VolType & 0x01)
                {
                    EnvInfo VolEnv = CalcEnvelope(Inst, Ch.VolEnvelope, false);

                    if (Inst.VolType & 0x02)
                    {
                        if (Ch.VolEnvelope != Inst.VolEnvelops[Inst.VolSustainPt*2] || Ch.Fading)
                            Ch.VolEnvelope ++ ;
                    }
                    else Ch.VolEnvelope ++ ;

                    if (Inst.VolType & 0x04)
                    {
                        if (Ch.VolEnvelope >= Inst.VolEnvelops[Inst.VolLoopEnd*2])
                            Ch.VolEnvelope = Inst.VolEnvelops[Inst.VolLoopStart*2];
                    }

                    if (Ch.VolEnvelope >= VolEnv.MaxPoint)
                        Ch.VolEnvelope = VolEnv.MaxPoint;

                    int16_t InstFadeout = Inst.FadeOut;
                    int32_t FadeOutVol;
                    if (Ch.Fading && InstFadeout > 0)
                    {
                        int16_t FadeOutLeng = 32768 / InstFadeout;
                        if (Ch.FadeTick < FadeOutLeng) Ch.FadeTick ++ ;
                        else Ch.Active = false;
                        FadeOutVol = 64*(FadeOutLeng - Ch.FadeTick)/FadeOutLeng;
                    }
                    else FadeOutVol = 64;

                    Ch.VolTargetInst = VolEnv.Value;
                    //Ch.VolTarget = FadeOutVol*GlobalVol/64*RealVol/64;
                    //Ch.VolTarget = FadeOutVol*VolEnv.Value/64*GlobalVol/64*RealVol/64;
                    VolTarget = FadeOutVol*GlobalVol/64*RealVol/64;
                }
                else
                {
                    Ch.VolTargetInst = 64;
                    if (Ch.Fading) Ch.Volume = 0;
                }
                Ch.VolTargetInst <<= INT_ACC_RAMPING;

                VolTarget = MAX(MIN(VolTarget, 64), 0);

                //Panning
                Ch.Pan = MAX(MIN(Ch.Pan, 255), 0);
                if (Inst.PanType & 0x01)
                {
                    EnvInfo PanEnv = CalcEnvelope(Inst, Ch.PanEnvelope, true);
                    if (Inst.PanType & 0x02)
                    {
                        if (Ch.PanEnvelope != Inst.PanEnvelops[Inst.PanSustainPt*2] || Ch.Fading)
                            Ch.PanEnvelope ++ ;
                    }
                    else Ch.PanEnvelope ++ ;

                    if (Inst.PanType & 0x04)
                    {
                        if (Ch.PanEnvelope >= Inst.PanEnvelops[Inst.PanLoopEnd*2])
                            Ch.PanEnvelope = Inst.PanEnvelops[Inst.PanLoopStart*2];
                    }

                    if (Ch.PanEnvelope >= PanEnv.MaxPoint)
                        Ch.PanEnvelope = PanEnv.MaxPoint;

                    Ch.PanFinal = Ch.Pan + (((PanEnv.Value - 32) * (128 - abs(Ch.Pan - 128))) >> 5);
                }
                else Ch.PanFinal = Ch.Pan;
                Ch.PanFinal = MAX(MIN(Ch.PanFinal, 255), 0);

                //Sample info
                Sample CurSample = Samples[Ch.SmpPlaying];
                Ch.Data = CurSample.Data;
                Ch.LoopType = CurSample.Type;

                Ch.Is16Bit = CurSample.Is16Bit;
                Ch.SmpLeng = CurSample.Length;
                Ch.LoopStart = CurSample.LoopStart;
                Ch.LoopLeng = CurSample.LoopLength;
                Ch.LoopEnd = Ch.LoopStart + Ch.LoopLeng;

                if (Ch.LoopType >= 2)
                {
                    Ch.LoopEnd += Ch.LoopLeng;
                    Ch.LoopLeng <<= 1;
                }
                if (!Ch.LoopType) Ch.Loop = 0;

                //Set volume ramping
                double VolRampSmps = SamplePerTick;
                double InstVolRampSmps = SamplePerTick;
                if (Ch.LxxEffect)
                {
                    InstVolRampSmps = VOLRAMP_NEW_INSTR;
                    Ch.LxxEffect = false;
                }

                if (((Ch.VolCmd & 0xF0) >= 0x10 &&
                    (Ch.VolCmd & 0xF0) <= 0x50) ||
                    (Ch.VolCmd & 0xF0) == 0xC0 ||
                     Ch.Effect == 12 || Ch.Effect == 8 ||
                    (Ch.Effect == 0x14 && (Ch.Parameter & 0xF0) == 0xC0))
                    VolRampSmps = VOLRAMP_VOLSET_SAMPLES;

                if (!(Inst.VolType & 0x01) && Ch.Fading)
                {
                    VolTarget = 0;
                    Ch.Fading = false;
                    VolRampSmps = VOLRAMP_VOLSET_SAMPLES;
                }

                if (Ch.InstTrig)
                {
                    //Ch.VolFinal = Ch.VolTarget;
                    //Ch.VolFinalInst = Ch.VolTargetInst;

                    VolRampSmps = VOLRAMP_VOLSET_SAMPLES;
                    InstVolRampSmps = VOLRAMP_NEW_INSTR;

                    //Ch.VolFinalL = Ch.VolFinalR = 0;
                    Ch.InstTrig = false;
                }
                //else if (Ch.Effect == 0xA)
                //    VolRampSmps = SamplePerTick;

                if (Stereo)
                {
                    if (!PanMode)
                    {
                        //https://modarchive.org/forums/index.php?topic=3517.0
                        //FT2 square root panning law
                        Ch.VolTargetL = (int32_t)(VolTarget * sqrt((256 - Ch.PanFinal) / 256.0) / .707) << INT_ACC_RAMPING;
                        Ch.VolTargetR = (int32_t)(VolTarget * sqrt(Ch.PanFinal / 256.0) / .707) << INT_ACC_RAMPING;
                    }
                    else
                    {
                        //Linear panning
                        if (Ch.PanFinal > 128)
                        {
                            Ch.VolTargetL = VolTarget * (256 - Ch.PanFinal) / 128.0;
                            Ch.VolTargetR = VolTarget;
                        }
                        else
                        {
                            Ch.VolTargetL = VolTarget;
                            Ch.VolTargetR = VolTarget * (Ch.PanFinal / 128.0);
                        }
                        Ch.VolTargetL <<= INT_ACC_RAMPING;
                        Ch.VolTargetR <<= INT_ACC_RAMPING;
                    }
                }
                else Ch.VolTargetL = Ch.VolTargetR = (VolTarget << INT_ACC_RAMPING);

                Ch.VolRampSpdL = (Ch.VolTargetL - Ch.VolFinalL) / VolRampSmps;
                Ch.VolRampSpdR = (Ch.VolTargetR - Ch.VolFinalR) / VolRampSmps;
                Ch.VolRampSpdInst = (Ch.VolTargetInst - Ch.VolFinalInst) / InstVolRampSmps;

                if (Ch.VolRampSpdL == 0) Ch.VolFinalL = Ch.VolTargetL;
                if (Ch.VolRampSpdR == 0) Ch.VolFinalR = Ch.VolTargetR;
                if (Ch.VolRampSpdInst == 0) Ch.VolFinalInst = Ch.VolTargetInst;
            }
            else Ch.VolFinalL = Ch.VolFinalR = 0;
            i ++ ;
        }
    }

    static void ChkEffectRow(Note ThisNote, uint8_t i, bool ByPassEffectCol, bool RxxRetrig = false)
    {
        uint8_t VolCmd = ThisNote.VolCmd;
        uint8_t VolPara = ThisNote.VolCmd & 0x0F;
        uint8_t Effect = ThisNote.Effect;
        uint8_t Para = ThisNote.Parameter;
        uint8_t SubEffect = Para & 0xF0;
        uint8_t SubPara = Para & 0x0F;

        //if ((Effect != 0) || (Para == 0))   //0XX
        //    Ch.NoteOfs = 0;

        if (ByPassEffectCol) goto VolCol;

        //Effect column
        switch (Effect)
        {
            default:
                break;
            case 1: //1xx
                if (Para != 0) Ch.SlideUpSpd = Para;
                break;
            case 2: //2xx
                if (Para != 0) Ch.SlideDnSpd = Para;
                break;
            case 3: //3xx
                if (Para != 0) Ch.SlideSpd = Para;
                break;
            case 4: //4xx
                if ((Para & 0xF) > 0)
                    Ch.VibratoPara = (Ch.VibratoPara&0xF0) + (Para&0xF);
                if (((Para >> 4) & 0xF) > 0)
                    Ch.VibratoPara = (Ch.VibratoPara&0xF) + (Para&0xF0);
                break;
            case 7: //7xx
                if ((Para & 0xF) > 0)
                    Ch.TremoloPara = (Ch.TremoloPara&0xF0) + (Para&0xF);
                if (((Para >> 4) & 0xF) > 0)
                    Ch.TremoloPara = (Ch.TremoloPara&0xF) + (Para&0xF0);
                break;
            case 8: //8xx
                Ch.Pan = Para;
                break;
            //case 9: //9xx
            //    //if (Ch.Active) Ch.Pos = Para*256;
            //    if (ThisNote.Note < 97) Ch.Pos = Para*256;
            //    break;
            case 5: //5xx
            case 6: //6xx
            case 10:    //Axx
                if (Para != 0)
                {
                    if (Para & 0xF) Ch.VolSlideSpd = -(Para & 0xF);
                    else if (Para & 0xF0) Ch.VolSlideSpd = (Para >> 4) & 0xF;
                }
                break;
            case 11:    //Bxx
                PatJump = Para;
                break;
            case 12:    //Cxx
                Ch.Volume = Para;
                break;
            case 13:    //Dxx
                PatBreak = (Para >> 4)*10 + (Para & 0xF);
                break;
            case 14:    //Exx
                switch (SubEffect)
                {
                    case 0x10:  //E1x
                        if (SubPara != 0) Ch.FineProtaUp = SubPara;
                        if (Ch.Period > 1) Ch.Period -= Ch.FineProtaUp * 4;
                        break;
                    case 0x20:  //E2x
                        if (SubPara != 0) Ch.FineProtaDn = SubPara;
                        if (Ch.Period < 7680) Ch.Period += Ch.FineProtaDn * 4;
                        break;
                    case 0x30:  //E3x
                        break;
                    case 0x40:  //E4x
                        break;
                    case 0x50:  //E5x
                        Ch.FineTune = FineTuneTable[(AmigaFreqTable ? 0 : 16) + SubPara];
                        break;
                    case 0x60:  //E6x
                        if (SubPara == 0 && RepeatPos < CurRow) {
                            RepeatPos = CurRow;
                            PatRepeat = 0;
                        } else if (PatRepeat < SubPara) {
                            PatRepeat ++ ;
                            RepeatTo = RepeatPos;
                        }
                        break;
                    case 0x70:  //E7x
                        break;
                    case 0x80:  //E8x
                        break;
                    case 0xA0:  //EAx
                        if (SubPara != 0) Ch.FineVolUp = SubPara;
                        Ch.Volume += Ch.FineVolUp;
                        break;
                    case 0xB0:  //EBx
                        if (SubPara != 0) Ch.FineVolDn = SubPara;
                        Ch.Volume -= Ch.FineVolDn;
                        break;
                    case 0x90:  //E9x
                    case 0xC0:  //ECx
                        Ch.Delay = SubPara - 1;
                        break;
                    case 0xD0:  //EDx
                        Ch.Delay = SubPara;
                        break;
                    case 0xE0:
                        PatDelay = SubPara;
                        break;
                }
                break;
            case 15:    //Fxx
                if (!IgnoreF00)
                {
                    if (Para < 32 && Para != 0) Speed = Para;
                    else Tempo = Para;
                }
                else if (Para != 0)
                {
                    if (Para < 32) Speed = Para;
                    else Tempo = Para;
                }
                break;
            case 16:    //Gxx
                GlobalVol = MAX(MIN(Para, 64), 0);
                break;
            case 17:    //Hxx
                if (Para != 0)
                {
                    if (Para & 0xF) Ch.GlobalVolSlideSpd = -(Para & 0xF);
                    else if (Para & 0xF0) Ch.GlobalVolSlideSpd = (Para >> 4) & 0xF;
                }
                break;
            case 20:    //Kxx
                if (Para == 0) Ch.KeyOff = Ch.Fading = true;
                else Ch.Delay = Para;
                break;
            case 21:    //Lxx
                if (Ch.Instrument && Ch.Instrument <= NumOfInstruments)
                {
                    if (Instruments[Ch.Instrument - 1].VolType & 0x02)
                        Ch.PanEnvelope = Para;

                    Ch.VolEnvelope = Para;
                    Ch.LxxEffect = true;
                    Ch.Active = true;
                }
                break;
            case 25:    //Pxx
                if (Para != 0)
                {
                    if (Para & 0xF) Ch.PanSlideSpd = -(Para & 0xF);
                    else if (Para & 0xF0) Ch.PanSlideSpd = (Para >> 4) & 0xF;
                }
                break;
            case 27:    //Rxx
                //Ch.RxxCounter = 1;
                //Ch.Delay = SubPara;
                //if ((Para & 0xF0) != 0) Ch.RetrigPara = (Para >> 4) & 0xF;
                if (((Para >> 4) & 0xF) > 0)
                    Ch.RetrigPara = (Ch.RetrigPara&0xF) + (Para&0xF0);
                /*
                if ((Ch.RetrigPara < 6) || (Ch.RetrigPara > 7 && Ch.RetrigPara < 14))
                    Ch.Volume += RxxVolSlideTable[Ch.RetrigPara];
                else
                {
                    switch (Ch.RetrigPara)
                    {
                        case 6:
                            Ch.Volume = (Ch.Volume << 1) / 3;
                            break;
                        case 7:
                            Ch.Volume >>= 1;
                            break;
                        case 14:
                            Ch.Volume = (Ch.Volume * 3) >> 1;
                            break;
                        case 15:
                            Ch.Volume <<= 1;
                            break;
                    }
                }
                */
                break;
            case 29:    //Txx
                if (Para != 0) Ch.TremorPara = Para;
                break;
            case 33:    //Xxx
                if (SubEffect == 0x10)  //X1x
                {
                    if (SubPara != 0) Ch.EFProtaUp = SubPara;
                    Ch.Period -= Ch.EFProtaUp;
                }
                else if (SubEffect == 0x20) //X2x
                {
                    if (SubPara != 0) Ch.EFProtaDn = SubPara;
                    Ch.Period += Ch.EFProtaDn;
                }
                break;
        }

        VolCol:
        //Volume column effects
        switch (VolCmd & 0xF0)
        {
            case 0x80:  //Dx
                Ch.Volume -= VolPara;
                break;
            case 0x90:  //Ux
                Ch.Volume += VolPara;
                break;
            case 0xA0:  //Sx
                Ch.VibratoPara = (Ch.VibratoPara&0xF) + ((VolPara << 4) & 0xF0);
                break;
            case 0xB0:  //Vx
                if (VolPara != 0)
                    Ch.VibratoPara = (Ch.VibratoPara&0xF0) + VolPara;
                break;
            case 0xC0:  //Px
                Ch.Pan = VolPara * 17;
                break;
            case 0xF0:  //Mx
                if (VolPara != 0) Ch.SlideSpd = ((VolPara << 4) & 0xF0) + VolPara;
                break;
            default:    //Vxx
                if (VolCmd >= 0x10 && VolCmd <= 0x50 && !RxxRetrig)
                    Ch.LastVol = Ch.Volume = VolCmd - 0x10;
                break;
        }
    }

    static void ChkNote(Note ThisNote, uint8_t i, bool ByPassDelayChk, bool RxxRetrig = false)
    {
        /*
        Note ThisNote;
        ThisNote.Note = Ch.LastNote;
        ThisNote.Instrument = Ch.LastInstrument;
        ThisNote.VolCmd = Ch.VolCmd | Ch.VolPara;
        ThisNote.Effect = Ch.Effect;
        ThisNote.Parameter = Ch.Parameter;
        */

        //uint8_t Note = ThisNote.Note;
        //if (ThisNote.Note > 96 || ThisNote.Note == 0) Note = Ch.Note;
        //Ch.Note = Note;

        Note ThisNoteOrig = ThisNote;
        if (ByPassDelayChk)
        {
            ThisNote.Note = Ch.LastNote;
            ThisNote.Instrument = Ch.LastInstrument;
            ThisNote.VolCmd = Ch.VolCmd;
            ThisNote.Effect = Ch.Effect;
            ThisNote.Parameter = Ch.Parameter;
        }

        if ((Ch.NoteArpeggio <= 119 && Ch.NoteArpeggio > 0) || Ch.PeriodOfs != 0/*&& (Ch.Effect != 0 || Ch.Parameter == 0)*/)
        {
            Ch.PeriodOfs = 0;
            Ch.NoteArpeggio = 0;
            if (AmigaFreqTable)
                Ch.Period = Ch.TargetPeriod;
        }

        bool Porta = (ThisNote.Effect == 3 || ThisNote.Effect == 5 || ((ThisNote.VolCmd & 0xF0) == 0xF0));

        bool HasNoteDelay = (ThisNote.Effect == 14 && ((ThisNote.Parameter&0xF0) >> 4) == 13 && (ThisNote.Parameter & 0x0F) != 0);

        if (ThisNote.Effect != 4 && ThisNote.Effect != 6 && !Ch.VolVibrato) Ch.VibratoPos = 0;

        if (!HasNoteDelay || ByPassDelayChk)
        {
            //Reference: https://github.com/milkytracker/MilkyTracker/blob/master/src/milkyplay/PlayerSTD.cpp - PlayerSTD::progressRow()

            Ch.Delay = -1;

            int8_t NoteNum = ThisNote.Note;
            uint8_t InstNum = ThisNote.Instrument;
            //bool ValidNote = true;

            //if (InstNum && InstNum <= NumOfInstruments)
            //{
            //    Ch.Instrument = InstNum;
            //}

            uint8_t OldInst = Ch.Instrument;
            int16_t OldSamp = Ch.Sample;

            bool InvalidInstr = true;
            if (InstNum && InstNum <= NumOfInstruments && NoteNum < 97)
            {
                if (Instruments[InstNum - 1].SampleNum > 0)
                {
                    Ch.NextInstrument = InstNum;
                    InvalidInstr = false;
                }
            }

            if (InstNum && InvalidInstr) Ch.NextInstrument = 255;

            bool ValidNote = true;
            bool TrigByNote = false;
            if (NoteNum && NoteNum <97)
            {
                /*
                if (InstNum && InstNum <= NumOfInstruments)
                {
                    Ch.Instrument = InstNum;
                }
                else InstNum = 0;

                bool InvalidInstr = true;
                if (Ch.Instrument && Ch.Instrument <= NumOfInstruments)
                {
                    if (Instruments[Ch.Instrument - 1].SampleNum > 0)
                        InvalidInstr = false;
                }
                */

                if (Ch.NextInstrument == 255)
                {
                    InstNum = 0;
                    Ch.Active = false;
                    Ch.Volume = 0;
                    Ch.Sample = -1;
                    Ch.Instrument = 0;
                }
                else Ch.Instrument = Ch.NextInstrument;

                if (Ch.NextInstrument && Ch.NextInstrument <= NumOfInstruments)
                {
                    Ch.Sample = Instruments[Ch.NextInstrument - 1].SampleMap[NoteNum - 1];

                    if (Ch.Sample != -1 && !Porta)
                    {
                        Sample Smp = Samples[Ch.Sample];
                        int8_t RelNote = Smp.RelNote;
                        int8_t FinalNote = NoteNum + RelNote;

                        if (FinalNote >= 1 && FinalNote <= 119)
                        {
                            Ch.FineTune = Smp.FineTune;
                            Ch.RelNote = RelNote;
                        }
                        else
                        {
                            NoteNum = Ch.Note;
                            ValidNote = false;
                        }
                    }

                    if (ValidNote)
                    {
                        Ch.Note = NoteNum;

                        CalcPeriod(i);

                        if (!Porta)
                        {
                            Ch.Period = Ch.TargetPeriod;
                            Ch.SmpPlaying = Ch.Sample;
                            Ch.AutoVibPos = Ch.AutoVibSweep = 0;
                            Ch.Loop = 0;

                            Ch.StartCount = 0;

                            if (ThisNote.Effect == 9) Ch.Pos = ThisNote.Parameter << 8;
                            else Ch.Pos = 0;

                            if (Ch.Pos > Samples[Ch.SmpPlaying].Length) Ch.Pos = Samples[Ch.SmpPlaying].Length;

                            //NoteTrig = true;
                            if (!Ch.Active && !InstNum)
                            {
                                TrigByNote = true;
                                goto TrigInst;
                            }
                        }
                    }
                }
            }

            //FT2 bug emulation
            if ((Porta || !ValidNote) && InstNum)
            {
                InstNum = Ch.Instrument = OldInst;
                Ch.Sample = OldSamp;
            }

            if (InstNum && Ch.Sample != -1)
            {
                TrigInst:
                if (!RxxRetrig && ThisNoteOrig.Instrument)
                {
                    //Ch.VolFinalL = Ch.VolFinalR = 0;
                    Ch.Volume = TrigByNote ? Ch.LastVol : Ch.LastVol = Samples[Ch.SmpPlaying].Volume;
                    Ch.Pan = Samples[Ch.SmpPlaying].Pan;
                    Ch.TremorMute = false;
                    Ch.TremorTick = 0;
                    //Ch.AutoVibPos = Ch.AutoVibSweep = 0;
                    Ch.TremorPos = 0;
                }
                Ch.VolVibrato = false;

                Ch.FadeTick = Ch.VolEnvelope = Ch.PanEnvelope = 0;

                Ch.InstTrig = Ch.Active = true;
                Ch.KeyOff = Ch.Fading = false;
            }

            if (NoteNum == 97)
                Ch.Fading = true;

            ChkEffectRow(ThisNote, i, ByPassDelayChk, RxxRetrig);
        }
        else
        {
            Ch.Delay = Ch.Parameter & 0x0F;
            if (Porta)
            {
                Ch.Period = Ch.TargetPeriod;
                if (Ch.SmpPlaying != -1) CalcPeriod(i);
            }
        }
    }

    static void ChkEffectTick(uint8_t i, Note ThisNote)
    {
        uint8_t VolCmd = ThisNote.VolCmd;
        uint8_t VolPara = ThisNote.VolCmd & 0x0F;
        uint8_t Effect = ThisNote.Effect;
        uint8_t Para = ThisNote.Parameter;
        uint8_t SubEffect = Para & 0xF0;
        uint8_t SubPara = Para & 0x0F;

        if (Effect != 4 && Effect != 6 && !Ch.VolVibrato) Ch.VibratoPos = 0;
        if (Effect != 27) Ch.RxxCounter = 0;

        uint8_t OnTime, OffTime;

        //Effect column
        switch (Effect)
        {
            case 1: //1xx
                Ch.Period -= Ch.SlideUpSpd * 4;
                break;
            case 2: //2xx
                Ch.Period += Ch.SlideDnSpd * 4;
                break;
            case 3: //3xx
                PortaEffect:
                if (Ch.Period > Ch.TargetPeriod)
                    Ch.Period -= Ch.SlideSpd * 4;
                else if (Ch.Period < Ch.TargetPeriod)
                    Ch.Period += Ch.SlideSpd * 4;
                if (abs(Ch.Period - Ch.TargetPeriod) < Ch.SlideSpd * 4)
                    Ch.Period = Ch.TargetPeriod;
                break;
            case 4: //4xx
                VibratoEffect:
                Ch.VibratoAmp = Ch.VibratoPara & 0xF;
                Ch.VibratoPos += (Ch.VibratoPara >> 4) & 0x0F;
                break;
            case 5: //5xx
                Ch.Volume += Ch.VolSlideSpd;
                goto PortaEffect;
                break;
            case 6: //6xx
                Ch.Volume += Ch.VolSlideSpd;
                goto VibratoEffect;
                break;
            case 7: //7xx
                Ch.TremorAmp = Ch.TremoloPara & 0xF;
                Ch.TremorPos += (Ch.TremoloPara >> 4) & 0x0F;
                break;
            case 8: //8xx
                Ch.Pan = Para;
                break;
            case 10:    //Axx
                Ch.Volume += Ch.VolSlideSpd;
                break;
            case 12:    //Cxx
                Ch.Volume = Para;
                break;
            case 14:    //Exx
                switch (SubEffect)
                {
                    case 0x90:  //E9x
                        if (Ch.Delay <= 0 && SubPara > 0)
                        {
                            ChkNote(ThisNote, i, true);
                            Ch.Delay = SubPara;
                        }
                        break;
                    case 0xC0:  //ECx
                        if (Ch.Delay <= 0)
                            Ch.Volume = 0;
                        break;
                    case 0xD0:  //EDx
                        if (Ch.Delay <= 1 && Ch.Delay != -1 && !Ch.KeyOff) ChkNote(ThisNote, i, true);
                        break;
                }
                break;
            case 17:    //Hxx
                GlobalVol = MAX(MIN(GlobalVol + Ch.GlobalVolSlideSpd, 64), 0);
                break;
            case 20:    //Kxx
                if (Ch.Delay <= 0)
                {
                    Ch.KeyOff = Ch.Fading = true;
                }
                break;
            case 25:    //Pxx
                Ch.Pan += Ch.PanSlideSpd;
                break;
            case 27:    //Rxx
                Ch.RxxCounter ++ ;
                if (Ch.RxxCounter >= (Ch.RetrigPara & 0x0F) - 1)
                {
                    ChkNote(ThisNote, i, true, true);
                    if ((Para & 0xF) > 0)
                        Ch.RetrigPara = (Ch.RetrigPara&0xF0) + (Para&0xF);

                    uint8_t RetrigVol = (Ch.RetrigPara >> 4) & 0x0F;
                    if ((RetrigVol < 6) || (RetrigVol > 7 && RetrigVol < 14))
                        Ch.Volume += RxxVolSlideTable[RetrigVol];
                    else
                    {
                        switch (RetrigVol)
                        {
                            case 6:
                                Ch.Volume = (Ch.Volume + Ch.Volume) / 3;
                                break;
                            case 7:
                                Ch.Volume >>= 1;
                                break;
                            case 14:
                                Ch.Volume = (Ch.Volume * 3) >> 1;
                                break;
                            case 15:
                                Ch.Volume <<= 1;
                                break;
                        }
                    }
                    Ch.RxxCounter = 0;
                }
                break;
            case 29:    //Txx
                Ch.TremorTick ++ ;
                OnTime = ((Ch.TremorPara >> 4) & 0xF) + 1;
                OffTime = (Ch.TremorPara & 0xF) + 1;
                Ch.TremorMute = (Ch.TremorTick > OnTime);
                if (Ch.TremorTick >= OnTime + OffTime) Ch.TremorTick = 0;
                break;
        }

        if (Ch.Delay > -1) Ch.Delay -- ;

        //Volume column effects
        switch (VolCmd & 0xF0)
        {
            case 0x60:  //Dx
                Ch.Volume -= VolPara;
                break;
            case 0x70:  //Ux
                Ch.Volume += VolPara;
                break;
            case 0xB0:  //Vx
                Ch.VibratoAmp = Ch.VibratoPara & 0xF;
                Ch.VibratoPos += (Ch.VibratoPara >> 4) & 0x0F;
                Ch.VolVibrato = true;
                break;
            case 0xD0:  //Lx
                Ch.Pan -= VolPara;
                break;
            case 0xE0:  //Rx
                Ch.Pan += VolPara;
                break;
            case 0xF0:  //Mx
                if (Ch.Period > Ch.TargetPeriod)
                    Ch.Period -= Ch.SlideSpd * 4;
                else if (Ch.Period < Ch.TargetPeriod)
                    Ch.Period += Ch.SlideSpd * 4;
                if (abs(Ch.Period - Ch.TargetPeriod) < Ch.SlideSpd * 4)
                    Ch.Period = Ch.TargetPeriod;
                break;
        }
    }

    static void NextRow()
    {
        if (PatDelay <= 0)
        {
            CurRow ++ ;
            if (PatBreak >= 0 && PatJump >= 0)
            {
                CurRow = PatBreak;
                CurPos = PatJump;
                PatRepeat = RepeatPos = 0;
                RepeatTo = PatBreak = PatJump = -1;
            }
            if (PatBreak >= 0)
            {
                CurRow = PatBreak;
                PatRepeat = RepeatPos = 0;
                PatBreak = RepeatTo = -1;
                CurPos ++ ;
            }
            if (PatJump >= 0)
            {
                CurPos = PatJump;
                CurRow = PatRepeat = RepeatPos = 0;
                RepeatTo = PatJump = -1;
            }
            if (RepeatTo >= 0)
            {
                CurRow = RepeatTo;
                RepeatTo = -1;
            }
            if (CurRow >= *(int16_t *)(PatternData + PatternAddr[OrderTable[CurPos]]))
            {
                //Pattern loop bug emulation
                CurRow = RepeatPos > 0 ? RepeatPos : 0;
                PatRepeat = RepeatPos = 0;
                RepeatTo = -1;
                CurPos ++ ;
            }
            if (CurPos >= SongLength)
            {
                if (Loop)
                    CurPos = RstPos;
                else
                {
                    ResetModule();
                    return;
                }
            }

            int i = 0;
            while (i < NumOfChannels)
            {
                Note ThisNote = GetNote(OrderTable[CurPos], CurRow, i);

                if (ThisNote.Note != 0) Ch.LastNote = ThisNote.Note;
                if (ThisNote.Instrument != 0) Ch.LastInstrument = ThisNote.Instrument;
                Ch.VolCmd = ThisNote.VolCmd;
                Ch.VolPara = ThisNote.VolCmd & 0x0F;
                Ch.Effect = ThisNote.Effect;
                Ch.Parameter = ThisNote.Parameter;

                ChkNote(ThisNote, i, false);
                //if (Ch.Delay > -1) Ch.Delay -- ;

                i ++ ;
            }
        }
        else PatDelay -- ;
    }

    static void NextTick()
    {
        int i = 0;
        Tick ++ ;

        if (Tick >= Speed) {
            Tick = 0;
            NextRow();
            return;
        }

        if (CurRow >= 0)
        {
            while (i < NumOfChannels)
            {
                Note ThisNote = GetNote(OrderTable[CurPos], CurRow, i);

                ChkEffectTick(i, ThisNote);

                i ++ ;
            }
        }
    }

    /*
    static inline void MixAudioOld(int16_t *Buffer, uint32_t Pos)
    {
        int32_t OutL = 0;
        int32_t OutR = 0;
        double Result = 0;

        int i = 0;
        while (i < NumOfChannels)
        {
            if (Ch.Active)
            {
                if (Ch.SmpPlaying != -1 && Ch.SmpPlaying < TotalSampleNum)
                {
                    int32_t ChPos = Ch.Pos;

                    if (Ch.StartCount >= SMP_CHANGE_RAMP)
                    {
                        Ch.PosL16 += Ch.Delta;
                        ChPos += Ch.PosL16 >> INT_ACC;
                        Ch.PosL16 &= INT_MASK;
                    }

                    //Volume ramping
                    if (Ch.VolFinalL != Ch.VolTargetL)
                    {
                        if (abs(Ch.VolFinalL - Ch.VolTargetL) >= abs(Ch.VolRampSpdL))
                            Ch.VolFinalL += Ch.VolRampSpdL;
                        else Ch.VolFinalL = Ch.VolTargetL;
                    }

                    if (Ch.VolFinalR != Ch.VolTargetR)
                    {
                        if (abs(Ch.VolFinalR - Ch.VolTargetR) >= abs(Ch.VolRampSpdR))
                            Ch.VolFinalR += Ch.VolRampSpdR;
                        else Ch.VolFinalR = Ch.VolTargetR;
                    }

                    if (Ch.VolFinalInst != Ch.VolTargetInst)
                    {
                        if (abs(Ch.VolFinalInst - Ch.VolTargetInst) >= abs(Ch.VolRampSpdInst))
                            Ch.VolFinalInst += Ch.VolRampSpdInst;
                        else Ch.VolFinalInst = Ch.VolTargetInst;
                    }

                    //Looping
                    if (Ch.LoopType)
                    {
                        if (ChPos < Ch.LoopStart)
                            Ch.Loop = 0;
                        else if (ChPos >= Ch.LoopEnd)
                        {
                            ChPos = Ch.LoopStart + (ChPos - Ch.LoopStart) % Ch.LoopLeng;
                            Ch.Loop = 1;
                        }
                    }
                    else if (ChPos >= Ch.SmpLeng)
                    {
                        Ch.Active = false;
                        Ch.EndSmp = Ch.Is16Bit ? *(int16_t *)(Ch.Data + ((Ch.SmpLeng - 1) << 1)) : (int16_t)(Ch.Data[Ch.SmpLeng - 1] << 8);
                        Ch.SmpPlaying = -1;
                        Ch.EndCount = 0;
                    }

                    Ch.Pos = ChPos;

                    //Don't mix when there is no sample playing or the channel is muted
                    if (Ch.Muted || Ch.SmpPlaying == -1) goto Continue;

                    if (Ch.StartCount >= SMP_CHANGE_RAMP)
                    {
                        //Interpolation
                        if (Interpolation)
                        {
                            int32_t PrevPos = ChPos;

                            if (ChPos > 0) PrevPos -- ;

                            if (Ch.Loop == 1 && ChPos <= Ch.LoopStart)
                                PrevPos = Ch.LoopEnd - 1;

                            int16_t PrevData;
                            int32_t dy;

                            uint16_t ix = Ch.PosL16 >> 1;

                            if (Ch.Is16Bit)
                            {
                                PrevData = *(int16_t *)(Ch.Data + (PrevPos << 1));
                                dy = *(int16_t *)(Ch.Data + (ChPos << 1)) - PrevData;
                            }
                            else
                            {
                                PrevData = Ch.Data[PrevPos] << 8;
                                dy = (Ch.Data[ChPos] << 8) - PrevData;
                            }

                            Result = (PrevData + ((dy * ix) >> INT_ACC_INTERPOL));
                        }
                        else
                        {
                            if (Ch.Is16Bit) Result = *(int16_t *)(Ch.Data + (ChPos << 1));
                            else Result = (int16_t)(Ch.Data[ChPos] << 8);
                        }

                        if (Ch.StartCount < SMP_CHANGE_RAMP + SMP_CHANGE_RAMP)
                        {
                            Result = Result * (Ch.StartCount - SMP_CHANGE_RAMP) / SMP_CHANGE_RAMP;
                            Ch.StartCount ++ ;
                        }
                        Ch.PrevSmp = Result;
                    }
                    else
                    {
                        Result = Ch.PrevSmp * (SMP_CHANGE_RAMP - Ch.StartCount) / SMP_CHANGE_RAMP;
                        Ch.StartCount ++ ;
                    }

                    Result *= AmpFinal * MasterVol * Ch.VolFinalInst / (64.0 * TOINT_SCL_RAMPING);

                    OutL += Result * (Ch.VolFinalL >> INT_ACC_RAMPING);
                    OutR += Result * (Ch.VolFinalR >> INT_ACC_RAMPING);
                }
                else goto NotActived;
            }
            else
            {
                NotActived:

                Ch.Pos = Ch.PosL16 = 0;
                Ch.PrevSmp = 0;
                Ch.StartCount = 0;
            }

            Continue:

            if (Ch.EndCount < SMP_CHANGE_RAMP)
            {
                Result = Ch.EndSmp * (SMP_CHANGE_RAMP - Ch.EndCount) / SMP_CHANGE_RAMP;
                Ch.EndCount ++ ;

                Result *= AmpFinal * MasterVol * Ch.VolFinalInst / (64.0 * TOINT_SCL_RAMPING);

                OutL += Result * (Ch.VolFinalL >> INT_ACC_RAMPING);
                OutR += Result * (Ch.VolFinalR >> INT_ACC_RAMPING);
            }

            i ++ ;
        }

        Buffer[Pos << 1] = OutL >> 16;
        Buffer[(Pos << 1) + 1] = OutR >> 16;
    }
    */

#define MIXPREFIX\
    int k = 0;\
    while (k < Samples)\
    {\
        int32_t OutL = 0;\
        int32_t OutR = 0;\
        double Result = 0;\
\
        if (Ch.Active)\
        {\
            if (Ch.SmpPlaying != -1 && Ch.SmpPlaying < TotalSampleNum)\
            {\
                int32_t ChPos = Ch.Pos;\
\
                if (Ch.StartCount >= SMP_CHANGE_RAMP)\
                {\
                    Ch.PosL16 += Ch.Delta;\
                    ChPos += Ch.PosL16 >> INT_ACC;\
                    Ch.PosL16 &= INT_MASK;\
                }\
\
                if (Ch.VolFinalL != Ch.VolTargetL)\
                {\
                    if (abs(Ch.VolFinalL - Ch.VolTargetL) >= abs(Ch.VolRampSpdL))\
                        Ch.VolFinalL += Ch.VolRampSpdL;\
                    else Ch.VolFinalL = Ch.VolTargetL;\
                }\
\
                if (Ch.VolFinalR != Ch.VolTargetR)\
                {\
                    if (abs(Ch.VolFinalR - Ch.VolTargetR) >= abs(Ch.VolRampSpdR))\
                        Ch.VolFinalR += Ch.VolRampSpdR;\
                    else Ch.VolFinalR = Ch.VolTargetR;\
                }\
\
                if (Ch.VolFinalInst != Ch.VolTargetInst)\
                {\
                    if (abs(Ch.VolFinalInst - Ch.VolTargetInst) >= abs(Ch.VolRampSpdInst))\
                        Ch.VolFinalInst += Ch.VolRampSpdInst;\
                    else Ch.VolFinalInst = Ch.VolTargetInst;\
                }

#define MIXNOLOOP\
    if (ChPos >= Ch.SmpLeng)\
    {\
        Ch.Active = false;\
        Ch.EndSmp = Ch.Is16Bit ? *(int16_t *)(Ch.Data + ((Ch.SmpLeng - 1) << 1)) : (int16_t)(Ch.Data[Ch.SmpLeng - 1] << 8);\
        Ch.SmpPlaying = -1;\
        Ch.EndCount = 0;\
    }\
    Ch.Pos = ChPos;

#define MIXLOOP\
    if (ChPos < Ch.LoopStart)\
        Ch.Loop = 0;\
    else if (ChPos >= Ch.LoopEnd)\
    {\
        ChPos = Ch.LoopStart + (ChPos - Ch.LoopStart) % Ch.LoopLeng;\
        Ch.Loop = 1;\
    }\
    Ch.Pos = ChPos;

#define MIXPART1(A)\
    if (Ch.Muted || Ch.SmpPlaying == -1) goto A;\
\
    if (Ch.StartCount >= SMP_CHANGE_RAMP)\
    {

#define MIXINTERPOLINIT\
    int32_t PrevPos = ChPos;\
\
    if (ChPos > 0) PrevPos -- ;\
\
    if (Ch.Loop == 1 && ChPos <= Ch.LoopStart)\
        PrevPos = Ch.LoopEnd - 1;\
\
    int16_t PrevData;\
    int32_t dy;\
\
    uint16_t ix = Ch.PosL16 >> 1;

#define MIXINTERPOL16BIT\
    PrevData = *(int16_t *)(Ch.Data + (PrevPos << 1));\
    dy = *(int16_t *)(Ch.Data + (ChPos << 1)) - PrevData;\
    Result = (PrevData + ((dy * ix) >> INT_ACC_INTERPOL));

#define MIXINTERPOL8BIT\
    PrevData = Ch.Data[PrevPos] << 8;\
    dy = (Ch.Data[ChPos] << 8) - PrevData;\
    Result = (PrevData + ((dy * ix) >> INT_ACC_INTERPOL));

#define MIXNEAREST16BIT\
    Result = *(int16_t *)(Ch.Data + (ChPos << 1));

#define MIXNEAREST8BIT\
    Result = (int16_t)(Ch.Data[ChPos] << 8);

#define MIXSUFFIX(A,B)\
                    if (Ch.StartCount < SMP_CHANGE_RAMP + SMP_CHANGE_RAMP)\
                    {\
                        Result = Result * (Ch.StartCount - SMP_CHANGE_RAMP) / SMP_CHANGE_RAMP;\
                        Ch.StartCount ++ ;\
                    }\
                    Ch.PrevSmp = Result;\
                }\
                else\
                {\
                    Result = Ch.PrevSmp * (SMP_CHANGE_RAMP - Ch.StartCount) / SMP_CHANGE_RAMP;\
                    Ch.StartCount ++ ;\
                }\
\
                Result *= AmpFinal * MasterVol * Ch.VolFinalInst / (64.0 * TOINT_SCL_RAMPING);\
\
                OutL = Result * (Ch.VolFinalL >> INT_ACC_RAMPING);\
                OutR = Result * (Ch.VolFinalR >> INT_ACC_RAMPING);\
            }\
            else goto B;\
        }\
        else\
        {\
            B:\
\
            Ch.Pos = Ch.PosL16 = 0;\
            Ch.PrevSmp = 0;\
            Ch.StartCount = 0;\
        }\
\
        A:\
\
        if (Ch.EndCount < SMP_CHANGE_RAMP)\
        {\
            Result = Ch.EndSmp * (SMP_CHANGE_RAMP - Ch.EndCount) / SMP_CHANGE_RAMP;\
            Ch.EndCount ++ ;\
\
            Result *= AmpFinal * MasterVol * Ch.VolFinalInst / (64.0 * TOINT_SCL_RAMPING);\
\
            OutL = Result * (Ch.VolFinalL >> INT_ACC_RAMPING);\
            OutR = Result * (Ch.VolFinalR >> INT_ACC_RAMPING);\
        }\
        else if (!Ch.Active) break;\
\
        Buffer[PosFinal++] += OutL >> 16;\
        Buffer[PosFinal++] += OutR >> 16;\
\
        k ++ ;\
    }


    static inline void MixAudio(int16_t *Buffer, uint32_t Pos, int32_t Samples)
    {
        int i = 0;
        while (i < NumOfChannels)
        {
            int32_t PosFinal = Pos << 1;

            if (!Ch.LoopType)
            {
                if (Ch.Is16Bit)
                {
                    if (Interpolation)
                    {
                        MIXPREFIX

                        MIXNOLOOP

                        MIXPART1(C1)

                        MIXINTERPOLINIT

                        MIXINTERPOL16BIT

                        MIXSUFFIX(C1,N1)
                    }
                    else
                    {
                        MIXPREFIX

                        MIXNOLOOP

                        MIXPART1(C2)

                        MIXNEAREST16BIT

                        MIXSUFFIX(C2,N2)
                    }
                }
                else
                {
                    if (Interpolation)
                    {
                        MIXPREFIX

                        MIXNOLOOP

                        MIXPART1(C3)

                        MIXINTERPOLINIT

                        MIXINTERPOL8BIT

                        MIXSUFFIX(C3,N3)
                    }
                    else
                    {
                        MIXPREFIX

                        MIXNOLOOP

                        MIXPART1(C4)

                        MIXNEAREST8BIT

                        MIXSUFFIX(C4,N4)
                    }
                }
            }
            else
            {
                if (Ch.Is16Bit)
                {
                    if (Interpolation)
                    {
                        MIXPREFIX

                        MIXLOOP

                        MIXPART1(C5)

                        MIXINTERPOLINIT

                        MIXINTERPOL16BIT

                        MIXSUFFIX(C5,N5)
                    }
                    else
                    {
                        MIXPREFIX

                        MIXLOOP

                        MIXPART1(C6)

                        MIXNEAREST16BIT

                        MIXSUFFIX(C6,N6)
                    }
                }
                else
                {
                    if (Interpolation)
                    {
                        MIXPREFIX

                        MIXLOOP

                        MIXPART1(C7)

                        MIXINTERPOLINIT

                        MIXINTERPOL8BIT

                        MIXSUFFIX(C7,N7)
                    }
                    else
                    {
                        MIXPREFIX

                        MIXLOOP

                        MIXPART1(C8)

                        MIXNEAREST8BIT

                        MIXSUFFIX(C8,N8)
                    }
                }
            }
            i ++ ;
        }
    }

    /*
    static void FillBufferOld(int16_t *Buffer)
    {
        int i = 0;

        StartTime = clock();

        while (i < BufferSize)
        {
            if (Playing)
                Timer += TimePerSample;

            if (Timer >= TimePerTick)
            {
                NextTick();
                UpdateChannelInfo();
                Timer = fmod(Timer, TimePerTick);
                //Timer -= TimePerTick;
                UpdateTimer();
            }

            MixAudioOld(Buffer, i);

            i ++ ;
        }

        EndTime = clock();
        ExcuteTime = EndTime - StartTime;
    }
    */

    static void FillBuffer(int16_t *Buffer)
    {
        int i = 0;

        StartTime = clock();

        memset(Buffer, 0, BufferSize<<2);
        while (i < BufferSize)
        {
            if (Playing)
            {
                int MixLength = BufferSize;

                if (i + SampleToNextTick >= BufferSize)
                {
                    MixLength = BufferSize - i;
                    SampleToNextTick -= BufferSize - i;
                }
                else
                {
                    if (SampleToNextTick)
                    {
                        MixLength = SampleToNextTick;
                        SampleToNextTick = 0;
                    }
                    else
                    {
                        NextTick();
                        UpdateChannelInfo();
                        Timer = fmod(Timer, TimePerTick);
                        UpdateTimer();

                        SampleToNextTick = SamplePerTick;
                        MixLength = SampleToNextTick;
                    }

                    if (i + SampleToNextTick >= BufferSize)
                    {
                        MixLength = BufferSize - i;
                        SampleToNextTick -= BufferSize - i;
                    }
                    else SampleToNextTick = 0;
                }

                MixAudio(Buffer, i, MixLength);

                i += MixLength;
            }
            else break;
        }

        EndTime = clock();
        ExcuteTime = EndTime - StartTime;
    }

    void SetLoop(bool LoopSong = true)
    {
        Loop = LoopSong;
    }

    void SetPanMode(int8_t Mode = 0)
    {
        PanMode = Mode;
    }

    void PlayPause(bool Play)
    {
        Playing = Play;
        SDL_PauseAudioDevice(DeviceID, !Play);
    }

    bool IsPlaying()
    {
        return Playing;
    }

    void SetAmp(float Value)
    {
        if (Value >= 0.1 && Value <= 10)
        {
            Amp = Value;
            RecalcAmp();
        }
    }

    void SetStereo(bool UseStereo = false)
    {
        Stereo = UseStereo;
    }

    void SetInterpolation(bool UseInterpol = true)
    {
        Interpolation = UseInterpol;
    }

    void SetPos(int16_t Pos)
    {
        if (Pos < 0) Pos = 0;
        if (Pos >= SongLength) Pos = SongLength - 1;

        ResetChannels();
        ResetPatternEffects();

        Tick = Speed - 1;
        CurRow = -1;
        CurPos = Pos;
    }

    void SetIgnoreF00(bool True)
    {
        IgnoreF00 = True;
    }

    void SetVolume(uint8_t Volume)
    {
        MasterVol = Volume;
    }

    char *GetSongName() {
        return SongName;
    }

    uint8_t *GetPatternOrder()
    {
        return OrderTable;
    }

    int16_t GetSpd()
    {
        return (int16_t)((Speed << 8) | Tempo);
    }

    int32_t GetPos() {
        return (int32_t)(((CurPos & 0xFF) << 24) | ((OrderTable[CurPos] & 0xFF) << 16) | (CurRow & 0xFFFF));
    }

    int32_t GetSongInfo()
    {
        return (SongLength | (NumOfPatterns << 8) | (NumOfChannels << 16) | (NumOfInstruments << 24));
    }

    uint8_t GetActiveChannels()
    {
        if (!SongLoaded) return 0;

        uint8_t Result = 0;
        int i = 0;
        while (i < NumOfChannels)
        {
            if (Ch.Active && Ch.SmpPlaying != -1 && Ch.SmpPlaying < TotalSampleNum) Result ++ ;
            i ++ ;
        }

        return Result;
    }

    long GetExcuteTime()
    {
        return ExcuteTime;
    }

    bool IsLoaded()
    {
        return SongLoaded;
    }

    int16_t GetPatLen(uint8_t PatNum)
    {
        if (PatNum >= NumOfPatterns)
        {
            return 0;
        }
        return *(int16_t *)(PatternData + PatternAddr[PatNum]);
    }

    Note GetNote(int16_t Pos, int16_t Row, uint8_t Col)
    {
        Note ThisNote;
        if (Pos < SongLength)
        {
            while (Row >= *(int16_t *)(PatternData + PatternAddr[OrderTable[Pos]]))
            {
                Row -= *(int16_t *)(PatternData + PatternAddr[OrderTable[Pos]]);
                Pos ++ ;
                if (Pos >= SongLength) goto End;
            }
            while (Row < 0)
            {
                Pos -- ;
                if (Pos < 0) goto End;
                Row += *(int16_t *)(PatternData + PatternAddr[OrderTable[Pos]]);
            }
        }
        else goto End;

        if (Col < NumOfChannels)
        {
            ThisNote = *(Note *)(PatternData + PatternAddr[OrderTable[Pos]] + ROW_SIZE_XM*Row + Col*NOTE_SIZE_XM + 2);
        }
        else
        {
            End:
            ThisNote.Note = 255;
            ThisNote.Instrument = ThisNote.VolCmd = ThisNote.Effect = ThisNote.Parameter = 0;
        }

        return ThisNote;
    }

    static void WriteBufferCallback(void *UserData, uint8_t *Buffer, int Length)
    {
        FillBuffer((int16_t *)Buffer);
    }

    bool PlayModule()
    {
        if (!SongLoaded) return false;

        /*
        int i = 0;
        while (i < 2)
        {
            if (SndBuffer[i] != nullptr)
                free SndBuffer[i]);

            SndBuffer[i] = (int16_t *)malloc(BufferSize);
            if (SndBuffer[i] == nullptr) return false;

            i ++ ;
        }
        */

        SDL_InitSubSystem(SDL_INIT_AUDIO);

        SDL_zero(AudioSpec);

        AudioSpec.freq = SamplingRate;
        AudioSpec.format = AUDIO_S16;
        AudioSpec.channels = 2;
        AudioSpec.samples = BufferSize;
        AudioSpec.callback = WriteBufferCallback;
        DeviceID = SDL_OpenAudioDevice(NULL, 0, &AudioSpec, &ActualSpec, 0);

        if (DeviceID != 0)
        {
            BufferSize = ActualSpec.samples;
            SDL_PauseAudioDevice(DeviceID, 0);
            Playing = true;
        }

        return true;
    }

    bool StopModule()
    {
        SDL_CloseAudioDevice(DeviceID);
        return true;
    }

    void CleanUp()
    {
        Playing = false;
        SongLoaded = false;
        StopModule();

        if (Channels != NULL)
            free(Channels);

        if (PatternData != NULL)
            free(PatternData);

        if (Instruments != NULL)
            free(Instruments);

        if (Samples != NULL)
            free(Samples);

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
