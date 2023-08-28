// Copyright iraj mohtasham aurelion.net 2023

using UnrealBuildTool;
using System.IO;
public class CaptureSubsystem : ModuleRules
{

    public CaptureSubsystem(ReadOnlyTargetRules Target) : base(Target)
    {
       // PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
       PublicDependencyModuleNames.AddRange(
            new string[]
            {
                "Core",
                "AudioMixer",
                "FFMPEG"
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
                "Projects",
                "Engine",
                "RHI",
                "InputCore",
                "RenderCore", "D3D12RHI"
                // ... add private dependencies that you statically link with here ...	
			}
            );

        if (Target.Type == TargetType.Editor)
        {
            PrivateDependencyModuleNames.Add("UnrealEd");
        }
        OptimizeCode = CodeOptimization.InShippingBuildsOnly;
        //bEnableUndefinedIdentifierWarnings = false;

        DynamicallyLoadedModuleNames.AddRange(
            new string[]
            {
				// ... add any modules that your module loads dynamically here ...
			}
            );
    }
}
