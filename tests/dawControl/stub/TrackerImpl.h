#pragma once
#include "SdTextFile.h"
struct PlatformStub { FileSystem* getFileSystem(){ return nullptr; } };
struct TrackerImpl { static TrackerImpl* getInstance(){ static TrackerImpl t; return &t; } PlatformStub* getPlatform(){ static PlatformStub p; return &p; } };
