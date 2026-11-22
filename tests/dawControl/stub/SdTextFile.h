#pragma once
#include <stddef.h>
class FileSystem { public: bool isSdCardInserted(){return false;} bool exists(const char*){return false;} };
namespace SdTextFile { inline bool readAll(FileSystem*, const char*, char*, size_t, size_t* = nullptr){return false;} }
