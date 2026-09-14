#pragma once

#include "CoreMinimal.h"
#include "NovaToolset.h"
#include "NovaToolset_Blueprint.generated.h"

class UBlueprint;

/** One metadata key and value. */
USTRUCT()
struct FNovaMetadataEntry
{
	GENERATED_BODY()

	UPROPERTY()
	FName Key;

	UPROPERTY()
	FString Value;
};

/** Metadata of a Blueprint member variable, read back after a change. */
USTRUCT()
struct FNovaBlueprintVariableMetadataInfo
{
	GENERATED_BODY()

	UPROPERTY()
	FName VariableName;

	UPROPERTY()
	TArray<FNovaMetadataEntry> Metadata;

	/** False when the Blueprint has compile errors after the change (the metadata is still set). */
	UPROPERTY()
	bool bCompiled = false;

	UPROPERTY()
	bool bSaved = false;
};

/** Metadata of a Blueprint function or custom event, read back after a change. */
USTRUCT()
struct FNovaBlueprintFunctionMetadataInfo
{
	GENERATED_BODY()

	UPROPERTY()
	FName FunctionName;

	/** Function or CustomEvent. */
	UPROPERTY()
	FString Kind;

	/** Shows a button for the function in the Details panel of placed instances. */
	UPROPERTY()
	bool bCallInEditor = false;

	UPROPERTY()
	FString ToolTip;

	UPROPERTY()
	FString FunctionCategory;

	UPROPERTY()
	FString Keywords;

	/** Any other metadata keys stored on the function. */
	UPROPERTY()
	TArray<FNovaMetadataEntry> Metadata;

	/** False when the Blueprint has compile errors after the change (the metadata is still set). */
	UPROPERTY()
	bool bCompiled = false;

	UPROPERTY()
	bool bSaved = false;
};

/** Compact text dump of a Blueprint graph. */
USTRUCT()
struct FNovaGraphDump
{
	GENERATED_BODY()

	UPROPERTY()
	FString Dump;

	UPROPERTY()
	int32 NodeCount = 0;

	/** Pin connections, each counted once. */
	UPROPERTY()
	int32 LinkCount = 0;
};

/**
 * Blueprint editing the stock Blueprint toolset cannot do: metadata on member variables (tooltips,
 * numeric limits) and on functions (Call In Editor, tooltip, category, keywords), plus a compact,
 * language-independent graph dump for verifying graphs built by other tools.
 * Every write tool runs in an undo transaction, compiles the Blueprint, optionally saves it, and returns
 * a read-back of the result.
 */
UCLASS()
class UNovaToolset_Blueprint : public UNovaToolset
{
	GENERATED_BODY()

public:
	/**
	 * Sets or removes a metadata key on a Blueprint member variable, as the variable's Details panel does:
	 * Tooltip (hover text, any language), ClampMin/ClampMax (hard numeric limits) and UIMin/UIMax (slider
	 * range) for numeric variables, or any other property metadata key.
	 *
	 * @param Blueprint The Blueprint asset
	 * @param VariableName Member variable name
	 * @param Key Metadata key, e.g. Tooltip, ClampMin, ClampMax, UIMin, UIMax
	 * @param Value New value; an empty string removes the key. ClampMin/ClampMax/UIMin/UIMax must be numbers
	 * @param bSave Save the Blueprint package after compiling
	 * @return All metadata of the variable after the change
	 */
	UFUNCTION(meta = (AICallable), Category = "Blueprint")
	static FNovaBlueprintVariableMetadataInfo SetVariableMetadata(UBlueprint* Blueprint, FName VariableName, FName Key, const FString& Value, bool bSave = true);

	/**
	 * Sets metadata on a Blueprint function or custom event. CallInEditor=true adds a button for it to the
	 * Details panel of placed instances. Functions also accept Tooltip, Category, Keywords and any other
	 * function metadata key; custom events only accept CallInEditor.
	 *
	 * @param Blueprint The Blueprint asset
	 * @param FunctionName Function graph name or custom event name
	 * @param Key CallInEditor, Tooltip, Category, Keywords, or another metadata key
	 * @param Value New value (true/false for CallInEditor); an empty string clears the key
	 * @param bSave Save the Blueprint package after compiling
	 * @return The function's metadata after the change
	 */
	UFUNCTION(meta = (AICallable), Category = "Blueprint")
	static FNovaBlueprintFunctionMetadataInfo SetFunctionMetadata(UBlueprint* Blueprint, FName FunctionName, FName Key, const FString& Value, bool bSave = true);

	/**
	 * Dumps a Blueprint graph as compact text for verifying its structure: one line per node, then one
	 * indented line per connection (listed once, from the output pin) and per unconnected input whose value
	 * differs from its default. Node identities use class, function, macro and variable names rather than
	 * editor display text, so the dump does not depend on the editor language.
	 *   K2Node_CallFunction_0 [Call GameplayStatics|GetAllActorsOfClass]
	 *     then -> K2Node_MacroInstance_0.execute
	 *     OutActors -> K2Node_MacroInstance_0.Array
	 *     ActorClass = /Script/Engine.Actor
	 *
	 * @param Blueprint The Blueprint asset
	 * @param GraphName EventGraph, a function or a macro name; None dumps every graph
	 * @return The dump with node and connection counts
	 */
	UFUNCTION(meta = (AICallable), Category = "Blueprint")
	static FNovaGraphDump DumpGraph(UBlueprint* Blueprint, FName GraphName = NAME_None);
};
