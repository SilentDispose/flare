#pragma once

#if defined(_WIN32)
#if defined(FLARE_SHARED)
#if defined(FLARE_BUILDING)
#define FLARE_API __declspec(dllexport)
#else
#define FLARE_API __declspec(dllimport)
#endif
#else
#define FLARE_API
#endif
#elif defined(__GNUC__) || defined(__clang__)
#if defined(FLARE_SHARED)
#define FLARE_API __attribute__((visibility("default")))
#else
#define FLARE_API
#endif
#else
#define FLARE_API
#endif
