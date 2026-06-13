using UnrealBuildTool;

public class PredictiveVisibilitySystemEditor : ModuleRules
{
	public PredictiveVisibilitySystemEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"PredictiveVisibilitySystemRuntime"
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"UnrealEd",
			"EditorSubsystem",
			"ToolMenus",
			"AssetTools",
			"AssetRegistry",
			"Slate",
			"SlateCore"
		});
	}
}
