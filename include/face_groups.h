#pragma once
#include "assets.h"
void groupsScan();
int groupsCount();
const char *groupName(int index);
const char *groupId(int index);
int groupCurrent();
int groupFaceCount();
const Face &groupFace(int index);
const char *groupError();
bool groupSelect(int index);
bool groupUploadBegin(const char *name,uint32_t bytes,const char *sha256="");
void groupSdStatus();
void groupSdInfo(const char *name);
bool groupUploadChunk(uint32_t offset,uint32_t bytes);
bool groupUploadFinish();
void groupUploadAbort();
bool groupUploading();
void groupUploadPoll();
uint32_t groupSdReadBytes();
uint32_t groupCachedBytes();

bool groupsStorageReady();
void groupsMaintenanceReset();
bool groupsValidateFile(const char *path);
