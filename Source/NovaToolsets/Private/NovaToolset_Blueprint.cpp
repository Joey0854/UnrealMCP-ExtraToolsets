#include "NovaToolset_Blueprint.h"

#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "K2Node_CallFunction.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_DynamicCast.h"
#include "K2Node_EditablePinBase.h"
#include "K2Node_Event.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_MacroInstance.h"
#include "K2Node_Variable.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "ScopedTransaction.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(NovaToolset_Blueprint)

#define LOCTEXT_NAMESPACE "NovaToolset_Blueprint"

namespace NovaToolsetBlueprint
{
	// FName comparisons ignore case, so these also match "tooltip", "callineditor", ...
	const FName TooltipKey(TEXT("Tooltip"));
	const FName CallInEditorKey(TEXT("CallInEditor"));
	const FName CategoryKey(TEXT("Category"));
	const FName KeywordsKey(TEXT("Keywords"));

	bool IsNumericLimitKey(FName Key)
	{
		return Key == FName(TEXT("ClampMin")) || Key == FName(TEXT("ClampMax")) || Key == FName(TEXT("UIMin")) || Key == FName(TEXT("UIMax"));
	}

	bool ParseBool(const FString& InValue, bool& bOut)
	{
		const FString Value = InValue.TrimStartAndEnd();
		if (Value.Equals(TEXT("true"), ESearchCase::IgnoreCase) || Value == TEXT("1"))
		{
			bOut = true;
			return true;
		}
		if (Value.IsEmpty() || Value.Equals(TEXT("false"), ESearchCase::IgnoreCase) || Value == TEXT("0"))
		{
			bOut = false;
			return true;
		}
		return false;
	}

	/** Compiles so the generated class and placed instances see the change; the metadata setters only update the skeleton. */
	void CompileAndSave(UBlueprint& Blueprint, bool bSave, bool& bOutCompiled, bool& bOutSaved)
	{
		FKismetEditorUtilities::CompileBlueprint(&Blueprint);
		bOutCompiled = Blueprint.Status != BS_Error;
		bOutSaved = bSave && UNovaToolset::SaveAssetSilently(&Blueprint);
	}

	UEdGraph* FindFunctionGraph(UBlueprint& Blueprint, FName FunctionName)
	{
		for (UEdGraph* Graph : Blueprint.FunctionGraphs)
		{
			if (Graph && Graph->GetFName() == FunctionName)
			{
				return Graph;
			}
		}
		return nullptr;
	}

	void DescribeFunction(UBlueprint& Blueprint, FName FunctionName, FNovaBlueprintFunctionMetadataInfo& Info)
	{
		Info.FunctionName = FunctionName;
		if (UEdGraph* Graph = FindFunctionGraph(Blueprint, FunctionName))
		{
			Info.Kind = TEXT("Function");
			if (const FKismetUserDeclaredFunctionMetadata* Meta = FBlueprintEditorUtils::GetGraphFunctionMetaData(Graph))
			{
				Info.bCallInEditor = Meta->bCallInEditor;
				Info.ToolTip = Meta->ToolTip.ToString();
				Info.FunctionCategory = Meta->Category.ToString();
				Info.Keywords = Meta->Keywords.ToString();
				for (const TPair<FName, FString>& Entry : Meta->GetMetaDataMap())
				{
					FNovaMetadataEntry& Out = Info.Metadata.AddDefaulted_GetRef();
					Out.Key = Entry.Key;
					Out.Value = Entry.Value;
				}
			}
		}
		else if (const UK2Node_CustomEvent* CustomEvent = Cast<UK2Node_CustomEvent>(FBlueprintEditorUtils::FindCustomEventNode(&Blueprint, FunctionName)))
		{
			Info.Kind = TEXT("CustomEvent");
			Info.bCallInEditor = CustomEvent->bCallInEditor;
		}
	}

	/** Reads a property by name, so fields whose C++ access level is not public stay reachable. */
	UObject* ReadObjectProperty(const UObject* Object, const TCHAR* Name)
	{
		const FObjectPropertyBase* Property = FindFProperty<FObjectPropertyBase>(Object->GetClass(), Name);
		return Property ? Property->GetObjectPropertyValue_InContainer(Object) : nullptr;
	}

	FName ReadNameProperty(const UObject* Object, const TCHAR* Name)
	{
		const FNameProperty* Property = FindFProperty<FNameProperty>(Object->GetClass(), Name);
		return Property ? Property->GetPropertyValue_InContainer(Object) : NAME_None;
	}

	/** Language-independent node identity: class, function, macro or variable names instead of the localized title. */
	FString NodeIdentity(const UEdGraphNode& Node)
	{
		if (const UK2Node_CallFunction* Call = Cast<UK2Node_CallFunction>(&Node))
		{
			if (const UFunction* Function = Call->GetTargetFunction())
			{
				const UClass* OwnerClass = Function->GetOuterUClass();
				return FString::Printf(TEXT("Call %s|%s"), OwnerClass ? *OwnerClass->GetName() : TEXT("?"), *Function->GetName());
			}
		}
		else if (const UK2Node_MacroInstance* Macro = Cast<UK2Node_MacroInstance>(&Node))
		{
			if (const UEdGraph* MacroGraph = Macro->GetMacroGraph())
			{
				return FString::Printf(TEXT("Macro %s"), *MacroGraph->GetName());
			}
		}
		else if (Cast<UK2Node_DynamicCast>(&Node))
		{
			const UObject* TargetType = ReadObjectProperty(&Node, TEXT("TargetType"));
			return FString::Printf(TEXT("Cast %s"), TargetType ? *TargetType->GetName() : TEXT("None"));
		}
		else if (const UK2Node_Variable* Variable = Cast<UK2Node_Variable>(&Node))
		{
			return FString::Printf(TEXT("%s %s"), *Node.GetClass()->GetName(), *Variable->GetVarName().ToString());
		}
		else if (Cast<UK2Node_FunctionEntry>(&Node))
		{
			return FString::Printf(TEXT("Entry %s"), Node.GetGraph() ? *Node.GetGraph()->GetName() : TEXT(""));
		}
		else if (Cast<UK2Node_Event>(&Node))
		{
			FName EventName = ReadNameProperty(&Node, TEXT("CustomFunctionName"));
			if (EventName.IsNone())
			{
				const FStructProperty* ReferenceProperty = FindFProperty<FStructProperty>(Node.GetClass(), TEXT("EventReference"));
				if (ReferenceProperty && ReferenceProperty->Struct->GetFName() == FName(TEXT("MemberReference")))
				{
					EventName = ReferenceProperty->ContainerPtrToValuePtr<FMemberReference>(&Node)->GetMemberName();
				}
			}
			return FString::Printf(TEXT("Event %s"), *EventName.ToString());
		}
		return FString::Printf(TEXT("%s: %s"), *Node.GetClass()->GetName(), *Node.GetNodeTitle(ENodeTitleType::ListView).ToString());
	}

	void DumpOneGraph(const UEdGraph& Graph, TArray<FString>& Lines, FNovaGraphDump& Result)
	{
		Lines.Add(FString::Printf(TEXT("== %s =="), *Graph.GetName()));
		for (const UEdGraphNode* Node : Graph.Nodes)
		{
			if (Node == nullptr)
			{
				continue;
			}
			++Result.NodeCount;
			Lines.Add(FString::Printf(TEXT("%s [%s]"), *Node->GetName(), *NodeIdentity(*Node)));
			for (const UEdGraphPin* Pin : Node->Pins)
			{
				if (Pin == nullptr)
				{
					continue;
				}
				if (Pin->Direction == EGPD_Output)
				{
					for (const UEdGraphPin* Linked : Pin->LinkedTo)
					{
						if (Linked && Linked->GetOwningNodeUnchecked())
						{
							++Result.LinkCount;
							Lines.Add(FString::Printf(TEXT("  %s -> %s.%s"), *Pin->PinName.ToString(),
								*Linked->GetOwningNodeUnchecked()->GetName(), *Linked->PinName.ToString()));
						}
					}
				}
				else if (Pin->LinkedTo.Num() == 0 && !Pin->bHidden)
				{
					// Only values someone set: literals such as a class to search for, not untouched defaults.
					const bool bHasObject = Pin->DefaultObject != nullptr;
					const bool bHasText = !Pin->DefaultTextValue.IsEmpty();
					const bool bChangedValue = !Pin->DefaultValue.IsEmpty() && Pin->DefaultValue != Pin->AutogeneratedDefaultValue;
					if (bHasObject || bHasText || bChangedValue)
					{
						const FString Value = bHasObject ? Pin->DefaultObject->GetPathName() : (bHasText ? Pin->DefaultTextValue.ToString() : Pin->DefaultValue);
						Lines.Add(FString::Printf(TEXT("  %s = %s"), *Pin->PinName.ToString(), *Value));
					}
				}
			}
		}
	}
}

FNovaBlueprintVariableMetadataInfo UNovaToolset_Blueprint::SetVariableMetadata(UBlueprint* Blueprint, FName VariableName, FName Key, const FString& Value, bool bSave)
{
	using namespace NovaToolsetBlueprint;

	if (Blueprint == nullptr)
	{
		Error(TEXT("Blueprint is null."));
		return {};
	}
	if (Key.IsNone())
	{
		Error(TEXT("Key is required, e.g. Tooltip, ClampMin, ClampMax, UIMin or UIMax."));
		return {};
	}
	if (Key == CategoryKey)
	{
		// A variable's category is a field of its description, not metadata; the compiler would ignore this key.
		Error(TEXT("A variable's Category is not metadata. Set it with the stock Blueprint toolset's variable category tool."));
		return {};
	}

	// The setter silently does nothing for an unknown name, so check first.
	if (FBlueprintEditorUtils::FindNewVariableIndex(Blueprint, VariableName) == INDEX_NONE)
	{
		TArray<FString> Names;
		for (const FBPVariableDescription& Variable : Blueprint->NewVariables)
		{
			Names.Add(Variable.VarName.ToString());
		}
		Error(FString::Printf(TEXT("Variable '%s' not found in '%s'. Member variables: %s"),
			*VariableName.ToString(), *Blueprint->GetName(), *FString::Join(Names, TEXT(", "))));
		return {};
	}

	// The Details panel stores the tooltip under FBlueprintMetadata::MD_Tooltip.
	const FName MetadataKey = Key == TooltipKey ? FBlueprintMetadata::MD_Tooltip : Key;
	FString NewValue = Value;
	if (IsNumericLimitKey(MetadataKey))
	{
		NewValue.TrimStartAndEndInline();
		if (!NewValue.IsEmpty() && !NewValue.IsNumeric())
		{
			Error(FString::Printf(TEXT("%s must be a number, got '%s'."), *MetadataKey.ToString(), *Value));
			return {};
		}
	}

	{
		FScopedTransaction Transaction(LOCTEXT("SetVariableMetadata", "Nova Toolset: Set Variable Metadata"));
		// Neither the setter nor the Details panel records the Blueprint for undo on its own.
		Blueprint->Modify();
		if (NewValue.IsEmpty())
		{
			FBlueprintEditorUtils::RemoveBlueprintVariableMetaData(Blueprint, VariableName, nullptr, MetadataKey);
		}
		else
		{
			FBlueprintEditorUtils::SetBlueprintVariableMetaData(Blueprint, VariableName, nullptr, MetadataKey, NewValue);
		}
	}

	FNovaBlueprintVariableMetadataInfo Info;
	Info.VariableName = VariableName;
	CompileAndSave(*Blueprint, bSave, Info.bCompiled, Info.bSaved);

	const int32 VariableIndex = FBlueprintEditorUtils::FindNewVariableIndex(Blueprint, VariableName);
	if (Blueprint->NewVariables.IsValidIndex(VariableIndex))
	{
		for (const FBPVariableMetaDataEntry& Entry : Blueprint->NewVariables[VariableIndex].MetaDataArray)
		{
			FNovaMetadataEntry& Out = Info.Metadata.AddDefaulted_GetRef();
			Out.Key = Entry.DataKey;
			Out.Value = Entry.DataValue;
		}
	}
	return Info;
}

FNovaBlueprintFunctionMetadataInfo UNovaToolset_Blueprint::SetFunctionMetadata(UBlueprint* Blueprint, FName FunctionName, FName Key, const FString& Value, bool bSave)
{
	using namespace NovaToolsetBlueprint;

	if (Blueprint == nullptr)
	{
		Error(TEXT("Blueprint is null."));
		return {};
	}
	if (Key.IsNone())
	{
		Error(TEXT("Key is required, e.g. CallInEditor or Tooltip."));
		return {};
	}

	UEdGraph* FunctionGraph = FindFunctionGraph(*Blueprint, FunctionName);
	UK2Node_CustomEvent* CustomEvent = FunctionGraph ? nullptr : Cast<UK2Node_CustomEvent>(FBlueprintEditorUtils::FindCustomEventNode(Blueprint, FunctionName));
	if (FunctionGraph == nullptr && CustomEvent == nullptr)
	{
		TArray<FString> Names;
		for (const UEdGraph* Graph : Blueprint->FunctionGraphs)
		{
			if (Graph)
			{
				Names.Add(Graph->GetName());
			}
		}
		Error(FString::Printf(TEXT("No function or custom event named '%s' in '%s'. Functions: %s"),
			*FunctionName.ToString(), *Blueprint->GetName(), *FString::Join(Names, TEXT(", "))));
		return {};
	}

	FKismetUserDeclaredFunctionMetadata* Meta = FunctionGraph ? FBlueprintEditorUtils::GetGraphFunctionMetaData(FunctionGraph) : nullptr;
	if (FunctionGraph && Meta == nullptr)
	{
		Error(FString::Printf(TEXT("Function '%s' has no function entry node."), *FunctionName.ToString()));
		return {};
	}

	bool bFlag = false;
	if (Key == CallInEditorKey && !ParseBool(Value, bFlag))
	{
		Error(FString::Printf(TEXT("CallInEditor must be true or false, got '%s'."), *Value));
		return {};
	}
	if (CustomEvent && Key != CallInEditorKey)
	{
		Error(FString::Printf(TEXT("'%s' is a custom event; custom events only support CallInEditor."), *FunctionName.ToString()));
		return {};
	}

	{
		FScopedTransaction Transaction(LOCTEXT("SetFunctionMetadata", "Nova Toolset: Set Function Metadata"));
		Blueprint->Modify();
		if (CustomEvent)
		{
			CustomEvent->Modify();
			CustomEvent->bCallInEditor = bFlag;
		}
		else if (Key == CategoryKey)
		{
			// Also moves the function in the My Blueprint panel; the compile below replaces its own recompile.
			FBlueprintEditorUtils::SetBlueprintFunctionOrMacroCategory(FunctionGraph, FText::FromString(Value), true);
		}
		else
		{
			FBlueprintEditorUtils::ModifyFunctionMetaData(FunctionGraph);
			if (Key == CallInEditorKey)
			{
				// The Details panel checkbox reads this field; the compiler turns it into the CallInEditor metadata.
				Meta->bCallInEditor = bFlag;
			}
			else if (Key == TooltipKey || Key == FBlueprintMetadata::MD_Tooltip)
			{
				Meta->ToolTip = FText::FromString(Value);
			}
			else if (Key == KeywordsKey)
			{
				Meta->Keywords = FText::FromString(Value);
			}
			else if (Value.IsEmpty())
			{
				Meta->RemoveMetaData(Key);
			}
			else
			{
				Meta->SetMetaData(Key, FStringView(Value));
			}
		}
		FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
	}

	FNovaBlueprintFunctionMetadataInfo Info;
	CompileAndSave(*Blueprint, bSave, Info.bCompiled, Info.bSaved);
	DescribeFunction(*Blueprint, FunctionName, Info);
	return Info;
}

FNovaGraphDump UNovaToolset_Blueprint::DumpGraph(UBlueprint* Blueprint, FName GraphName)
{
	using namespace NovaToolsetBlueprint;

	if (Blueprint == nullptr)
	{
		Error(TEXT("Blueprint is null."));
		return {};
	}

	TArray<UEdGraph*> Graphs;
	Blueprint->GetAllGraphs(Graphs);

	FNovaGraphDump Result;
	TArray<FString> Lines;
	for (const UEdGraph* Graph : Graphs)
	{
		if (Graph && (GraphName.IsNone() || Graph->GetFName() == GraphName))
		{
			DumpOneGraph(*Graph, Lines, Result);
		}
	}

	if (Lines.Num() == 0)
	{
		TArray<FString> Names;
		for (const UEdGraph* Graph : Graphs)
		{
			if (Graph)
			{
				Names.Add(Graph->GetName());
			}
		}
		Error(FString::Printf(TEXT("No graph named '%s' in '%s'. Graphs: %s"),
			*GraphName.ToString(), *Blueprint->GetName(), *FString::Join(Names, TEXT(", "))));
		return {};
	}

	Result.Dump = FString::Join(Lines, TEXT("\n"));
	return Result;
}

#undef LOCTEXT_NAMESPACE
