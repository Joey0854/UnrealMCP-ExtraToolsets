#pragma once

#include "CoreMinimal.h"
#include "Engine/TextureRenderTarget2D.h"
#include "NovaToolset.h"
#include "NovaToolset_RenderTarget.generated.h"

class UTexture;

/** Read-back of a render target asset after a create or resize. */
USTRUCT()
struct FNovaRenderTargetInfo
{
	GENERATED_BODY()

	/** The render target asset. Pass it back to other tools as-is. */
	UPROPERTY()
	TObjectPtr<UTexture> Asset = nullptr;

	/** TextureRenderTarget2D or TextureRenderTargetVolume. */
	UPROPERTY()
	FString ClassName;

	UPROPERTY()
	int32 SizeX = 0;

	UPROPERTY()
	int32 SizeY = 0;

	/** Always 0 for 2D render targets. */
	UPROPERTY()
	int32 SizeZ = 0;

	/** Resolved pixel format, e.g. PF_FloatRGBA. */
	UPROPERTY()
	FString PixelFormat;

	UPROPERTY()
	bool bSupportsUAV = false;

	/** Whether the package was written to disk by this call. */
	UPROPERTY()
	bool bSaved = false;
};

/**
 * Creates and resizes render target assets (2D and Volume) with size and format applied at
 * creation time.
 *
 * Use these instead of ObjectTools.set_properties on SizeX/SizeY: editing a 2D render target's size
 * above 2048 through the property path opens a modal Yes/No dialog on the game thread, which stalls
 * the editor and every later MCP call. These tools never go through PostEditChangeProperty.
 * The stock factories also ignore their Format field, so format is applied here directly.
 */
UCLASS()
class UNovaToolset_RenderTarget : public UNovaToolset
{
	GENERATED_BODY()

public:
	/**
	 * Creates a new TextureRenderTarget2D asset. Fails if the path already exists.
	 *
	 * @param AssetPath New asset path, e.g. /Game/FX/WindField/RT_WindField
	 * @param SizeX Width in texels, 1-16384
	 * @param SizeY Height in texels, 1-16384
	 * @param Format Render target format, e.g. RTF_RGBA16f
	 * @param bSupportsUAV Allow GPU compute (e.g. Niagara grid) writes to the texture
	 * @param bClampAddress Use TA_Clamp addressing on both axes instead of TA_Wrap
	 * @param ClearColor Color the target is cleared to
	 * @param bSave Save the package to disk immediately
	 * @return Read-back of the created asset
	 */
	UFUNCTION(meta = (AICallable), Category = "RenderTarget")
	static FNovaRenderTargetInfo CreateRenderTarget2D(const FString& AssetPath, int32 SizeX, int32 SizeY, ETextureRenderTargetFormat Format = RTF_RGBA16f, bool bSupportsUAV = false, bool bClampAddress = true, FLinearColor ClearColor = FLinearColor::Black, bool bSave = true);

	/**
	 * Creates a new TextureRenderTargetVolume asset (3D texture). Fails if the path already exists.
	 *
	 * @param AssetPath New asset path, e.g. /Game/FX/WindField/RTV_WindField
	 * @param SizeX Width in texels, 1-2048
	 * @param SizeY Height in texels, 1-2048
	 * @param SizeZ Depth in texels, 1-2048
	 * @param Format Render target format, e.g. RTF_RGBA16f
	 * @param bSupportsUAV Allow GPU compute (e.g. Niagara grid) writes to the texture
	 * @param ClearColor Color the target is cleared to
	 * @param bSave Save the package to disk immediately
	 * @return Read-back of the created asset
	 */
	UFUNCTION(meta = (AICallable), Category = "RenderTarget")
	static FNovaRenderTargetInfo CreateRenderTargetVolume(const FString& AssetPath, int32 SizeX, int32 SizeY, int32 SizeZ, ETextureRenderTargetFormat Format = RTF_RGBA16f, bool bSupportsUAV = false, FLinearColor ClearColor = FLinearColor::Black, bool bSave = true);

	/**
	 * Resizes an existing 2D or Volume render target without the property-edit path, so no dialog can
	 * appear. Format and other settings are kept.
	 *
	 * @param RenderTarget A TextureRenderTarget2D or TextureRenderTargetVolume asset
	 * @param SizeX New width in texels
	 * @param SizeY New height in texels
	 * @param SizeZ New depth in texels; ignored for 2D render targets
	 * @param bSave Save the package to disk immediately
	 * @return Read-back of the resized asset
	 */
	UFUNCTION(meta = (AICallable), Category = "RenderTarget")
	static FNovaRenderTargetInfo ResizeRenderTarget(UTexture* RenderTarget, int32 SizeX, int32 SizeY, int32 SizeZ = 0, bool bSave = true);

	/**
	 * Reads size, pixel format and UAV support of a 2D or Volume render target.
	 *
	 * @param RenderTarget A TextureRenderTarget2D or TextureRenderTargetVolume asset
	 * @return Read-back of the asset
	 */
	UFUNCTION(meta = (AICallable), Category = "RenderTarget")
	static FNovaRenderTargetInfo GetRenderTargetInfo(UTexture* RenderTarget);
};
