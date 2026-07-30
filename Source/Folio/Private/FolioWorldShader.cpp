#include "FolioWorldShader.h"

IMPLEMENT_GLOBAL_SHADER(FFolioWorldVS, "/Folio/FolioWorld.usf", "WorldVS", SF_Vertex);
IMPLEMENT_GLOBAL_SHADER(FFolioWorldPS, "/Folio/FolioWorld.usf", "WorldPS", SF_Pixel);
IMPLEMENT_GLOBAL_SHADER(FFolioWorldMsdfPS, "/Folio/FolioWorld.usf", "WorldMsdfPS", SF_Pixel);
