// Copyright 2017 Gwi - All rights reserved
using UnrealBuildTool;
using System.IO;

public class yamlcpp : ModuleRules
{
	public yamlcpp(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;
		
		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			PublicIncludePaths.Add(Path.Combine(ModuleDirectory, "yaml-cpp/include"));
			PublicAdditionalLibraries.Add(Path.Combine(ModuleDirectory, "yaml-cpp/bin/Release/yaml-cpp.lib"));
		}
	}
}


