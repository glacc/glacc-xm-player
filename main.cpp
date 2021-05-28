#include <termios.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/poll.h>
#include <iostream>
#include <stdio.h>
#include <fstream>
#include <iostream>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <signal.h>
#include "GXMPlayer.h"
#include "GXMPatternView.h"

using namespace std;
using namespace GXMPlayer;
using namespace GXMPatternView;

static ifstream InputFile;
static char *FileData;

static bool UseStereo = true;
static bool UseInterpolation = false;
static bool UsePatternView = false;
static bool DetailedView = true;
static bool UseLoop = true;
static bool IgnoreF00 = true;
static int8_t PanMode = 0;

static char StatChars[9] = "        ";

//static int SleepTime = 25000;

static int BufTime = 100;
static int BufSize = 4096;
static int SmpRate = 44100;
static double Amp = 1;

static int RefreshInterval = 100000;

static char *FileName;
static float CPUUsageSmooth = 0;

//From openmpt123
static termios saved_attributes;

static void reset_input_mode() {
	tcsetattr( STDIN_FILENO, TCSANOW, &saved_attributes );
}

static void set_input_mode() {
	termios tattr;
	if ( !isatty( STDIN_FILENO ) ) {
		return;
	}
	tcgetattr( STDIN_FILENO, &saved_attributes );
	atexit( reset_input_mode );
	tcgetattr( STDIN_FILENO, &tattr );
	tattr.c_lflag &= ~( ICANON | ECHO );
	tattr.c_cc[VMIN] = 1;
	tattr.c_cc[VTIME] = 0;
	tcsetattr( STDIN_FILENO, TCSAFLUSH, &tattr );
}

char GetKeyPress()
{
    while ( true ) {
        //From openmpt123
        pollfd pollfds;
        pollfds.fd = STDIN_FILENO;
        pollfds.events = POLLIN;
        poll(&pollfds, 1, 0);
        if ( !( pollfds.revents & POLLIN ) ) {
            break;
        }
        char c = 0;
        if ( read( STDIN_FILENO, &c, 1 ) != 1 ) {
            break;
        }

        return c;
    }
    return 0;
}

void ProcessArguments(int argc, char *argv[])
{
    int Parsing = 0;
    for (int i = 0; i < argc; i ++)
    {
        if (i == 1)
        {
            FileName = argv[i];
            InputFile.open(FileName/*, ios_base::binary*/);
        }
        if (i > 1)
        {
            if (!Parsing)
            {
                if (strcmp(argv[i], "-i") == 0)
                    UseInterpolation = true;

                if (strcmp(argv[i], "--no-stereo") == 0)
                    UseStereo = false;

                if (strcmp(argv[i], "--no-repeat") == 0)
                    UseLoop = false;

                if (strcmp(argv[i], "--process-f00") == 0)
                    IgnoreF00 = false;

                if (strcmp(argv[i], "--pattern") == 0)
                    UsePatternView = true;

                if (strcmp(argv[i], "--pattern2") == 0)
                {
                    UsePatternView = true;
                    DetailedView = false;
                }

                if (strcmp(argv[i], "-s") == 0)
                    Parsing = 1;

                if (strcmp(argv[i], "-b") == 0)
                    Parsing = 2;

                if (strcmp(argv[i], "-a") == 0)
                    Parsing = 3;

                if (strcmp(argv[i], "--pan-mode") == 0)
                    Parsing = 4;

                if (strcmp(argv[i], "-r") == 0)
                    Parsing = 5;
            }
            else
            {
                if (Parsing == 1)
                {
                    SmpRate = atoi(argv[i]);
                    if (SmpRate < 8000) SmpRate = 8000;
                    if (SmpRate > 192000) SmpRate = 192000;
                }

                if (Parsing == 2)
                {
                    BufTime = atoi(argv[i]);
                    if (BufTime < 1) BufSize = 1;
                    if (BufTime > 1000) BufSize = 1000;
                }

                if (Parsing == 3)
                    Amp = atof(argv[i]);

                if (Parsing == 4)
                    PanMode = atof(argv[i]);

                if (Parsing == 5)
                {
                    RefreshInterval = atoi(argv[i])*1000;
                    if (RefreshInterval < 10000) RefreshInterval = 10000;
                    if (RefreshInterval > 200000) RefreshInterval = 200000;
                }

                Parsing = 0;
            }
        }
    }
}

void ExitSig(int Sig)
{
    reset_input_mode();
    cout << "\u001b[0m\u001b[?25h" << endl;

    exit(0);
}

int main(int argc, char *argv[])
{
    signal(SIGINT, ExitSig);

    ProcessArguments(argc, argv);

    if (argc == 1)
    {
        cout << "\ngxm file [options]\n" << endl;
        cout << "    -i               Use interpolation    " << endl;
        cout << "    --no-stereo      Disable stereo" << endl;
        cout << "    --no-repeat      Replay whole song after finishing playing the song\n" << endl;
        cout << "    --process-f00    Don't ignore F00 command" << endl;
        cout << "    --pan-mode mode  Set panning mode (0: FT2, other: Linear, Default: 0)\n" << endl;
        cout << "    --pattern        Enable pattern viewer (Detailed)" << endl;
        cout << "    --pattern2       Enable pattern viewer\n" << endl;
        cout << "    -s rate          Set sampling rate (Default: 44100, 8000 < rate < 192000)" << endl;
        cout << "    -b size          Set buffer size in ms (Default: 100, 1 < size < 1000)" << endl;
        cout << "    -a amp           Set amplifier (Default: 1.0, 0.1 < amp < 10)\n" << endl;
        cout << "    -r interval      Set info refreshing rate (Default: 100, 10 < interval < 200)\n" << endl;
        cout << "Controls: \n" << endl;
        cout << "    a/d              Prev/Next pattern" << endl;
        cout << "    z/c              Pattern viewer left/right" << endl;
        cout << "    r                Replay" << endl;
        cout << "    l                Repeat on/off" << endl;
        cout << "    s                Stereo on/off" << endl;
        cout << "    i                Interpolation on/off" << endl;
        cout << "    q                Quit\n" << endl;
        cout << "Version 210110" << endl;
        cout << "Lib Version 200110" << endl;
        cout << "Glacc 2021\n" << endl;
        return 0;
    }

    BufSize = SmpRate * BufTime / 1000;

    struct winsize WinSize;
    ioctl(STDOUT_FILENO, TIOCGWINSZ, &WinSize);

    for (int i = 0; i < WinSize.ws_row; i ++)
    {
        cout << "\n";
    }

    //cout << endl;
    cout << "\u001b[2J";
    cout << "\u001b[0;0H";

    if (!InputFile)
    {
        cout << "Failed to open file " << FileName << endl;
        return 0;
    }
    InputFile.seekg(0, ios_base::end);
    int FileSize = InputFile.tellg();

    FileData = (char *)malloc(FileSize);
    InputFile.seekg(0, ios_base::beg);
    InputFile.read(FileData, FileSize);

    if (!GXMPlayer::LoadModule((uint8_t *)FileData, FileSize, UseInterpolation, UseStereo, UseLoop, BufSize, SmpRate) || !GXMPlayer::PlayModule())
    {
        cout << "Failed to load file." << endl;
        return 0;
    }
    GXMPlayer::SetAmp(Amp);
    GXMPlayer::SetIgnoreF00(IgnoreF00);
    GXMPlayer::SetPanMode(PanMode);

    cout << "Glacc XM Player Version 210110 by Glacc " << endl;
    cout << "File: " << FileName << endl;
    cout << "Song Name: " << GXMPlayer::GetSongName() << endl;

    int32_t SongInfo = GXMPlayer::GetSongInfo();
    uint8_t NumOfInstr = (SongInfo >> 24) & 0xFF;
    uint8_t NumOfChn = (SongInfo >> 16) & 0xFF;
    uint8_t NumOfPat = (SongInfo >> 8) & 0xFF;
    uint8_t SongLeng = SongInfo & 0xFF;
    printf("Length: %d, Channels: %d, Instruments: %d, Patterns: %d\n\n", SongLeng, NumOfChn, NumOfInstr, NumOfPat);

    InputFile.close();
    free(FileData);

    cout << "\u001b[s";
    //cout << "\u001b[=7l";
    cout << "\u001b[?25l";

    if (UsePatternView) GXMPatternView::InitViewer(12, true, DetailedView);

    //int SleepTime = BufTime*1000;
    //if (SleepTime > 10000) SleepTime = 10000;

    bool Redraw = false;

    float CPUUsageConst = (CLOCKS_PER_SEC*((float)BufSize/SmpRate))*2;
    float CPUUsageSmoothness = (30/(RefreshInterval/10000.0));

    set_input_mode();
    while (true)
    {
        int32_t Info = GXMPlayer::GetPos();
        int16_t Spd = GXMPlayer::GetSpd();

        uint8_t Pos = ((Info & 0xFF000000) >> 24);
        uint8_t Pat = ((Info & 0x00FF0000) >> 16);
        int16_t Row = (int16_t)(Info & 0x0000FFFF);

        uint8_t Speed = Spd >> 8;
        uint8_t Tempo = Spd & 0xFF;

        StatChars[0] = GXMPlayer::IsPlaying() ? ' ' : 'P';
        StatChars[2] = UseInterpolation ? 'I' : ' ';
        StatChars[4] = UseStereo ? 'S' : ' ';
        StatChars[6] = UseLoop ? 'L' : ' ';

        float CPUUsage = GXMPlayer::GetExcuteTime() * 100.0 / CPUUsageConst;
        CPUUsageSmooth -= (CPUUsageSmooth - CPUUsage) / CPUUsageSmoothness;

        if (GXMPlayer::IsPlaying() || Redraw)
        {
            cout << "\u001b[u";
            cout << StatChars << endl;

            printf("Pos: %d, Pat: %d, Row: %d      \nTempo: %d, Tick/Row: %d      \nActive Channels: %d   \nMixer CPU Usage: %.2f%%     \n\n", Pos, Pat, Row, Tempo, Speed, GXMPlayer::GetActiveChannels(), CPUUsageSmooth);

            if (UsePatternView) cout << GXMPatternView::DrawPatternView();

            Redraw = false;
        }

        char KeyPress = GetKeyPress();

        if (KeyPress) Redraw = true;

        switch (KeyPress)
        {
            case 'q':
                GXMPlayer::StopModule();
                GXMPlayer::CleanUp();
                goto Exit;
                break;
            case ' ':
                GXMPlayer::PlayPause(!GXMPlayer::IsPlaying());
                break;
            //case 'p':
            //    DetailedView = !DetailedView;
            //    GXMPatternView::SetDetailed(DetailedView);
            //    break;
            case 'a':
                if (Pos >= 0) GXMPlayer::SetPos(Pos - 1);
                break;
            case 'd':
                if (Pos <= SongLeng - 1) GXMPlayer::SetPos(Pos + 1);
                break;
            case 'z':
                GXMPatternView::MoveNext(false);
                break;
            case 'c':
                GXMPatternView::MoveNext(true);
                break;
            case 'r':
                GXMPlayer::ResetModule();
                break;
            case 'i':
                UseInterpolation = !UseInterpolation;
                GXMPlayer::SetInterpolation(UseInterpolation);
                break;
            case 'l':
                UseLoop = !UseLoop;
                GXMPlayer::SetLoop(UseLoop);
                break;
            case 's':
                UseStereo = !UseStereo;
                GXMPlayer::SetStereo(UseStereo);
                break;
        }

        if (!Redraw) usleep(RefreshInterval);
    }

    Exit:
    ExitSig(0);

    return 0;
}
