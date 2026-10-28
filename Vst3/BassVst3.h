#ifndef BASSVST3_H
#define BASSVST3_H

// Drop-in front end for the BASS_VST_* functions used by the program.
// A path ending in ".vst3" (optionally "|classIndex") is hosted by Vst3Host and
// connected to BASS; every other call is forwarded to the real bass_vst library.

#include <bass.h>
#include <bass_vst.h>
#include <string>

namespace bv {

DWORD ChannelSetDSP(DWORD chHandle, const void *dllFile, DWORD flags, int priority);
BOOL  ChannelRemoveDSP(DWORD chHandle, DWORD vstHandle);
DWORD ChannelCreate(DWORD freq, DWORD chans, const void *dllFile, DWORD flags);
BOOL  ChannelFree(DWORD vstHandle);

BOOL  GetInfo(DWORD vstHandle, BASS_VST_INFO *ret);
BOOL  EmbedEditor(DWORD vstHandle, void *parentWindow);

int   GetParamCount(DWORD vstHandle);
float GetParam(DWORD vstHandle, int paramIndex);
BOOL  SetParam(DWORD vstHandle, int paramIndex, float value);

char *GetChunk(DWORD vstHandle, BOOL isPreset, DWORD *length);
DWORD SetChunk(DWORD vstHandle, BOOL isPreset, const char *chunk, DWORD length);

int   GetProgram(DWORD vstHandle);
BOOL  SetProgram(DWORD vstHandle, int programIndex);

BOOL  SetBypass(DWORD vstHandle, BOOL bypass);

BOOL  ProcessEvent(DWORD vstHandle, DWORD midiCh, DWORD event, DWORD param);
BOOL  ProcessEventRaw(DWORD vstHandle, const void *event, DWORD length);

// true when the handle belongs to a VST3 plug-in
bool  isVst3Handle(DWORD vstHandle);

// why the last VST3 load failed (empty when it did not); every failure is also written to ~/.PTHKKaraoke/vst3.log
std::string lastError();

} // namespace bv

#endif // BASSVST3_H
