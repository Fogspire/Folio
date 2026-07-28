#include "FolioUIShader.h"

IMPLEMENT_GLOBAL_SHADER(FFolioUIVS, "/Folio/FolioUI.usf", "MainVS", SF_Vertex);
IMPLEMENT_GLOBAL_SHADER(FFolioUIPS, "/Folio/FolioUI.usf", "MainPS", SF_Pixel);
IMPLEMENT_GLOBAL_SHADER(FFolioUIMaskPS, "/Folio/FolioUI.usf", "MaskPS", SF_Pixel);

void FFolioUIVertexDeclaration::InitRHI(FRHICommandListBase& RHICmdList)
{
	FVertexDeclarationElementList Elements;
	const uint16 Stride = sizeof(FFolioUIVertex);

	Elements.Add(FVertexElement(0, STRUCT_OFFSET(FFolioUIVertex, Position), VET_Float2, 0, Stride));
	Elements.Add(FVertexElement(0, STRUCT_OFFSET(FFolioUIVertex, UV), VET_Float2, 1, Stride));
	Elements.Add(FVertexElement(0, STRUCT_OFFSET(FFolioUIVertex, Color), VET_Float4, 2, Stride));

	VertexDeclarationRHI = PipelineStateCache::GetOrCreateVertexDeclaration(Elements);
}

void FFolioUIVertexDeclaration::ReleaseRHI()
{
	VertexDeclarationRHI.SafeRelease();
}

TGlobalResource<FFolioUIVertexDeclaration> GFolioUIVertexDeclaration;
