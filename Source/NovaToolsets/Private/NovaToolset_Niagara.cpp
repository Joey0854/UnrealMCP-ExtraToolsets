#include "NovaToolset_Niagara.h"

#include "Internationalization/Text.h"
#include "NiagaraDataInterface.h"
#include "NiagaraEditorSettings.h"
#include "NiagaraEditorUtilities.h"
#include "NiagaraEmitterHandle.h"
#include "NiagaraExternalSystemEditorUtilities.h"
#include "NiagaraGraph.h"
#include "NiagaraNodeFunctionCall.h"
#include "NiagaraNodeOutput.h"
#include "NiagaraScript.h"
#include "NiagaraScriptSource.h"
#include "NiagaraScriptVariable.h"
#include "NiagaraSimulationStageBase.h"
#include "NiagaraSystem.h"
#include "NiagaraSystemEditorData.h"
#include "NiagaraTypes.h"
#include "NovaNiagaraEditorViewModel.h"
#include "ScopedTransaction.h"
#include "ViewModels/NiagaraEmitterHandleViewModel.h"
#include "ViewModels/NiagaraSystemViewModel.h"
#include "ViewModels/Stack/INiagaraStackItemGroupAddUtilities.h"
#include "ViewModels/Stack/NiagaraStackEmitterPropertiesGroup.h"
#include "ViewModels/Stack/NiagaraStackEntry.h"
#include "ViewModels/Stack/NiagaraStackGraphUtilities.h"
#include "ViewModels/Stack/NiagaraStackViewModel.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(NovaToolset_Niagara)

#define LOCTEXT_NAMESPACE "NovaToolset_Niagara"

namespace NovaToolsetNiagara
{
	bool ValidateSystem(const UNiagaraSystem* System)
	{
		if (System == nullptr)
		{
			UNovaToolset::Error(TEXT("System is null."));
			return false;
		}
		return true;
	}

	FNiagaraEmitterHandle* FindEmitter(UNiagaraSystem& System, FName EmitterName)
	{
		TArray<FString> Names;
		for (FNiagaraEmitterHandle& Handle : System.GetEmitterHandles())
		{
			if (Handle.GetName() == EmitterName)
			{
				return &Handle;
			}
			Names.Add(Handle.GetName().ToString());
		}
		UNovaToolset::Error(FString::Printf(TEXT("Emitter '%s' not found in '%s'. Emitters: %s"),
			*EmitterName.ToString(), *System.GetName(), *FString::Join(Names, TEXT(", "))));
		return nullptr;
	}

	FName EmitterNameFromId(UNiagaraSystem& System, const FGuid& Id)
	{
		if (!Id.IsValid())
		{
			return NAME_None;
		}
		for (const FNiagaraEmitterHandle& Handle : System.GetEmitterHandles())
		{
			if (Handle.GetId() == Id)
			{
				return Handle.GetName();
			}
		}
		return NAME_None;
	}

	bool IsSystemStack(ENiagaraScriptUsage Usage)
	{
		return Usage == ENiagaraScriptUsage::SystemSpawnScript || Usage == ENiagaraScriptUsage::SystemUpdateScript;
	}

	bool IsStandardStack(ENiagaraScriptUsage Usage)
	{
		switch (Usage)
		{
		case ENiagaraScriptUsage::ParticleSpawnScript:
		case ENiagaraScriptUsage::ParticleUpdateScript:
		case ENiagaraScriptUsage::EmitterSpawnScript:
		case ENiagaraScriptUsage::EmitterUpdateScript:
		case ENiagaraScriptUsage::SystemSpawnScript:
		case ENiagaraScriptUsage::SystemUpdateScript:
			return true;
		default:
			return false;
		}
	}

	bool IsStageStack(ENiagaraScriptUsage Usage)
	{
		return Usage == ENiagaraScriptUsage::ParticleSimulationStageScript || Usage == ENiagaraScriptUsage::ParticleEventScript;
	}

	FString UsageName(ENiagaraScriptUsage Usage)
	{
		return StaticEnum<ENiagaraScriptUsage>()->GetNameStringByValue(static_cast<int64>(Usage));
	}

	/** Localization-proof comparison: the editor UI may be translated, source strings are not. */
	bool TextMatches(const FText& A, const FText& B)
	{
		if (A.EqualTo(B))
		{
			return true;
		}
		const FString* SourceA = FTextInspector::GetSourceString(A);
		const FString* SourceB = FTextInspector::GetSourceString(B);
		return SourceA && SourceB && SourceA->Equals(*SourceB);
	}

	bool TextHasSource(const FText& Text, const TCHAR* Source)
	{
		const FString* SourceString = FTextInspector::GetSourceString(Text);
		return (SourceString && SourceString->Equals(Source)) || Text.ToString().Equals(Source);
	}

	/** Same options UNiagaraExternalEditUtilities uses for its headless edits. */
	TSharedRef<FNiagaraSystemViewModel> MakeDataOnlyViewModel(UNiagaraSystem& System)
	{
		TSharedRef<FNiagaraSystemViewModel> ViewModel = MakeShared<FNiagaraSystemViewModel>();
		FNiagaraSystemViewModelOptions Options;
		Options.bCanModifyEmittersFromTimeline = false;
		Options.bCompileForEdit = false;
		Options.bCanSimulate = false;
		Options.bCanAutoCompile = false;
		Options.bIsForDataProcessingOnly = true;
		// Required: Initialize subscribes to the Niagara message manager with this key, and an unset key
		// is a hard assert (NiagaraMessageManager.cpp "Tried to subscribe to an asset without a set asset key").
		Options.MessageLogGuid = System.GetAssetGuid();
		ViewModel->Initialize(System, Options);
		return ViewModel;
	}

	/**
	 * The view model stage menus run on: the open Niagara editor's own, so the editor sees the change as if
	 * its "+ Stage" button was used, or a headless one when the system is not open. Null on error.
	 */
	TSharedPtr<FNiagaraSystemViewModel> AcquireViewModel(UNiagaraSystem& System)
	{
		TSharedPtr<FNiagaraSystemViewModel> EditorViewModel;
		switch (NovaNiagara::FindEditorViewModel(System, EditorViewModel))
		{
		case NovaNiagara::EEditorLookup::Found:
			return EditorViewModel;
		case NovaNiagara::EEditorLookup::NotOpen:
			return MakeDataOnlyViewModel(System);
		default:
			UNovaToolset::Error(NovaNiagara::UnreachableEditorMessage(System));
			return nullptr;
		}
	}

	UNiagaraStackEmitterPropertiesGroup* FindPropertiesGroup(UNiagaraStackEntry* Entry, int32 Depth)
	{
		if (Entry == nullptr || Depth > 3)
		{
			return nullptr;
		}
		if (UNiagaraStackEmitterPropertiesGroup* Group = Cast<UNiagaraStackEmitterPropertiesGroup>(Entry))
		{
			return Group;
		}
		TArray<UNiagaraStackEntry*> Children;
		Entry->GetUnfilteredChildren(Children);
		for (UNiagaraStackEntry* Child : Children)
		{
			if (UNiagaraStackEmitterPropertiesGroup* Found = FindPropertiesGroup(Child, Depth + 1))
			{
				return Found;
			}
		}
		return nullptr;
	}

	/**
	 * Finds an entry of the emitter's "+ Stage" menu. Executing that action runs the editor's own add
	 * path, which also rebuilds the stage's graph output (ResetGraphForOutput is not exported, so this
	 * is the only linkable way to get a correctly wired stage).
	 */
	TSharedPtr<INiagaraStackItemGroupAddAction> FindStageAddAction(FNiagaraSystemViewModel& ViewModel, FName EmitterName,
		TFunctionRef<bool(const INiagaraStackItemGroupAddAction&)> Predicate, INiagaraStackItemGroupAddUtilities*& OutUtilities)
	{
		OutUtilities = nullptr;

		const TSharedRef<FNiagaraEmitterHandleViewModel>* HandleViewModel = ViewModel.GetEmitterHandleViewModels().FindByPredicate(
			[EmitterName](const TSharedRef<FNiagaraEmitterHandleViewModel>& Candidate) { return Candidate->GetName() == EmitterName; });
		UNiagaraStackViewModel* StackViewModel = HandleViewModel ? (*HandleViewModel)->GetEmitterStackViewModel() : nullptr;
		UNiagaraStackEntry* Root = StackViewModel ? StackViewModel->GetRootEntry() : nullptr;
		if (Root == nullptr)
		{
			UNovaToolset::Error(FString::Printf(TEXT("Could not build the stack for emitter '%s'."), *EmitterName.ToString()));
			return nullptr;
		}

		UNiagaraStackEmitterPropertiesGroup* Group = FindPropertiesGroup(Root, 0);
		if (Group == nullptr)
		{
			Root->RefreshChildren();
			Group = FindPropertiesGroup(Root, 0);
		}
		OutUtilities = Group ? Group->GetAddUtilities() : nullptr;
		if (OutUtilities == nullptr)
		{
			// NiagaraStackRoot only builds the Properties group (which owns the "+ Stage" menu) in the full
			// emitter view; Summary View and stateless emitters skip it.
			UNovaToolset::Error(FString::Printf(TEXT("Emitter '%s' has no stage add menu. Turn off Summary View for this emitter; stateless emitters do not support stages."), *EmitterName.ToString()));
			return nullptr;
		}

		TArray<TSharedRef<INiagaraStackItemGroupAddAction>> Actions;
		FNiagaraStackItemGroupAddOptions Options;
		Options.bIncludeNonLibrary = true;
		OutUtilities->GenerateAddActions(Actions, Options);

		TArray<FString> Available;
		for (const TSharedRef<INiagaraStackItemGroupAddAction>& Action : Actions)
		{
			if (Predicate(*Action))
			{
				return Action;
			}
			Available.Add(Action->GetDisplayName().ToString());
		}

		UNovaToolset::Error(FString::Printf(TEXT("No matching entry in the stage menu of emitter '%s'. Available: %s"),
			*EmitterName.ToString(), *FString::Join(Available, TEXT(", "))));
		return nullptr;
	}

	FNovaSimulationStageInfo DescribeStage(UNiagaraSimulationStageBase* Stage, int32 StageIndex)
	{
		FNovaSimulationStageInfo Info;
		Info.StageIndex = StageIndex;
		Info.Stage = Stage;
		if (Stage == nullptr)
		{
			return Info;
		}

		Info.StageClass = Stage->GetClass()->GetName();
		Info.StageName = Stage->SimulationStageName;
		Info.bEnabled = Stage->bEnabled != 0;
		Info.UsageId = Stage->Script ? Stage->Script->GetUsageId().ToString() : FString();
		if (const UNiagaraSimulationStageGeneric* Generic = Cast<UNiagaraSimulationStageGeneric>(Stage))
		{
			Info.IterationSource = Generic->IterationSource;
			Info.DataInterfaceName = Generic->DataInterface.BoundVariable.GetName();
			Info.ExecuteBehavior = Generic->ExecuteBehavior;
		}
		return Info;
	}

	FNovaEventHandlerInfo DescribeEventHandler(UNiagaraSystem& System, const FNiagaraEventScriptProperties& Props, int32 StageIndex)
	{
		FNovaEventHandlerInfo Info;
		Info.StageIndex = StageIndex;
		Info.UsageId = Props.Script ? Props.Script->GetUsageId().ToString() : FString();
		Info.ExecutionMode = Props.ExecutionMode;
		Info.SpawnNumber = static_cast<int32>(Props.SpawnNumber);
		Info.MaxEventsPerFrame = static_cast<int32>(Props.MaxEventsPerFrame);
		Info.SourceEmitterName = EmitterNameFromId(System, Props.SourceEmitterID);
		Info.SourceEventName = Props.SourceEventName;
		Info.bRandomSpawnNumber = Props.bRandomSpawnNumber;
		Info.MinSpawnNumber = static_cast<int32>(Props.MinSpawnNumber);
		Info.bUpdateAttributeInitialValues = Props.UpdateAttributeInitialValues;
		return Info;
	}

	FNovaLocalModuleInfo DescribeLocalModule(UNiagaraScript* Script)
	{
		FNovaLocalModuleInfo Info;
		Info.Script = Script;
		if (Script != nullptr)
		{
			Info.ScriptName = Script->GetFName();
			if (const FVersionedNiagaraScriptData* ScriptData = Script->GetLatestScriptData())
			{
				Info.ModuleUsageBitmask = ScriptData->ModuleUsageBitmask;
			}
		}
		return Info;
	}

	/** Resolves the output node that terminates a simulation stage or event handler stack. */
	UNiagaraNodeOutput* FindStageOutputNode(FNiagaraEmitterHandle& Handle, ENiagaraScriptUsage TargetStack, int32 StageIndex)
	{
		FVersionedNiagaraEmitterData* EmitterData = Handle.GetEmitterData();
		if (EmitterData == nullptr)
		{
			UNovaToolset::Error(TEXT("Emitter has no data."));
			return nullptr;
		}

		FGuid UsageId;
		if (TargetStack == ENiagaraScriptUsage::ParticleSimulationStageScript)
		{
			const TArray<UNiagaraSimulationStageBase*>& Stages = EmitterData->GetSimulationStages();
			if (!Stages.IsValidIndex(StageIndex) || Stages[StageIndex] == nullptr || Stages[StageIndex]->Script == nullptr)
			{
				UNovaToolset::Error(FString::Printf(TEXT("StageIndex %d is not a simulation stage of emitter '%s' (it has %d)."), StageIndex, *Handle.GetName().ToString(), Stages.Num()));
				return nullptr;
			}
			UsageId = Stages[StageIndex]->Script->GetUsageId();
		}
		else
		{
			const TArray<FNiagaraEventScriptProperties>& Handlers = EmitterData->GetEventHandlers();
			if (!Handlers.IsValidIndex(StageIndex) || Handlers[StageIndex].Script == nullptr)
			{
				UNovaToolset::Error(FString::Printf(TEXT("StageIndex %d is not an event handler of emitter '%s' (it has %d)."), StageIndex, *Handle.GetName().ToString(), Handlers.Num()));
				return nullptr;
			}
			UsageId = Handlers[StageIndex].Script->GetUsageId();
		}

		UNiagaraScriptSource* Source = Cast<UNiagaraScriptSource>(EmitterData->GraphSource);
		UNiagaraGraph* Graph = Source ? Source->NodeGraph.Get() : nullptr;
		if (Graph == nullptr)
		{
			UNovaToolset::Error(TEXT("Emitter graph is missing."));
			return nullptr;
		}

		TArray<UNiagaraNodeOutput*> OutputNodes;
		Graph->GetNodesOfClass(OutputNodes);
		for (UNiagaraNodeOutput* OutputNode : OutputNodes)
		{
			if (OutputNode->GetUsage() == TargetStack && OutputNode->GetUsageId() == UsageId)
			{
				return OutputNode;
			}
		}

		UNovaToolset::Error(FString::Printf(TEXT("No output node for %s stage %d in emitter '%s'. Open the system once in the Niagara editor to repair the graph."),
			*UsageName(TargetStack), StageIndex, *Handle.GetName().ToString()));
		return nullptr;
	}

	bool CheckModuleAllowed(const UNiagaraScript* Module, ENiagaraScriptUsage TargetStack)
	{
		if (Module == nullptr || Module->GetUsage() != ENiagaraScriptUsage::Module)
		{
			UNovaToolset::Error(TEXT("ModuleAsset must be a Niagara module script."));
			return false;
		}
		const FVersionedNiagaraScriptData* ScriptData = Module->GetLatestScriptData();
		if (ScriptData && (ScriptData->ModuleUsageBitmask & (1 << static_cast<int32>(TargetStack))) == 0)
		{
			UNovaToolset::Error(FString::Printf(TEXT("Module '%s' does not allow %s stacks (see its ModuleUsageBitmask)."), *Module->GetName(), *UsageName(TargetStack)));
			return false;
		}
		return true;
	}

	/** Places a module in a simulation stage or event handler stack. Returns an empty info on failure. */
	FNovaStackModuleInfo PlaceModuleInStage(UNiagaraSystem& System, FNiagaraEmitterHandle& Handle, ENiagaraScriptUsage TargetStack, int32 StageIndex, UNiagaraScript* Module, int32 TargetIndex)
	{
		UNiagaraNodeOutput* OutputNode = FindStageOutputNode(Handle, TargetStack, StageIndex);
		if (OutputNode == nullptr || !CheckModuleAllowed(Module, TargetStack))
		{
			return {};
		}

		System.Modify();
		UNiagaraNodeFunctionCall* ModuleNode = FNiagaraStackGraphUtilities::AddScriptModuleToStack(Module, *OutputNode, TargetIndex < 0 ? INDEX_NONE : TargetIndex);
		if (ModuleNode == nullptr)
		{
			UNovaToolset::Error(FString::Printf(TEXT("Failed to add '%s' to the stack."), *Module->GetName()));
			return {};
		}

		FNovaStackModuleInfo Info;
		Info.ModuleScript = Module;
		Info.ModuleNodeName = ModuleNode->GetFName();
		Info.DisplayName = ModuleNode->GetNodeTitle(ENodeTitleType::ListView).ToString();
		Info.TargetStack = TargetStack;
		Info.StageIndex = StageIndex;
		return Info;
	}

	/** Places a module in one of the six standard stacks through the stock external edit path. */
	FNovaStackModuleInfo PlaceModuleInStandardStack(UNiagaraSystem& System, FName EmitterName, ENiagaraScriptUsage TargetStack, UNiagaraScript* Module)
	{
		FNiagaraExt_StackItemReference Ref;
		Ref.System = &System;
		Ref.EmitterName = IsSystemStack(TargetStack) ? NAME_None : EmitterName;
		Ref.ScriptName = FName(*UsageName(TargetStack));

		FNiagaraExternalEditContext Context(Ref);
		FNiagaraExt_ModuleTopology Topology;
		UNiagaraExternalEditUtilities::AddModule(Ref, Module, Topology, Context);
		if (Context.HasErrors())
		{
			for (const FText& ContextError : Context.Errors)
			{
				UNovaToolset::Error(ContextError);
			}
			return {};
		}

		FNovaStackModuleInfo Info;
		Info.ModuleScript = Module;
		Info.ModuleNodeName = Topology.ModuleName;
		Info.DisplayName = Topology.ModuleName.ToString();
		Info.TargetStack = TargetStack;
		return Info;
	}

	void FinishEdit(UNiagaraSystem& System, bool bSave)
	{
		// An open Niagara editor rebuilds its view models on this notification (FNiagaraSystemViewModel::SystemChanged).
		FPropertyChangedEvent ChangedEvent(nullptr, EPropertyChangeType::ValueSet);
		System.PostEditChangeProperty(ChangedEvent);
		System.RequestCompile(false);

		TSharedPtr<FNiagaraSystemViewModel> EditorViewModel;
		if (NovaNiagara::FindEditorViewModel(System, EditorViewModel) == NovaNiagara::EEditorLookup::Found)
		{
			// The stack's own drag and drop requests the same deferred refresh after reordering stages.
			for (const TSharedRef<FNiagaraEmitterHandleViewModel>& HandleViewModel : EditorViewModel->GetEmitterHandleViewModels())
			{
				if (UNiagaraStackViewModel* StackViewModel = HandleViewModel->GetEmitterStackViewModel())
				{
					StackViewModel->RequestRefreshDeferred();
				}
			}
		}

		if (bSave)
		{
			UNovaToolset::SaveAssetSilently(&System);
		}
	}
}

FNovaEmitterStagesInfo UNovaToolset_Niagara::GetEmitterStages(UNiagaraSystem* System, FName EmitterName)
{
	using namespace NovaToolsetNiagara;

	if (!ValidateSystem(System))
	{
		return {};
	}
	FNiagaraEmitterHandle* Handle = FindEmitter(*System, EmitterName);
	FVersionedNiagaraEmitterData* EmitterData = Handle ? Handle->GetEmitterData() : nullptr;
	if (EmitterData == nullptr)
	{
		return {};
	}

	FNovaEmitterStagesInfo Info;
	Info.EmitterName = EmitterName;
	Info.SimTarget = EmitterData->SimTarget;

	const TArray<UNiagaraSimulationStageBase*>& Stages = EmitterData->GetSimulationStages();
	for (int32 Index = 0; Index < Stages.Num(); ++Index)
	{
		Info.SimulationStages.Add(DescribeStage(Stages[Index], Index));
	}

	const TArray<FNiagaraEventScriptProperties>& Handlers = EmitterData->GetEventHandlers();
	for (int32 Index = 0; Index < Handlers.Num(); ++Index)
	{
		Info.EventHandlers.Add(DescribeEventHandler(*System, Handlers[Index], Index));
	}
	return Info;
}

FNovaSimulationStageInfo UNovaToolset_Niagara::AddSimulationStage(UNiagaraSystem* System, FName EmitterName, FName StageName, TSubclassOf<UNiagaraSimulationStageBase> StageClass, ENiagaraIterationSource IterationSource, FName DataInterfaceName, TSubclassOf<UNiagaraDataInterface> DataInterfaceClass, ENiagaraSimStageExecuteBehavior ExecuteBehavior, int32 TargetIndex, bool bSave)
{
	using namespace NovaToolsetNiagara;

	if (!ValidateSystem(System))
	{
		return {};
	}
	FNiagaraEmitterHandle* Handle = FindEmitter(*System, EmitterName);
	FVersionedNiagaraEmitterData* EmitterData = Handle ? Handle->GetEmitterData() : nullptr;
	if (EmitterData == nullptr)
	{
		return {};
	}

	UClass* ResolvedStageClass = StageClass ? StageClass.Get() : UNiagaraSimulationStageGeneric::StaticClass();
	if (ResolvedStageClass->HasAnyClassFlags(CLASS_Abstract))
	{
		Error(FString::Printf(TEXT("StageClass '%s' is abstract."), *ResolvedStageClass->GetName()));
		return {};
	}
	const bool bIsGeneric = ResolvedStageClass->IsChildOf(UNiagaraSimulationStageGeneric::StaticClass());
	if (bIsGeneric && IterationSource == ENiagaraIterationSource::DataInterface && (DataInterfaceName.IsNone() || !DataInterfaceClass))
	{
		Error(TEXT("DataInterfaceName and DataInterfaceClass are required when IterationSource is DataInterface."));
		return {};
	}

	const TArray<UNiagaraSimulationStageBase*> StagesBefore = EmitterData->GetSimulationStages();
	if (TargetIndex < -1 || TargetIndex > StagesBefore.Num())
	{
		Error(FString::Printf(TEXT("TargetIndex %d is out of range; use -1 or 0-%d (emitter '%s' has %d stages)."), TargetIndex, StagesBefore.Num(), *EmitterName.ToString(), StagesBefore.Num()));
		return {};
	}

	FScopedTransaction Transaction(LOCTEXT("AddSimulationStage", "Nova Toolset: Add Simulation Stage"));
	{
		TSharedPtr<FNiagaraSystemViewModel> ViewModel = AcquireViewModel(*System);
		if (!ViewModel.IsValid())
		{
			return {};
		}
		const FText StageDisplayName = ResolvedStageClass->GetDisplayNameText();
		INiagaraStackItemGroupAddUtilities* AddUtilities = nullptr;
		TSharedPtr<INiagaraStackItemGroupAddAction> AddAction = FindStageAddAction(*ViewModel, EmitterName,
			[&StageDisplayName](const INiagaraStackItemGroupAddAction& Action) { return TextMatches(Action.GetDisplayName(), StageDisplayName); },
			AddUtilities);
		if (!AddAction.IsValid())
		{
			return {};
		}
		// The add action appends the stage and then calls MoveSimulationStageToIndex(TargetIndex). Starting
		// from the last slot, that insert position is also the stage's final index.
		AddUtilities->ExecuteAddAction(AddAction.ToSharedRef(), TargetIndex < 0 ? INDEX_NONE : TargetIndex);
	}

	EmitterData = Handle->GetEmitterData();
	const TArray<UNiagaraSimulationStageBase*>& StagesAfter = EmitterData->GetSimulationStages();
	UNiagaraSimulationStageBase* NewStage = nullptr;
	int32 NewStageIndex = INDEX_NONE;
	for (int32 Index = 0; Index < StagesAfter.Num(); ++Index)
	{
		if (!StagesBefore.Contains(StagesAfter[Index]))
		{
			NewStage = StagesAfter[Index];
			NewStageIndex = Index;
			break;
		}
	}
	if (NewStage == nullptr)
	{
		Error(TEXT("The stage menu action ran but no new simulation stage appeared."));
		return {};
	}

	NewStage->Modify();
	if (!StageName.IsNone())
	{
		NewStage->SimulationStageName = StageName;
	}
	FProperty* ChangedProperty = FindFProperty<FProperty>(UNiagaraSimulationStageBase::StaticClass(), GET_MEMBER_NAME_CHECKED(UNiagaraSimulationStageBase, SimulationStageName));
	if (UNiagaraSimulationStageGeneric* Generic = Cast<UNiagaraSimulationStageGeneric>(NewStage))
	{
		Generic->IterationSource = IterationSource;
		Generic->ExecuteBehavior = ExecuteBehavior;
		if (IterationSource == ENiagaraIterationSource::DataInterface)
		{
			Generic->DataInterface.BoundVariable = FNiagaraVariable(FNiagaraTypeDefinition(DataInterfaceClass.Get()), DataInterfaceName);
		}
		ChangedProperty = FindFProperty<FProperty>(UNiagaraSimulationStageGeneric::StaticClass(), GET_MEMBER_NAME_CHECKED(UNiagaraSimulationStageGeneric, DataInterface));
	}
	// Both properties are on the stage's force-recompile list, so this requests the emitter recompile.
	FPropertyChangedEvent ChangedEvent(ChangedProperty, EPropertyChangeType::ValueSet);
	NewStage->PostEditChangeProperty(ChangedEvent);

	FinishEdit(*System, bSave);
	return DescribeStage(NewStage, NewStageIndex);
}

FNovaEmitterStagesInfo UNovaToolset_Niagara::MoveSimulationStage(UNiagaraSystem* System, FName EmitterName, int32 StageIndex, int32 NewIndex, bool bSave)
{
	using namespace NovaToolsetNiagara;

	if (!ValidateSystem(System))
	{
		return {};
	}
	FNiagaraEmitterHandle* Handle = FindEmitter(*System, EmitterName);
	FVersionedNiagaraEmitterData* EmitterData = Handle ? Handle->GetEmitterData() : nullptr;
	if (EmitterData == nullptr)
	{
		return {};
	}

	const TArray<UNiagaraSimulationStageBase*>& Stages = EmitterData->GetSimulationStages();
	if (!Stages.IsValidIndex(StageIndex) || !Stages.IsValidIndex(NewIndex))
	{
		Error(FString::Printf(TEXT("StageIndex %d and NewIndex %d must both be within 0-%d (emitter '%s' has %d stages)."),
			StageIndex, NewIndex, Stages.Num() - 1, *EmitterName.ToString(), Stages.Num()));
		return {};
	}

	if (StageIndex != NewIndex)
	{
		UNiagaraSimulationStageBase* Stage = Stages[StageIndex];
		const FVersionedNiagaraEmitter VersionedEmitter = Handle->GetInstance();

		FScopedTransaction Transaction(LOCTEXT("MoveSimulationStage", "Nova Toolset: Move Simulation Stage"));
		// MoveSimulationStageToIndex does not call Modify itself; without it the move is invisible to undo.
		VersionedEmitter.Emitter->Modify();
		// Its index is an insert position in the list before removal (drag and drop semantics), so moving
		// down needs one past the final slot.
		const int32 InsertPosition = NewIndex > StageIndex ? NewIndex + 1 : NewIndex;
		VersionedEmitter.Emitter->MoveSimulationStageToIndex(Stage, InsertPosition, VersionedEmitter.Version);
		FinishEdit(*System, bSave);
	}

	return GetEmitterStages(System, EmitterName);
}

FNovaEventHandlerInfo UNovaToolset_Niagara::AddEventHandler(UNiagaraSystem* System, FName EmitterName, FName SourceEmitterName, FName SourceEventName, EScriptExecutionMode ExecutionMode, int32 SpawnNumber, int32 MaxEventsPerFrame, bool bRandomSpawnNumber, int32 MinSpawnNumber, bool bUpdateAttributeInitialValues, bool bSave)
{
	using namespace NovaToolsetNiagara;

	if (!ValidateSystem(System))
	{
		return {};
	}
	FNiagaraEmitterHandle* Handle = FindEmitter(*System, EmitterName);
	FVersionedNiagaraEmitterData* EmitterData = Handle ? Handle->GetEmitterData() : nullptr;
	if (EmitterData == nullptr)
	{
		return {};
	}

	if (SpawnNumber < 0 || MaxEventsPerFrame < 0 || MinSpawnNumber < 0)
	{
		Error(TEXT("SpawnNumber, MaxEventsPerFrame and MinSpawnNumber must not be negative."));
		return {};
	}
	if (EmitterData->SimTarget == ENiagaraSimTarget::GPUComputeSim)
	{
		Error(FString::Printf(TEXT("Emitter '%s' is a GPU emitter. Niagara events only work on CPU emitters."), *EmitterName.ToString()));
		return {};
	}

	FGuid SourceEmitterId;
	if (!SourceEmitterName.IsNone() && SourceEmitterName != EmitterName)
	{
		FNiagaraEmitterHandle* SourceHandle = FindEmitter(*System, SourceEmitterName);
		if (SourceHandle == nullptr)
		{
			return {};
		}
		SourceEmitterId = SourceHandle->GetId();
	}

	TSet<FGuid> UsageIdsBefore;
	for (const FNiagaraEventScriptProperties& Existing : EmitterData->GetEventHandlers())
	{
		if (Existing.Script)
		{
			UsageIdsBefore.Add(Existing.Script->GetUsageId());
		}
	}

	FScopedTransaction Transaction(LOCTEXT("AddEventHandler", "Nova Toolset: Add Event Handler"));
	{
		TSharedPtr<FNiagaraSystemViewModel> ViewModel = AcquireViewModel(*System);
		if (!ViewModel.IsValid())
		{
			return {};
		}
		INiagaraStackItemGroupAddUtilities* AddUtilities = nullptr;
		TSharedPtr<INiagaraStackItemGroupAddAction> AddAction = FindStageAddAction(*ViewModel, EmitterName,
			[](const INiagaraStackItemGroupAddAction& Action) { return TextHasSource(Action.GetDisplayName(), TEXT("Event Handler")); },
			AddUtilities);
		if (!AddAction.IsValid())
		{
			return {};
		}
		AddUtilities->ExecuteAddAction(AddAction.ToSharedRef(), INDEX_NONE);
	}

	EmitterData = Handle->GetEmitterData();
	int32 NewHandlerIndex = INDEX_NONE;
	for (int32 Index = 0; Index < EmitterData->EventHandlerScriptProps.Num(); ++Index)
	{
		const UNiagaraScript* Script = EmitterData->EventHandlerScriptProps[Index].Script;
		if (Script && !UsageIdsBefore.Contains(Script->GetUsageId()))
		{
			NewHandlerIndex = Index;
			break;
		}
	}
	if (NewHandlerIndex == INDEX_NONE)
	{
		Error(TEXT("The event handler menu action ran but no new event handler appeared."));
		return {};
	}

	const FVersionedNiagaraEmitter VersionedEmitter = Handle->GetInstance();
	VersionedEmitter.Emitter->Modify();

	FNiagaraEventScriptProperties& Props = EmitterData->EventHandlerScriptProps[NewHandlerIndex];
	Props.ExecutionMode = ExecutionMode;
	Props.SpawnNumber = static_cast<uint32>(SpawnNumber);
	Props.MaxEventsPerFrame = static_cast<uint32>(MaxEventsPerFrame);
	Props.SourceEmitterID = SourceEmitterId;
	Props.SourceEventName = SourceEventName;
	Props.bRandomSpawnNumber = bRandomSpawnNumber;
	Props.MinSpawnNumber = static_cast<uint32>(MinSpawnNumber);
	Props.UpdateAttributeInitialValues = bUpdateAttributeInitialValues;

	// Same notification the stack's event handler details panel sends after an edit.
	FPropertyChangedEvent ChangedEvent(nullptr, EPropertyChangeType::ValueSet);
	VersionedEmitter.Emitter->PostEditChangeVersionedProperty(ChangedEvent, VersionedEmitter.Version);

	FinishEdit(*System, bSave);
	return DescribeEventHandler(*System, Handle->GetEmitterData()->EventHandlerScriptProps[NewHandlerIndex], NewHandlerIndex);
}

FNovaStackModuleInfo UNovaToolset_Niagara::AddModuleToStage(UNiagaraSystem* System, FName EmitterName, ENiagaraScriptUsage TargetStack, int32 StageIndex, UNiagaraScript* ModuleAsset, int32 TargetIndex, bool bSave)
{
	using namespace NovaToolsetNiagara;

	if (!ValidateSystem(System))
	{
		return {};
	}
	if (!IsStageStack(TargetStack))
	{
		Error(TEXT("TargetStack must be ParticleSimulationStageScript or ParticleEventScript. Use NiagaraToolset_System.AddModule for the standard stacks."));
		return {};
	}
	FNiagaraEmitterHandle* Handle = FindEmitter(*System, EmitterName);
	if (Handle == nullptr)
	{
		return {};
	}

	FScopedTransaction Transaction(LOCTEXT("AddModuleToStage", "Nova Toolset: Add Module To Stage"));
	FNovaStackModuleInfo Info = PlaceModuleInStage(*System, *Handle, TargetStack, StageIndex, ModuleAsset, TargetIndex);
	if (Info.ModuleScript == nullptr)
	{
		Transaction.Cancel();
		return {};
	}

	FinishEdit(*System, bSave);
	return Info;
}

FNovaLocalModuleInfo UNovaToolset_Niagara::CreateLocalModule(UNiagaraSystem* System, FName EmitterName, ENiagaraScriptUsage TargetStack, int32 StageIndex, FName ModuleName, bool bSave)
{
	using namespace NovaToolsetNiagara;

	if (!ValidateSystem(System))
	{
		return {};
	}

	const bool bStageStack = IsStageStack(TargetStack);
	if (!bStageStack && !IsStandardStack(TargetStack))
	{
		Error(FString::Printf(TEXT("TargetStack %s cannot hold modules."), *UsageName(TargetStack)));
		return {};
	}

	FNiagaraEmitterHandle* Handle = nullptr;
	if (!IsSystemStack(TargetStack))
	{
		Handle = FindEmitter(*System, EmitterName);
		if (Handle == nullptr)
		{
			return {};
		}
	}

	// Validate the destination before creating anything, so a bad StageIndex leaves no orphan script.
	if (bStageStack && FindStageOutputNode(*Handle, TargetStack, StageIndex) == nullptr)
	{
		return {};
	}

	UNiagaraScript* DefaultModule = Cast<UNiagaraScript>(GetDefault<UNiagaraEditorSettings>()->DefaultModuleScript.TryLoad());
	if (DefaultModule == nullptr)
	{
		Error(TEXT("DefaultModuleScript in the Niagara editor settings could not be loaded."));
		return {};
	}

	FScopedTransaction Transaction(LOCTEXT("CreateLocalModule", "Nova Toolset: Create Local Module"));

	// Mirrors UNiagaraScratchPadViewModel::CreateNewScript for system assets: duplicate the default
	// module into the system and register it in ScratchPadScripts.
	if (!System->HasAnyFlags(RF_Transactional))
	{
		System->SetFlags(RF_Transactional);
	}
	System->Modify();

	const FName BaseName = ModuleName.IsNone() ? FName(TEXT("ScratchModule")) : ModuleName;
	const FName ObjectName = StaticFindObjectFast(nullptr, System, BaseName) ? MakeUniqueObjectName(System, UNiagaraScript::StaticClass(), BaseName) : BaseName;
	UNiagaraScript* NewScript = CastChecked<UNiagaraScript>(StaticDuplicateObject(DefaultModule, System, ObjectName));
	NewScript->ClearFlags(RF_Public | RF_Standalone);
	System->ScratchPadScripts.Add(NewScript);
	if (FVersionedNiagaraScriptData* ScriptData = NewScript->GetLatestScriptData())
	{
		ScriptData->ModuleUsageBitmask |= (1 << static_cast<int32>(TargetStack));
	}

	FNovaStackModuleInfo Placed = bStageStack
		? PlaceModuleInStage(*System, *Handle, TargetStack, StageIndex, NewScript, INDEX_NONE)
		: PlaceModuleInStandardStack(*System, EmitterName, TargetStack, NewScript);
	if (Placed.ModuleScript == nullptr)
	{
		// Cancel() only drops the undo record; move the duplicate out of the system so no unreferenced
		// subobject gets saved with it.
		System->ScratchPadScripts.Remove(NewScript);
		NewScript->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_NonTransactional);
		Transaction.Cancel();

		TSharedPtr<FNiagaraSystemViewModel> EditorViewModel;
		if (NovaNiagara::FindEditorViewModel(*System, EditorViewModel) == NovaNiagara::EEditorLookup::Found)
		{
			// The placement attempt may already have refreshed the open editor while the script was listed.
			EditorViewModel->RefreshAll();
		}
		return {};
	}

	FinishEdit(*System, bSave);

	FNovaLocalModuleInfo Info = DescribeLocalModule(NewScript);
	Info.PlacedModule = Placed;
	return Info;
}

TArray<FNovaLocalModuleInfo> UNovaToolset_Niagara::ListLocalModules(UNiagaraSystem* System)
{
	using namespace NovaToolsetNiagara;

	TArray<FNovaLocalModuleInfo> Modules;
	if (!ValidateSystem(System))
	{
		return Modules;
	}
	for (const TObjectPtr<UNiagaraScript>& Script : System->ScratchPadScripts)
	{
		if (Script)
		{
			Modules.Add(DescribeLocalModule(Script));
		}
	}
	return Modules;
}

FNovaUserVariableInfo UNovaToolset_Niagara::SetUserVariableDescription(UNiagaraSystem* System, FName VariableName, const FString& Description, bool bSave)
{
	using namespace NovaToolsetNiagara;

	if (!ValidateSystem(System))
	{
		return {};
	}

	// The exact variable (name and type) from the exposed parameters is required: the metadata lookup below
	// redirects and compares it by value.
	FString WantedName = VariableName.ToString();
	WantedName.RemoveFromStart(TEXT("User."));
	TArray<FNiagaraVariable> UserParameters;
	System->GetExposedParameters().GetUserParameters(UserParameters);
	const FNiagaraVariable* UserParameter = UserParameters.FindByPredicate([&WantedName](const FNiagaraVariable& Candidate)
	{
		FString CandidateName = Candidate.GetName().ToString();
		CandidateName.RemoveFromStart(TEXT("User."));
		return CandidateName.Equals(WantedName, ESearchCase::IgnoreCase);
	});
	if (UserParameter == nullptr)
	{
		TArray<FString> Names;
		for (const FNiagaraVariable& Candidate : UserParameters)
		{
			Names.Add(Candidate.GetName().ToString());
		}
		Error(FString::Printf(TEXT("User parameter '%s' not found in '%s'. User parameters: %s"),
			*VariableName.ToString(), *System->GetName(), *FString::Join(Names, TEXT(", "))));
		return {};
	}

	UNiagaraSystemEditorData* EditorData = Cast<UNiagaraSystemEditorData>(System->GetEditorData());
	if (EditorData == nullptr)
	{
		Error(FString::Printf(TEXT("'%s' has no system editor data."), *System->GetName()));
		return {};
	}

	FNovaUserVariableInfo Info;
	{
		FScopedTransaction Transaction(LOCTEXT("SetUserVariableDescription", "Nova Toolset: Set User Parameter Description"));
		// The lookup adds a metadata entry for a parameter that never had one, which changes the editor data.
		EditorData->Modify();
		UNiagaraScriptVariable* ScriptVariable = FNiagaraEditorUtilities::UserParameters::GetScriptVariableForUserParameter(*UserParameter, *System);
		if (ScriptVariable == nullptr)
		{
			Error(FString::Printf(TEXT("Could not resolve the editor metadata of '%s'."), *UserParameter->GetName().ToString()));
			Transaction.Cancel();
			return {};
		}

		// Only loaded script variables get RF_Transactional (in PostLoad); one created by the lookup above needs it for undo.
		ScriptVariable->SetFlags(RF_Transactional);
		ScriptVariable->Modify();
		ScriptVariable->Metadata.Description = FText::FromString(Description);
		ScriptVariable->PostEditChange();
		System->MarkPackageDirty();
		// The User Parameters panel of an open Niagara editor rebuilds from the script variables on this.
		EditorData->OnUserParameterScriptVariablesSynced().Broadcast();

		Info.VariableName = UserParameter->GetName();
		Info.Description = ScriptVariable->Metadata.Description.ToString();
	}

	Info.bSaved = bSave && SaveAssetSilently(System);
	return Info;
}

#undef LOCTEXT_NAMESPACE
