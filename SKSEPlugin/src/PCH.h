#pragma once

#include <RE/Skyrim.h>
#include <SKSE/SKSE.h>

#include <spdlog/sinks/basic_file_sink.h>

#ifdef GetObject
#    undef GetObject
#endif

#define DLLEXPORT __declspec(dllexport)

using namespace std::literals;
