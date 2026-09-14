#include "NovaToolset.h"

#include "Editor.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Misc/PackageName.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(NovaToolset)

void UNovaToolset::Error(const FString& Message)
{
	UKismetSystemLibrary::RaiseScriptError(Message);
}

void UNovaToolset::Error(const FText& Message)
{
	UKismetSystemLibrary::RaiseScriptError(Message.ToString());
}

bool UNovaToolset::EnsureNoOpenEditor(UObject* Asset)
{
	if (Asset == nullptr)
	{
		Error(TEXT("Asset is null."));
		return false;
	}

	UAssetEditorSubsystem* AssetEditorSubsystem = GEditor ? GEditor->GetEditorSubsystem<UAssetEditorSubsystem>() : nullptr;
	if (AssetEditorSubsystem && AssetEditorSubsystem->FindEditorsForAsset(Asset).Num() > 0)
	{
		// An open editor keeps its own view model of the asset. Structural edits made behind its back
		// leave it holding stale pointers, which is how undo in an open Niagara editor crashed before.
		Error(FString::Printf(TEXT("Close the asset editor for '%s' before running this tool."), *Asset->GetPathName()));
		return false;
	}
	return true;
}

bool UNovaToolset::SaveAssetSilently(UObject* Asset)
{
	if (Asset == nullptr)
	{
		return false;
	}

	UPackage* Package = Asset->GetOutermost();
	const FString Filename = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());

	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	SaveArgs.SaveFlags = SAVE_NoError;
	SaveArgs.bSlowTask = false;
	// GError is fatal in the editor; route save errors to the log instead.
	SaveArgs.Error = GWarn;

	if (!UPackage::SavePackage(Package, Asset, *Filename, SaveArgs))
	{
		Error(FString::Printf(TEXT("Failed to save '%s' to '%s'. The file may be read-only."), *Package->GetName(), *Filename));
		return false;
	}
	return true;
}

bool UNovaToolset::SplitNewAssetPath(const FString& AssetPath, FString& OutPackageName, FString& OutAssetName)
{
	FString PackageName = AssetPath.TrimStartAndEnd();
	if (PackageName.Contains(TEXT(".")))
	{
		PackageName = FPackageName::ObjectPathToPackageName(PackageName);
	}

	if (!FPackageName::IsValidLongPackageName(PackageName))
	{
		Error(FString::Printf(TEXT("'%s' is not a valid long package name, e.g. /Game/FX/WindField/RTV_WindField."), *AssetPath));
		return false;
	}

	if (FindPackage(nullptr, *PackageName) != nullptr || FPackageName::DoesPackageExist(PackageName))
	{
		Error(FString::Printf(TEXT("An asset already exists at '%s'. Pick a new path; this tool never overwrites."), *PackageName));
		return false;
	}

	OutPackageName = PackageName;
	OutAssetName = FPackageName::GetShortName(PackageName);
	return true;
}
