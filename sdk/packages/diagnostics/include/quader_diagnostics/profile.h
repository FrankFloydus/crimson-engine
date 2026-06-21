#pragma once

#if defined(QUADER_ENABLE_TRACY) && QUADER_ENABLE_TRACY
#include <tracy/Tracy.hpp>
#define QDR_PROFILE_SCOPE(name) ZoneScopedN(name)
#define QDR_PROFILE_FUNCTION() ZoneScoped
#define QDR_PROFILE_FRAME_MARK() FrameMark
#define QDR_PROFILE_MESSAGE(message) TracyMessageL(message)
#else
#define QDR_PROFILE_SCOPE(name) ((void)0)
#define QDR_PROFILE_FUNCTION() ((void)0)
#define QDR_PROFILE_FRAME_MARK() ((void)0)
#define QDR_PROFILE_MESSAGE(message) ((void)0)
#endif
