using UnityEngine;
using UnityEditor;
using System.Collections.Generic;
using System.IO;
using System.Linq;

public class MapExporter : EditorWindow
{
    private const uint MAGIC = 0x4E435347; // "GSCN"
    private const uint VERSION = 1;

    [MenuItem("Hack And Slash/Export Scene Geometry")]
    static void ExportSceneGeometry()
    {
        var allVertices = new List<Vector3>();
        var allTriangles = new List<int>();

        var meshFilters = Object.FindObjectsByType<MeshFilter>(FindObjectsSortMode.None)
            .Where(mf => mf.gameObject.isStatic && mf.sharedMesh != null)
            .ToArray();

        if (meshFilters.Length == 0)
        {
            Debug.LogError("[MapExporter] No static MeshFilter objects found in scene.");
            return;
        }

        foreach (var mf in meshFilters)
        {
            var mesh = mf.sharedMesh;
            var tr = mf.transform;
            int vertexOffset = allVertices.Count;

            // Transform vertices to world space
            foreach (var v in mesh.vertices)
            {
                allVertices.Add(tr.TransformPoint(v));
            }

            // Add triangles with offset
            foreach (var idx in mesh.triangles)
            {
                allTriangles.Add(idx + vertexOffset);
            }

            Debug.Log($"[MapExporter] {mf.gameObject.name}: {mesh.vertexCount} verts, {mesh.triangles.Length / 3} tris");
        }

        int vertexCount = allVertices.Count;
        int triangleCount = allTriangles.Count / 3;

        // Write binary file
        string outputDir = Path.GetFullPath(Path.Combine(Application.dataPath, "../../ShareDir/maps"));
        if (!Directory.Exists(outputDir))
            Directory.CreateDirectory(outputDir);

        string outputPath = Path.Combine(outputDir, "default.scene.bin");

        using (var writer = new BinaryWriter(File.Create(outputPath)))
        {
            // Header (20 bytes)
            writer.Write(MAGIC);           // 4B magic
            writer.Write(VERSION);         // 4B version
            writer.Write((uint)vertexCount);    // 4B vertex count
            writer.Write((uint)triangleCount);  // 4B triangle count
            writer.Write((uint)0);              // 4B reserved

            // Vertices (vertexCount * 12 bytes)
            foreach (var v in allVertices)
            {
                writer.Write(v.x);
                writer.Write(v.y);
                writer.Write(v.z);
            }

            // Triangles (triangleCount * 12 bytes)
            for (int i = 0; i < allTriangles.Count; i += 3)
            {
                writer.Write(allTriangles[i]);
                writer.Write(allTriangles[i + 1]);
                writer.Write(allTriangles[i + 2]);
            }
        }

        long fileSize = new FileInfo(outputPath).Length;
        Debug.Log($"[MapExporter] Exported: {vertexCount} vertices, {triangleCount} triangles, {fileSize} bytes");
        Debug.Log($"[MapExporter] Saved to: {outputPath}");

        AssetDatabase.Refresh();
    }
}
