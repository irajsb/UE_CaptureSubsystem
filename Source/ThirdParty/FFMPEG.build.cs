// Copyright iraj mohtasham aurelion.net 2023

using UnrealBuildTool;
using System.IO;
public class FFMPEG : ModuleRules
{
    public string BinFolder(ReadOnlyTargetRules Target)
    {
        if(Target.Platform == UnrealTargetPlatform.Mac)
            return Path.GetFullPath(Path.Combine(ModuleDirectory, "../../../../Binaries/Mac/"));
        else if(Target.Platform == UnrealTargetPlatform.IOS)
            return Path.GetFullPath(Path.Combine(ModuleDirectory, "../../../../Binaries/IOS/"));
        if(Target.Platform == UnrealTargetPlatform.Win64)
            return Path.GetFullPath(Path.Combine(ModuleDirectory, "../../../../Binaries/Win64/"));
        if(Target.Platform == UnrealTargetPlatform.Android)
            return Path.GetFullPath(Path.Combine(ModuleDirectory, "../../../../Binaries/Android/"));
        if(Target.Platform == UnrealTargetPlatform.Linux)
            return Path.GetFullPath(Path.Combine(ModuleDirectory, "../../../../Binaries/Linux/"));
        return "";
    }

    public FFMPEG(ReadOnlyTargetRules Target) : base(Target)
    {
        Type = ModuleType.External;
        string BinaryFolder = BinFolder(Target);
        if (Target.Platform == UnrealTargetPlatform.Win64)
        {
            string PlatformString = "x64";
            string LibrariesPath = Path.Combine(Path.Combine(Path.Combine(ModuleDirectory, "ffmpeg", "lib"), "vs"), PlatformString);    

            PublicAdditionalLibraries.Add(Path.Combine(LibrariesPath, "avcodec.lib"));
            PublicAdditionalLibraries.Add(Path.Combine(LibrariesPath, "avdevice.lib"));
            PublicAdditionalLibraries.Add(Path.Combine(LibrariesPath, "avfilter.lib"));
            PublicAdditionalLibraries.Add(Path.Combine(LibrariesPath, "avformat.lib"));
            PublicAdditionalLibraries.Add(Path.Combine(LibrariesPath, "avutil.lib"));
            PublicAdditionalLibraries.Add(Path.Combine(LibrariesPath, "swresample.lib"));
            PublicAdditionalLibraries.Add(Path.Combine(LibrariesPath, "swscale.lib"));
          
            string[] Dlls = { "avcodec-59.dll", "avdevice-59.dll", "avfilter-8.dll", "avformat-59.dll", "avutil-57.dll", "swresample-4.dll", "swscale-6.dll", "postproc-56.dll" };
            Directory.CreateDirectory(BinaryFolder);
            string SrcBinariesPath = Path.Combine(Path.Combine(Path.Combine(ModuleDirectory, "ffmpeg", "bin"), "vs"), PlatformString);
          
            foreach (string Dll in Dlls)
            {
                string BinPath =Path.Combine(ModuleDirectory, BinaryFolder);
                CopyFile(Path.Combine(SrcBinariesPath, Dll),Path.Combine(BinPath,Dll));
                RuntimeDependencies.Add(Path.Combine(BinPath,Dll));
            }
        }
        else if (Target.Platform == UnrealTargetPlatform.Mac)
        {
            string LibrariesPath = Path.Combine(Path.Combine(ModuleDirectory, "ffmpeg", "lib"), "osx");

            System.Console.WriteLine("... LibrariesPath -> " + LibrariesPath);

            string[] libs = { "libavcodec.58.dylib", "libavdevice.58.dylib", "libavfilter.7.dylib", "libavformat.58.dylib", "libavutil.56.dylib", "libswresample.3.dylib", "libswscale.5.dylib", "libpostproc.55.dylib" };
            foreach (string lib in libs)
            {
                PublicAdditionalLibraries.Add(Path.Combine(LibrariesPath, lib));
                RuntimeDependencies.Add(Path.Combine(LibrariesPath, lib), StagedFileType.NonUFS);
            }

        }
        // Include path
        PublicIncludePaths.Add(Path.Combine(ModuleDirectory, "ffmpeg", "include"));
        


       

        DynamicallyLoadedModuleNames.AddRange(
            new string[]
            {
				// ... add any modules that your module loads dynamically here ...
			}
            );

    }
    
    public void CopyFile(string Source, string Dest)
    {
        System.Console.WriteLine("Copying {0} to {1}", Source, Dest);
        if (System.IO.File.Exists(Dest))
        {
            System.IO.File.SetAttributes(Dest, System.IO.File.GetAttributes(Dest) & ~System.IO.FileAttributes.ReadOnly);
        }
        try
        {
            //Make Folder
		
            System.IO.File.Copy(Source, Dest, true);
        }
        catch (System.Exception ex)
        {
            System.Console.WriteLine("Failed to copy file: {0}", ex.Message);
        }
    }
}
