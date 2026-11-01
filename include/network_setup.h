#pragma once
#include <Arduino.h>
#include <Preferences.h>
void networkBegin(Preferences &prefs);
bool networkNeedsSetup();
bool networkSetupStart(const String &zone,bool (*saveZone)(const String &));
void networkSetupStop();
void networkSetupPoll();
bool networkSetupActive();
const char *networkSetupSsid();
const char *networkSetupPassword();
const char *networkSetupMessage();
uint32_t networkSetupRevision();
void networkSetupStatus();
