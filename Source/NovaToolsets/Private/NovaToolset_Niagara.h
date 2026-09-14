#pragma once

#include "CoreMinimal.h"
#include "NiagaraCommon.h"
#include "NiagaraCore.h"
#include "NiagaraEmitter.h"
#include "NiagaraScriptBase.h"
#include "NovaToolset.h"
#include "NovaToolset_Niagara.generated.h"

class UNiagaraDataInterface;
class UNiagaraScript;
class UNiagaraSimulationStageBase;
class UNiagaraSystem;

/** Read-back of one simulation stage on an emitter. */
USTRUCT()
struct FNovaSimulationStageInfo
{
	GENERATED_BODY()

	/** Position in the emitter's stage list. Use as StageIndex in AddModuleToStage. */
	UPROPERTY()
	int32 StageIndex = INDEX_NONE;

	/** The stage object. Its EditAnywhere properties can be edited with ObjectTools.set_properties. */
	UPROPERTY()
	TObjectPtr<UNiagaraSimulationStageBase> Stage = nullptr;

	UPROPERTY()
	FString StageClass;

	UPROPERTY()
	FName StageName;

	UPROPERTY()
	bool bEnabled = false;

	/** Usage id of the stage script; identifies its stack in the emitter graph. */
	UPROPERTY()
	FString UsageId;

	/** Generic stages only. */
	UPROPERTY()
	ENiagaraIterationSource IterationSource = ENiagaraIterationSource::Particles;

	/** Generic stages only: the bound data interface parameter, e.g. Emitter.VelocityGrid. */
	UPROPERTY()
	FName DataInterfaceName;

	/** Generic stages only. */
	UPROPERTY()
	ENiagaraSimStageExecuteBehavior ExecuteBehavior = ENiagaraSimStageExecuteBehavior::Always;
};

/** Read-back of one event handler on an emitter. */
USTRUCT()
struct FNovaEventHandlerInfo
{
	GENERATED_BODY()

	/** Position in the emitter's event handler list. Use as StageIndex in AddModuleToStage. */
	UPROPERTY()
	int32 StageIndex = INDEX_NONE;

	/** Usage id of the event script; identifies its stack in the emitter graph. */
	UPROPERTY()
	FString UsageId;

	UPROPERTY()
	EScriptExecutionMode ExecutionMode = EScriptExecutionMode::EveryParticle;

	UPROPERTY()
	int32 SpawnNumber = 0;

	UPROPERTY()
	int32 MaxEventsPerFrame = 0;

	/** Emitter that generates the events. None means this emitter. */
	UPROPERTY()
	FName SourceEmitterName;

	UPROPERTY()
	FName SourceEventName;

	UPROPERTY()
	bool bRandomSpawnNumber = false;

	UPROPERTY()
	int32 MinSpawnNumber = 0;

	UPROPERTY()
	bool bUpdateAttributeInitialValues = true;
};

/** Stage layout of one emitter: simulation stages and event handlers, in execution order. */
USTRUCT()
struct FNovaEmitterStagesInfo
{
	GENERATED_BODY()

	UPROPERTY()
	FName EmitterName;

	UPROPERTY()
	ENiagaraSimTarget SimTarget = ENiagaraSimTarget::CPUSim;

	UPROPERTY()
	TArray<FNovaSimulationStageInfo> SimulationStages;

	UPROPERTY()
	TArray<FNovaEventHandlerInfo> EventHandlers;
};

/** A module node placed in a stack by AddModuleToStage or CreateLocalModule. */
USTRUCT()
struct FNovaStackModuleInfo
{
	GENERATED_BODY()

	/** The module script that was placed. */
	UPROPERTY()
	TObjectPtr<UNiagaraScript> ModuleScript = nullptr;

	/** Object name of the function call node in the emitter or system graph. */
	UPROPERTY()
	FName ModuleNodeName;

	/** Title shown in the Niagara stack. */
	UPROPERTY()
	FString DisplayName;

	/** Stack the module was placed in. */
	UPROPERTY()
	ENiagaraScriptUsage TargetStack = ENiagaraScriptUsage::ParticleUpdateScript;

	/** Stage index for simulation stage and event handler stacks, otherwise -1. */
	UPROPERTY()
	int32 StageIndex = INDEX_NONE;
};

/** A local (scratch pad) module owned by a system. */
USTRUCT()
struct FNovaLocalModuleInfo
{
	GENERATED_BODY()

	/** The module script. Pass it to AddModuleToStage to place it in another stack. */
	UPROPERTY()
	TObjectPtr<UNiagaraScript> Script = nullptr;

	UPROPERTY()
	FName ScriptName;

	/** Bit N set means the module may be placed in stacks of ENiagaraScriptUsage value N. */
	UPROPERTY()
	int32 ModuleUsageBitmask = 0;

	/** Set by CreateLocalModule: where the new module was placed. */
	UPROPERTY()
	FNovaStackModuleInfo PlacedModule;
};

/** Editor metadata of a system user parameter, read back after a change. */
USTRUCT()
struct FNovaUserVariableInfo
{
	GENERATED_BODY()

	/** Full parameter name, e.g. User.WindSourcePosition. */
	UPROPERTY()
	FName VariableName;

	UPROPERTY()
	FString Description;

	UPROPERTY()
	bool bSaved = false;
};

/**
 * Niagara system editing that the stock NiagaraToolset_System cannot do: add simulation stages and
 * event handlers, place modules in simulation stage and event handler stacks, and create local
 * (scratch pad) modules.
 *
 * Emitters are addressed by EmitterName (as returned by NiagaraToolset_System.GetSystemSummary).
 * Stages and event handlers are addressed by StageIndex from GetEmitterStages.
 * Write tools also work while the system is open in the Niagara editor: stage menus run on the
 * editor's own view model and the editor refreshes after each change. Every write tool runs in an undo
 * transaction, requests a recompile, and returns a read-back of what it changed.
 * Local module graph contents (Custom HLSL nodes, pins) are edited with NovaToolset_NiagaraGraph.
 */
UCLASS()
class UNovaToolset_Niagara : public UNovaToolset
{
	GENERATED_BODY()

public:
	/**
	 * Lists an emitter's simulation stages and event handlers with their settings.
	 *
	 * @param System The Niagara System
	 * @param EmitterName Emitter name inside the system
	 * @return Stage layout of the emitter
	 */
	UFUNCTION(meta = (AICallable), Category = "Niagara|Stages")
	static FNovaEmitterStagesInfo GetEmitterStages(UNiagaraSystem* System, FName EmitterName);

	/**
	 * Adds a simulation stage to an emitter, the same way the emitter's "+ Stage" menu does, and
	 * applies the given settings. For grid solvers use IterationSource DataInterface with the grid
	 * parameter, e.g. DataInterfaceName Emitter.VelocityGrid and DataInterfaceClass
	 * /Script/Niagara.NiagaraDataInterfaceGrid3DCollection.
	 *
	 * @param System The Niagara System
	 * @param EmitterName Emitter to add the stage to
	 * @param StageName Stage name shown in the stack, e.g. Advect
	 * @param StageClass Simulation stage class; None means NiagaraSimulationStageGeneric
	 * @param IterationSource What the stage iterates over
	 * @param DataInterfaceName Data interface parameter to iterate, required when IterationSource is DataInterface
	 * @param DataInterfaceClass Class of that data interface, required when IterationSource is DataInterface
	 * @param ExecuteBehavior When the stage runs relative to simulation resets
	 * @param TargetIndex Final position of the new stage in the stage list (0 runs first); -1 appends after the last stage
	 * @param bSave Save the system package after the change
	 * @return Read-back of the new stage
	 */
	UFUNCTION(meta = (AICallable), Category = "Niagara|Stages")
	static FNovaSimulationStageInfo AddSimulationStage(UNiagaraSystem* System, FName EmitterName, FName StageName, TSubclassOf<UNiagaraSimulationStageBase> StageClass = nullptr, ENiagaraIterationSource IterationSource = ENiagaraIterationSource::Particles, FName DataInterfaceName = NAME_None, TSubclassOf<UNiagaraDataInterface> DataInterfaceClass = nullptr, ENiagaraSimStageExecuteBehavior ExecuteBehavior = ENiagaraSimStageExecuteBehavior::Always, int32 TargetIndex = -1, bool bSave = true);

	/**
	 * Moves a simulation stage to another position in the emitter's stage list, like dragging it in the
	 * stack. Stages run in list order. Its modules and settings move with it.
	 *
	 * @param System The Niagara System
	 * @param EmitterName Emitter that owns the stage
	 * @param StageIndex Current index of the stage (from GetEmitterStages)
	 * @param NewIndex Index the stage should end up at; other stages shift to make room
	 * @param bSave Save the system package after the change
	 * @return The emitter's stage layout after the move
	 */
	UFUNCTION(meta = (AICallable), Category = "Niagara|Stages")
	static FNovaEmitterStagesInfo MoveSimulationStage(UNiagaraSystem* System, FName EmitterName, int32 StageIndex, int32 NewIndex, bool bSave = true);

	/**
	 * Adds an event handler to an emitter, the same way the emitter's "+ Stage > Event Handler" menu
	 * does, and applies the given settings. Niagara events only work on CPU emitters.
	 *
	 * @param System The Niagara System
	 * @param EmitterName Emitter that receives the events
	 * @param SourceEmitterName Emitter that generates the events; None means EmitterName itself
	 * @param SourceEventName Event name written by the generating module. Check the generator module for the exact name
	 * @param ExecutionMode EveryParticle runs on existing particles; SpawnedParticles spawns new ones per event
	 * @param SpawnNumber Particles spawned per event in SpawnedParticles mode
	 * @param MaxEventsPerFrame Events processed per frame; 0 means no limit
	 * @param bRandomSpawnNumber Spawn a random count between MinSpawnNumber and SpawnNumber
	 * @param MinSpawnNumber Lower bound when bRandomSpawnNumber is set
	 * @param bUpdateAttributeInitialValues Update initial attribute values of spawned particles
	 * @param bSave Save the system package after the change
	 * @return Read-back of the new event handler
	 */
	UFUNCTION(meta = (AICallable), Category = "Niagara|Stages")
	static FNovaEventHandlerInfo AddEventHandler(UNiagaraSystem* System, FName EmitterName, FName SourceEmitterName, FName SourceEventName, EScriptExecutionMode ExecutionMode = EScriptExecutionMode::EveryParticle, int32 SpawnNumber = 0, int32 MaxEventsPerFrame = 0, bool bRandomSpawnNumber = false, int32 MinSpawnNumber = 0, bool bUpdateAttributeInitialValues = true, bool bSave = true);

	/**
	 * Places a module in a simulation stage or event handler stack. (NiagaraToolset_System.AddModule
	 * only reaches the six standard stacks.)
	 *
	 * @param System The Niagara System
	 * @param EmitterName Emitter that owns the stage
	 * @param TargetStack ParticleSimulationStageScript or ParticleEventScript
	 * @param StageIndex Index from GetEmitterStages (SimulationStages or EventHandlers list)
	 * @param ModuleAsset Module script to place: a library module asset or a local module from ListLocalModules
	 * @param TargetIndex Position in the stack; -1 appends
	 * @param bSave Save the system package after the change
	 * @return Read-back of the placed module
	 */
	UFUNCTION(meta = (AICallable), Category = "Niagara|Stages")
	static FNovaStackModuleInfo AddModuleToStage(UNiagaraSystem* System, FName EmitterName, ENiagaraScriptUsage TargetStack, int32 StageIndex, UNiagaraScript* ModuleAsset, int32 TargetIndex = -1, bool bSave = true);

	/**
	 * Creates a local (scratch pad) module in the system from the default module template and places it
	 * in a stack. Edit its graph afterwards with NovaToolset_NiagaraGraph.
	 *
	 * @param System The Niagara System
	 * @param EmitterName Emitter whose stack receives the module; ignored for SystemSpawnScript/SystemUpdateScript
	 * @param TargetStack ParticleSpawnScript, ParticleUpdateScript, EmitterSpawnScript, EmitterUpdateScript, SystemSpawnScript, SystemUpdateScript, ParticleSimulationStageScript or ParticleEventScript
	 * @param StageIndex Stage index for ParticleSimulationStageScript/ParticleEventScript, otherwise ignored
	 * @param ModuleName Object name for the new module; made unique if taken. None uses ScratchModule
	 * @param bSave Save the system package after the change
	 * @return The new local module and where it was placed
	 */
	UFUNCTION(meta = (AICallable), Category = "Niagara|LocalModules")
	static FNovaLocalModuleInfo CreateLocalModule(UNiagaraSystem* System, FName EmitterName, ENiagaraScriptUsage TargetStack = ENiagaraScriptUsage::ParticleUpdateScript, int32 StageIndex = -1, FName ModuleName = NAME_None, bool bSave = true);

	/**
	 * Lists the local (scratch pad) modules owned by a system.
	 *
	 * @param System The Niagara System
	 * @return One entry per local module
	 */
	UFUNCTION(meta = (AICallable), Category = "Niagara|LocalModules")
	static TArray<FNovaLocalModuleInfo> ListLocalModules(UNiagaraSystem* System);

	/**
	 * Sets the description of a system user parameter: the tooltip shown in the User Parameters panel and
	 * on placed components. Works for every parameter type, including data interfaces such as Array Float3
	 * whose description NiagaraToolset_System.AddUserVariables does not store.
	 *
	 * @param System The Niagara System
	 * @param VariableName Parameter name, with or without the User. prefix
	 * @param Description Description text in any language; an empty string clears it
	 * @param bSave Save the system package after the change
	 * @return The parameter's description after the change
	 */
	UFUNCTION(meta = (AICallable), Category = "Niagara|UserParameters")
	static FNovaUserVariableInfo SetUserVariableDescription(UNiagaraSystem* System, FName VariableName, const FString& Description, bool bSave = true);
};
