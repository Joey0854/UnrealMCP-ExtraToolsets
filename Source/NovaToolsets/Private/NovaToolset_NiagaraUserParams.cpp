#include "NovaToolset_NiagaraUserParams.h"

#include "DataHierarchyViewModelBase.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "JsonObjectConverter.h"
#include "NiagaraDataInterface.h"
#include "NiagaraEditorUtilities.h"
#include "NiagaraEmitter.h"
#include "NiagaraEmitterHandle.h"
#include "NiagaraGraph.h"
#include "NiagaraRendererProperties.h"
#include "NiagaraScript.h"
#include "NiagaraScriptSource.h"
#include "NiagaraScriptVariable.h"
#include "NiagaraSimulationStageBase.h"
#include "NiagaraSystem.h"
#include "NiagaraSystemEditorData.h"
#include "ScopedTransaction.h"
#include "StructUtils/InstancedStruct.h"
#include "UObject/UnrealType.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(NovaToolset_NiagaraUserParams)

#define LOCTEXT_NAMESPACE "NovaToolset_NiagaraUserParams"

namespace NovaUserParams
{
	FString StripUserPrefix(FString Name)
	{
		Name.RemoveFromStart(TEXT("User."));
		return Name;
	}

	bool ResolveHierarchy(UNiagaraSystem* System, UNiagaraSystemEditorData*& OutEditorData, UHierarchyRoot*& OutRoot)
	{
		OutEditorData = nullptr;
		OutRoot = nullptr;
		if (System == nullptr)
		{
			UNovaToolset::Error(TEXT("System is null."));
			return false;
		}
		OutEditorData = Cast<UNiagaraSystemEditorData>(System->GetEditorData());
		if (OutEditorData == nullptr)
		{
			UNovaToolset::Error(FString::Printf(TEXT("'%s' has no system editor data."), *System->GetName()));
			return false;
		}
		OutRoot = OutEditorData->UserParameterHierarchy;
		if (OutRoot == nullptr)
		{
			UNovaToolset::Error(FString::Printf(TEXT("'%s' has no user parameter hierarchy root."), *System->GetName()));
			return false;
		}
		return true;
	}

	// UNiagaraHierarchyUserParameter is not exported from NiagaraEditor, so neither StaticClass() nor its
	// Initialize() link from a plugin. The class is looked up by path and its single property is set through
	// reflection, which is exactly what Initialize() does (NiagaraUserParametersHierarchyViewModel.cpp:18).
	UClass* UserParameterItemClass()
	{
		return FindObject<UClass>(nullptr, TEXT("/Script/NiagaraEditor.NiagaraHierarchyUserParameter"));
	}

	FObjectProperty* UserParameterItemProperty(UClass* ItemClass)
	{
		return ItemClass ? FindFProperty<FObjectProperty>(ItemClass, TEXT("UserParameterScriptVariable")) : nullptr;
	}

	// Section membership is stored as FDataHierarchyElementMetaData_SectionAssociation in the element's MetaData
	// map (DataHierarchyViewModelBase.cpp:588). That struct is not exported either, so the templated
	// FindOrAddMetaDataOfType<T>() would not link (it needs T::StaticStruct()); the map is edited through
	// reflection instead. TInstancedStruct<T> is reflected as FInstancedStruct.
	UScriptStruct* SectionAssociationStruct()
	{
		return FindObject<UScriptStruct>(nullptr, TEXT("/Script/DataHierarchyEditor.DataHierarchyElementMetaData_SectionAssociation"));
	}

	FMapProperty* MetaDataProperty()
	{
		return FindFProperty<FMapProperty>(UHierarchyElement::StaticClass(), TEXT("MetaData"));
	}

	const UHierarchySection* GetSectionAssociation(const UHierarchyElement& Element)
	{
		UScriptStruct* AssociationStruct = SectionAssociationStruct();
		FMapProperty* MapProperty = MetaDataProperty();
		FWeakObjectProperty* SectionProperty = AssociationStruct ? FindFProperty<FWeakObjectProperty>(AssociationStruct, TEXT("Section")) : nullptr;
		if (MapProperty == nullptr || SectionProperty == nullptr)
		{
			return nullptr;
		}
		TObjectPtr<UStruct> Key = AssociationStruct;
		FScriptMapHelper Helper(MapProperty, MapProperty->ContainerPtrToValuePtr<void>(const_cast<UHierarchyElement*>(&Element)));
		const uint8* ValuePtr = Helper.FindValueFromHash(&Key);
		const FInstancedStruct* Value = static_cast<const FInstancedStruct*>(static_cast<const void*>(ValuePtr));
		if (Value == nullptr || Value->GetScriptStruct() != AssociationStruct || Value->GetMemory() == nullptr)
		{
			return nullptr;
		}
		return Cast<UHierarchySection>(SectionProperty->GetObjectPropertyValue_InContainer(Value->GetMemory()));
	}

	bool SetSectionAssociation(UHierarchyElement& Element, UHierarchySection* Section)
	{
		UScriptStruct* AssociationStruct = SectionAssociationStruct();
		FMapProperty* MapProperty = MetaDataProperty();
		FWeakObjectProperty* SectionProperty = AssociationStruct ? FindFProperty<FWeakObjectProperty>(AssociationStruct, TEXT("Section")) : nullptr;
		if (MapProperty == nullptr || SectionProperty == nullptr)
		{
			return false;
		}
		TObjectPtr<UStruct> Key = AssociationStruct;
		FScriptMapHelper Helper(MapProperty, MapProperty->ContainerPtrToValuePtr<void>(&Element));
		FInstancedStruct* Value = static_cast<FInstancedStruct*>(Helper.FindOrAdd(&Key));
		if (Value == nullptr)
		{
			return false;
		}
		if (Value->GetScriptStruct() != AssociationStruct)
		{
			Value->InitializeAs(AssociationStruct);
		}
		SectionProperty->SetObjectPropertyValue_InContainer(Value->GetMutableMemory(), Section);
		return true;
	}

	const FNiagaraVariable* FindUserParameter(const TArray<FNiagaraVariable>& UserParameters, FName Name)
	{
		const FString Wanted = StripUserPrefix(Name.ToString());
		return UserParameters.FindByPredicate([&Wanted](const FNiagaraVariable& Candidate)
		{
			return StripUserPrefix(Candidate.GetName().ToString()).Equals(Wanted, ESearchCase::IgnoreCase);
		});
	}

	FString JoinUserParameterNames(const TArray<FNiagaraVariable>& UserParameters)
	{
		TArray<FString> Names;
		for (const FNiagaraVariable& Parameter : UserParameters)
		{
			Names.Add(StripUserPrefix(Parameter.GetName().ToString()));
		}
		return FString::Join(Names, TEXT(", "));
	}

	/** Parameters under Element in display order; nested categories are flattened into their parent. */
	void CollectParameters(const UHierarchyElement& Element, UClass* ItemClass, FObjectProperty* ItemProperty, TArray<FName>& OutNames)
	{
		for (const TObjectPtr<UHierarchyElement>& Child : Element.GetChildren())
		{
			if (Child == nullptr)
			{
				continue;
			}
			if (ItemClass && ItemProperty && Child->IsA(ItemClass))
			{
				if (const UNiagaraScriptVariable* ScriptVariable = Cast<UNiagaraScriptVariable>(ItemProperty->GetObjectPropertyValue_InContainer(Child.Get())))
				{
					OutNames.Add(FName(*StripUserPrefix(ScriptVariable->Variable.GetName().ToString())));
				}
			}
			else if (Child->IsA<UHierarchyCategory>())
			{
				CollectParameters(*Child, ItemClass, ItemProperty, OutNames);
			}
		}
	}

	FNovaUserParameterHierarchyInfo DescribeHierarchy(const UNiagaraSystem& System, const UHierarchyRoot& Root)
	{
		FNovaUserParameterHierarchyInfo Info;
		UClass* ItemClass = UserParameterItemClass();
		FObjectProperty* ItemProperty = UserParameterItemProperty(ItemClass);
		TSet<FName> Placed;

		auto DescribeCategory = [&](const UHierarchyElement& Element, FName CategoryName)
		{
			FNovaUserParameterCategoryLayout Layout;
			Layout.Category = CategoryName;
			CollectParameters(Element, ItemClass, ItemProperty, Layout.Parameters);
			Placed.Append(Layout.Parameters);
			return Layout;
		};

		// Only top-level categories carry a section association (NiagaraUserParametersBuilderBase.cpp:170).
		for (const TObjectPtr<UHierarchySection>& Section : Root.GetSectionData())
		{
			if (Section == nullptr)
			{
				continue;
			}
			FNovaUserParameterSectionLayout SectionLayout;
			SectionLayout.Section = Section->GetSectionName();
			for (const TObjectPtr<UHierarchyElement>& Child : Root.GetChildren())
			{
				const UHierarchyCategory* Category = Cast<UHierarchyCategory>(Child.Get());
				if (Category && GetSectionAssociation(*Category) == Section.Get())
				{
					SectionLayout.Categories.Add(DescribeCategory(*Category, Category->GetCategoryName()));
				}
			}
			Info.Sections.Add(SectionLayout);
		}

		FNovaUserParameterSectionLayout NoSection;
		for (const TObjectPtr<UHierarchyElement>& Child : Root.GetChildren())
		{
			const UHierarchyCategory* Category = Cast<UHierarchyCategory>(Child.Get());
			if (Category == nullptr)
			{
				continue;
			}
			if (GetSectionAssociation(*Category) == nullptr)
			{
				NoSection.Categories.Add(DescribeCategory(*Category, Category->GetCategoryName()));
			}
		}

		// Items directly under the root, outside any category.
		FNovaUserParameterCategoryLayout RootItems;
		for (const TObjectPtr<UHierarchyElement>& Child : Root.GetChildren())
		{
			if (Child && ItemClass && ItemProperty && Child->IsA(ItemClass))
			{
				if (const UNiagaraScriptVariable* ScriptVariable = Cast<UNiagaraScriptVariable>(ItemProperty->GetObjectPropertyValue_InContainer(Child.Get())))
				{
					RootItems.Parameters.Add(FName(*StripUserPrefix(ScriptVariable->Variable.GetName().ToString())));
				}
			}
		}
		if (RootItems.Parameters.Num() > 0)
		{
			Placed.Append(RootItems.Parameters);
			NoSection.Categories.Add(RootItems);
		}
		if (NoSection.Categories.Num() > 0)
		{
			Info.Sections.Add(NoSection);
		}

		TArray<FNiagaraVariable> UserParameters;
		System.GetExposedParameters().GetUserParameters(UserParameters);
		for (const FNiagaraVariable& Parameter : UserParameters)
		{
			const FName Name(*StripUserPrefix(Parameter.GetName().ToString()));
			if (!Placed.Contains(Name))
			{
				Info.UnplacedParameters.Add(Name);
			}
		}
		return Info;
	}

	/** True when Text contains "User.<Key>" not followed by another identifier character. */
	bool TextReferencesParameter(const FString& Text, const FString& Key)
	{
		const FString Needle = TEXT("User.") + Key;
		int32 From = 0;
		while (true)
		{
			const int32 Found = Text.Find(Needle, ESearchCase::CaseSensitive, ESearchDir::FromStart, From);
			if (Found == INDEX_NONE)
			{
				return false;
			}
			const int32 End = Found + Needle.Len();
			if (End >= Text.Len() || !(FChar::IsAlnum(Text[End]) || Text[End] == TEXT('_')))
			{
				return true;
			}
			From = Found + 1;
		}
	}
}

FNovaUserParameterHierarchyInfo UNovaToolset_NiagaraUserParams::GetUserParameterHierarchy(UNiagaraSystem* System)
{
	using namespace NovaUserParams;

	UNiagaraSystemEditorData* EditorData = nullptr;
	UHierarchyRoot* Root = nullptr;
	if (!ResolveHierarchy(System, EditorData, Root))
	{
		return {};
	}
	return DescribeHierarchy(*System, *Root);
}

FNovaUserParameterHierarchyInfo UNovaToolset_NiagaraUserParams::SetUserParameterHierarchy(UNiagaraSystem* System, const TArray<FNovaUserParameterSectionLayout>& Sections, bool bSave)
{
	using namespace NovaUserParams;

	UNiagaraSystemEditorData* EditorData = nullptr;
	UHierarchyRoot* Root = nullptr;
	if (!ResolveHierarchy(System, EditorData, Root))
	{
		return {};
	}
	// An open editor keeps its own hierarchy view models over this data; rebuilding the data underneath them
	// leaves those view models pointing at deleted elements.
	if (!EnsureNoOpenEditor(System))
	{
		return {};
	}

	UClass* ItemClass = UserParameterItemClass();
	FObjectProperty* ItemProperty = UserParameterItemProperty(ItemClass);
	if (ItemClass == nullptr || ItemProperty == nullptr)
	{
		Error(TEXT("NiagaraHierarchyUserParameter or its UserParameterScriptVariable property was not found; the engine's hierarchy classes changed."));
		return {};
	}
	UScriptStruct* AssociationStruct = SectionAssociationStruct();
	if (AssociationStruct == nullptr || MetaDataProperty() == nullptr || FindFProperty<FWeakObjectProperty>(AssociationStruct, TEXT("Section")) == nullptr)
	{
		Error(TEXT("The section association metadata (UHierarchyElement::MetaData / FDataHierarchyElementMetaData_SectionAssociation::Section) was not found; the engine's hierarchy types changed."));
		return {};
	}

	TArray<FNiagaraVariable> UserParameters;
	System->GetExposedParameters().GetUserParameters(UserParameters);

	// Validate everything before touching the asset.
	TArray<FString> Problems;
	TSet<FString> Seen;
	TSet<FName> SectionNames;
	for (const FNovaUserParameterSectionLayout& SectionLayout : Sections)
	{
		if (!SectionLayout.Section.IsNone())
		{
			if (SectionNames.Contains(SectionLayout.Section))
			{
				Problems.Add(FString::Printf(TEXT("section '%s' is listed twice"), *SectionLayout.Section.ToString()));
			}
			SectionNames.Add(SectionLayout.Section);
		}
		for (const FNovaUserParameterCategoryLayout& CategoryLayout : SectionLayout.Categories)
		{
			for (const FName& ParameterName : CategoryLayout.Parameters)
			{
				const FNiagaraVariable* Parameter = FindUserParameter(UserParameters, ParameterName);
				if (Parameter == nullptr)
				{
					Problems.Add(FString::Printf(TEXT("'%s' is not a user parameter"), *ParameterName.ToString()));
					continue;
				}
				const FString Key = StripUserPrefix(Parameter->GetName().ToString());
				if (Seen.Contains(Key))
				{
					Problems.Add(FString::Printf(TEXT("'%s' is placed more than once"), *Key));
				}
				Seen.Add(Key);
			}
		}
	}
	if (Problems.Num() > 0)
	{
		Error(FString::Printf(TEXT("Hierarchy not changed: %s. User parameters of '%s': %s"),
			*FString::Join(Problems, TEXT("; ")), *System->GetName(), *JoinUserParameterNames(UserParameters)));
		return {};
	}

	{
		FScopedTransaction Transaction(LOCTEXT("SetUserParameterHierarchy", "Nova Toolset: Set User Parameter Hierarchy"));
		if (!System->HasAnyFlags(RF_Transactional))
		{
			System->SetFlags(RF_Transactional);
		}
		System->Modify();
		EditorData->Modify();

		// Resolve every script variable first. The lookup can create a missing metadata entry, which is harmless,
		// but a failure has to stop the call before the old hierarchy is emptied.
		TMap<FString, UNiagaraScriptVariable*> ScriptVariables;
		for (const FString& Key : Seen)
		{
			const FNiagaraVariable* Parameter = FindUserParameter(UserParameters, FName(*Key));
			UNiagaraScriptVariable* ScriptVariable = Parameter ? FNiagaraEditorUtilities::UserParameters::GetScriptVariableForUserParameter(*Parameter, *System).Get() : nullptr;
			if (ScriptVariable == nullptr)
			{
				Transaction.Cancel();
				Error(FString::Printf(TEXT("Could not resolve the editor metadata of user parameter '%s'; hierarchy not changed."), *Key));
				return {};
			}
			ScriptVariable->SetFlags(RF_Transactional);
			ScriptVariables.Add(Key, ScriptVariable);
		}

		Root->SetFlags(RF_Transactional);
		Root->Modify();
		Root->EmptyAllData();

		for (const FNovaUserParameterSectionLayout& SectionLayout : Sections)
		{
			UHierarchySection* Section = nullptr;
			if (!SectionLayout.Section.IsNone())
			{
				Section = NewObject<UHierarchySection>(Root, NAME_None, RF_Transactional);
				Section->SetSectionName(SectionLayout.Section);
				Root->GetSectionDataMutable().Add(Section);
			}

			for (const FNovaUserParameterCategoryLayout& CategoryLayout : SectionLayout.Categories)
			{
				UHierarchyElement* Parent = Root;
				if (!CategoryLayout.Category.IsNone())
				{
					UHierarchyCategory* Category = Root->AddChild<UHierarchyCategory>();
					Category->SetCategoryName(CategoryLayout.Category);
					Category->SetIdentity(UHierarchyCategory::ConstructIdentity());
					if (Section)
					{
						// The reflection lookups were verified before the hierarchy was emptied.
						SetSectionAssociation(*Category, Section);
					}
					Parent = Category;
				}

				for (const FName& ParameterName : CategoryLayout.Parameters)
				{
					const FNiagaraVariable* Parameter = FindUserParameter(UserParameters, ParameterName);
					UNiagaraScriptVariable* ScriptVariable = ScriptVariables.FindRef(StripUserPrefix(Parameter->GetName().ToString()));

					UHierarchyItem* Item = NewObject<UHierarchyItem>(Parent, ItemClass, NAME_None, RF_Transactional);
					ItemProperty->SetObjectPropertyValue_InContainer(Item, ScriptVariable);
					Item->SetIdentity(FHierarchyElementIdentity({ ScriptVariable->Metadata.GetVariableGuid() }, {}));
					Parent->GetChildrenMutable().Add(Item);
				}
			}
		}

		System->MarkPackageDirty();
		// Panels built from the script variables (an editor opened later, component Details) rebuild on this.
		EditorData->OnUserParameterScriptVariablesSynced().Broadcast();
	}

	FNovaUserParameterHierarchyInfo Info = DescribeHierarchy(*System, *Root);
	Info.bSaved = bSave && SaveAssetSilently(System);
	return Info;
}

TArray<FNovaUserParameterUsage> UNovaToolset_NiagaraUserParams::GetUserParameterUsage(UNiagaraSystem* System)
{
	using namespace NovaUserParams;

	TArray<FNovaUserParameterUsage> Result;
	if (System == nullptr)
	{
		Error(TEXT("System is null."));
		return Result;
	}

	TArray<FNiagaraVariable> UserParameters;
	System->GetExposedParameters().GetUserParameters(UserParameters);

	TArray<FString> Keys;
	TMap<FString, TArray<FString>> References;
	for (const FNiagaraVariable& Parameter : UserParameters)
	{
		const FString Key = StripUserPrefix(Parameter.GetName().ToString());
		Keys.Add(Key);
		References.Add(Key);
	}

	auto AddReference = [&References](const FString& Key, const FString& Where)
	{
		if (TArray<FString>* List = References.Find(Key))
		{
			List->AddUnique(Where);
		}
	};

	// Any property text naming the parameter counts: user parameter bindings, parameter bindings with value
	// (simulation stage iteration counts), renderer material bindings. Over-reporting only keeps a parameter.
	auto ScanObject = [&](const UObject* Object, const FString& Where)
	{
		if (Object == nullptr)
		{
			return;
		}
		FString Json;
		if (!FJsonObjectConverter::UStructToJsonObjectString(Object->GetClass(), Object, Json, 0, 0))
		{
			return;
		}
		for (const FString& Key : Keys)
		{
			if (TextReferencesParameter(Json, Key))
			{
				AddReference(Key, Where);
			}
		}
	};

	auto ScanGraph = [&](const UEdGraph* Graph, const FString& Where)
	{
		if (Graph == nullptr)
		{
			return;
		}
		for (const TObjectPtr<UEdGraphNode>& Node : Graph->Nodes)
		{
			if (Node == nullptr)
			{
				continue;
			}

			// Map Get output pins are reads whether or not they are wired (BuildParameterMapHistory reads every
			// output pin), and stack inputs linked to a user parameter are Map Get nodes in the emitter graph.
			// The class is matched by name because UNiagaraNodeParameterMapGet lives in NiagaraEditor/Private.
			if (Node->GetClass()->GetFName() == TEXT("NiagaraNodeParameterMapGet"))
			{
				for (const UEdGraphPin* Pin : Node->Pins)
				{
					if (Pin && Pin->Direction == EGPD_Output && !Pin->bOrphanedPin)
					{
						const FString PinName = Pin->PinName.ToString();
						if (PinName.StartsWith(TEXT("User.")))
						{
							AddReference(StripUserPrefix(PinName), Where + TEXT(": Map Get"));
						}
					}
				}
			}

			// Data interfaces held by input nodes (UNiagaraNodeInput::DataInterface is private and its getter is
			// not exported, so any object property holding a data interface is scanned).
			for (TFieldIterator<FObjectPropertyBase> It(Node->GetClass()); It; ++It)
			{
				if (const UNiagaraDataInterface* DataInterface = Cast<UNiagaraDataInterface>(It->GetObjectPropertyValue_InContainer(Node.Get())))
				{
					ScanObject(DataInterface, Where + TEXT(": data interface ") + DataInterface->GetClass()->GetName());
				}
			}
		}
	};

	auto ScanScript = [&](const UNiagaraScript* Script, const FString& Where)
	{
		if (Script == nullptr)
		{
			return;
		}
		const UNiagaraScriptSource* Source = Cast<UNiagaraScriptSource>(Script->GetLatestSource());
		ScanGraph(Source ? Source->NodeGraph.Get() : nullptr, Where);
	};

	// The system spawn and update scripts share one graph.
	ScanScript(System->GetSystemSpawnScript(), TEXT("System graph"));

	for (FNiagaraEmitterHandle& Handle : System->GetEmitterHandles())
	{
		FVersionedNiagaraEmitterData* EmitterData = Handle.GetEmitterData();
		if (EmitterData == nullptr)
		{
			continue;
		}
		const FString EmitterLabel = TEXT("Emitter ") + Handle.GetName().ToString();
		const UNiagaraScriptSource* Source = Cast<UNiagaraScriptSource>(EmitterData->GraphSource);
		ScanGraph(Source ? Source->NodeGraph.Get() : nullptr, EmitterLabel);
		for (const UNiagaraRendererProperties* Renderer : EmitterData->GetRenderers())
		{
			ScanObject(Renderer, EmitterLabel + TEXT(": renderer ") + (Renderer ? Renderer->GetClass()->GetName() : FString()));
		}
		for (const UNiagaraSimulationStageBase* Stage : EmitterData->GetSimulationStages())
		{
			ScanObject(Stage, EmitterLabel + TEXT(": simulation stage ") + (Stage ? Stage->SimulationStageName.ToString() : FString()));
		}
	}

	for (const TObjectPtr<UNiagaraScript>& Script : System->ScratchPadScripts)
	{
		ScanScript(Script.Get(), TEXT("Local module ") + (Script ? Script->GetName() : FString()));
	}

	for (const FNiagaraVariable& Parameter : UserParameters)
	{
		const FString Key = StripUserPrefix(Parameter.GetName().ToString());
		FNovaUserParameterUsage Usage;
		Usage.Name = FName(*Key);
		Usage.Type = Parameter.GetType().GetName();
		Usage.References = References.FindRef(Key);
		Usage.References.Sort();
		Usage.bUsed = Usage.References.Num() > 0;
		Result.Add(Usage);
	}
	return Result;
}

#undef LOCTEXT_NAMESPACE
