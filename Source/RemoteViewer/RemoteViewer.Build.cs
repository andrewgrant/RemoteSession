// Copyright 2017 Andrew Grant
// This file is part of RemoteViewer and is freely licensed for commercial and 
// non-commercial use under an MIT license
// See https://github.com/andrewgrant/RemoteViewer for more info

using UnrealBuildTool;

public class RemoteViewer : ModuleRules
{
	public RemoteViewer(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		
		PublicIncludePaths.AddRange(
			new string[] {
				"RemoteViewer/Public"
				// ... add public include paths required here ...
			}
		);
				
		
		PrivateIncludePaths.AddRange(
			new string[] {
				"RemoteViewer/Private",
				// ... add other private include paths required here ...
			}
		);
			
		
		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				// ... add other public dependencies that you statically link with here ...
			}
		);
			
		
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"CoreUObject",
				"Engine",
				"Slate",
				"SlateCore",
				"InputDevice",
				"InputCore",
				"BackChannel",
				"RenderCore",
				"RHI",
				"ImageWrapper"
			}
		);

		if (Target.bBuildEditor == true)
		{
			//reference the module "MyModule"
			PrivateDependencyModuleNames.Add("UnrealEd");
		}

		DynamicallyLoadedModuleNames.AddRange(
			new string[]
			{
				// ... add any modules that your module loads dynamically here ...
			}
		);
	}
}
