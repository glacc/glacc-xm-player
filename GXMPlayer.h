//Version 200430

#ifndef LIBGXMPLAY_H_INCLUDED
#define LIBGXMPLAY_H_INCLUDED

#include <stdint.h>

namespace GXMPlayer
{
    static const int SMP_RATE = 44100;
    static const int BUFFER_SIZE = 4096;

    struct Note
    {
        uint8_t Note;
        uint8_t Instrument;
        uint8_t VolCmd;
        uint8_t Effect;
        uint8_t Parameter;
    };

    bool LoadModule(uint8_t *SongDataOrig, uint32_t SongDataLeng, bool UsingInterpolation = true, bool UseStereo = true, bool LoopSong = true, int BufSize = BUFFER_SIZE, int SmpRate = SMP_RATE);
    bool PlayModule();
    bool StopModule();
    void ResetModule();
    void PlayPause(bool Play);
    bool IsPlaying();
    void SetPos(int16_t Pos);
    void SetVolume(uint8_t Volume);
    void SetAmp(float Value);
    void SetInterpolation(bool UseInterpol = true);
    void SetStereo(bool UseStereo = true);
    void SetLoop(bool LoopSong = true);
    void SetPanMode(int8_t Mode = 0);
    void SetIgnoreF00(bool True);

    bool IsLoaded();
    int16_t GetSpd();
    int32_t GetPos();
    int32_t GetSongInfo();
    int16_t GetPatLen(uint8_t PatNum);
    Note GetNotePat(int16_t Pos, int16_t Row, uint8_t Col);
    uint8_t *GetPatternOrder();
    char *GetSongName();
    long GetExcuteTime();
    uint8_t GetActiveChannels();

    void CleanUp();
}

#endif
