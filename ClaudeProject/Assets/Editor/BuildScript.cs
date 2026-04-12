using UnityEditor;
using UnityEditor.Build.Reporting;
using UnityEngine;
using System.IO;

public class BuildScript
{
    private const string BuildFolder = "Build";

    [MenuItem("Build/Build And Run (Relative Path)")]
    public static void BuildAndRun()
    {
        Build(BuildOptions.AutoRunPlayer);
    }

    [MenuItem("Build/Build Only (Relative Path)")]
    public static void BuildOnly()
    {
        Build(BuildOptions.None);
    }

    private static void Build(BuildOptions options)
    {
        var target = EditorUserBuildSettings.activeBuildTarget;
        var fileName = PlayerSettings.productName + GetExtension(target);
        var buildPath = Path.Combine(BuildFolder, fileName);

        if (!Directory.Exists(BuildFolder))
            Directory.CreateDirectory(BuildFolder);

        var scenes = GetEnabledScenes();
        var report = BuildPipeline.BuildPlayer(scenes, buildPath, target, options);

        if (report.summary.result == BuildResult.Succeeded)
            Debug.Log($"Build succeeded: {buildPath}");
        else
            Debug.LogError($"Build failed: {report.summary.result}");
    }

    private static string[] GetEnabledScenes()
    {
        var scenes = EditorBuildSettings.scenes;
        var enabledScenes = new System.Collections.Generic.List<string>();
        foreach (var scene in scenes)
        {
            if (scene.enabled)
                enabledScenes.Add(scene.path);
        }
        return enabledScenes.ToArray();
    }

    private static string GetExtension(BuildTarget target)
    {
        switch (target)
        {
            case BuildTarget.StandaloneWindows:
            case BuildTarget.StandaloneWindows64:
                return ".exe";
            case BuildTarget.StandaloneOSX:
                return ".app";
            case BuildTarget.StandaloneLinux64:
                return "";
            case BuildTarget.Android:
                return ".apk";
            case BuildTarget.WebGL:
                return "";
            default:
                return "";
        }
    }
}
