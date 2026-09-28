////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#if defined(QUADER_ENABLE_TRACY) && QUADER_ENABLE_TRACY
#include <tracy/Tracy.hpp>
#define QDR_PROFILE_SCOPE(name) ZoneScopedN(name)
#define QDR_PROFILE_FUNCTION() ZoneScoped
#define QDR_PROFILE_NAME(name, size) ZoneName(name, size)
#define QDR_PROFILE_TEXT(text, size) ZoneText(text, size)
#define QDR_PROFILE_FRAME_MARK() FrameMark
#define QDR_PROFILE_MESSAGE(message) TracyMessageL(message)
#define QDR_PROFILE_MESSAGE_DYNAMIC(message, size) TracyMessage(message, size)
#define QDR_PROFILE_PLOT(name, value) TracyPlot(name, value)
#else
#define QDR_PROFILE_SCOPE(name) ((void)0)
#define QDR_PROFILE_FUNCTION() ((void)0)
#define QDR_PROFILE_NAME(name, size) ((void)0)
#define QDR_PROFILE_TEXT(text, size) ((void)0)
#define QDR_PROFILE_FRAME_MARK() ((void)0)
#define QDR_PROFILE_MESSAGE(message) ((void)0)
#define QDR_PROFILE_MESSAGE_DYNAMIC(message, size) ((void)0)
#define QDR_PROFILE_PLOT(name, value) ((void)0)
#endif
