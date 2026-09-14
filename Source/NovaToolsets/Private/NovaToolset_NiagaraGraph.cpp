#include "NovaToolset_NiagaraGraph.h"

#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_Niagara.h"
#include "Internationalization/Text.h"
#include "NiagaraActions.h"
#include "NiagaraDataInterface.h"
#include "NiagaraGraph.h"
#include "NiagaraNode.h"
#include "NiagaraNodeCustomHlsl.h"
#include "NiagaraNodeFunctionCall.h"
#include "NiagaraNodeOutput.h"
#include "NiagaraScript.h"
#include "NiagaraScriptSource.h"
#include "NiagaraSystem.h"
#include "NiagaraTypes.h"
#include "NovaNiagaraEditorViewModel.h"
#include "ScopedTransaction.h"
#include "UObject/UObjectIterator.h"
#include "UObject/UnrealType.h"
#include "ViewModels/NiagaraScratchPadScriptViewModel.h"
#include "ViewModels/NiagaraScratchPadViewModel.h"
#include "ViewModels/NiagaraSystemViewModel.h"
#include "ViewModels/Stack/NiagaraStackGraphUtilities.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(NovaToolset_NiagaraGraph)

#define LOCTEXT_NAMESPACE "NovaToolset_NiagaraGraph"

namespace NovaToolsetNiagaraGraph
{
	// UNiagaraNode::ReallocatePins is exported but protected. A member pointer formed through a derived
	// class is the standard way to reach it; this type is never instantiated.
	struct FNiagaraNodeProtectedAccess : public UNiagaraNode
	{
		using FReallocatePinsFn = bool (UNiagaraNode::*)(bool);
		static FReallocatePinsFn ReallocatePinsFn() { return &FNiagaraNodeProtectedAccess::ReallocatePins; }
	};

	const FName CustomHlslPropertyName(TEXT("CustomHlsl"));
	const FName VirtualIncludesPropertyName(TEXT("VirtualIncludeFilePaths"));
	const FName AbsoluteIncludesPropertyName(TEXT("AbsoluteIncludeFilePaths"));

	/** Where a script's graph is read and edited. */
	struct FGraphTarget
	{
		UNiagaraGraph* Graph = nullptr;
		/**
		 * Set when the script is a local module of a system open in the Niagara editor. The editor edits a
		 * working copy of the module and copies it over the original on Apply, so tools use that copy too;
		 * edits to the original would be invisible in the editor and lost on its next Apply.
		 */
		TSharedPtr<FNiagaraScratchPadScriptViewModel> ScratchPadScript;
	};

	bool ResolveGraphTarget(UNiagaraScript* Script, bool bForWrite, FGraphTarget& Out)
	{
		if (Script == nullptr)
		{
			UNovaToolset::Error(TEXT("Script is null."));
			return false;
		}

		UNiagaraScript* GraphScript = Script;
		UObject* Owner = Script->GetOutermostObject();
		if (UNiagaraSystem* System = Cast<UNiagaraSystem>(Owner))
		{
			if (System->ScratchPadScripts.Contains(Script))
			{
				TSharedPtr<FNiagaraSystemViewModel> EditorViewModel;
				const NovaNiagara::EEditorLookup Lookup = NovaNiagara::FindEditorViewModel(*System, EditorViewModel);
				if (Lookup == NovaNiagara::EEditorLookup::Unreachable)
				{
					UNovaToolset::Error(NovaNiagara::UnreachableEditorMessage(*System));
					return false;
				}
				UNiagaraScratchPadViewModel* ScratchPad = EditorViewModel.IsValid() ? EditorViewModel->GetScriptScratchPadViewModel() : nullptr;
				if (ScratchPad != nullptr)
				{
					Out.ScratchPadScript = ScratchPad->GetViewModelForScript(Script);
					if (!Out.ScratchPadScript.IsValid() || Out.ScratchPadScript->GetEditScript().Script == nullptr)
					{
						UNovaToolset::Error(FString::Printf(TEXT("The open Niagara editor for '%s' has no working copy of '%s'. Retry after the editor refreshes."),
							*System->GetPathName(), *Script->GetName()));
						return false;
					}
					GraphScript = Out.ScratchPadScript->GetEditScript().Script;
				}
			}
			else if (bForWrite && !UNovaToolset::EnsureNoOpenEditor(System))
			{
				// System and emitter graphs back the open editor's stacks, which do not refresh on direct graph edits.
				return false;
			}
		}
		else if (bForWrite && !UNovaToolset::EnsureNoOpenEditor(Owner))
		{
			// Module and emitter asset editors keep a working copy as well, but expose no API to reach it.
			return false;
		}

		UNiagaraScriptSource* Source = Cast<UNiagaraScriptSource>(GraphScript->GetLatestSource());
		Out.Graph = Source ? Source->NodeGraph.Get() : nullptr;
		if (Out.Graph == nullptr)
		{
			UNovaToolset::Error(FString::Printf(TEXT("Script '%s' has no editable graph."), *Script->GetPathName()));
			return false;
		}
		return true;
	}

	UNiagaraGraph* GetScriptGraph(UNiagaraScript* Script)
	{
		FGraphTarget Target;
		return ResolveGraphTarget(Script, false, Target) ? Target.Graph : nullptr;
	}

	/** Graph lookup for write tools; also refuses module and emitter assets whose own editor is open. */
	UNiagaraGraph* GetWritableGraph(UNiagaraScript* Script)
	{
		FGraphTarget Target;
		return ResolveGraphTarget(Script, true, Target) ? Target.Graph : nullptr;
	}

	FString NodeIdOf(const UEdGraphNode* Node)
	{
		return Node ? Node->NodeGuid.ToString(EGuidFormats::Digits) : FString();
	}

	UEdGraphNode* FindNode(UNiagaraGraph& Graph, const FString& NodeId)
	{
		FGuid Guid;
		const bool bIsGuid = FGuid::Parse(NodeId, Guid);
		for (UEdGraphNode* Node : Graph.Nodes)
		{
			if (Node && ((bIsGuid && Node->NodeGuid == Guid) || Node->GetName() == NodeId))
			{
				return Node;
			}
		}
		UNovaToolset::Error(FString::Printf(TEXT("Node '%s' not found. Use GetGraph for node ids."), *NodeId));
		return nullptr;
	}

	UEdGraphPin* FindPin(UEdGraphNode& Node, FName PinName, EEdGraphPinDirection Direction)
	{
		if (UEdGraphPin* Pin = Node.FindPin(PinName, Direction))
		{
			return Pin;
		}
		TArray<FString> Names;
		for (const UEdGraphPin* Pin : Node.Pins)
		{
			if (Pin && Pin->Direction == Direction)
			{
				Names.Add(FString::Printf(TEXT("'%s'"), *Pin->PinName.ToString()));
			}
		}
		UNovaToolset::Error(FString::Printf(TEXT("%s pin '%s' not found on node '%s'. %s pins: %s"),
			Direction == EGPD_Output ? TEXT("Output") : TEXT("Input"), *PinName.ToString(),
			*Node.GetNodeTitle(ENodeTitleType::ListView).ToString(),
			Direction == EGPD_Output ? TEXT("Output") : TEXT("Input"), *FString::Join(Names, TEXT(", "))));
		return nullptr;
	}

	bool IsAddPin(const UEdGraphPin* Pin)
	{
		return Pin && Pin->PinType.PinCategory == UEdGraphSchema_Niagara::PinCategoryMisc && Pin->PinName == TEXT("Add");
	}

	FString* FindStringProperty(UObject* Object, FName Name)
	{
		FStrProperty* Property = FindFProperty<FStrProperty>(Object->GetClass(), Name);
		return Property ? Property->GetPropertyValuePtr_InContainer(Object) : nullptr;
	}

	TArray<FString>* FindStringArrayProperty(UObject* Object, FName Name)
	{
		FArrayProperty* Property = FindFProperty<FArrayProperty>(Object->GetClass(), Name);
		return Property && Property->Inner->IsA<FStrProperty>() ? Property->ContainerPtrToValuePtr<TArray<FString>>(Object) : nullptr;
	}

	/** TArray<FFilePath> accessed through reflection, so the plugin does not depend on where FFilePath is declared. */
	bool AccessFilePathArray(UObject* Object, FName Name, TArray<FString>* InValues, TArray<FString>* OutValues)
	{
		FArrayProperty* Property = FindFProperty<FArrayProperty>(Object->GetClass(), Name);
		FStructProperty* Inner = Property ? CastField<FStructProperty>(Property->Inner) : nullptr;
		FStrProperty* PathProperty = Inner ? FindFProperty<FStrProperty>(Inner->Struct, TEXT("FilePath")) : nullptr;
		if (PathProperty == nullptr)
		{
			return false;
		}

		FScriptArrayHelper Helper(Property, Property->ContainerPtrToValuePtr<void>(Object));
		if (OutValues)
		{
			for (int32 Index = 0; Index < Helper.Num(); ++Index)
			{
				OutValues->Add(PathProperty->GetPropertyValue_InContainer(Helper.GetRawPtr(Index)));
			}
		}
		if (InValues)
		{
			Helper.EmptyValues();
			for (const FString& Value : *InValues)
			{
				const int32 Index = Helper.AddValue();
				PathProperty->SetPropertyValue_InContainer(Helper.GetRawPtr(Index), Value);
			}
		}
		return true;
	}

	/**
	 * Specifiers of a data interface function call. The node's own map stays empty until its widget is
	 * built, and compiling copies that empty map over Signature.FunctionSpecifiers, so an unopened node that
	 * has been compiled has neither. Fall back to the data interface's own signature for the function, the
	 * same lookup UNiagaraNodeFunctionCall::RefreshSignature uses.
	 */
	TMap<FName, FName> GetEffectiveSpecifiers(const UNiagaraNodeFunctionCall& FunctionCall)
	{
		if (FunctionCall.FunctionSpecifiers.Num() > 0)
		{
			return FunctionCall.FunctionSpecifiers;
		}
		const FNiagaraFunctionSignature& Signature = FunctionCall.Signature;
		if (Signature.FunctionSpecifiers.Num() > 0)
		{
			return Signature.FunctionSpecifiers;
		}
		if (FunctionCall.FunctionScript != nullptr || Signature.Name.IsNone() || !Signature.bMemberFunction
			|| Signature.Inputs.Num() == 0 || !Signature.Inputs[0].GetType().IsDataInterface())
		{
			return {};
		}

		const UClass* DataInterfaceClass = Signature.Inputs[0].GetType().GetClass();
		const UNiagaraDataInterface* DataInterface = DataInterfaceClass ? Cast<UNiagaraDataInterface>(DataInterfaceClass->GetDefaultObject()) : nullptr;
		if (DataInterface == nullptr)
		{
			return {};
		}
		TArray<FNiagaraFunctionSignature> BaseSignatures;
		DataInterface->GetFunctionSignatures(BaseSignatures);
		const FNiagaraFunctionSignature* BaseSignature = BaseSignatures.FindByPredicate(
			[&Signature](const FNiagaraFunctionSignature& Candidate) { return Candidate.Name == Signature.Name; });
		return BaseSignature ? BaseSignature->FunctionSpecifiers : TMap<FName, FName>();
	}

	FNovaGraphNodeInfo DescribeNode(UEdGraphNode* Node)
	{
		FNovaGraphNodeInfo Info;
		if (Node == nullptr)
		{
			return Info;
		}

		Info.NodeId = NodeIdOf(Node);
		Info.NodeClass = Node->GetClass()->GetName();
		Info.Title = Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString();
		Info.PosX = Node->NodePosX;
		Info.PosY = Node->NodePosY;

		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (Pin == nullptr)
			{
				continue;
			}
			FNovaGraphPinInfo PinInfo;
			PinInfo.PinName = Pin->PinName;
			PinInfo.bOutput = Pin->Direction == EGPD_Output;
			PinInfo.bIsAddPin = IsAddPin(Pin);
			PinInfo.bHidden = Pin->bHidden;
			if (!PinInfo.bIsAddPin)
			{
				const FNiagaraTypeDefinition TypeDef = UEdGraphSchema_Niagara::PinToTypeDefinition(Pin);
				PinInfo.Type = TypeDef.IsValid() ? TypeDef.GetName() : FString();
			}
			PinInfo.DefaultValue = Pin->DefaultValue;
			for (const UEdGraphPin* Linked : Pin->LinkedTo)
			{
				if (Linked)
				{
					PinInfo.LinkedTo.Add(NodeIdOf(Linked->GetOwningNode()) + TEXT("|") + Linked->PinName.ToString());
				}
			}
			Info.Pins.Add(MoveTemp(PinInfo));
		}

		if (UNiagaraNodeCustomHlsl* HlslNode = Cast<UNiagaraNodeCustomHlsl>(Node))
		{
			if (const FString* Hlsl = FindStringProperty(HlslNode, CustomHlslPropertyName))
			{
				Info.CustomHlsl = *Hlsl;
			}
			if (const TArray<FString>* VirtualIncludes = FindStringArrayProperty(HlslNode, VirtualIncludesPropertyName))
			{
				Info.VirtualIncludeFilePaths = *VirtualIncludes;
			}
			AccessFilePathArray(HlslNode, AbsoluteIncludesPropertyName, nullptr, &Info.AbsoluteIncludeFilePaths);
		}

		if (const UNiagaraNodeFunctionCall* FunctionCall = Cast<UNiagaraNodeFunctionCall>(Node))
		{
			for (const TPair<FName, FName>& Specifier : GetEffectiveSpecifiers(*FunctionCall))
			{
				FNovaGraphSpecifier Entry;
				Entry.Name = Specifier.Key;
				Entry.Value = Specifier.Value;
				Info.FunctionSpecifiers.Add(MoveTemp(Entry));
			}
		}
		return Info;
	}

	FNovaGraphInfo DescribeGraph(UNiagaraScript* Script, UNiagaraGraph& Graph)
	{
		FNovaGraphInfo Info;
		Info.Script = Script;
		Info.OwningAsset = Script->GetOutermostObject()->GetPathName();
		for (UEdGraphNode* Node : Graph.Nodes)
		{
			if (Node)
			{
				Info.Nodes.Add(DescribeNode(Node));
			}
		}
		return Info;
	}

	bool ResolveType(const FString& InTypeName, FNiagaraTypeDefinition& OutType)
	{
		const FString TypeName = InTypeName.TrimStartAndEnd();
		struct FAlias
		{
			const TCHAR* Name;
			const FNiagaraTypeDefinition& (*Get)();
		};
		static const FAlias Aliases[] =
		{
			{ TEXT("float"), &FNiagaraTypeDefinition::GetFloatDef },
			{ TEXT("int"), &FNiagaraTypeDefinition::GetIntDef },
			{ TEXT("int32"), &FNiagaraTypeDefinition::GetIntDef },
			{ TEXT("bool"), &FNiagaraTypeDefinition::GetBoolDef },
			{ TEXT("vec2"), &FNiagaraTypeDefinition::GetVec2Def },
			{ TEXT("vector2d"), &FNiagaraTypeDefinition::GetVec2Def },
			{ TEXT("vec3"), &FNiagaraTypeDefinition::GetVec3Def },
			{ TEXT("vector"), &FNiagaraTypeDefinition::GetVec3Def },
			{ TEXT("vec4"), &FNiagaraTypeDefinition::GetVec4Def },
			{ TEXT("vector4"), &FNiagaraTypeDefinition::GetVec4Def },
			{ TEXT("color"), &FNiagaraTypeDefinition::GetColorDef },
			{ TEXT("linearcolor"), &FNiagaraTypeDefinition::GetColorDef },
			{ TEXT("position"), &FNiagaraTypeDefinition::GetPositionDef },
			{ TEXT("quat"), &FNiagaraTypeDefinition::GetQuatDef },
			{ TEXT("matrix"), &FNiagaraTypeDefinition::GetMatrix4Def },
			{ TEXT("matrix4"), &FNiagaraTypeDefinition::GetMatrix4Def },
			{ TEXT("map"), &FNiagaraTypeDefinition::GetParameterMapDef },
			{ TEXT("parametermap"), &FNiagaraTypeDefinition::GetParameterMapDef },
		};
		for (const FAlias& Alias : Aliases)
		{
			const FNiagaraTypeDefinition& Candidate = Alias.Get();
			if (TypeName.Equals(Alias.Name, ESearchCase::IgnoreCase) || TypeName.Equals(Candidate.GetName(), ESearchCase::IgnoreCase))
			{
				OutType = Candidate;
				return true;
			}
		}

		// Data interface classes by path or short name.
		UClass* Class = TypeName.StartsWith(TEXT("/"))
			? LoadObject<UClass>(nullptr, *TypeName)
			: FindFirstObject<UClass>(*TypeName, EFindFirstObjectOptions::NativeFirst);
		if (Class && Class->IsChildOf(UNiagaraDataInterface::StaticClass()) && !Class->HasAnyClassFlags(CLASS_Abstract))
		{
			OutType = FNiagaraTypeDefinition(Class);
			return true;
		}

		UNovaToolset::Error(FString::Printf(TEXT("Unknown type '%s'. Use float, int, bool, vec2, vec3, vec4, color, position, quat, matrix, map, or a data interface class such as /Script/Niagara.NiagaraDataInterfaceGrid3DCollection."), *InTypeName));
		return false;
	}

	FString CategoryOf(const FNiagaraAction_NewNode& Action)
	{
		return FString::Join(Action.Categories, TEXT(" > "));
	}

	FString SourceNameOf(const FText& Text)
	{
		const FString* Source = FTextInspector::GetSourceString(Text);
		return Source ? *Source : Text.ToString();
	}

	/** Resolves the optional drag-from pin shared by ListNodeActions and AddNode. Returns false on error. */
	bool ResolveFromPin(UNiagaraGraph& Graph, FName FromNodeId, FName FromPinName, bool bFromOutput, UEdGraphPin*& OutPin)
	{
		OutPin = nullptr;
		if (FromNodeId.IsNone())
		{
			return true;
		}
		UEdGraphNode* FromNode = FindNode(Graph, FromNodeId.ToString());
		OutPin = FromNode ? FindPin(*FromNode, FromPinName, bFromOutput ? EGPD_Output : EGPD_Input) : nullptr;
		return OutPin != nullptr;
	}

	void MarkNodeChanged(UEdGraphNode* Node, const TCHAR* Reason)
	{
		if (UNiagaraNode* NiagaraNode = Cast<UNiagaraNode>(Node))
		{
			NiagaraNode->MarkNodeRequiresSynchronization(Reason, true);
		}
		// An open graph tab rebuilds its node widgets on this; without it changed pin lists stay stale on screen.
		if (UEdGraph* Graph = Node ? Node->GetGraph() : nullptr)
		{
			Graph->NotifyGraphChanged();
		}
	}

	bool ResolveConnection(UNiagaraGraph& Graph, const FString& FromNodeId, FName FromPinName, const FString& ToNodeId, FName ToPinName, UEdGraphPin*& OutFrom, UEdGraphPin*& OutTo)
	{
		UEdGraphNode* FromNode = FindNode(Graph, FromNodeId);
		OutFrom = FromNode ? FindPin(*FromNode, FromPinName, EGPD_Output) : nullptr;
		if (OutFrom == nullptr)
		{
			return false;
		}
		UEdGraphNode* ToNode = FindNode(Graph, ToNodeId);
		OutTo = ToNode ? FindPin(*ToNode, ToPinName, EGPD_Input) : nullptr;
		return OutTo != nullptr;
	}
}

FNovaGraphInfo UNovaToolset_NiagaraGraph::GetGraph(UNiagaraScript* Script)
{
	using namespace NovaToolsetNiagaraGraph;

	UNiagaraGraph* Graph = GetScriptGraph(Script);
	return Graph ? DescribeGraph(Script, *Graph) : FNovaGraphInfo();
}

TArray<FNovaNodeActionInfo> UNovaToolset_NiagaraGraph::ListNodeActions(UNiagaraScript* Script, const FString& Filter, FName FromNodeId, FName FromPinName, bool bFromOutput, int32 MaxResults)
{
	using namespace NovaToolsetNiagaraGraph;

	TArray<FNovaNodeActionInfo> Results;
	UNiagaraGraph* Graph = GetScriptGraph(Script);
	const UEdGraphSchema_Niagara* Schema = Graph ? Cast<UEdGraphSchema_Niagara>(Graph->GetSchema()) : nullptr;
	UEdGraphPin* FromPin = nullptr;
	if (Schema == nullptr || !ResolveFromPin(*Graph, FromNodeId, FromPinName, bFromOutput, FromPin))
	{
		return Results;
	}

	UEdGraph* OwnerOfTemporaries = NewObject<UEdGraph>(GetTransientPackage());
	const TArray<TSharedPtr<FNiagaraAction_NewNode>> Actions = Schema->GetGraphActions(Graph, FromPin, OwnerOfTemporaries);
	for (const TSharedPtr<FNiagaraAction_NewNode>& Action : Actions)
	{
		if (!Action.IsValid())
		{
			continue;
		}
		FNovaNodeActionInfo Info;
		Info.DisplayName = Action->DisplayName.ToString();
		Info.SourceName = SourceNameOf(Action->DisplayName);
		Info.Category = CategoryOf(*Action);
		const bool bMatches = Filter.IsEmpty()
			|| Info.DisplayName.Contains(Filter) || Info.SourceName.Contains(Filter) || Info.Category.Contains(Filter)
			|| Action->Keywords.ToString().Contains(Filter);
		if (bMatches)
		{
			Results.Add(MoveTemp(Info));
			if (MaxResults > 0 && Results.Num() >= MaxResults)
			{
				break;
			}
		}
	}
	return Results;
}

FNovaGraphNodeInfo UNovaToolset_NiagaraGraph::AddNode(UNiagaraScript* Script, const FString& ActionName, FName MenuCategory, FName FromNodeId, FName FromPinName, bool bFromOutput, int32 PosX, int32 PosY)
{
	using namespace NovaToolsetNiagaraGraph;

	UNiagaraGraph* Graph = GetWritableGraph(Script);
	const UEdGraphSchema_Niagara* Schema = Graph ? Cast<UEdGraphSchema_Niagara>(Graph->GetSchema()) : nullptr;
	UEdGraphPin* FromPin = nullptr;
	if (Schema == nullptr || !ResolveFromPin(*Graph, FromNodeId, FromPinName, bFromOutput, FromPin))
	{
		return {};
	}

	UEdGraph* OwnerOfTemporaries = NewObject<UEdGraph>(GetTransientPackage());
	const TArray<TSharedPtr<FNiagaraAction_NewNode>> Actions = Schema->GetGraphActions(Graph, FromPin, OwnerOfTemporaries);

	TArray<TSharedPtr<FNiagaraAction_NewNode>> Matches;
	for (const TSharedPtr<FNiagaraAction_NewNode>& Action : Actions)
	{
		if (!Action.IsValid())
		{
			continue;
		}
		const bool bNameMatches = Action->DisplayName.ToString().Equals(ActionName, ESearchCase::IgnoreCase)
			|| SourceNameOf(Action->DisplayName).Equals(ActionName, ESearchCase::IgnoreCase);
		if (bNameMatches && (MenuCategory.IsNone() || CategoryOf(*Action).Contains(MenuCategory.ToString())))
		{
			Matches.Add(Action);
		}
	}

	if (Matches.Num() != 1)
	{
		TArray<FString> Candidates;
		for (const TSharedPtr<FNiagaraAction_NewNode>& Match : Matches)
		{
			Candidates.Add(FString::Printf(TEXT("'%s' [%s]"), *Match->DisplayName.ToString(), *CategoryOf(*Match)));
		}
		Error(Matches.Num() == 0
			? FString::Printf(TEXT("No menu entry named '%s'. Use ListNodeActions to find names."), *ActionName)
			: FString::Printf(TEXT("'%s' matches %d entries; pass MenuCategory. Candidates: %s"), *ActionName, Matches.Num(), *FString::Join(Candidates, TEXT(", "))));
		return {};
	}

	// CreateNode shows a modal message box when the node is not allowed in this graph, which would stall
	// the game thread under MCP. Run the same check first and report it as a tool error instead.
	if (const UNiagaraNode* Template = Cast<UNiagaraNode>(Matches[0]->WeakNodeTemplate.Get()))
	{
		FString CannotAddReason;
		if (!Template->CanAddToGraph(Graph, CannotAddReason))
		{
			Error(FString::Printf(TEXT("'%s' cannot be added to this graph: %s"), *ActionName, *CannotAddReason));
			return {};
		}
	}

	FScopedTransaction Transaction(LOCTEXT("AddNode", "Nova Toolset: Add Niagara Graph Node"));
	Graph->Modify();
	UEdGraphNode* NewNode = Matches[0]->CreateNode(Graph, FromPin, FVector2D(PosX, PosY), false);
	if (NewNode == nullptr)
	{
		Error(FString::Printf(TEXT("Menu entry '%s' did not create a node."), *ActionName));
		Transaction.Cancel();
		return {};
	}
	MarkNodeChanged(NewNode, TEXT("NovaToolset AddNode"));
	return DescribeNode(NewNode);
}

FNovaGraphNodeInfo UNovaToolset_NiagaraGraph::AddParameterPin(UNiagaraScript* Script, const FString& NodeId, FName ParameterName, const FString& Type)
{
	using namespace NovaToolsetNiagaraGraph;

	UNiagaraGraph* Graph = GetWritableGraph(Script);
	UEdGraphNode* Node = Graph ? FindNode(*Graph, NodeId) : nullptr;
	if (Node == nullptr)
	{
		return {};
	}

	// The map node headers are private to NiagaraEditor, so classes are resolved by path.
	UClass* MapBaseClass = FindObject<UClass>(nullptr, TEXT("/Script/NiagaraEditor.NiagaraNodeParameterMapBase"));
	UClass* MapSetClass = FindObject<UClass>(nullptr, TEXT("/Script/NiagaraEditor.NiagaraNodeParameterMapSet"));
	if (MapBaseClass == nullptr || !Node->GetClass()->IsChildOf(MapBaseClass))
	{
		Error(FString::Printf(TEXT("Node '%s' is a %s, not a Map Get or Map Set node."), *NodeId, *Node->GetClass()->GetName()));
		return {};
	}
	if (ParameterName.IsNone())
	{
		Error(TEXT("ParameterName is required, e.g. Emitter.RTV_WindField."));
		return {};
	}

	FNiagaraTypeDefinition TypeDef;
	if (!ResolveType(Type, TypeDef))
	{
		return {};
	}

	const bool bCreateInputPin = MapSetClass && Node->GetClass()->IsChildOf(MapSetClass);
	if (Node->FindPin(ParameterName, bCreateInputPin ? EGPD_Input : EGPD_Output))
	{
		Error(FString::Printf(TEXT("Node already has a pin named '%s'."), *ParameterName.ToString()));
		return {};
	}

	FScopedTransaction Transaction(LOCTEXT("AddParameterPin", "Nova Toolset: Add Parameter Pin"));
	// Single inheritance down to UObject, so the address is the same; the header of the target type is private.
	FNiagaraStackGraphUtilities::AddNewVariableToParameterMapNode(reinterpret_cast<UNiagaraNodeParameterMapBase*>(Node), bCreateInputPin, FNiagaraVariable(TypeDef, ParameterName));
	MarkNodeChanged(Node, TEXT("NovaToolset AddParameterPin"));
	return DescribeNode(Node);
}

FNovaGraphNodeInfo UNovaToolset_NiagaraGraph::SetFunctionSpecifier(UNiagaraScript* Script, const FString& NodeId, FName Specifier, FName Value)
{
	using namespace NovaToolsetNiagaraGraph;

	UNiagaraGraph* Graph = GetWritableGraph(Script);
	UEdGraphNode* Node = Graph ? FindNode(*Graph, NodeId) : nullptr;
	if (Node == nullptr)
	{
		return {};
	}
	UNiagaraNodeFunctionCall* FunctionCall = Cast<UNiagaraNodeFunctionCall>(Node);
	if (FunctionCall == nullptr)
	{
		Error(FString::Printf(TEXT("Node '%s' is a %s, not a function call node."), *NodeId, *Node->GetClass()->GetName()));
		return {};
	}

	FScopedTransaction Transaction(LOCTEXT("SetFunctionSpecifier", "Nova Toolset: Set Function Specifier"));
	FunctionCall->Modify();
	// UNiagaraNodeFunctionCall only fills its own map when its widget is first built (CreateVisualWidget),
	// which is why the keys appear once the graph has been opened. Fill it the same way.
	if (FunctionCall->FunctionSpecifiers.Num() == 0 && FunctionCall->FunctionScript == nullptr)
	{
		FunctionCall->FunctionSpecifiers = GetEffectiveSpecifiers(*FunctionCall);
	}

	FName* Existing = FunctionCall->FunctionSpecifiers.Find(Specifier);
	if (Existing == nullptr)
	{
		TArray<FString> Names;
		for (const TPair<FName, FName>& Entry : FunctionCall->FunctionSpecifiers)
		{
			Names.Add(Entry.Key.ToString());
		}
		Error(Names.Num() == 0
			? FString::Printf(TEXT("Node '%s' has no specifiers; only data interface function calls have them."), *NodeId)
			: FString::Printf(TEXT("Node '%s' has no specifier named '%s'. Specifiers: %s"), *NodeId, *Specifier.ToString(), *FString::Join(Names, TEXT(", "))));
		Transaction.Cancel();
		return {};
	}
	*Existing = Value;

	// The same notification the node's own specifier text box sends on commit.
	MarkNodeChanged(FunctionCall, TEXT("NovaToolset SetFunctionSpecifier"));
	return DescribeNode(FunctionCall);
}

FNovaGraphNodeInfo UNovaToolset_NiagaraGraph::ConnectPins(UNiagaraScript* Script, const FString& FromNodeId, FName FromPinName, const FString& ToNodeId, FName ToPinName)
{
	using namespace NovaToolsetNiagaraGraph;

	UNiagaraGraph* Graph = GetWritableGraph(Script);
	UEdGraphPin* FromPin = nullptr;
	UEdGraphPin* ToPin = nullptr;
	if (Graph == nullptr || !ResolveConnection(*Graph, FromNodeId, FromPinName, ToNodeId, ToPinName, FromPin, ToPin))
	{
		return {};
	}

	const UEdGraphSchema* Schema = Graph->GetSchema();
	const FPinConnectionResponse Response = Schema->CanCreateConnection(FromPin, ToPin);
	if (Response.Response == CONNECT_RESPONSE_DISALLOW)
	{
		Error(FString::Printf(TEXT("Cannot connect: %s"), *Response.Message.ToString()));
		return {};
	}

	// The input node is captured before connecting: connecting to an Add pin renames that pin.
	UEdGraphNode* ToNode = ToPin->GetOwningNode();
	FScopedTransaction Transaction(LOCTEXT("ConnectPins", "Nova Toolset: Connect Pins"));
	if (!Schema->TryCreateConnection(FromPin, ToPin))
	{
		Error(TEXT("TryCreateConnection refused the connection."));
		Transaction.Cancel();
		return {};
	}
	MarkNodeChanged(ToNode, TEXT("NovaToolset ConnectPins"));
	return DescribeNode(ToNode);
}

FNovaGraphNodeInfo UNovaToolset_NiagaraGraph::DisconnectPins(UNiagaraScript* Script, const FString& FromNodeId, FName FromPinName, const FString& ToNodeId, FName ToPinName)
{
	using namespace NovaToolsetNiagaraGraph;

	UNiagaraGraph* Graph = GetWritableGraph(Script);
	UEdGraphPin* FromPin = nullptr;
	UEdGraphPin* ToPin = nullptr;
	if (Graph == nullptr || !ResolveConnection(*Graph, FromNodeId, FromPinName, ToNodeId, ToPinName, FromPin, ToPin))
	{
		return {};
	}
	if (!FromPin->LinkedTo.Contains(ToPin))
	{
		Error(TEXT("Those pins are not connected."));
		return {};
	}

	UEdGraphNode* ToNode = ToPin->GetOwningNode();
	FScopedTransaction Transaction(LOCTEXT("DisconnectPins", "Nova Toolset: Disconnect Pins"));
	Graph->GetSchema()->BreakSinglePinLink(FromPin, ToPin);
	MarkNodeChanged(ToNode, TEXT("NovaToolset DisconnectPins"));
	return DescribeNode(ToNode);
}

FNovaGraphInfo UNovaToolset_NiagaraGraph::RemoveNode(UNiagaraScript* Script, const FString& NodeId)
{
	using namespace NovaToolsetNiagaraGraph;

	UNiagaraGraph* Graph = GetWritableGraph(Script);
	UEdGraphNode* Node = Graph ? FindNode(*Graph, NodeId) : nullptr;
	if (Node == nullptr)
	{
		return {};
	}
	if (Node->IsA<UNiagaraNodeOutput>())
	{
		Error(TEXT("Output nodes cannot be deleted; the script would no longer compile."));
		return {};
	}

	FScopedTransaction Transaction(LOCTEXT("RemoveNode", "Nova Toolset: Remove Niagara Graph Node"));
	Graph->Modify();
	Node->Modify();
	Node->BreakAllNodeLinks();
	Node->DestroyNode();
	Graph->NotifyGraphChanged();
	return DescribeGraph(Script, *Graph);
}

FNovaGraphNodeInfo UNovaToolset_NiagaraGraph::SetPinDefaultValue(UNiagaraScript* Script, const FString& NodeId, FName PinName, const FString& Value)
{
	using namespace NovaToolsetNiagaraGraph;

	UNiagaraGraph* Graph = GetWritableGraph(Script);
	UEdGraphNode* Node = Graph ? FindNode(*Graph, NodeId) : nullptr;
	UEdGraphPin* Pin = Node ? FindPin(*Node, PinName, EGPD_Input) : nullptr;
	if (Pin == nullptr)
	{
		return {};
	}
	if (Pin->LinkedTo.Num() > 0)
	{
		Error(FString::Printf(TEXT("Pin '%s' is connected; its default value is not used."), *PinName.ToString()));
		return {};
	}

	FScopedTransaction Transaction(LOCTEXT("SetPinDefaultValue", "Nova Toolset: Set Pin Default Value"));
	Node->Modify();
	Graph->GetSchema()->TrySetDefaultValue(*Pin, Value, true);
	MarkNodeChanged(Node, TEXT("NovaToolset SetPinDefaultValue"));
	return DescribeNode(Node);
}

FNovaGraphNodeInfo UNovaToolset_NiagaraGraph::SetCustomHlsl(UNiagaraScript* Script, const FString& NodeId, const FString& Hlsl, const TArray<FNovaHlslPin>& Inputs, const TArray<FNovaHlslPin>& Outputs, const TArray<FString>& VirtualIncludeFilePaths, const TArray<FString>& AbsoluteIncludeFilePaths, bool bReplacePins)
{
	using namespace NovaToolsetNiagaraGraph;

	UNiagaraGraph* Graph = GetWritableGraph(Script);
	UEdGraphNode* Node = Graph ? FindNode(*Graph, NodeId) : nullptr;
	if (Node == nullptr)
	{
		return {};
	}
	UNiagaraNodeCustomHlsl* HlslNode = Cast<UNiagaraNodeCustomHlsl>(Node);
	FString* HlslText = HlslNode ? FindStringProperty(HlslNode, CustomHlslPropertyName) : nullptr;
	TArray<FString>* VirtualIncludes = HlslNode ? FindStringArrayProperty(HlslNode, VirtualIncludesPropertyName) : nullptr;
	if (HlslText == nullptr || VirtualIncludes == nullptr)
	{
		Error(FString::Printf(TEXT("Node '%s' is a %s, not a Custom HLSL node."), *NodeId, *Node->GetClass()->GetName()));
		return {};
	}

	// Resolve every declaration before touching the node, so a bad type leaves it unchanged.
	TArray<FNiagaraVariable> NewInputs;
	TArray<FNiagaraVariableBase> NewOutputs;
	if (bReplacePins)
	{
		TSet<FName> SeenInputs;
		for (const FNovaHlslPin& Pin : Inputs)
		{
			FNiagaraTypeDefinition TypeDef;
			if (Pin.Name.IsNone() || SeenInputs.Contains(Pin.Name))
			{
				Error(FString::Printf(TEXT("Input names must be set and unique (got '%s')."), *Pin.Name.ToString()));
				return {};
			}
			if (!ResolveType(Pin.Type, TypeDef))
			{
				return {};
			}
			SeenInputs.Add(Pin.Name);
			NewInputs.Add(FNiagaraVariable(TypeDef, Pin.Name));
		}
		TSet<FName> SeenOutputs;
		for (const FNovaHlslPin& Pin : Outputs)
		{
			FNiagaraTypeDefinition TypeDef;
			if (Pin.Name.IsNone() || SeenOutputs.Contains(Pin.Name))
			{
				Error(FString::Printf(TEXT("Output names must be set and unique (got '%s')."), *Pin.Name.ToString()));
				return {};
			}
			if (!ResolveType(Pin.Type, TypeDef))
			{
				return {};
			}
			SeenOutputs.Add(Pin.Name);
			NewOutputs.Add(FNiagaraVariableBase(TypeDef, Pin.Name));
		}

		// Keep the node's parameter map pins unless the caller declared map pins itself; dropping them
		// would silently cut a module's Custom HLSL node out of the parameter map flow.
		const FNiagaraTypeDefinition& MapDef = FNiagaraTypeDefinition::GetParameterMapDef();
		if (!NewInputs.ContainsByPredicate([&MapDef](const FNiagaraVariable& Var) { return Var.GetType() == MapDef; }))
		{
			int32 InsertAt = 0;
			for (const FNiagaraVariable& Existing : HlslNode->Signature.Inputs)
			{
				if (Existing.GetType() == MapDef && !SeenInputs.Contains(Existing.GetName()))
				{
					NewInputs.Insert(Existing, InsertAt++);
				}
			}
		}
		if (!NewOutputs.ContainsByPredicate([&MapDef](const FNiagaraVariableBase& Var) { return Var.GetType() == MapDef; }))
		{
			int32 InsertAt = 0;
			for (const FNiagaraVariableBase& Existing : HlslNode->Signature.Outputs)
			{
				if (Existing.GetType() == MapDef && !SeenOutputs.Contains(Existing.GetName()))
				{
					NewOutputs.Insert(Existing, InsertAt++);
				}
			}
		}
	}

	TArray<FString> AbsoluteIncludes = AbsoluteIncludeFilePaths;
	FScopedTransaction Transaction(LOCTEXT("SetCustomHlsl", "Nova Toolset: Set Custom HLSL"));
	HlslNode->Modify();

	// CustomHlsl and the include lists are private UPROPERTYs and their setters are not exported;
	// reflection writes them without needing those symbols.
	*HlslText = Hlsl;
	*VirtualIncludes = VirtualIncludeFilePaths;
	AccessFilePathArray(HlslNode, AbsoluteIncludesPropertyName, &AbsoluteIncludes, nullptr);

	if (bReplacePins)
	{
		// Custom HLSL pins are generated from Signature in AllocateDefaultPins; ReallocatePins regenerates
		// them and carries connections over for pins whose name did not change.
		HlslNode->Signature.Inputs = MoveTemp(NewInputs);
		HlslNode->Signature.Outputs = MoveTemp(NewOutputs);
		(HlslNode->*FNiagaraNodeProtectedAccess::ReallocatePinsFn())(true);
	}

	HlslNode->RefreshFromExternalChanges();
	MarkNodeChanged(HlslNode, TEXT("NovaToolset SetCustomHlsl"));
	return DescribeNode(HlslNode);
}

FNovaApplyGraphInfo UNovaToolset_NiagaraGraph::ApplyGraphChanges(UNiagaraScript* Script, bool bSave)
{
	using namespace NovaToolsetNiagaraGraph;

	FGraphTarget Target;
	if (!ResolveGraphTarget(Script, true, Target))
	{
		return {};
	}

	if (Target.ScratchPadScript.IsValid())
	{
		// The open editor's Apply button: copies its working copy over the local module and refreshes the
		// module nodes that call it. The loop below repeats that refresh, which is harmless.
		Target.ScratchPadScript->ApplyChanges();
		Script = Target.ScratchPadScript->GetOriginalScript();
	}

	FNovaApplyGraphInfo Info;
	Info.Script = Script;

	// Mirrors FNiagaraScratchPadScriptViewModel::ApplyChanges: every function call node that references the
	// script refreshes its pins from the edited graph and is flagged for recompilation.
	TSet<UNiagaraSystem*> AffectedSystems;
	for (TObjectIterator<UNiagaraNodeFunctionCall> It; It; ++It)
	{
		UNiagaraNodeFunctionCall* FunctionCall = *It;
		if (!IsValid(FunctionCall)
			|| FunctionCall->FunctionScript != Script
			|| FunctionCall->HasAnyFlags(RF_ClassDefaultObject | RF_Transient)
			|| FunctionCall->GetOutermost() == GetTransientPackage())
		{
			continue;
		}
		FunctionCall->Modify();
		FunctionCall->RefreshFromExternalChanges();
		FunctionCall->MarkNodeRequiresSynchronization(TEXT("NovaToolset ApplyGraphChanges"), true);
		++Info.RefreshedModuleNodes;
		if (UNiagaraSystem* System = FunctionCall->GetTypedOuter<UNiagaraSystem>())
		{
			AffectedSystems.Add(System);
		}
	}

	if (UNiagaraSystem* OwningSystem = Cast<UNiagaraSystem>(Script->GetOutermostObject()))
	{
		AffectedSystems.Add(OwningSystem);
	}

	bool bAllSaved = true;
	for (UNiagaraSystem* System : AffectedSystems)
	{
		System->RequestCompile(false);
		Info.RecompiledSystems.Add(System->GetPathName());
		if (bSave)
		{
			bAllSaved &= SaveAssetSilently(System);
		}
	}

	if (bSave && !Script->GetOutermostObject()->IsA<UNiagaraSystem>())
	{
		bAllSaved &= SaveAssetSilently(Script);
	}
	Info.bSaved = bSave && bAllSaved;
	return Info;
}

#undef LOCTEXT_NAMESPACE
