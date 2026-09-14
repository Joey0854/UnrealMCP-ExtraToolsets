using UnrealBuildTool;

public class NovaToolsets : ModuleRules
{
	public NovaToolsets(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PrivateDependencyModuleNames.AddRange(new[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"RHI",
			"RenderCore",
			"Slate",
			"SlateCore",
			"UnrealEd",
			"EditorFramework",
			"AssetRegistry",
			"Json",
			"JsonUtilities",
			"JsonUtilitiesEditor",
			"ToolsetRegistry",
			"Niagara",
			"NiagaraCore",
			"NiagaraShader",
			"NiagaraEditor",
			// UHierarchyRoot / sections / categories behind the Niagara user parameter hierarchy.
			"DataHierarchyEditor",
			// Private dependencies of NiagaraEditor whose headers leak through NiagaraSystemViewModel.h,
			// NiagaraEditorModule.h and the stack view model headers.
			"AssetTools",
			"BlueprintGraph",
			"GraphEditor",
			"PropertyEditor",
			"CurveEditor",
			"MovieScene",
			"SequencerCore",
			"Sequencer",
			"TimeManagement",
			"ToolMenus",
			"VectorVM",
		});
	}
}
