#pragma once

#include "Runtime/Launch/Resources/Version.h"

#if defined(ENGINE_MAJOR_VERSION) && \
    ((ENGINE_MAJOR_VERSION > 5) || \
     (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 8))
#define MATBP2FP_WITH_UE58_MCP 1
#else
#define MATBP2FP_WITH_UE58_MCP 0
#endif
