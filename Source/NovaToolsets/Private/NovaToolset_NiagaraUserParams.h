#pragma once

#include "CoreMinimal.h"
#include "NovaToolset.h"
#include "NovaToolset_NiagaraUserParams.generated.h"

class UNiagaraSystem;

/** One collapsible category of the User Parameters panel and the parameters inside it, in display order. */
USTRUCT()
struct FNovaUserParameterCategoryLayout
{
	GENERATED_BODY()

	/** Category name. None places the parameters directly under the root, where they show in the All tab only. */
	UPROPERTY()
	FName Category;

	/** User parameter names, with or without the User. prefix. */
	UPROPERTY()
	TArray<FName> Parameters;
};

/** One tab (section) of the User Parameters panel. */
USTRUCT()
struct FNovaUserParameterSectionLayout
{
	GENERATED_BODY()

	/** Section (tab) name. None leaves its categories without a tab, so they show in the All tab only. */
	UPROPERTY()
	FName Section;

	UPROPERTY()
	TArray<FNovaUserParameterCategoryLayout> Categories;
};

/** The user parameter hierarchy of a system, read back from the asset. */
USTRUCT()
struct FNovaUserParameterHierarchyInfo
{
	GENERATED_BODY()

	/** Sections in tab order. Categories that belong to no section are listed under a section named None. */
	UPROPERTY()
	TArray<FNovaUserParameterSectionLayout> Sections;

	/** User parameters not placed anywhere in the hierarchy; the panels append them to the All tab. */
	UPROPERTY()
	TArray<FName> UnplacedParameters;

	UPROPERTY()
	bool bSaved = false;
};

/** Where one user parameter is referenced inside the system. */
USTRUCT()
struct FNovaUserParameterUsage
{
	GENERATED_BODY()

	UPROPERTY()
	FName Name;

	UPROPERTY()
	FString Type;

	/** True when at least one reference was found. False means no graph, data interface, renderer or simulation stage refers to it. */
	UPROPERTY()
	bool bUsed = false;

	/** One entry per referencing place, e.g. "Local module NMS_Advect: Map Get" or "Emitter WindFieldSolver: simulation stage Project_Pressure". */
	UPROPERTY()
	TArray<FString> References;
};

/**
 * Organises and audits the User Parameters of a Niagara system: the section (tab) and category layout shown
 * in the Niagara editor and in the Details panel of placed components, and a scan of where each parameter
 * is still referenced before removing it. Removing parameters is done with the stock
 * NiagaraToolset_System.RemoveUserVariables.
 */
UCLASS()
class UNovaToolset_NiagaraUserParams : public UNovaToolset
{
	GENERATED_BODY()

public:
	/**
	 * Returns the user parameter hierarchy of a system: sections (tabs) in order, their categories and the
	 * parameters inside, plus any parameter that is not placed.
	 *
	 * @param System The Niagara system asset
	 * @return The hierarchy as stored in the asset
	 */
	UFUNCTION(meta = (AICallable), Category = "Niagara|UserParameters")
	static FNovaUserParameterHierarchyInfo GetUserParameterHierarchy(UNiagaraSystem* System);

	/**
	 * Replaces the whole user parameter hierarchy with the given sections, categories and parameter order,
	 * as the Niagara editor's User Parameter Hierarchy tab does. Parameter names and values are untouched, so
	 * component overrides and Blueprint writes keep working. Every name must be an existing user parameter
	 * and appear at most once; the call fails without changing anything otherwise. Parameters left out are
	 * appended to the All tab by the panels. The system's Niagara editor must be closed; Details panels of
	 * placed components show the new layout the next time they are rebuilt (reselect the actor).
	 *
	 * @param System The Niagara system asset
	 * @param Sections Tabs in display order, each with its categories and parameters in display order
	 * @param bSave Save the system after the change
	 * @return The hierarchy read back after the change
	 */
	UFUNCTION(meta = (AICallable), Category = "Niagara|UserParameters")
	static FNovaUserParameterHierarchyInfo SetUserParameterHierarchy(UNiagaraSystem* System, const TArray<FNovaUserParameterSectionLayout>& Sections, bool bSave = true);

	/**
	 * Lists every user parameter with the places that reference it: Map Get pins in the system graph, the
	 * emitter graphs (which include stack inputs linked to the parameter) and the local modules, plus any
	 * data interface, renderer or simulation stage whose properties name it (user parameter bindings,
	 * simulation stage iteration bindings). A parameter with no reference is safe to remove from the system
	 * itself; Blueprints or C++ that set it by name are outside the asset and are not scanned.
	 *
	 * @param System The Niagara system asset
	 * @return One entry per user parameter
	 */
	UFUNCTION(meta = (AICallable), Category = "Niagara|UserParameters")
	static TArray<FNovaUserParameterUsage> GetUserParameterUsage(UNiagaraSystem* System);
};
