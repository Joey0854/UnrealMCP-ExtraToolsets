#include "NovaToolset_RenderTarget.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/TextureRenderTargetVolume.h"
#include "PixelFormat.h"
#include "UObject/Package.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(NovaToolset_RenderTarget)

namespace NovaToolsetRenderTarget
{
	// PostEditChangeProperty clamps to these; enforcing them up front keeps the asset consistent
	// with what the editor would produce.
	constexpr int32 Max2DSize = 16384;
	constexpr int32 MaxVolumeSize = 2048;

	bool IsFloatFormat(ETextureRenderTargetFormat Format)
	{
		switch (Format)
		{
		case RTF_R16f: case RTF_RG16f: case RTF_RGBA16f:
		case RTF_R32f: case RTF_RG32f: case RTF_RGBA32f:
			return true;
		default:
			return false;
		}
	}

	FNovaRenderTargetInfo Describe(UTexture* Texture, bool bSaved)
	{
		FNovaRenderTargetInfo Info;
		Info.Asset = Texture;
		Info.bSaved = bSaved;
		if (Texture == nullptr)
		{
			return Info;
		}

		Info.ClassName = Texture->GetClass()->GetName();
		EPixelFormat Format = PF_Unknown;
		if (UTextureRenderTarget2D* RT2D = Cast<UTextureRenderTarget2D>(Texture))
		{
			Info.SizeX = RT2D->SizeX;
			Info.SizeY = RT2D->SizeY;
			Info.bSupportsUAV = RT2D->bSupportsUAV;
			Format = RT2D->GetFormat();
		}
		else if (UTextureRenderTargetVolume* RTV = Cast<UTextureRenderTargetVolume>(Texture))
		{
			Info.SizeX = RTV->SizeX;
			Info.SizeY = RTV->SizeY;
			Info.SizeZ = RTV->SizeZ;
			Info.bSupportsUAV = RTV->bSupportsUAV;
			Format = RTV->OverrideFormat != PF_Unknown ? RTV->OverrideFormat.GetValue() : (RTV->bHDR ? PF_FloatRGBA : PF_B8G8R8A8);
		}
		Info.PixelFormat = GPixelFormats[Format].Name;
		return Info;
	}

	void FinishCreatedAsset(UObject* Asset)
	{
		FAssetRegistryModule::AssetCreated(Asset);
		Asset->MarkPackageDirty();
	}
}

FNovaRenderTargetInfo UNovaToolset_RenderTarget::CreateRenderTarget2D(const FString& AssetPath, int32 SizeX, int32 SizeY, ETextureRenderTargetFormat Format, bool bSupportsUAV, bool bClampAddress, FLinearColor ClearColor, bool bSave)
{
	using namespace NovaToolsetRenderTarget;

	if (SizeX < 1 || SizeY < 1 || SizeX > Max2DSize || SizeY > Max2DSize)
	{
		Error(FString::Printf(TEXT("SizeX/SizeY must be within 1-%d (got %dx%d)."), Max2DSize, SizeX, SizeY));
		return {};
	}

	FString PackageName, AssetName;
	if (!SplitNewAssetPath(AssetPath, PackageName, AssetName))
	{
		return {};
	}

	UPackage* Package = CreatePackage(*PackageName);
	UTextureRenderTarget2D* RenderTarget = NewObject<UTextureRenderTarget2D>(Package, FName(*AssetName), RF_Public | RF_Standalone | RF_Transactional);
	RenderTarget->RenderTargetFormat = Format;
	RenderTarget->ClearColor = ClearColor;
	RenderTarget->bSupportsUAV = bSupportsUAV;
	RenderTarget->AddressX = bClampAddress ? TA_Clamp : TA_Wrap;
	RenderTarget->AddressY = RenderTarget->AddressX;
	// InitAutoFormat sets the size and creates the resource directly. It skips PostEditChangeProperty,
	// which is where the >2048 confirmation dialog lives.
	RenderTarget->InitAutoFormat(SizeX, SizeY);

	// PostEditChangeProperty would derive gamma from the format; mirror that here.
	const bool bWantLinearGamma = Format != RTF_RGBA8_SRGB;
	if (RenderTarget->bForceLinearGamma != bWantLinearGamma)
	{
		RenderTarget->bForceLinearGamma = bWantLinearGamma;
		RenderTarget->UpdateResource();
	}

	FinishCreatedAsset(RenderTarget);
	const bool bSaved = bSave && SaveAssetSilently(RenderTarget);
	return Describe(RenderTarget, bSaved);
}

FNovaRenderTargetInfo UNovaToolset_RenderTarget::CreateRenderTargetVolume(const FString& AssetPath, int32 SizeX, int32 SizeY, int32 SizeZ, ETextureRenderTargetFormat Format, bool bSupportsUAV, FLinearColor ClearColor, bool bSave)
{
	using namespace NovaToolsetRenderTarget;

	if (SizeX < 1 || SizeY < 1 || SizeZ < 1 || SizeX > MaxVolumeSize || SizeY > MaxVolumeSize || SizeZ > MaxVolumeSize)
	{
		Error(FString::Printf(TEXT("SizeX/SizeY/SizeZ must be within 1-%d (got %dx%dx%d)."), MaxVolumeSize, SizeX, SizeY, SizeZ));
		return {};
	}

	FString PackageName, AssetName;
	if (!SplitNewAssetPath(AssetPath, PackageName, AssetName))
	{
		return {};
	}

	UPackage* Package = CreatePackage(*PackageName);
	UTextureRenderTargetVolume* RenderTarget = NewObject<UTextureRenderTargetVolume>(Package, FName(*AssetName), RF_Public | RF_Standalone | RF_Transactional);
	RenderTarget->ClearColor = ClearColor;
	RenderTarget->bSupportsUAV = bSupportsUAV;
	RenderTarget->bHDR = IsFloatFormat(Format);
	RenderTarget->bForceLinearGamma = Format != RTF_RGBA8_SRGB;
	// Init stores the explicit pixel format in OverrideFormat and creates the resource.
	RenderTarget->Init(SizeX, SizeY, SizeZ, GetPixelFormatFromRenderTargetFormat(Format));

	FinishCreatedAsset(RenderTarget);
	const bool bSaved = bSave && SaveAssetSilently(RenderTarget);
	return Describe(RenderTarget, bSaved);
}

FNovaRenderTargetInfo UNovaToolset_RenderTarget::ResizeRenderTarget(UTexture* RenderTarget, int32 SizeX, int32 SizeY, int32 SizeZ, bool bSave)
{
	using namespace NovaToolsetRenderTarget;

	if (UTextureRenderTarget2D* RT2D = Cast<UTextureRenderTarget2D>(RenderTarget))
	{
		if (SizeX < 1 || SizeY < 1 || SizeX > Max2DSize || SizeY > Max2DSize)
		{
			Error(FString::Printf(TEXT("SizeX/SizeY must be within 1-%d (got %dx%d)."), Max2DSize, SizeX, SizeY));
			return {};
		}
		RT2D->Modify();
		RT2D->ResizeTarget(SizeX, SizeY);
	}
	else if (UTextureRenderTargetVolume* RTV = Cast<UTextureRenderTargetVolume>(RenderTarget))
	{
		if (SizeX < 1 || SizeY < 1 || SizeZ < 1 || SizeX > MaxVolumeSize || SizeY > MaxVolumeSize || SizeZ > MaxVolumeSize)
		{
			Error(FString::Printf(TEXT("SizeX/SizeY/SizeZ must be within 1-%d (got %dx%dx%d)."), MaxVolumeSize, SizeX, SizeY, SizeZ));
			return {};
		}
		const EPixelFormat Format = RTV->OverrideFormat != PF_Unknown ? RTV->OverrideFormat.GetValue() : (RTV->bHDR ? PF_FloatRGBA : PF_B8G8R8A8);
		RTV->Modify();
		RTV->Init(SizeX, SizeY, SizeZ, Format);
	}
	else
	{
		Error(TEXT("RenderTarget must be a TextureRenderTarget2D or TextureRenderTargetVolume."));
		return {};
	}

	RenderTarget->MarkPackageDirty();
	const bool bSaved = bSave && SaveAssetSilently(RenderTarget);
	return Describe(RenderTarget, bSaved);
}

FNovaRenderTargetInfo UNovaToolset_RenderTarget::GetRenderTargetInfo(UTexture* RenderTarget)
{
	if (!Cast<UTextureRenderTarget2D>(RenderTarget) && !Cast<UTextureRenderTargetVolume>(RenderTarget))
	{
		Error(TEXT("RenderTarget must be a TextureRenderTarget2D or TextureRenderTargetVolume."));
		return {};
	}
	return NovaToolsetRenderTarget::Describe(RenderTarget, false);
}
