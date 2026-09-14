#pragma once

#include "CoreMinimal.h"
#include "NovaToolset.h"
#include "NovaToolset_NiagaraGraph.generated.h"

class UNiagaraScript;

/** One pin of a graph node. */
USTRUCT()
struct FNovaGraphPinInfo
{
	GENERATED_BODY()

	/** Pin name. Parameter map pins are named after the parameter, e.g. Emitter.RTV_WindField. Execution map pins may have an empty name. */
	UPROPERTY()
	FName PinName;

	UPROPERTY()
	bool bOutput = false;

	/** Niagara type name, e.g. NiagaraFloat, NiagaraPosition, NiagaraParameterMap, NiagaraDataInterfaceGrid3DCollection. Empty for add pins. */
	UPROPERTY()
	FString Type;

	UPROPERTY()
	FString DefaultValue;

	/** The "+" pin. Connecting a typed pin to it creates a new pin of that type (Custom HLSL, Map Get/Set). */
	UPROPERTY()
	bool bIsAddPin = false;

	/** Hidden in the editor UI, e.g. the default-value input Map Get keeps for each parameter pin. */
	UPROPERTY()
	bool bHidden = false;

	/** Connected pins as "NodeId|PinName". */
	UPROPERTY()
	TArray<FString> LinkedTo;
};

/** One entry of a data interface function call node's specifier list, e.g. Attribute = Position. */
USTRUCT()
struct FNovaGraphSpecifier
{
	GENERATED_BODY()

	UPROPERTY()
	FName Name;

	UPROPERTY()
	FName Value;
};

/** One node of a Niagara script graph. */
USTRUCT()
struct FNovaGraphNodeInfo
{
	GENERATED_BODY()

	/** Stable node id (NodeGuid). Pass it to the other graph tools. */
	UPROPERTY()
	FString NodeId;

	UPROPERTY()
	FString NodeClass;

	UPROPERTY()
	FString Title;

	UPROPERTY()
	int32 PosX = 0;

	UPROPERTY()
	int32 PosY = 0;

	UPROPERTY()
	TArray<FNovaGraphPinInfo> Pins;

	/** Custom HLSL nodes only. */
	UPROPERTY()
	FString CustomHlsl;

	/** Custom HLSL nodes only. */
	UPROPERTY()
	TArray<FString> VirtualIncludeFilePaths;

	/** Custom HLSL nodes only. */
	UPROPERTY()
	TArray<FString> AbsoluteIncludeFilePaths;

	/**
	 * Data interface function call nodes only: the specifiers shown on the node, such as Attribute on
	 * Get Position by Index. An empty Value means no attribute has been picked yet.
	 */
	UPROPERTY()
	TArray<FNovaGraphSpecifier> FunctionSpecifiers;
};

/** Whole graph of a Niagara script. */
USTRUCT()
struct FNovaGraphInfo
{
	GENERATED_BODY()

	UPROPERTY()
	TObjectPtr<UNiagaraScript> Script = nullptr;

	/** Asset that owns the script: the Niagara System for local modules, the script itself for module assets. */
	UPROPERTY()
	FString OwningAsset;

	UPROPERTY()
	TArray<FNovaGraphNodeInfo> Nodes;
};

/** An entry of the graph's node creation menu. */
USTRUCT()
struct FNovaNodeActionInfo
{
	GENERATED_BODY()

	/** Name as shown in the (possibly localized) editor menu. */
	UPROPERTY()
	FString DisplayName;

	/** Untranslated name. Either this or DisplayName can be passed to AddNode. */
	UPROPERTY()
	FString SourceName;

	/** Menu category path, e.g. "Math > Operators". */
	UPROPERTY()
	FString Category;
};

/** Input or output declaration of a Custom HLSL node. */
USTRUCT()
struct FNovaHlslPin
{
	GENERATED_BODY()

	/** Name used in the HLSL code. */
	UPROPERTY()
	FName Name;

	/** Type name: float, int, bool, vec2, vec3, vec4, color, position, quat, matrix, map, a Niagara type name, or a data interface class (e.g. /Script/Niagara.NiagaraDataInterfaceGrid3DCollection). */
	UPROPERTY()
	FString Type;
};

/** Result of pushing graph edits to everything that uses the script. */
USTRUCT()
struct FNovaApplyGraphInfo
{
	GENERATED_BODY()

	UPROPERTY()
	TObjectPtr<UNiagaraScript> Script = nullptr;

	/** Function call nodes (module instances in system stacks) refreshed from the edited graph. */
	UPROPERTY()
	int32 RefreshedModuleNodes = 0;

	/** Systems that were asked to recompile. */
	UPROPERTY()
	TArray<FString> RecompiledSystems;

	UPROPERTY()
	bool bSaved = false;
};

/**
 * Reads and edits the node graph of a Niagara script: a local (scratch pad) module such as
 * /Game/FX/NS_Foo.NS_Foo:MyLocalModule, or a module asset.
 *
 * Workflow: GetGraph -> ListNodeActions -> AddNode -> AddParameterPin / SetCustomHlsl ->
 * ConnectPins -> ApplyGraphChanges. Edits only reach the systems that use the module after
 * ApplyGraphChanges, exactly like the Apply button of the local module editor.
 *
 * Nodes are addressed by NodeId from GetGraph. Connections always go from an output pin to an input
 * pin. While the owning system is open in the Niagara editor, every tool reads and edits the editor's
 * working copy of the local module (what its graph tab shows) and ApplyGraphChanges presses its Apply
 * button. Module assets and emitter assets must have their own editor closed for write tools.
 * Tip for solver code: keep the HLSL in a .ush file and reference it through SetCustomHlsl include
 * paths, so later changes only touch the file.
 */
UCLASS()
class UNovaToolset_NiagaraGraph : public UNovaToolset
{
	GENERATED_BODY()

public:
	/**
	 * Returns every node of the script graph with its pins, types, default values and connections.
	 *
	 * @param Script Local module (e.g. /Game/FX/NS_Foo.NS_Foo:MyModule) or module asset
	 * @return The graph
	 */
	UFUNCTION(meta = (AICallable), Category = "NiagaraGraph")
	static FNovaGraphInfo GetGraph(UNiagaraScript* Script);

	/**
	 * Lists entries of the node creation menu (right click in the graph). With FromNodeId and
	 * FromPinName, lists what the menu shows when dragging from that pin, which includes data
	 * interface functions for data interface pins.
	 *
	 * @param Script Local module or module asset
	 * @param Filter Case-insensitive substring matched against names and categories; empty lists everything
	 * @param FromNodeId Optional node to drag from
	 * @param FromPinName Pin on FromNodeId to drag from
	 * @param bFromOutput Whether FromPinName is an output pin
	 * @param MaxResults Maximum entries returned
	 * @return Matching menu entries
	 */
	UFUNCTION(meta = (AICallable), Category = "NiagaraGraph")
	static TArray<FNovaNodeActionInfo> ListNodeActions(UNiagaraScript* Script, const FString& Filter, FName FromNodeId = NAME_None, FName FromPinName = NAME_None, bool bFromOutput = true, int32 MaxResults = 100);

	/**
	 * Creates a node from a menu entry of ListNodeActions. When FromNodeId/FromPinName are given the new
	 * node is created as if dragged from that pin and is connected to it automatically when possible.
	 *
	 * @param Script Local module or module asset
	 * @param ActionName DisplayName or SourceName from ListNodeActions
	 * @param MenuCategory Category from ListNodeActions, used when several entries share a name
	 * @param FromNodeId Optional node to drag from
	 * @param FromPinName Pin on FromNodeId to drag from
	 * @param bFromOutput Whether FromPinName is an output pin
	 * @param PosX Graph X position
	 * @param PosY Graph Y position
	 * @return The new node
	 */
	// Optional ids are FName/NAME_None rather than FString/TEXT(""): the registry treats an empty-string
	// default as "no default" and marks the parameter required. The parameter is not called Category
	// because that name collides with the UFUNCTION's own Category metadata.
	UFUNCTION(meta = (AICallable), Category = "NiagaraGraph")
	static FNovaGraphNodeInfo AddNode(UNiagaraScript* Script, const FString& ActionName, FName MenuCategory = NAME_None, FName FromNodeId = NAME_None, FName FromPinName = NAME_None, bool bFromOutput = true, int32 PosX = 0, int32 PosY = 0);

	/**
	 * Adds a named parameter pin to a Map Get (output pin) or Map Set (input pin) node and registers the
	 * parameter in the graph.
	 *
	 * @param Script Local module or module asset
	 * @param NodeId A Map Get or Map Set node
	 * @param ParameterName Namespaced parameter, e.g. Emitter.RTV_WindField, Particles.Position, Module.Strength
	 * @param Type float, int, bool, vec2, vec3, vec4, color, position, quat, matrix, a Niagara type name, or a data interface class path
	 * @return The node after the change
	 */
	UFUNCTION(meta = (AICallable), Category = "NiagaraGraph")
	static FNovaGraphNodeInfo AddParameterPin(UNiagaraScript* Script, const FString& NodeId, FName ParameterName, const FString& Type);

	/**
	 * Sets a specifier on a data interface function call node: the small field on the node itself, such as
	 * Attribute on Get Position by Index or Get ID by Index. GetGraph lists the specifiers each node has.
	 *
	 * @param Script Local module or module asset
	 * @param NodeId Data interface function call node from GetGraph
	 * @param Specifier Specifier name, e.g. Attribute
	 * @param Value Value to set, e.g. Position
	 * @return The node after the change
	 */
	UFUNCTION(meta = (AICallable), Category = "NiagaraGraph")
	static FNovaGraphNodeInfo SetFunctionSpecifier(UNiagaraScript* Script, const FString& NodeId, FName Specifier, FName Value);

	/**
	 * Connects an output pin to an input pin, with the same type checks as dragging a wire. Connecting to
	 * an input named Add on a Custom HLSL or Map Set node creates a new typed pin.
	 *
	 * @param Script Local module or module asset
	 * @param FromNodeId Node owning the output pin
	 * @param FromPinName Output pin name
	 * @param ToNodeId Node owning the input pin
	 * @param ToPinName Input pin name
	 * @return The node owning the input pin after the change
	 */
	UFUNCTION(meta = (AICallable), Category = "NiagaraGraph")
	static FNovaGraphNodeInfo ConnectPins(UNiagaraScript* Script, const FString& FromNodeId, FName FromPinName, const FString& ToNodeId, FName ToPinName);

	/**
	 * Removes the connection between an output pin and an input pin.
	 *
	 * @param Script Local module or module asset
	 * @param FromNodeId Node owning the output pin
	 * @param FromPinName Output pin name
	 * @param ToNodeId Node owning the input pin
	 * @param ToPinName Input pin name
	 * @return The node owning the input pin after the change
	 */
	UFUNCTION(meta = (AICallable), Category = "NiagaraGraph")
	static FNovaGraphNodeInfo DisconnectPins(UNiagaraScript* Script, const FString& FromNodeId, FName FromPinName, const FString& ToNodeId, FName ToPinName);

	/**
	 * Deletes a node and its connections. Output nodes cannot be deleted.
	 *
	 * @param Script Local module or module asset
	 * @param NodeId Node to delete
	 * @return The graph after the change
	 */
	UFUNCTION(meta = (AICallable), Category = "NiagaraGraph")
	static FNovaGraphInfo RemoveNode(UNiagaraScript* Script, const FString& NodeId);

	/**
	 * Sets the inline default value of an unconnected input pin, e.g. "1.5" or "0,0,1".
	 *
	 * @param Script Local module or module asset
	 * @param NodeId Node owning the pin
	 * @param PinName Input pin name
	 * @param Value Default value text
	 * @return The node after the change
	 */
	UFUNCTION(meta = (AICallable), Category = "NiagaraGraph")
	static FNovaGraphNodeInfo SetPinDefaultValue(UNiagaraScript* Script, const FString& NodeId, FName PinName, const FString& Value);

	/**
	 * Sets the code, include files and (optionally) the input/output pins of a Custom HLSL node. Replacing
	 * pins rebuilds them; connections survive only on pins whose name is unchanged, so reconnect the rest.
	 * Existing parameter map pins are kept when Inputs/Outputs declare no map-typed pin.
	 *
	 * @param Script Local module or module asset
	 * @param NodeId A Custom HLSL node
	 * @param Hlsl HLSL body; refer to inputs and outputs by name
	 * @param Inputs Input declarations in order; ignored when bReplacePins is false
	 * @param Outputs Output declarations in order; ignored when bReplacePins is false
	 * @param VirtualIncludeFilePaths Virtual shader paths to include, e.g. /Plugin/FX/Niagara/Private/NiagaraStrandsPhysics.ush
	 * @param AbsoluteIncludeFilePaths Absolute .ush file paths to include; edits to these files do not trigger a recompile by themselves
	 * @param bReplacePins Replace the node's pins with Inputs and Outputs
	 * @return The node after the change
	 */
	UFUNCTION(meta = (AICallable), Category = "NiagaraGraph")
	static FNovaGraphNodeInfo SetCustomHlsl(UNiagaraScript* Script, const FString& NodeId, const FString& Hlsl, const TArray<FNovaHlslPin>& Inputs, const TArray<FNovaHlslPin>& Outputs, const TArray<FString>& VirtualIncludeFilePaths, const TArray<FString>& AbsoluteIncludeFilePaths, bool bReplacePins = true);

	/**
	 * Pushes graph edits to every module instance that uses the script (the Apply button of the local
	 * module editor), requests a recompile of the affected systems, and optionally saves them.
	 * While the owning system is open in the Niagara editor this applies the editor's whole working copy,
	 * including edits the user made there and has not applied yet.
	 *
	 * @param Script Local module or module asset
	 * @param bSave Save the owning asset and affected systems
	 * @return What was refreshed and recompiled
	 */
	UFUNCTION(meta = (AICallable), Category = "NiagaraGraph")
	static FNovaApplyGraphInfo ApplyGraphChanges(UNiagaraScript* Script, bool bSave = true);
};
