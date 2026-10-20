#pragma once
#include <stdint.h>
bool tickAudioBegin(int volume);
void tickAudioPlay();
bool tickAudioChime();
uint32_t tickAudioChimeCount();
void tickAudioEnable();
void tickAudioSilence();
void tickAudioVolume(int volume);
bool tickAudioReady();
void tickAudioDiagnostics();
bool tickAudioTest();
uint32_t tickAudioCount();
uint32_t tickAudioDropped();
