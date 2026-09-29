/*
 * ShellCommandsAudio.c — PR-G-audio-5：play [path]；无参=蜂鸣；WAV=PCM 48k/16
 */
#include "ShellPrivate.h"
#include "Console.h"
#include "HalDevices.h"
#include "FileSystem.h"
#include "PhysicalMemory.h"
#include "Fat.h"

#define PLAY_MAX_BYTES  (64u * 1024u)
#define PLAY_MAX_PAGES  16u
#define PLAY_CHUNK_FR   2048u /* 立体声帧/块；≤ HDA PCM 缓冲 */

static UINT8 *gPlayBuf;

static UINT32 Rd32(const UINT8 *P) {
    return (UINT32)P[0] | ((UINT32)P[1] << 8) | ((UINT32)P[2] << 16) |
           ((UINT32)P[3] << 24);
}

static UINT16 Rd16(const UINT8 *P) {
    return (UINT16)P[0] | ((UINT16)P[1] << 8);
}

static int ParseWav(const UINT8 *Buf, UINTN Sz, const INT16 **OutPcm,
                    UINTN *OutBytes, UINT32 *OutCh) {
    UINTN Off;
    UINT32 Rate, DataBytes;
    UINT16 Fmt, Ch, Bits;
    const UINT8 *Data;

    if (Sz < 44u || Rd32(Buf) != 0x46464952u || Rd32(Buf + 8) != 0x45564157u) {
        return 0;
    }
    Off = 12u;
    Fmt = 0;
    Ch = 0;
    Bits = 0;
    Rate = 0;
    Data = 0;
    DataBytes = 0;
    while (Off + 8u <= Sz) {
        UINT32 Id = Rd32(Buf + Off);
        UINT32 Csz = Rd32(Buf + Off + 4u);
        const UINT8 *Body = Buf + Off + 8u;
        if (Off + 8u + Csz > Sz) {
            break;
        }
        if (Id == 0x20746D66u && Csz >= 16u) {
            Fmt = Rd16(Body);
            Ch = Rd16(Body + 2);
            Rate = Rd32(Body + 4);
            Bits = Rd16(Body + 14);
        } else if (Id == 0x61746164u) {
            Data = Body;
            DataBytes = Csz;
        }
        Off += 8u + Csz;
        if (Csz & 1u) {
            Off++;
        }
    }
    if (Fmt != 1u || Bits != 16u || (Ch != 1u && Ch != 2u) || Rate != 48000u ||
        Data == 0 || DataBytes < 4u) {
        return 0;
    }
    *OutPcm = (const INT16 *)(UINTN)Data;
    *OutBytes = DataBytes;
    *OutCh = Ch;
    return 1;
}

static int PlayPcmChunks(const INT16 *StereoOrMono, UINTN Frames, UINT32 Ch) {
    INT16 Chunk[PLAY_CHUNK_FR * 2u];
    UINTN Done = 0;

    while (Done < Frames) {
        UINTN N = Frames - Done;
        UINTN I;
        if (N > PLAY_CHUNK_FR) {
            N = PLAY_CHUNK_FR;
        }
        if (Ch == 2u) {
            for (I = 0; I < N * 2u; I++) {
                Chunk[I] = StereoOrMono[Done * 2u + I];
            }
        } else {
            for (I = 0; I < N; I++) {
                INT16 S = StereoOrMono[Done + I];
                Chunk[I * 2u] = S;
                Chunk[I * 2u + 1u] = S;
            }
        }
        if (!HalAudioPlayPcm(Chunk, N * 4u, 48000u, 2u, 16u)) {
            return 0;
        }
        Done += N;
    }
    return 1;
}

static void CommandPlay(int Argc, char **Argv) {
    UINTN Got;
    const INT16 *Pcm;
    UINTN Bytes;
    UINT32 Ch;
    int Err;

    if (!HalAudioProbe()) {
        ConsoleWrite("play: no audio\n");
        return;
    }
    if (Argc < 2) {
        HalAudioBeep();
        ConsoleWrite("play: beep\n");
        return;
    }
    if (gPlayBuf == 0) {
        gPlayBuf = (UINT8 *)PhysicalMemoryAllocatePages(PLAY_MAX_PAGES);
        if (!gPlayBuf) {
            ConsoleWrite("play: oom\n");
            return;
        }
    }
    Got = 0;
    Err = FileSystemReadFile(Argv[1], gPlayBuf, PLAY_MAX_BYTES, &Got);
    if (Err != FAT_OK || Got < 12u) {
        ConsoleWrite("play: read fail\n");
        return;
    }
    if (!ParseWav(gPlayBuf, Got, &Pcm, &Bytes, &Ch)) {
        ConsoleWrite("play: need WAV PCM 48k/16/1-2ch\n");
        return;
    }
    if (!PlayPcmChunks(Pcm, Bytes / (Ch * 2u), Ch)) {
        ConsoleWrite("play: pcm fail\n");
        return;
    }
    ConsoleWrite("play: ok\n");
}

void ShellCommandsAudioRegister(void) {
    ConsoleRegister("play", "play [WAV] (no arg=beep; 48k/16 PCM)", CommandPlay);
    ConsoleRegisterAlias("play", "beep");
}
