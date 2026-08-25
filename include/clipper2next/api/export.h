#pragma once

#if defined(_WIN32)
#if defined(CLIPPER2NEXT_BUILDING_LIBRARY)
#define CLIPPER2NEXT_API __declspec(dllexport)
#else
#define CLIPPER2NEXT_API __declspec(dllimport)
#endif
#else
#define CLIPPER2NEXT_API __attribute__((visibility("default")))
#endif
