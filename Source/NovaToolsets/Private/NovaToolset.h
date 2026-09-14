#pragma once

#include "CoreMinimal.h"
#include "ToolsetRegistry/ToolsetDefinition.h"
#include "NovaToolset.generated.h"

/**
 * Shared base for Nova toolsets. Holds the error helper and the guards every write tool runs:
 * refuse to touch an asset whose editor is open, and save without any path that can pop a modal
 * dialog (a modal on the game thread stalls every MCP call behind it).
 */
UCLASS(Abstract)
class UNovaToolset : public UToolsetDefinition
{
	GENERATED_BODY()

public:
	static void Error(const FString& Message);
	static void Error(const FText& Message);

	/** Raises an error and returns false when an asset editor is open for Asset. */
	static bool EnsureNoOpenEditor(UObject* Asset);

	/** Saves the asset's package via UPackage::SavePackage (no checkout or overwrite prompts). */
	static bool SaveAssetSilently(UObject* Asset);

	/**
	 * Validates a new asset path such as "/Game/FX/WindField/RTV_WindField" (an object path with
	 * ".Name" is also accepted) and rejects paths that already exist, so creation can never hit an
	 * overwrite prompt.
	 */
	static bool SplitNewAssetPath(const FString& AssetPath, FString& OutPackageName, FString& OutAssetName);
};
