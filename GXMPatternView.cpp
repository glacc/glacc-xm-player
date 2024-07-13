#include "GXMPlayer.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <malloc.h>
#include <memory.h>
#include <sys/ioctl.h>
#include <unistd.h>

namespace GXMPatternView
{
    static bool Init = false;
    static bool Detailed = true;
    static bool WithPatNum = false;
    static uint8_t ViewerRange = 0;
    static int16_t OffsetY = 0;
    static int8_t NumOfChannels = 0;
    static int8_t MaxCols = 0;
    static int8_t CurPos = 0;
    static int8_t Lines;

    static int WinWidth, WinHeight;

    static char Empty[2];
    static char *OutputBuffer;
    static uint8_t *OrderTable;

    static char NoteCharacters[12][3] =
    {
        "C-", "C#", "D-", "D#", "E-", "F-", "F#", "G-", "G#", "A-", "A#", "B-"
    };

    static char VolCmdCharacter[10] =
    {
        '-', '+', 'd', 'u', 'S', 'V', 'P', 'L', 'R', 'M'
    };

    static char HexCharacters[36] =
    {
        '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'A', 'B', 'C', 'D', 'E', 'F'
    };

    void SetDetailed(bool True = true)
    {
        Detailed = True;
    }

    static void CalcViewerSize()
    {
        struct winsize WinSize;
        ioctl(STDOUT_FILENO, TIOCGWINSZ, &WinSize);

        WinWidth = WinSize.ws_col;
        WinHeight = WinSize.ws_row;

        MaxCols = (WinWidth - (WithPatNum ? 12 : 4) - 2) / (Detailed ? 16 : 4);

        if (MaxCols > NumOfChannels) MaxCols = NumOfChannels;

        if (CurPos > NumOfChannels - MaxCols) CurPos = NumOfChannels - MaxCols;

        if (CurPos < 0) CurPos = 0;

        ViewerRange = (WinHeight - OffsetY - 3) >> 1;

        Lines = (ViewerRange << 1) + 2;
    }

    void MoveNext(bool True = true)
    {
        if (True) CurPos ++ ;
        else CurPos -- ;
    }

    void InitViewer(int HeadSize, bool PatternDisplay = false, bool DetailedView = true)
    {
        if (!GXMPlayer::IsLoaded()) return;

        CurPos = 0;

        OffsetY = HeadSize;

        WithPatNum = PatternDisplay;

        int32_t SongInfo = GXMPlayer::GetSongInfo();
        uint8_t NumOfChn = (SongInfo >> 16) & 0xFF;
        NumOfChannels = NumOfChn;

        Detailed = DetailedView;

        CalcViewerSize();

        OutputBuffer = (char *)malloc(50000);
        //OutputBuffer = (char *)malloc(32768);
        Init = true;
    }


    #pragma GCC push_options
    #pragma GCC optimize ("O0")

    char *DrawPatternView()
    {
        if (!Init || !GXMPlayer::IsLoaded()) return Empty;

        CalcViewerSize();

        if (ViewerRange < 2) return Empty;
        if (MaxCols <= 0) return Empty;

        OrderTable = GXMPlayer::GetPatternOrder();
        int SongLeng = GXMPlayer::GetSongInfo() & 0xFF;

        int32_t Info = GXMPlayer::GetPos();

        uint8_t Pos = ((Info & 0xFF000000) >> 24);
        uint8_t Pat = ((Info & 0x00FF0000) >> 16);
        int16_t Row = (int16_t)(Info & 0x0000FFFF);

        int RelRow = Row - ViewerRange;

        int PatternRange = (ViewerRange >> 2);
        if (PatternRange < 2) PatternRange = 2;
        int PatStart = Row - PatternRange - 1;
        int PatEnd = Row + PatternRange;

        char *WritePtr = OutputBuffer;

        memset(OutputBuffer, ' ', 50000);

        int PosY = 0;
        int PosX_Max = CurPos + MaxCols;
        while (PosY < Lines)
        {
            if (PosY != 0)
            {
                if (RelRow == Row) *(WritePtr) = '>';
                WritePtr ++ ;

                if (RelRow >= 0 && RelRow < GXMPlayer::GetPatLen(Pat))
                {
                    sprintf(WritePtr, "%02X", RelRow);
                    *(WritePtr + 2) = ' ';
                }
                else
                {
                    sprintf(WritePtr, "-");
                    *(WritePtr + 1) = ' ';
                }
            }
            WritePtr += 3;

            int PosX = CurPos;
            while (PosX < PosX_Max)
            {
                if (PosY != 0)
                {
                    GXMPlayer::Note ThisNote = GXMPlayer::GetNotePat(Pos, RelRow, PosX);
                    if (ThisNote.Note != 255)
                    {
                        //Note
                        if (ThisNote.Note == 97)
                        {
                            sprintf(WritePtr, "===");
                            *(WritePtr + 3) = ' ';
                        }
                        else if (ThisNote.Note)
                        {
                            int8_t NoteNum = ThisNote.Note % 12;
                            int8_t NoteOctave = ThisNote.Note / 12;
                            sprintf(WritePtr, "%s", NoteCharacters[NoteNum]);
                            *(WritePtr + 2) = '0' + NoteOctave;
                        }
                        else if (!Detailed)
                        {
                            if (ThisNote.Instrument)
                            {
                                sprintf(WritePtr + 1, "%02X", ThisNote.Instrument);
                                *(WritePtr + 3) = ' ';
                            }
                            else if (ThisNote.VolCmd >= 0x10)
                            {
                                if (ThisNote.VolCmd <= 0x50)
                                {
                                    sprintf(WritePtr, "v%02d", ThisNote.VolCmd - 0x10);
                                    *(WritePtr + 3) = ' ';
                                }
                                else
                                {
                                    uint8_t VolCmdTyp = (ThisNote.VolCmd >> 4) & 0x0F;
                                    uint8_t SubPara = ThisNote.VolCmd & 0x0F;
                                    *(WritePtr + 1) = VolCmdCharacter[VolCmdTyp - 6];
                                    *(WritePtr + 2) = HexCharacters[SubPara];
                                }
                            }
                            else
                            {
                                if (ThisNote.Effect == 0 && ThisNote.Parameter == 0)
                                {
                                    sprintf(WritePtr, "...");
                                    *(WritePtr + 3) = ' ';
                                }

                                if (ThisNote.Effect < 10)
                                {
                                    if (ThisNote.Effect != 0 || ThisNote.Parameter != 0)
                                        *WritePtr = '0' + ThisNote.Effect;
                                    else
                                        *WritePtr = '.';
                                }
                                else
                                {
                                    *WritePtr = 'A' - 10 + ThisNote.Effect;
                                }

                                if (ThisNote.Effect != 0 || ThisNote.Parameter != 0)
                                {
                                    sprintf(WritePtr + 1, "%02X", ThisNote.Parameter);
                                    *(WritePtr + 3) = ' ';
                                }
                            }
                        }
                        else
                        {
                            sprintf(WritePtr, "...");
                            *(WritePtr + 3) = ' ';
                        }

                        WritePtr += 4;

                        //Detailed info
                        if (Detailed)
                        {
                            //Instrument
                            if (ThisNote.Instrument)
                                sprintf(WritePtr, "%02X", ThisNote.Instrument);
                            else sprintf(WritePtr, "..");

                            *(WritePtr + 2) = ' ';
                            WritePtr += 3;

                            //Volume
                            if (ThisNote.VolCmd)
                            {
                                if (ThisNote.VolCmd >= 0x10)
                                {
                                    if (ThisNote.VolCmd <= 0x50)
                                    {
                                        sprintf(WritePtr, "%02d", ThisNote.VolCmd - 0x10);
                                        *(WritePtr + 2) = ' ';
                                    }
                                    else
                                    {
                                        uint8_t VolCmdTyp = (ThisNote.VolCmd >> 4) & 0x0F;
                                        uint8_t SubPara = ThisNote.VolCmd & 0x0F;
                                        *WritePtr = VolCmdCharacter[VolCmdTyp - 6];
                                        *(WritePtr + 1) = HexCharacters[SubPara];
                                    }
                                }
                                else
                                {
                                    sprintf(WritePtr, "..");
                                    *(WritePtr + 2) = ' ';
                                }
                            }
                            else
                            {
                                sprintf(WritePtr, "..");
                                *(WritePtr + 2) = ' ';
                            }

                            WritePtr += 3;

                            //Effect
                            if (ThisNote.Effect != 0 || ThisNote.Parameter != 0)
                            {
                                sprintf(WritePtr + 1, "%02X", ThisNote.Parameter);
                                *(WritePtr + 3) = ' ';
                            }

                            if (ThisNote.Effect < 10)
                            {
                                if (ThisNote.Effect != 0 || ThisNote.Parameter != 0)
                                    *WritePtr = '0' + ThisNote.Effect;
                                else
                                    *WritePtr = '-';
                            }
                            else
                            {
                                *WritePtr = 'A' - 10 + ThisNote.Effect;
                            }

                            if (ThisNote.Effect == 0 && ThisNote.Parameter == 0)
                            {
                                sprintf(WritePtr, "...");
                                *(WritePtr + 3) = ' ';
                            }

                            WritePtr += 6;
                        }
                    }
                    else
                    {
                        WritePtr += Detailed ? 16 : 4;
                    }

                    if (PosX < PosX_Max - 1 && Detailed)
                    {
                        if (RelRow == Row)
                        {
                            sprintf(WritePtr - 3, "---");
                            *(WritePtr) = ' ';
                        }
                        else *(WritePtr - 2) = '|';
                    }
                    else if (RelRow == Row)
                    {
                        if (Detailed) *(WritePtr - 3) = '<';
                        else *(WritePtr - 1) = PosX < PosX_Max - 1 ? '-' : '<';
                    }
                }
                else
                {
                    if (Detailed)
                    {
                        sprintf(WritePtr + 1, "Ch %2d", PosX + 1);
                        *(WritePtr+6) = ' ';
                    }
                    else
                    {
                        sprintf(WritePtr + 2, "%2d", PosX + 1);
                        *(WritePtr+4) = ' ';
                    }
                    WritePtr += Detailed ? 16 : 4;
                }

                PosX ++ ;
            }

            if (WithPatNum)
            {
                if (!Detailed) WritePtr ++ ;
                else WritePtr -- ;

                if (RelRow == PatStart)
                {
                    sprintf(WritePtr, "Pos|Pat");
                    *(WritePtr+7) = ' ';
                    WritePtr += 7;
                }
                else if (RelRow > PatStart && RelRow <= PatEnd)
                {
                    int PatNum = Pos + RelRow - Row;

                    if (RelRow == Row) *WritePtr = '>';
                    WritePtr ++;

                    if (PatNum >= 0 && PatNum < SongLeng)
                    {
                        sprintf(WritePtr, "%02X|%02X", (uint8_t)PatNum, OrderTable[PatNum]);
                        *(WritePtr+5) = RelRow == Row ? '<' : ' ';
                    }
                    else *(WritePtr+2) = '|';

                    WritePtr += 6;
                }
                else WritePtr += 7;
            }

            if (PosY < Lines - 1)
            {
                *WritePtr = '\n';
                WritePtr ++ ;
            }
            else *WritePtr = 0;

            if (PosY != 0) RelRow ++ ;
            PosY ++ ;
        }

        return OutputBuffer;
    }
    #pragma GCC pop_options
}
