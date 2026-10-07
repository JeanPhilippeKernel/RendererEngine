#include <ZEngine/Core/Memory/MemoryManager.h>
#include <ZEngine/Core/VFS/Meta/MetaFileIO.h>
#include <ZEngine/Core/VFS/VFSContext.h>
#include <ZEngine/Core/VFS/VFSMemoryBackend.h>
#include <ZEngine/Importers/AssimpImporter.h>
#include <ZEngine/Importers/FbxImporter.h>
#include <ZEngine/Importers/GltfImporter.h>
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <stb/stb_image_write.h>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
#include <random>
#include <string>
#include <vector>

using namespace ZEngine::Core::VFS;
using namespace ZEngine::Importers;
using namespace ZEngine::Importers::AssetCodec;
using ZEngine::Core::Containers::ArrayView;
using ZEngine::Core::Memory::ArenaAllocator;

namespace
{
    enum class FailurePoint
    {
        None,
        CreateDir,
        Open,
        Write,
        ShortWrite,
        Flush,
        Close,
        Rename
    };

    // Use the real in-memory VFS; inject only the operation under test.
    struct FaultVFSContext : VFSContext
    {
        struct FileProxy : IVFSFile
        {
            IVFSFile*         Inner = nullptr;
            FailurePoint      Point = FailurePoint::None;

            VFSResult<size_t> Read(ArrayView<uint8_t> buffer, uint64_t offset) override
            {
                return Inner->Read(buffer, offset);
            }
            VFSResult<size_t> Write(ArrayView<const uint8_t> buffer, uint64_t offset) override
            {
                if (Point == FailurePoint::Write)
                    return VFSResult<size_t>::Fail(VFSError::IOError);
                if (Point == FailurePoint::ShortWrite)
                    return Inner->Write({buffer.data(), buffer.size() - 1}, offset);
                return Inner->Write(buffer, offset);
            }
            VFSResult<uint64_t> Size() const override
            {
                return Inner->Size();
            }
            VFSResult<VFSFileStat> Stat() const override
            {
                return Inner->Stat();
            }
            VFSResult<void> Flush() override
            {
                return Point == FailurePoint::Flush ? VFSResult<void>::Fail(VFSError::IOError) : Inner->Flush();
            }
            const VFSPath& Path() const override
            {
                return Inner->Path();
            }
            VFSResult<void> Close() override
            {
                auto result = Inner->Close();
                return Point == FailurePoint::Close ? VFSResult<void>::Fail(VFSError::IOError) : result;
            }
        } File;

        FailurePoint Point = FailurePoint::None;
        std::string  FailurePath;
        size_t       WriteNumber        = 0;
        size_t       FailureWriteNumber = 0;
        VFSError     MetadataReadError  = VFSError::OK;
        std::string  MetadataReadPath;

        bool         Matches(const VFSPath& path) const
        {
            return (FailureWriteNumber == 0 || FailureWriteNumber == WriteNumber) && std::string(path.CStr()).find(FailurePath) != std::string::npos;
        }

        VFSResult<void> CreateDir(const VFSPath& path) override
        {
            if (Point == FailurePoint::CreateDir && Matches(path))
                return VFSResult<void>::Fail(VFSError::PermissionDenied);
            return VFSContext::CreateDir(path);
        }

        VFSResult<IVFSFile*> Open(const VFSPath& path, VFSOpenFlags flags) override
        {
            if (!HasFlag(flags, VFSOpenFlags::Write))
            {
                const std::string_view name(path.CStr());
                if (MetadataReadError != VFSError::OK && name.ends_with(".meta") && name.find(MetadataReadPath) != std::string_view::npos)
                    return VFSResult<IVFSFile*>::Fail(MetadataReadError);
                return VFSContext::Open(path, flags);
            }
            ++WriteNumber;
            if (Point == FailurePoint::Open && Matches(path))
                return VFSResult<IVFSFile*>::Fail(VFSError::PermissionDenied);
            auto result = VFSContext::Open(path, flags);
            if (result.Failed())
                return result;
            File.Inner = result.Value();
            File.Point = Matches(path) ? Point : FailurePoint::None;
            return VFSResult<IVFSFile*>::Ok(&File);
        }

        void Close(IVFSFile* file) override
        {
            if (file == &File)
            {
                VFSContext::Close(File.Inner);
                File.Inner = nullptr;
            }
            else
                VFSContext::Close(file);
        }

        VFSResult<void> Rename(const VFSPath& src, const VFSPath& dst) override
        {
            if (Point == FailurePoint::Rename && Matches(dst))
                return VFSResult<void>::Fail(VFSError::IOError);
            return VFSContext::Rename(src, dst);
        }
    };

    struct ImportCallbacks
    {
        int                              Completed = 0;
        int                              Failed    = 0;
        float                            Progress  = 0;
        std::string                      Error;
        std::vector<AssetImporterOutput> Outputs;

        static void                      Complete(void* context, ArrayView<AssetImporterOutput> outputs)
        {
            auto& self = *static_cast<ImportCallbacks*>(context);
            ++self.Completed;
            self.Outputs.clear();
            for (size_t i = 0; i < outputs.size(); ++i)
                self.Outputs.push_back(outputs[i]);
        }
        static void Fail(void* context, std::string_view message)
        {
            auto& self = *static_cast<ImportCallbacks*>(context);
            ++self.Failed;
            self.Error = message;
        }
        static void Advance(void* context, float progress)
        {
            static_cast<ImportCallbacks*>(context)->Progress = progress;
        }
    };
    struct PausedImportCallbacks
    {
        ImportCallbacks          Results;
        std::promise<void>       Entered;
        std::shared_future<void> Release;
        bool                     Paused = false;

        static void              Complete(void* context, ArrayView<AssetImporterOutput> outputs)
        {
            ImportCallbacks::Complete(&static_cast<PausedImportCallbacks*>(context)->Results, outputs);
        }
        static void Fail(void* context, std::string_view message)
        {
            ImportCallbacks::Fail(&static_cast<PausedImportCallbacks*>(context)->Results, message);
        }
        static void Advance(void* context, float progress)
        {
            auto& self            = *static_cast<PausedImportCallbacks*>(context);
            self.Results.Progress = progress;
            if (!self.Paused)
            {
                self.Paused = true;
                self.Entered.set_value();
                self.Release.wait();
            }
        }
    };
} // namespace

class ModelImporterOutputTest : public ::testing::Test
{
protected:
    // Declaration order keeps arena storage alive through backend/importer destruction.
    ZEngine::Core::Memory::MemoryManager Memory;
    ArenaAllocator&                      Arena = Memory.MainArena;
    VFSMemoryBackend                     Backend;
    FaultVFSContext                      Context;
    GltfImporter                         Importer;
    ImportConfiguration                  Config;
    ImportCallbacks                      Callbacks;
    std::filesystem::path                TempDirectory;

    void                                 SetUp() override
    {
        // FBX reserves a 512 MiB sub-arena; pages are committed only as used.
        Memory.Initialize(ZMega(768), {});
        Backend.Initialize(&Arena);
        Context.Initialize(&Arena, 4);
        ASSERT_TRUE(Context.Mount(&Backend, VFSPath::Root(), 0).Succeeded());
        Importer.Initialize(&Arena);

        Config.OutputWorkingSpacePath.init(&Arena, "/project");
        Config.OutputAssetsPath.init(&Arena, "/meshes");
        Config.OutputMaterialPath.init(&Arena, "/materials");
        Config.OutputTextureFilesPath.init(&Arena, "/textures");
        Config.OutputAssetFile.init(&Arena, "triangle.zemesh");
        Config.AssetName.init(&Arena, "triangle");
        Config.VFS                     = &Context;
        Config.Options.ImportMaterials = false;
        Config.Options.ImportTextures  = false;
        Config.Options.NormalsMode     = 0;

        std::random_device           rd;
        std::mt19937                 random(rd());
        uuids::uuid_random_generator uuid(random);
#if defined(_WIN32)
        // With no project root configured, runtime imports resolve drive-less VFS
        // paths on the current drive. CI's system temp directory may be on another.
        const auto input_root = std::filesystem::current_path();
#else
        const auto input_root = std::filesystem::temp_directory_path();
#endif
        TempDirectory = input_root / ("zengine_import_output_" + uuids::to_string(uuid()));
        ASSERT_TRUE(std::filesystem::create_directory(TempDirectory));
        Config.InputBaseAssetFilePath.init(&Arena, TempDirectory.string().c_str());
    }

    void TearDown() override
    {
        Context.Shutdown();
        std::error_code error;
        std::filesystem::remove_all(TempDirectory, error);
    }

    VFSPath Path(const char* raw)
    {
        return VFSPath::Parse(raw).Value();
    }

    bool Exists(const char* raw)
    {
        auto result = Context.Exists(Path(raw));
        return result.Succeeded() && result.Value();
    }

    void ExpectNoTemporaryFiles(const char* directory)
    {
        auto entries = Context.List(Path(directory), &Arena);
        if (entries.Failed() && entries.Error() == VFSError::NotFound)
            return;
        ASSERT_TRUE(entries.Succeeded());
        for (const auto& entry : entries.Value())
        {
            EXPECT_FALSE(std::string_view(entry.Path.CStr()).ends_with(".tmp")) << entry.Path.CStr();
            if (entry.IsDirectory)
                ExpectNoTemporaryFiles(entry.Path.CStr());
        }
    }

    std::vector<uint8_t> ReadFile(const char* raw)
    {
        auto open = Context.Open(Path(raw), VFSOpenFlags::Read);
        EXPECT_TRUE(open.Succeeded());
        if (open.Failed())
            return {};
        auto size = open.Value()->Size();
        EXPECT_TRUE(size.Succeeded());
        std::vector<uint8_t> bytes(size.Succeeded() ? size.Value() : 0);
        auto                 read = open.Value()->ReadAll({bytes.data(), bytes.size()});
        EXPECT_TRUE(read.Succeeded());
        Context.Close(open.Value());
        return bytes;
    }

    std::string WriteGltf(size_t material_count = 0)
    {
        const std::array<float, 9> vertices = {0, 0, 0, 1, 0, 0, 0, 1, 0};
        {
            std::ofstream file(TempDirectory / "triangle.bin", std::ios::binary);
            file.write(reinterpret_cast<const char*>(vertices.data()), sizeof(vertices));
        }
        auto document = nlohmann::json::parse(R"({
            "asset": {"version": "2.0"},
            "buffers": [{"uri": "triangle.bin", "byteLength": 36}],
            "bufferViews": [{"buffer": 0, "byteLength": 36}],
            "accessors": [{"bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3", "min": [0,0,0], "max": [1,1,0]}],
            "meshes": [{"primitives": [{"attributes": {"POSITION": 0}}]}],
            "nodes": [{"mesh": 0}], "scenes": [{"nodes": [0]}], "scene": 0
        })");
        if (material_count)
        {
            document["meshes"][0]["primitives"][0]["material"] = 0;
            for (size_t i = 0; i < material_count; ++i)
                document["materials"].push_back({
                    {"name", std::string(80, 'm') + std::to_string(i)}
                });
        }
        const auto filename = (TempDirectory / "triangle.gltf").string();
        std::ofstream(filename) << document.dump();
        return filename;
    }

    void ImportGltf(const std::string& filename)
    {
        Importer.ImportFile(filename.c_str(), Config, &Arena, &Callbacks, ImportCallbacks::Complete, ImportCallbacks::Advance, ImportCallbacks::Fail, nullptr);
    }

    std::string WriteTexturedGltf()
    {
        const auto     filename = WriteGltf(1);
        nlohmann::json document;
        {
            std::ifstream file(filename);
            file >> document;
        }
        document["images"] = {
            {{"name", "one_pixel"}, {"uri", "data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+a3ioAAAAASUVORK5CYII="}}
        };
        document["textures"]                                                          = {{{"source", 0}}};
        document["materials"][0]["pbrMetallicRoughness"]["baseColorTexture"]["index"] = 0;
        std::ofstream(filename) << document.dump();
        return filename;
    }

    void ChangeTriangleVertices()
    {
        const std::array<float, 9> vertices = {0.25f, 0, 0, 1, 0, 0, 0, 1, 0};
        std::ofstream              file(TempDirectory / "triangle.bin", std::ios::binary);
        file.write(reinterpret_cast<const char*>(vertices.data()), sizeof(vertices));
    }

    std::string WriteObj(bool with_normals = false)
    {
        const auto    filename = (TempDirectory / "triangle.obj").string();
        std::ofstream file(filename);
        file << "o Triangle\nv 0 0 0\nv 1 0 0\nv 0 1 0\n";
        file << (with_normals ? "vn 0 0 1\nf 1//1 2//1 3//1\n" : "f 1 2 3\n");
        return filename;
    }

    std::string WriteFbx(bool with_material = false)
    {
        const auto  filename = (TempDirectory / "triangle.fbx").string();
        std::string contents = R"(; FBX 7.4.0 project file
FBXHeaderExtension: { FBXHeaderVersion: 1003
    FBXVersion: 7400
}
Objects: {
    Geometry: 1, "Geometry::Triangle", "Mesh" {
        Vertices: *9 { a: 0,0,0,1,0,0,0,1,0 }
        PolygonVertexIndex: *3 { a: 0,1,-3 }
    }
    Model: 2, "Model::Triangle", "Mesh" { Version: 232 }
}
Connections: {
    C: "OO",1,2
    C: "OO",2,0
}
)";
        if (with_material)
        {
            contents.insert(contents.find("}\nConnections:"), R"(    Material: 3, "Material::TestMaterial", "" {
        Version: 102
        ShadingModel: "phong"
        Properties70: { P: "DiffuseColor", "Color", "", "A", 1, 0, 0 }
    }
)");
            contents.insert(contents.rfind('}'), "    C: \"OO\",3,2\n");
        }
        std::ofstream(filename) << contents;
        return filename;
    }

    void ReadCookedMesh(AssetMesh& mesh, AssetNodeHierarchy& hierarchy)
    {
        const auto bytes    = ReadFile("/meshes/triangle.zemesh");
        const auto filename = (TempDirectory / "triangle.zemesh").string();
        {
            std::ofstream file(filename, std::ios::binary);
            file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        }
        DeserializeMeshAssetFile(&Arena, filename.c_str(), mesh, hierarchy);
    }

    template <typename ModelImporter>
    void VerifyInvalidConfigurations(ModelImporter& importer, const std::string& filename)
    {
        const auto start  = importer.Arena.m_current_offset;
        auto       verify = [&](const ImportConfiguration& config, const char* source) {
            const auto failures = Callbacks.Failed;
            importer.ImportFile(source, config, &Arena, &Callbacks, ImportCallbacks::Complete, ImportCallbacks::Advance, ImportCallbacks::Fail, nullptr);
            EXPECT_EQ(Callbacks.Failed, failures + 1);
            EXPECT_EQ(Callbacks.Completed, 0);
            EXPECT_EQ(importer.Arena.m_current_offset, start);
            EXPECT_NE(Callbacks.Error.find("VFS error"), std::string::npos);
        };
        const std::string long_path = "/" + std::string(MAX_FILE_PATH_COUNT, 'x');
        auto              invalid   = Config;
        invalid.VFS                 = nullptr;
        verify(invalid, filename.c_str());
        invalid = Config;
        invalid.OutputAssetsPath.init(&Arena, long_path.c_str());
        verify(invalid, filename.c_str());
        invalid = Config;
        invalid.OutputAssetFile.init(&Arena, "");
        verify(invalid, filename.c_str());
        invalid.OutputAssetFile.init(&Arena, std::string(MAX_FILE_PATH_COUNT, 'x').c_str());
        verify(invalid, filename.c_str());
        invalid.OutputAssetFile.init(&Arena, std::string(MAX_FILE_PATH_COUNT - 1 - std::string_view("/meshes/").size(), 'x').c_str());
        verify(invalid, filename.c_str()); // Fits VFS, but not its .meta.tmp sidecar.
        invalid                         = Config;
        invalid.Options.ImportMaterials = true;
        invalid.OutputMaterialPath.init(&Arena, long_path.c_str());
        verify(invalid, filename.c_str());
        invalid                         = Config;
        invalid.Options.ImportMaterials = invalid.Options.ImportTextures = true;
        invalid.OutputTextureFilesPath.init(&Arena, long_path.c_str());
        verify(invalid, filename.c_str());
        invalid.OutputTextureFilesPath = Config.OutputTextureFilesPath;
        invalid.AssetName.init(&Arena, "");
        verify(invalid, filename.c_str());
        verify(Config, nullptr);
        verify(Config, "");
        importer.ImportFile(filename.c_str(), Config, &Arena, &Callbacks, ImportCallbacks::Complete, ImportCallbacks::Advance, ImportCallbacks::Fail, nullptr);
        EXPECT_EQ(Callbacks.Completed, 1) << Callbacks.Error;
    }

    template <typename ModelImporter>
    void VerifySameInstanceSerialization(ModelImporter& importer, const std::string& filename, bool runtime_call)
    {
        using namespace std::chrono_literals;
        std::promise<void>    release;
        PausedImportCallbacks paused;
        paused.Release                    = release.get_future().share();
        auto                entered       = paused.Entered.get_future();
        ImportConfiguration second_config = Config;
        second_config.OutputAssetFile.init(&Arena, "second.zemesh");

        auto       first        = std::async(std::launch::async, [&] { importer.ImportFile(filename.c_str(), Config, &Arena, &paused, PausedImportCallbacks::Complete, PausedImportCallbacks::Advance, PausedImportCallbacks::Fail, nullptr); });
        const bool first_paused = entered.wait_for(5s) == std::future_status::ready;
        EXPECT_TRUE(first_paused);
        if (!first_paused)
        {
            release.set_value();
            first.get();
            return;
        }

        std::promise<void> second_started;
        auto               started = second_started.get_future();
        auto               second  = std::async(std::launch::async, [&] {
            second_started.set_value();
            if (runtime_call)
            {
                // Even a failed runtime import must respect the editor import's lock.
                auto result = importer.Import(Context, Path("/zengine_nonexistent_import_source"), {});
                EXPECT_TRUE(result.Failed());
            }
            else
                importer.ImportFile(filename.c_str(), second_config, &Arena, &Callbacks, ImportCallbacks::Complete, ImportCallbacks::Advance, ImportCallbacks::Fail, nullptr);
        });
        EXPECT_EQ(started.wait_for(5s), std::future_status::ready);
        EXPECT_EQ(second.wait_for(100ms), std::future_status::timeout);
        release.set_value();
        first.get();
        second.get();
        EXPECT_EQ(paused.Results.Completed, 1) << paused.Results.Error;
        EXPECT_EQ(paused.Results.Failed, 0);
        if (!runtime_call)
        {
            EXPECT_EQ(Callbacks.Completed, 1) << Callbacks.Error;
            EXPECT_EQ(Callbacks.Failed, 0);
            ASSERT_EQ(Callbacks.Outputs.size(), 1u);
            EXPECT_EQ(Callbacks.Outputs[0].Path, "/meshes/second.zemesh");
        }
    }
};

TEST_F(ModelImporterOutputTest, MeshOnlyGltfOwnsShortAndEmptyOutputStringsAfterImport)
{
    Config.OutputAssetsPath.init(&Arena, "/m");
    Config.OutputAssetFile.init(&Arena, "x.zemesh");
    Config.OutputWorkingSpacePath.init(&Arena, "");
    // A source material must be harmless when material/texture import is disabled.
    ImportGltf(WriteGltf(1));

    EXPECT_EQ(Callbacks.Failed, 0) << Callbacks.Error;
    ASSERT_EQ(Callbacks.Completed, 1);
    ASSERT_EQ(Callbacks.Outputs.size(), 1u);
    EXPECT_EQ(Callbacks.Outputs[0].Type, AssetFileType::MESH);
    EXPECT_EQ(Callbacks.Outputs[0].Path, "/m/x.zemesh");
    EXPECT_TRUE(Callbacks.Outputs[0].RootPath.empty());
    EXPECT_TRUE(Exists("/m/x.zemesh"));
    EXPECT_FLOAT_EQ(Callbacks.Progress, 1.0f);
    EXPECT_EQ(Importer.Arena.m_current_offset, Importer.Arena.m_initial_current_offset);
}

TEST_F(ModelImporterOutputTest, RuntimeSourcePathResolvesGeneratedInput)
{
    const auto filename                    = WriteGltf();
    char       native[MAX_FILE_PATH_COUNT] = {};
    Path(filename.c_str()).ToNative(native, sizeof(native));
    EXPECT_TRUE(std::filesystem::equivalent(filename, native));
}

TEST_F(ModelImporterOutputTest, InvalidGltfNodeGraphsFailInEditorAndRuntimeWithoutPublication)
{
    for (int scenario = 0; scenario < 8; ++scenario)
    {
        SCOPED_TRACE(scenario);
        Callbacks               = {};
        const auto     filename = WriteGltf();
        nlohmann::json document;
        std::ifstream(filename) >> document;
        switch (scenario)
        {
            case 0:
                document["scene"] = 1000000;
                break;
            case 1:
                document["scenes"][0]["nodes"] = {1000000};
                break;
            case 2:
                document["nodes"][0]["mesh"] = 1000000;
                break;
            case 3:
                document["nodes"][0]["children"] = {1000000};
                break;
            case 4:
                document["nodes"][0]["children"] = {0};
                break;
            case 5:
                document["nodes"][0]["children"] = {1};
                document["nodes"].push_back({
                    {"children", {0}}
                });
                break;
            case 6:
                document["nodes"][0]["children"] = {2};
                document["nodes"].push_back({
                    {"children", {2}}
                });
                document["nodes"].push_back(nlohmann::json::object());
                document["scenes"][0]["nodes"] = {0, 1};
                break;
            case 7:
                document["scenes"][0]["nodes"] = {0, 0};
                break;
        }
        std::ofstream(filename) << document.dump();
        ImportGltf(filename);
        EXPECT_EQ(Callbacks.Completed, 0);
        EXPECT_EQ(Callbacks.Failed, 1);
        EXPECT_FALSE(Exists("/meshes/triangle.zemesh"));
        EXPECT_EQ(Importer.Import(Context, Path(filename.c_str()), {}).Error(), VFSError::Corrupted);
        EXPECT_EQ(Importer.Arena.m_current_offset, Importer.Arena.m_initial_current_offset);
    }
}

TEST_F(ModelImporterOutputTest, DeepGltfHierarchyUsesIterativeTraversalAndSizedMaps)
{
    const auto     filename = WriteGltf();
    nlohmann::json document;
    std::ifstream(filename) >> document;
    document["nodes"]      = nlohmann::json::array();
    constexpr size_t count = 4096;
    for (size_t i = 0; i + 1 < count; ++i)
        document["nodes"].push_back({
            {"children", {i + 1}}
        });
    document["nodes"].push_back({
        {"mesh", 0}
    });
    std::ofstream(filename) << document.dump();
    ImportGltf(filename);
    ASSERT_EQ(Callbacks.Completed, 1) << Callbacks.Error;
    ASSERT_EQ(Callbacks.Failed, 0);
    AssetMesh          mesh;
    AssetNodeHierarchy hierarchy;
    ReadCookedMesh(mesh, hierarchy);
    ASSERT_EQ(hierarchy.Hierarchies.size(), count + 1);
    EXPECT_EQ(hierarchy.NodeNames.size(), count + 1);
    EXPECT_EQ(hierarchy.NodeMeshes.size(), 1u);
    EXPECT_EQ(hierarchy.Hierarchies.back().DepthLevel, count);
}

TEST_F(ModelImporterOutputTest, MultipleGltfScenesCanReuseTheSameTree)
{
    const auto     filename = WriteGltf();
    nlohmann::json document;
    std::ifstream(filename) >> document;
    document.erase("scene");
    document["scenes"].push_back(document["scenes"][0]);
    std::ofstream(filename) << document.dump();
    ImportGltf(filename);
    ASSERT_EQ(Callbacks.Completed, 1) << Callbacks.Error;
    AssetMesh          mesh;
    AssetNodeHierarchy hierarchy;
    ReadCookedMesh(mesh, hierarchy);
    EXPECT_EQ(hierarchy.Hierarchies.size(), 4u);
    EXPECT_EQ(hierarchy.NodeMeshes.size(), 2u);
}

TEST_F(ModelImporterOutputTest, OversizedValidGltfFailsCleanlyAndImporterRemainsReusable)
{
    const auto     filename = WriteGltf();
    nlohmann::json document;
    std::ifstream(filename) >> document;
    constexpr size_t         count = 1050000;
    const std::vector<float> vertices(count * 3, 0.0f);
    {
        std::ofstream binary(TempDirectory / "triangle.bin", std::ios::binary);
        binary.write(reinterpret_cast<const char*>(vertices.data()), vertices.size() * sizeof(float));
    }
    document["buffers"][0]["byteLength"]     = vertices.size() * sizeof(float);
    document["bufferViews"][0]["byteLength"] = vertices.size() * sizeof(float);
    document["accessors"][0]["count"]        = count;
    std::ofstream(filename) << document.dump();
    ImportGltf(filename);
    EXPECT_EQ(Callbacks.Completed, 0);
    EXPECT_EQ(Callbacks.Failed, 1);
    EXPECT_NE(Callbacks.Error.find("scratch memory budget"), std::string::npos);
    EXPECT_FALSE(Exists("/meshes/triangle.zemesh"));
    EXPECT_EQ(Importer.Import(Context, Path(filename.c_str()), {}).Error(), VFSError::OutOfMemory);
    EXPECT_EQ(Importer.Arena.m_current_offset, Importer.Arena.m_initial_current_offset);
    ImportGltf(WriteGltf());
    EXPECT_EQ(Callbacks.Completed, 1) << Callbacks.Error;
}

TEST_F(ModelImporterOutputTest, DuplicateGltfImageNamesAreDistinctButSharedImagesKeepIdentity)
{
    Config.Options.ImportMaterials = Config.Options.ImportTextures = true;
    const auto                   filename                          = WriteTexturedGltf();
    const std::array<uint8_t, 4> red                               = {255, 0, 0, 255};
    const std::array<uint8_t, 4> green                             = {0, 255, 0, 255};
    ASSERT_TRUE(stbi_write_png((TempDirectory / "red.png").string().c_str(), 1, 1, 4, red.data(), 4));
    ASSERT_TRUE(stbi_write_png((TempDirectory / "green.png").string().c_str(), 1, 1, 4, green.data(), 4));
    nlohmann::json document;
    std::ifstream(filename) >> document;
    document["images"][0]["uri"] = "red.png";
    document["images"].push_back(document["images"][0]);
    document["images"][1]["uri"] = "green.png";
    document["textures"].push_back({
        {"source", 1}
    });
    document["textures"].push_back({
        {"source", 0}
    });
    for (int i = 1; i < 3; ++i)
    {
        auto material                                                 = document["materials"][0];
        material["name"]                                              = std::to_string(i);
        material["pbrMetallicRoughness"]["baseColorTexture"]["index"] = i;
        document["materials"].push_back(material);
    }
    std::ofstream(filename) << document.dump();
    auto textures = [&] {
        std::vector<nlohmann::json> result;
        for (size_t i = 1; i < Callbacks.Outputs.size(); ++i)
        {
            const auto bytes = ReadFile(Callbacks.Outputs[i].Path.c_str());
            result.push_back(nlohmann::json::parse(bytes.begin(), bytes.end())["textures"]["albedo"]);
        }
        return result;
    };
    ImportGltf(filename);
    ASSERT_EQ(Callbacks.Completed, 1) << Callbacks.Error;
    const auto first = textures();
    ASSERT_EQ(first.size(), 3u);
    EXPECT_NE(first[0]["path"], first[1]["path"]);
    EXPECT_NE(first[0]["uuid"], first[1]["uuid"]);
    EXPECT_EQ(first[0], first[2]);
    EXPECT_NE(ReadFile("/textures/triangle/one_pixel_0.png"), ReadFile("/textures/triangle/one_pixel_1.png"));
    ImportGltf(filename);
    EXPECT_EQ(Callbacks.Completed, 2) << Callbacks.Error;
    EXPECT_EQ(textures(), first);
}

TEST_F(ModelImporterOutputTest, NativeTextureCopiesWithSameBasenameKeepDifferentContents)
{
    ASSERT_TRUE(std::filesystem::create_directory(TempDirectory / "red"));
    ASSERT_TRUE(std::filesystem::create_directory(TempDirectory / "green"));
    std::ofstream(TempDirectory / "red/same.png", std::ios::binary) << "red image";
    std::ofstream(TempDirectory / "green/same.png", std::ios::binary) << "green image";
    ZEngine::Core::Containers::Array<AssetTexture> textures;
    textures.init(&Arena, 2, 2);
    textures[0].Path.init(&Arena, "red/same.png");
    textures[1].Path.init(&Arena, "green/same.png");
    ASSERT_TRUE(CopyTextureFiles(ArrayView{textures}, Config).Succeeded());
    EXPECT_STREQ(textures[0].Path.c_str(), "/textures/triangle/same_0.png");
    EXPECT_STREQ(textures[1].Path.c_str(), "/textures/triangle/same_1.png");
    EXPECT_NE(ReadFile(textures[0].Path.c_str()), ReadFile(textures[1].Path.c_str()));
}

TEST_F(ModelImporterOutputTest, UnnamedSharedGltfImageUsesOneStableDestination)
{
    Config.Options.ImportMaterials = Config.Options.ImportTextures = true;
    const auto     filename                                        = WriteTexturedGltf();
    nlohmann::json document;
    std::ifstream(filename) >> document;
    document["images"][0].erase("name");
    document["textures"].push_back({
        {"source", 0}
    });
    document["materials"][0]["normalTexture"] = {
        {"index", 1}
    };
    std::ofstream(filename) << document.dump();
    ImportGltf(filename);
    ASSERT_EQ(Callbacks.Completed, 1) << Callbacks.Error;
    ASSERT_EQ(Callbacks.Failed, 0);
    const auto bytes    = ReadFile(Callbacks.Outputs[1].Path.c_str());
    const auto material = nlohmann::json::parse(bytes.begin(), bytes.end());
    EXPECT_EQ(material["textures"]["albedo"]["path"], "/textures/triangle/image_0.png");
    EXPECT_EQ(material["textures"]["albedo"], material["textures"]["normal"]);
}

TEST_F(ModelImporterOutputTest, CorruptTextureIdentityIsRepairedBeforeMaterialPublication)
{
    Config.Options.ImportMaterials = Config.Options.ImportTextures = true;
    const auto filename                                            = WriteTexturedGltf();
    ASSERT_TRUE(Context.CreateDir(Path("/textures/triangle")).Succeeded());
    const std::string metadata = R"({"uuid":"invalid","source_hash":0})";
    ASSERT_TRUE(WriteFileAtomically(Context, Path("/textures/triangle/one_pixel_0.png.meta"), {reinterpret_cast<const uint8_t*>(metadata.data()), metadata.size()}).Succeeded());
    ImportGltf(filename);
    ASSERT_EQ(Callbacks.Completed, 1) << Callbacks.Error;
    ASSERT_EQ(Callbacks.Failed, 0);
    const auto bytes    = ReadFile(Callbacks.Outputs[1].Path.c_str());
    const auto material = nlohmann::json::parse(bytes.begin(), bytes.end());
    auto       meta     = MetaFileIO::Read(Context, Path("/textures/triangle/one_pixel_0.png"));
    ASSERT_TRUE(meta.Succeeded());
    EXPECT_FALSE(meta.Value().AssetUUID.is_nil());
    EXPECT_EQ(material["textures"]["albedo"]["uuid"], uuids::to_string(meta.Value().AssetUUID));
}

TEST_F(ModelImporterOutputTest, ModelMetadataReadErrorsNeverPublishReplacementIdentities)
{
    auto verify = [&](auto& importer, const std::string& filename) {
        for (bool material : {false, true})
        {
            Config.Options.ImportMaterials = material;
            Context.MetadataReadPath       = material ? "/materials/" : "/meshes/";
            for (auto error : {VFSError::PermissionDenied, VFSError::IOError, VFSError::OutOfMemory})
            {
                SCOPED_TRACE(static_cast<int>(error));
                Context.MetadataReadError = error;
                Callbacks                 = {};
                importer.ImportFile(filename.c_str(), Config, &Arena, &Callbacks, ImportCallbacks::Complete, ImportCallbacks::Advance, ImportCallbacks::Fail, nullptr);
                EXPECT_EQ(Callbacks.Failed, 1) << Callbacks.Error;
                EXPECT_EQ(Callbacks.Completed, 0);
                EXPECT_FALSE(Exists("/meshes/triangle.zemesh"));
            }
        }
        Context.MetadataReadError = VFSError::OK;
    };
    verify(Importer, WriteGltf(1));
    AssimpImporter assimp;
    assimp.Initialize(&Arena);
    verify(assimp, WriteObj());
    FbxImporter fbx;
    fbx.Initialize(&Arena);
    verify(fbx, WriteFbx(true));
}

TEST_F(ModelImporterOutputTest, MaterialOutputsExceedOldCapacityAndOwnLongPaths)
{
    const std::string root = "/project/" + std::string(100, 'p');
    Config.OutputWorkingSpacePath.init(&Arena, root.c_str());
    Config.Options.ImportMaterials = true;
    ImportGltf(WriteGltf(20));

    EXPECT_EQ(Callbacks.Failed, 0) << Callbacks.Error;
    ASSERT_EQ(Callbacks.Completed, 1);
    ASSERT_EQ(Callbacks.Outputs.size(), 21u);
    for (size_t i = 0; i < 20; ++i)
    {
        const auto& output = Callbacks.Outputs[i + 1];
        EXPECT_EQ(output.Type, AssetFileType::MATERIAL);
        EXPECT_EQ(output.Path, "/materials/triangle/" + std::string(80, 'm') + std::to_string(i) + "_" + std::to_string(i) + ".zematerial");
        EXPECT_EQ(output.RootPath, root);
        EXPECT_TRUE(Exists(output.Path.c_str()));
    }
}

TEST_F(ModelImporterOutputTest, MeshOnlyGltfArtifactRoundTrips)
{
    ImportGltf(WriteGltf());
    ASSERT_EQ(Callbacks.Completed, 1) << Callbacks.Error;
    AssetMesh          mesh;
    AssetNodeHierarchy hierarchy;
    ReadCookedMesh(mesh, hierarchy);
    ASSERT_EQ(mesh.Vertices.size(), 24u);
    ASSERT_EQ(mesh.Indices.size(), 3u);
    ASSERT_EQ(mesh.SubMeshes.size(), 1u);
    EXPECT_EQ(mesh.MeshUUID, hierarchy.MeshUUID);
    EXPECT_EQ(mesh.SubMeshes[0].VertexCount, 3u);
    EXPECT_EQ(mesh.SubMeshes[0].IndexCount, 3u);
    EXPECT_TRUE(hierarchy.MaterialNames.empty());
}

TEST_F(ModelImporterOutputTest, MaterialWriteFailureNeverReportsSuccess)
{
    Config.Options.ImportMaterials = true;
    Context.Point                  = FailurePoint::Write;
    Context.FailurePath            = "/materials/";
    ImportGltf(WriteGltf(2));

    EXPECT_EQ(Callbacks.Failed, 1);
    EXPECT_EQ(Callbacks.Completed, 0);
    EXPECT_FALSE(Exists("/meshes/triangle.zemesh"));
    EXPECT_TRUE(Callbacks.Outputs.empty());
    EXPECT_LT(Callbacks.Progress, 1.0f);
    EXPECT_NE(Callbacks.Error.find("/materials/triangle/" + std::string(80, 'm') + "0_0.zematerial"), std::string::npos);
    EXPECT_NE(Callbacks.Error.find("VFS error 8"), std::string::npos);
    ExpectNoTemporaryFiles("/materials");
}

TEST_F(ModelImporterOutputTest, AssimpWriteFailureReleasesScratchAndAllowsRetry)
{
    const auto filename            = WriteObj(true);
    Config.Options.ImportMaterials = true;
    AssimpImporter importer;
    importer.Initialize(&Arena);
    Context.Point       = FailurePoint::Write;
    Context.FailurePath = "/meshes/";
    importer.ImportFile(filename.c_str(), Config, &Arena, &Callbacks, ImportCallbacks::Complete, ImportCallbacks::Advance, ImportCallbacks::Fail, nullptr);

    EXPECT_EQ(Callbacks.Failed, 1);
    EXPECT_EQ(Callbacks.Completed, 0);
    EXPECT_NE(Callbacks.Error.find("/meshes/triangle.zemesh"), std::string::npos);
    EXPECT_EQ(importer.Arena.m_current_offset, importer.Arena.m_initial_current_offset);
    // The same importer must still work after failure.
    Context.Point = FailurePoint::None;
    importer.ImportFile(filename.c_str(), Config, &Arena, &Callbacks, ImportCallbacks::Complete, ImportCallbacks::Advance, ImportCallbacks::Fail, nullptr);
    EXPECT_EQ(Callbacks.Completed, 1);
}

TEST_F(ModelImporterOutputTest, FbxWriteFailureReleasesScratchAndAllowsRetry)
{
    const auto  filename = WriteFbx();
    FbxImporter importer;
    importer.Initialize(&Arena);
    Context.Point = FailurePoint::Write;
    importer.ImportFile(filename.c_str(), Config, &Arena, &Callbacks, ImportCallbacks::Complete, ImportCallbacks::Advance, ImportCallbacks::Fail, nullptr);

    ASSERT_EQ(Callbacks.Failed, 1);
    EXPECT_EQ(Callbacks.Completed, 0);
    EXPECT_NE(Callbacks.Error.find("/meshes/triangle.zemesh"), std::string::npos) << Callbacks.Error;
    Context.Point = FailurePoint::None;
    importer.ImportFile(filename.c_str(), Config, &Arena, &Callbacks, ImportCallbacks::Complete, ImportCallbacks::Advance, ImportCallbacks::Fail, nullptr);
    EXPECT_EQ(Callbacks.Failed, 1);
    ASSERT_EQ(Callbacks.Completed, 1) << Callbacks.Error;
    ASSERT_EQ(Callbacks.Outputs.size(), 1u);
    EXPECT_EQ(Callbacks.Outputs[0].Path, "/meshes/triangle.zemesh");
}

TEST_F(ModelImporterOutputTest, AssimpWithoutNormalsOrMaterialsUsesFallbacks)
{
    const auto     filename = WriteObj();
    AssimpImporter importer;
    importer.Initialize(&Arena);
    importer.ImportFile(filename.c_str(), Config, &Arena, &Callbacks, ImportCallbacks::Complete, ImportCallbacks::Advance, ImportCallbacks::Fail, nullptr);
    EXPECT_EQ(Callbacks.Failed, 0) << Callbacks.Error;
    ASSERT_EQ(Callbacks.Completed, 1);
    ASSERT_EQ(Callbacks.Outputs.size(), 1u);

    AssetMesh          mesh;
    AssetNodeHierarchy hierarchy;
    ReadCookedMesh(mesh, hierarchy);
    ASSERT_EQ(mesh.Vertices.size(), 24u);
    ASSERT_EQ(mesh.SubMeshes.size(), 1u);
    EXPECT_TRUE(mesh.SubMeshes[0].MaterialUUID.is_nil());
    EXPECT_TRUE(hierarchy.MaterialNames.empty());
    EXPECT_TRUE(hierarchy.NodeMaterials.empty());
    for (size_t vertex = 0; vertex < 3; ++vertex)
    {
        EXPECT_FLOAT_EQ(mesh.Vertices[vertex * 8 + 3], 0);
        EXPECT_FLOAT_EQ(mesh.Vertices[vertex * 8 + 4], 1);
        EXPECT_FLOAT_EQ(mesh.Vertices[vertex * 8 + 5], 0);
    }
}

TEST_F(ModelImporterOutputTest, AssimpPreservesProvidedNormalsAndMaterialIdentity)
{
    Config.Options.ImportMaterials = true;
    const auto     filename        = WriteObj(true);
    AssimpImporter importer;
    importer.Initialize(&Arena);
    importer.ImportFile(filename.c_str(), Config, &Arena, &Callbacks, ImportCallbacks::Complete, ImportCallbacks::Advance, ImportCallbacks::Fail, nullptr);
    EXPECT_EQ(Callbacks.Failed, 0) << Callbacks.Error;
    ASSERT_EQ(Callbacks.Completed, 1);
    ASSERT_EQ(Callbacks.Outputs.size(), 2u);
    AssetMesh          mesh;
    AssetNodeHierarchy hierarchy;
    ReadCookedMesh(mesh, hierarchy);
    ASSERT_EQ(mesh.SubMeshes.size(), 1u);
    EXPECT_FALSE(mesh.SubMeshes[0].MaterialUUID.is_nil());
    EXPECT_FALSE(hierarchy.NodeMaterials.empty());
    EXPECT_EQ(hierarchy.MaterialNames.size(), 1u);
    for (size_t vertex = 0; vertex < 3; ++vertex)
    {
        EXPECT_FLOAT_EQ(mesh.Vertices[vertex * 8 + 3], 0);
        EXPECT_FLOAT_EQ(mesh.Vertices[vertex * 8 + 4], 0);
        EXPECT_FLOAT_EQ(mesh.Vertices[vertex * 8 + 5], 1);
    }
}

TEST_F(ModelImporterOutputTest, GltfEditorImportsShareOneScratchLock)
{
    VerifySameInstanceSerialization(Importer, WriteGltf(), false);
}

TEST_F(ModelImporterOutputTest, GltfRuntimeAndEditorImportsShareOneScratchLock)
{
    VerifySameInstanceSerialization(Importer, WriteGltf(), true);
}

TEST_F(ModelImporterOutputTest, AssimpEditorImportsShareOneScratchLock)
{
    AssimpImporter importer;
    importer.Initialize(&Arena);
    VerifySameInstanceSerialization(importer, WriteObj(), false);
}

TEST_F(ModelImporterOutputTest, AssimpRuntimeAndEditorImportsShareOneScratchLock)
{
    AssimpImporter importer;
    importer.Initialize(&Arena);
    VerifySameInstanceSerialization(importer, WriteObj(), true);
}

TEST_F(ModelImporterOutputTest, FbxEditorImportsShareOneScratchLock)
{
    FbxImporter importer;
    importer.Initialize(&Arena);
    VerifySameInstanceSerialization(importer, WriteFbx(), false);
}

TEST_F(ModelImporterOutputTest, FbxRuntimeAndEditorImportsShareOneScratchLock)
{
    FbxImporter importer;
    importer.Initialize(&Arena);
    VerifySameInstanceSerialization(importer, WriteFbx(), true);
}

TEST_F(ModelImporterOutputTest, SeparateImporterInstancesRemainIndependent)
{
    using namespace std::chrono_literals;
    const auto   filename = WriteGltf();
    GltfImporter other;
    other.Initialize(&Arena);
    std::promise<void>    release;
    PausedImportCallbacks paused;
    paused.Release = release.get_future().share();
    auto entered   = paused.Entered.get_future();
    auto first     = std::async(std::launch::async, [&] { Importer.ImportFile(filename.c_str(), Config, &Arena, &paused, PausedImportCallbacks::Complete, PausedImportCallbacks::Advance, PausedImportCallbacks::Fail, nullptr); });
    EXPECT_EQ(entered.wait_for(5s), std::future_status::ready);
    auto second = std::async(std::launch::async, [&] { other.ImportFile(filename.c_str(), Config, &Arena, &Callbacks, ImportCallbacks::Complete, ImportCallbacks::Advance, ImportCallbacks::Fail, nullptr); });
    EXPECT_EQ(second.wait_for(5s), std::future_status::ready);
    release.set_value();
    first.get();
    second.get();
    EXPECT_EQ(paused.Results.Completed, 1) << paused.Results.Error;
    EXPECT_EQ(Callbacks.Completed, 1) << Callbacks.Error;
}

TEST_F(ModelImporterOutputTest, CodecRejectsInvalidOutputInsteadOfReturningAnEmptyAsset)
{
    AssetMesh          mesh;
    AssetNodeHierarchy hierarchy;
    Config.OutputAssetFile.init(&Arena, "");
    EXPECT_EQ(SerializeMeshAssetFile(&Arena, mesh, hierarchy, Config).Error(), VFSError::InvalidPath);
    Config.OutputAssetFile.init(&Arena, "triangle.zemesh");
    Config.OutputAssetsPath.init(&Arena, ("/" + std::string(MAX_FILE_PATH_COUNT, 'x')).c_str());
    EXPECT_EQ(SerializeMeshAssetFile(&Arena, mesh, hierarchy, Config).Error(), VFSError::InvalidPath);
    Config.VFS = nullptr;
    EXPECT_EQ(SerializeMeshAssetFile(&Arena, mesh, hierarchy, Config).Error(), VFSError::Unsupported);
}

TEST_F(ModelImporterOutputTest, GltfInvalidConfigurationsReportErrorsAndAllowRetry)
{
    VerifyInvalidConfigurations(Importer, WriteGltf());
}

TEST_F(ModelImporterOutputTest, AssimpInvalidConfigurationsReportErrorsAndAllowRetry)
{
    AssimpImporter importer;
    importer.Initialize(&Arena);
    VerifyInvalidConfigurations(importer, WriteObj());
}

TEST_F(ModelImporterOutputTest, FbxInvalidConfigurationsReportErrorsAndAllowRetry)
{
    FbxImporter importer;
    importer.Initialize(&Arena);
    VerifyInvalidConfigurations(importer, WriteFbx());
}

TEST_F(ModelImporterOutputTest, InvalidMaterialNameReportsErrorBeforePublishingMesh)
{
    Config.Options.ImportMaterials = true;
    const auto     filename        = WriteGltf(1);
    nlohmann::json document;
    {
        std::ifstream file(filename);
        file >> document;
    }
    document["materials"][0]["name"] = std::string(MAX_FILE_PATH_COUNT, 'm');
    std::ofstream(filename) << document.dump();
    ImportGltf(filename);
    EXPECT_EQ(Callbacks.Failed, 1);
    EXPECT_EQ(Callbacks.Completed, 0);
    EXPECT_FALSE(Exists("/meshes/triangle.zemesh"));
    EXPECT_EQ(Importer.Arena.m_current_offset, Importer.Arena.m_initial_current_offset);
}

TEST_F(ModelImporterOutputTest, FailedMaterialRecookPreservesPreviouslyPublishedMesh)
{
    Config.Options.ImportMaterials = true;
    const auto filename            = WriteGltf(2);
    ImportGltf(filename);
    ASSERT_EQ(Callbacks.Completed, 1);
    const auto before = ReadFile("/meshes/triangle.zemesh");
    ChangeTriangleVertices();
    // Fail the second material, after the first dependency was published.
    Context.FailurePath = std::string(80, 'm') + "1_1.zematerial";
    // Unique temporary names no longer contain the destination filename;
    // fail its final rename instead, preserving the old material as well.
    Context.Point       = FailurePoint::Rename;
    ImportGltf(filename);
    EXPECT_EQ(Callbacks.Failed, 1);
    EXPECT_EQ(Callbacks.Completed, 1);
    EXPECT_EQ(ReadFile("/meshes/triangle.zemesh"), before);
    ExpectNoTemporaryFiles("/materials");
    Context.Point = FailurePoint::None;
    ImportGltf(filename);
    EXPECT_EQ(Callbacks.Completed, 2);
    EXPECT_NE(ReadFile("/meshes/triangle.zemesh"), before);
}

TEST_F(ModelImporterOutputTest, AssimpMaterialWriteFailurePreservesPreviouslyPublishedMesh)
{
    Config.Options.ImportMaterials = true;
    const auto     filename        = WriteObj();
    AssimpImporter importer;
    importer.Initialize(&Arena);
    importer.ImportFile(filename.c_str(), Config, &Arena, &Callbacks, ImportCallbacks::Complete, ImportCallbacks::Advance, ImportCallbacks::Fail, nullptr);
    ASSERT_EQ(Callbacks.Completed, 1);
    const auto before = ReadFile("/meshes/triangle.zemesh");
    std::ofstream(filename) << "o Triangle\nv 0.25 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n";
    Context.Point       = FailurePoint::Write;
    Context.FailurePath = "/materials/";
    importer.ImportFile(filename.c_str(), Config, &Arena, &Callbacks, ImportCallbacks::Complete, ImportCallbacks::Advance, ImportCallbacks::Fail, nullptr);
    EXPECT_EQ(Callbacks.Failed, 1);
    EXPECT_EQ(Callbacks.Completed, 1);
    EXPECT_EQ(ReadFile("/meshes/triangle.zemesh"), before);
    ExpectNoTemporaryFiles("/materials");
}

TEST_F(ModelImporterOutputTest, FbxMaterialWriteFailurePreservesPreviouslyPublishedMesh)
{
    Config.Options.ImportMaterials = true;
    const auto  filename           = WriteFbx(true);
    FbxImporter importer;
    importer.Initialize(&Arena);
    importer.ImportFile(filename.c_str(), Config, &Arena, &Callbacks, ImportCallbacks::Complete, ImportCallbacks::Advance, ImportCallbacks::Fail, nullptr);
    ASSERT_EQ(Callbacks.Completed, 1) << Callbacks.Error;
    ASSERT_EQ(Callbacks.Outputs.size(), 2u);
    const auto  before = ReadFile("/meshes/triangle.zemesh");
    std::string contents;
    {
        std::ifstream file(filename);
        contents.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    }
    contents.replace(contents.find("0,0,0,1,0,0"), std::string_view("0,0,0,1,0,0").size(), "0.25,0,0,1,0,0");
    std::ofstream(filename) << contents;
    Context.Point       = FailurePoint::Write;
    Context.FailurePath = "/materials/";
    importer.ImportFile(filename.c_str(), Config, &Arena, &Callbacks, ImportCallbacks::Complete, ImportCallbacks::Advance, ImportCallbacks::Fail, nullptr);
    EXPECT_EQ(Callbacks.Failed, 1);
    EXPECT_EQ(Callbacks.Completed, 1);
    EXPECT_EQ(ReadFile("/meshes/triangle.zemesh"), before);
    ExpectNoTemporaryFiles("/materials");
}

TEST_F(ModelImporterOutputTest, IndependentAtomicWritersUseDifferentTemporarySiblings)
{
    struct InterleavedContext : VFSContext
    {
        bool                 Interleaved     = false;
        bool                 NestedSucceeded = false;
        std::vector<VFSPath> TemporaryPaths;
        VFSResult<IVFSFile*> Open(const VFSPath& path, VFSOpenFlags flags) override
        {
            if (HasFlag(flags, VFSOpenFlags::Write))
                TemporaryPaths.push_back(path);
            return VFSContext::Open(path, flags);
        }
        VFSResult<void> Rename(const VFSPath& source, const VFSPath& destination) override
        {
            if (!Interleaved)
            {
                Interleaved                        = true;
                const std::array<uint8_t, 2> other = {9, 9};
                NestedSucceeded                    = WriteFileAtomically(*this, destination, {other.data(), other.size()}).Succeeded();
            }
            return VFSContext::Rename(source, destination);
        }
    } interleaved;
    interleaved.Initialize(&Arena, 4);
    ASSERT_TRUE(interleaved.Mount(&Backend, VFSPath::Root(), 0).Succeeded());
    ASSERT_TRUE(interleaved.CreateDir(Path("/meshes")).Succeeded());
    const std::array<uint8_t, 4> bytes = {1, 2, 3, 4};
    EXPECT_TRUE(WriteFileAtomically(interleaved, Path("/meshes/collision.zemesh"), {bytes.data(), bytes.size()}).Succeeded());
    EXPECT_TRUE(interleaved.NestedSucceeded);
    ASSERT_EQ(interleaved.TemporaryPaths.size(), 2u);
    EXPECT_NE(interleaved.TemporaryPaths[0], interleaved.TemporaryPaths[1]);
    EXPECT_EQ(ReadFile("/meshes/collision.zemesh"), (std::vector<uint8_t>{1, 2, 3, 4}));
    ExpectNoTemporaryFiles("/meshes");
    interleaved.Shutdown();
}

TEST_F(ModelImporterOutputTest, AtomicWriteSupportsMaximumLengthDestinationAndEmptyData)
{
    const std::string filename(MAX_FILE_PATH_COUNT - 1 - std::string_view("/meshes/").size(), 'x');
    auto              path = VFSPath::Parse(("/meshes/" + filename).c_str());
    ASSERT_TRUE(path.Succeeded());
    EXPECT_EQ(MakeOutputPath("/meshes", filename.c_str()).Error(), VFSError::InvalidPath);
    ASSERT_TRUE(Context.CreateDir(Path("/meshes")).Succeeded());
    EXPECT_TRUE(WriteFileAtomically(Context, path.Value(), {}).Succeeded());
    EXPECT_TRUE(ReadFile(path.Value().CStr()).empty());
    EXPECT_EQ(WriteFileAtomically(Context, VFSPath::Root(), {}).Error(), VFSError::InvalidPath);
    EXPECT_EQ(WriteFileAtomically(Context, VFSPath{}, {}).Error(), VFSError::InvalidPath);
    ExpectNoTemporaryFiles("/meshes");
}

TEST_F(ModelImporterOutputTest, CookedPathsReserveRoomForMetadataSidecars)
{
    const std::string filename(MAX_FILE_PATH_COUNT - 1 - std::string_view("/meshes/").size() - std::string_view(".meta").size(), 'x');
    EXPECT_TRUE(MakeOutputPath("/meshes", filename.c_str()).Succeeded());
    EXPECT_EQ(MakeOutputPath("/meshes", (filename + "x").c_str()).Error(), VFSError::InvalidPath);
    EXPECT_EQ(MakeOutputPath("/meshes", "../outside.zemesh").Error(), VFSError::InvalidPath);
}

TEST_F(ModelImporterOutputTest, CodecSupportsUninitializedOptionalRootAndMaterialName)
{
    Config.OutputWorkingSpacePath = {};
    AssetMesh          mesh;
    AssetNodeHierarchy hierarchy;
    auto               mesh_result = SerializeMeshAssetFile(&Arena, mesh, hierarchy, Config);
    ASSERT_TRUE(mesh_result.Succeeded());
    EXPECT_TRUE(mesh_result.Value().RootPath.empty());
    AssetMaterial material;
    auto          material_result = SerializeMaterialAssetFile(&Arena, material, Config, 0);
    ASSERT_TRUE(material_result.Succeeded());
    EXPECT_EQ(material_result.Value().Path, "/materials/triangle/material_0.zematerial");
    EXPECT_TRUE(material_result.Value().RootPath.empty());
    Config.AssetName = {};
    EXPECT_EQ(SerializeTextureAssetFiles(&Arena, {}, Config).Error(), VFSError::InvalidPath);
}

TEST_F(ModelImporterOutputTest, SkippedPrimitivesDoNotShiftMaterialsOrHierarchySubmeshIndices)
{
    Config.Options.ImportMaterials = true;
    const auto     filename        = WriteGltf(2);
    nlohmann::json document;
    {
        std::ifstream file(filename);
        file >> document;
    }
    auto triangle                  = document["meshes"][0]["primitives"][0];
    triangle["material"]           = 1;
    auto points                    = triangle;
    points["mode"]                 = 0;
    points["material"]             = 0;
    // Includes a mesh with no surviving primitives, plus a multi-primitive mesh.
    document["meshes"]             = {{{"primitives", {points}}}, {{"primitives", {points, triangle, triangle}}}, {{"primitives", {triangle}}}};
    document["nodes"]              = {{{"mesh", 0}}, {{"mesh", 1}}, {{"mesh", 2}}};
    document["scenes"][0]["nodes"] = {0, 1, 2};
    std::ofstream(filename) << document.dump();
    ImportGltf(filename);
    ASSERT_EQ(Callbacks.Completed, 1) << Callbacks.Error;
    ASSERT_EQ(Callbacks.Outputs.size(), 3u);
    const auto         material = nlohmann::json::parse(ReadFile(Callbacks.Outputs[2].Path.c_str()));
    AssetMesh          mesh;
    AssetNodeHierarchy hierarchy;
    ReadCookedMesh(mesh, hierarchy);
    ASSERT_EQ(mesh.SubMeshes.size(), 3u);
    for (const auto& submesh : mesh.SubMeshes)
        EXPECT_EQ(uuids::to_string(submesh.MaterialUUID), material["uuid"].get<std::string>());
    ASSERT_EQ(hierarchy.NodeMeshes.size(), 3u);
    std::array<bool, 3> mapped = {};
    for (const auto& [node, index] : hierarchy.NodeMeshes)
    {
        ASSERT_LT(index, mapped.size());
        mapped[index] = true;
    }
    EXPECT_TRUE(mapped[0] && mapped[1] && mapped[2]);
}

TEST_F(ModelImporterOutputTest, DisabledGltfMaterialsLeaveNoDanglingHierarchyMaterialIndices)
{
    ImportGltf(WriteGltf(1));
    ASSERT_EQ(Callbacks.Completed, 1);
    AssetMesh          mesh;
    AssetNodeHierarchy hierarchy;
    ReadCookedMesh(mesh, hierarchy);
    EXPECT_TRUE(hierarchy.NodeMaterials.empty());
    EXPECT_TRUE(hierarchy.MaterialNames.empty());
    EXPECT_TRUE(mesh.SubMeshes[0].MaterialUUID.is_nil());
}

TEST_F(ModelImporterOutputTest, SharedTextureCopyReportsMissingSourcesAndWriteErrors)
{
    ZEngine::Core::Containers::Array<AssetTexture> textures;
    textures.init(&Arena, 1, 1);
    textures[0].Path.init(&Arena, "missing.png");
    EXPECT_EQ(CopyTextureFiles(ArrayView{textures}, Config).Error(), VFSError::NotFound);
    std::ofstream(TempDirectory / "present.png", std::ios::binary) << "test image bytes";
    textures[0].Path.init(&Arena, "present.png");
    Context.Point       = FailurePoint::Write;
    Context.FailurePath = "/textures/";
    EXPECT_EQ(CopyTextureFiles(ArrayView{textures}, Config).Error(), VFSError::IOError);
    EXPECT_STREQ(textures[0].Path.c_str(), "present.png");
    ExpectNoTemporaryFiles("/textures/triangle");
    Context.Point = FailurePoint::None;
    EXPECT_TRUE(CopyTextureFiles(ArrayView{textures}, Config).Succeeded());
    EXPECT_STREQ(textures[0].Path.c_str(), "/textures/triangle/present_0.png");
}

TEST_F(ModelImporterOutputTest, SharedGltfTextureUpdatesEveryMaterialSlotAndSurvivesRecook)
{
    Config.Options.ImportMaterials = true;
    Config.Options.ImportTextures  = true;
    const auto     filename        = WriteTexturedGltf();
    nlohmann::json document;
    std::ifstream(filename) >> document;
    auto& material              = document["materials"][0];
    material["emissiveTexture"] = {
        {"index", 0}
    };
    material["normalTexture"] = {
        {"index", 0}
    };
    material["occlusionTexture"] = {
        {"index", 0}
    };
    material["pbrMetallicRoughness"]["metallicRoughnessTexture"] = {
        {"index", 0}
    };
    auto second    = material;
    second["name"] = "second";
    document["materials"].push_back(second);
    std::ofstream(filename) << document.dump();
    ImportGltf(filename);
    ASSERT_EQ(Callbacks.Failed, 0) << Callbacks.Error;
    ASSERT_EQ(Callbacks.Completed, 1);
    ASSERT_EQ(Callbacks.Outputs.size(), 3u);
    const auto first    = nlohmann::json::parse(ReadFile(Callbacks.Outputs[1].Path.c_str()));
    const auto expected = first["textures"]["albedo"];
    EXPECT_EQ(expected["path"], "/textures/triangle/one_pixel_0.png");
    for (size_t m = 1; m < Callbacks.Outputs.size(); ++m)
    {
        const auto cooked = nlohmann::json::parse(ReadFile(Callbacks.Outputs[m].Path.c_str()));
        for (const auto* slot : {"albedo", "emissive", "normal", "opacity", "specular"})
            EXPECT_EQ(cooked["textures"][slot], expected);
    }
    ImportGltf(filename);
    ASSERT_EQ(Callbacks.Completed, 2);
    EXPECT_EQ(nlohmann::json::parse(ReadFile(Callbacks.Outputs[1].Path.c_str()))["textures"]["albedo"], expected);
}

TEST_F(ModelImporterOutputTest, UnnamedAndDuplicateMaterialsKeepDistinctStableOutputsAndDisplayNames)
{
    Config.Options.ImportMaterials = true;
    const auto     filename        = WriteGltf(3);
    nlohmann::json document;
    std::ifstream(filename) >> document;
    document["materials"][0].erase("name");
    document["materials"][1]["name"] = "same";
    document["materials"][2]["name"] = "same";
    const auto primitive             = document["meshes"][0]["primitives"][0];
    for (size_t i = 1; i < 3; ++i)
    {
        auto next        = primitive;
        next["material"] = i;
        document["meshes"][0]["primitives"].push_back(next);
    }
    std::ofstream(filename) << document.dump();
    ImportGltf(filename);
    ASSERT_EQ(Callbacks.Completed, 1) << Callbacks.Error;
    const auto outputs = Callbacks.Outputs;
    EXPECT_EQ(outputs[1].Path, "/materials/triangle/material_0.zematerial");
    EXPECT_EQ(outputs[2].Path, "/materials/triangle/same_1.zematerial");
    EXPECT_EQ(outputs[3].Path, "/materials/triangle/same_2.zematerial");
    AssetMesh          mesh;
    AssetNodeHierarchy hierarchy;
    ReadCookedMesh(mesh, hierarchy);
    ASSERT_EQ(mesh.SubMeshes.size(), 3u);
    for (size_t m = 0; m < 3; ++m)
    {
        const auto cooked = nlohmann::json::parse(ReadFile(outputs[m + 1].Path.c_str()));
        EXPECT_EQ(cooked["name"], m == 0 ? "material" : "same");
        EXPECT_EQ(cooked["uuid"], uuids::to_string(mesh.SubMeshes[m].MaterialUUID));
        MetaFileData meta;
        meta.AssetUUID = mesh.SubMeshes[m].MaterialUUID;
        ASSERT_TRUE(MetaFileIO::Write(Context, Path(outputs[m + 1].Path.c_str()), meta).Succeeded());
    }
    ImportGltf(filename);
    ASSERT_EQ(Callbacks.Completed, 2) << Callbacks.Error;
    for (size_t m = 0; m < 3; ++m)
    {
        EXPECT_EQ(Callbacks.Outputs[m + 1].Path, outputs[m + 1].Path);
        EXPECT_EQ(nlohmann::json::parse(ReadFile(outputs[m + 1].Path.c_str()))["uuid"], uuids::to_string(mesh.SubMeshes[m].MaterialUUID));
    }
}

TEST_F(ModelImporterOutputTest, DifferentModelsCannotOverwriteEachOthersMaterials)
{
    Config.Options.ImportMaterials = true;
    const auto filename            = WriteGltf(1);
    ImportGltf(filename);
    ASSERT_EQ(Callbacks.Completed, 1) << Callbacks.Error;
    const auto original_path  = Callbacks.Outputs[1].Path;
    const auto original_bytes = ReadFile(original_path.c_str());
    Config.AssetName.init(&Arena, "other_model");
    Config.OutputAssetFile.init(&Arena, "other_model.zemesh");
    ImportGltf(filename);
    ASSERT_EQ(Callbacks.Completed, 2) << Callbacks.Error;
    EXPECT_NE(Callbacks.Outputs[1].Path, original_path);
    EXPECT_EQ(ReadFile(original_path.c_str()), original_bytes);
}

TEST_F(ModelImporterOutputTest, InvalidImageBufferRangesAreRejectedInEditorAndRuntime)
{
    Config.Options.ImportMaterials = true;
    Config.Options.ImportTextures  = true;
    const auto     filename        = WriteTexturedGltf();
    nlohmann::json original;
    std::ifstream(filename) >> original;
    for (size_t variant = 0; variant < 5; ++variant)
    {
        auto document = original;
        document["bufferViews"].push_back({
            {    "buffer",  0},
            {"byteLength", 36}
        });
        document["images"][0] = {
            {      "name",       "bad"},
            {"bufferView",           1},
            {  "mimeType", "image/png"}
        };
        if (variant == 0)
            document["bufferViews"][1]["byteOffset"] = 1073741824;
        else if (variant == 1)
            document["bufferViews"][1]["byteLength"] = 37;
        else if (variant == 2)
            document["bufferViews"][1]["buffer"] = 1;
        else if (variant == 3)
            document["images"][0]["bufferView"] = 2;
        else
            document["textures"][0]["source"] = 1;
        std::ofstream(filename) << document.dump();
        ImportGltf(filename);
        EXPECT_EQ(Callbacks.Completed, 0);
        EXPECT_EQ(Callbacks.Failed, static_cast<int>(variant + 1));
        EXPECT_FALSE(Exists("/meshes/triangle.zemesh"));
        MetaFileData meta;
        EXPECT_TRUE(Importer.Import(Context, VFSPath::FromNative(filename.c_str()).Value(), meta).Failed());
        EXPECT_EQ(Importer.Arena.m_current_offset, Importer.Arena.m_initial_current_offset);
    }
    std::ofstream(filename) << original.dump();
    ImportGltf(filename);
    EXPECT_EQ(Callbacks.Completed, 1) << Callbacks.Error;
}

TEST_F(ModelImporterOutputTest, InvalidTriangleCountsAndAccessorRangesReportErrorsAndAllowRetry)
{
    const auto     filename = WriteGltf();
    nlohmann::json original;
    std::ifstream(filename) >> original;
    const std::array<float, 12> vertices = {0, 0, 0, 1, 0, 0, 0, 1, 0, 1, 1, 0};
    for (size_t variant = 0; variant < 5; ++variant)
    {
        auto document = original;
        {
            std::ofstream binary(TempDirectory / "triangle.bin", std::ios::binary);
            binary.write(reinterpret_cast<const char*>(vertices.data()), sizeof(vertices));
        }
        document["buffers"][0]["byteLength"]     = sizeof(vertices);
        document["bufferViews"][0]["byteLength"] = sizeof(vertices);
        if (variant == 0)
            document["accessors"][0]["count"] = 4;
        else if (variant == 1)
            document["accessors"][0]["byteOffset"] = 16;
        else if (variant == 2)
            document["bufferViews"][0]["byteStride"] = 0;
        else if (variant == 3)
            document["meshes"][0]["primitives"][0]["attributes"]["POSITION"] = 1;
        else
        {
            document["accessors"].push_back(document["accessors"][0]);
            document["accessors"][1]["count"]                              = 4;
            document["meshes"][0]["primitives"][0]["attributes"]["NORMAL"] = 1;
        }
        std::ofstream(filename) << document.dump();
        ImportGltf(filename);
        EXPECT_EQ(Callbacks.Failed, static_cast<int>(variant + 1));
        EXPECT_EQ(Callbacks.Completed, 0);
        EXPECT_EQ(Importer.Arena.m_current_offset, Importer.Arena.m_initial_current_offset);
    }
    std::ofstream(filename) << original.dump();
    ImportGltf(filename);
    EXPECT_EQ(Callbacks.Completed, 1) << Callbacks.Error;
}

TEST_F(ModelImporterOutputTest, IndexedTrianglesRejectInvalidCountsAndVertexReferences)
{
    const auto     filename = WriteGltf();
    nlohmann::json document;
    std::ifstream(filename) >> document;
    document["buffers"][0]["byteLength"] = 52;
    document["bufferViews"].push_back({
        {    "buffer",  0},
        {"byteOffset", 36},
        {"byteLength", 16}
    });
    document["accessors"].push_back({
        {   "bufferView",        1},
        {"componentType",     5125},
        {        "count",        4},
        {         "type", "SCALAR"}
    });
    document["meshes"][0]["primitives"][0]["indices"] = 1;
    const std::array<float, 9> vertices               = {0, 0, 0, 1, 0, 0, 0, 1, 0};
    for (size_t variant = 0; variant < 3; ++variant)
    {
        const std::array<uint32_t, 4> indices = {0, 1, variant == 1 ? 3u : 2u, 0};
        {
            std::ofstream binary(TempDirectory / "triangle.bin", std::ios::binary);
            binary.write(reinterpret_cast<const char*>(vertices.data()), sizeof(vertices));
            binary.write(reinterpret_cast<const char*>(indices.data()), sizeof(indices));
        }
        document["accessors"][1]["count"] = variant == 0 ? 4 : 3;
        std::ofstream(filename) << document.dump();
        ImportGltf(filename);
        EXPECT_EQ(Callbacks.Completed, variant == 2 ? 1 : 0) << Callbacks.Error;
        EXPECT_EQ(Callbacks.Failed, variant == 0 ? 1 : 2);
    }
}

TEST_F(ModelImporterOutputTest, EmbeddedImageAtExactBufferBoundaryImportsSuccessfully)
{
    Config.Options.ImportMaterials = true;
    Config.Options.ImportTextures  = true;
    const auto filename            = WriteTexturedGltf();
    ImportGltf(filename);
    ASSERT_EQ(Callbacks.Completed, 1) << Callbacks.Error;
    const auto     image  = ReadFile("/textures/triangle/one_pixel_0.png");
    const auto     cooked = nlohmann::json::parse(ReadFile(Callbacks.Outputs[1].Path.c_str()));
    nlohmann::json document;
    std::ifstream(filename) >> document;
    const std::array<float, 9> vertices = {0, 0, 0, 1, 0, 0, 0, 1, 0};
    {
        std::ofstream binary(TempDirectory / "triangle.bin", std::ios::binary);
        binary.write(reinterpret_cast<const char*>(vertices.data()), sizeof(vertices));
        binary.write(reinterpret_cast<const char*>(image.data()), image.size());
    }
    document["buffers"][0]["byteLength"] = sizeof(vertices) + image.size();
    document["bufferViews"].push_back({
        {    "buffer",                0},
        {"byteOffset", sizeof(vertices)},
        {"byteLength",     image.size()}
    });
    document["images"][0] = {
        {      "name", "one_pixel"},
        {"bufferView",           1},
        {  "mimeType", "image/png"}
    };
    std::ofstream(filename) << document.dump();
    ImportGltf(filename);
    ASSERT_EQ(Callbacks.Completed, 2) << Callbacks.Error;
    EXPECT_EQ(Callbacks.Failed, 0);
    EXPECT_EQ(ReadFile("/textures/triangle/one_pixel_0.png"), image);
    EXPECT_EQ(nlohmann::json::parse(ReadFile(Callbacks.Outputs[1].Path.c_str()))["textures"], cooked["textures"]);
}

TEST_F(ModelImporterOutputTest, MetadataWritersUseIndependentTemporaryFilesEvenWhenOneFails)
{
    struct InterleavedMetadataContext : VFSContext
    {
        bool                 InsideOtherWriter = false;
        bool                 OtherSucceeded    = false;
        VFSPath              AssetPath;
        VFSPath              FirstTemporary;
        VFSPath              OtherTemporary;
        MetaFileData         OtherData{};
        VFSResult<IVFSFile*> Open(const VFSPath& path, VFSOpenFlags flags) override
        {
            auto result = VFSContext::Open(path, flags);
            if (result.Succeeded() && HasFlag(flags, VFSOpenFlags::Write))
            {
                if (InsideOtherWriter)
                    OtherTemporary = path;
                else
                {
                    FirstTemporary    = path;
                    InsideOtherWriter = true;
                    OtherSucceeded    = MetaFileIO::Write(*this, AssetPath, OtherData).Succeeded();
                    InsideOtherWriter = false;
                }
            }
            return result;
        }
        VFSResult<void> Rename(const VFSPath& src, const VFSPath& dst) override
        {
            return InsideOtherWriter ? VFSContext::Rename(src, dst) : VFSResult<void>::Fail(VFSError::IOError);
        }
    } writers;
    writers.Initialize(&Arena, 4);
    ASSERT_TRUE(writers.Mount(&Backend, VFSPath::Root(), 0).Succeeded());
    ASSERT_TRUE(writers.CreateDir(Path("/textures")).Succeeded());
    writers.AssetPath           = Path("/textures/shared.png");
    writers.OtherData.AssetUUID = uuids::uuid::from_string("22222222-2222-4222-8222-222222222222").value();
    MetaFileData first;
    first.AssetUUID = uuids::uuid::from_string("11111111-1111-4111-8111-111111111111").value();
    EXPECT_TRUE(MetaFileIO::Write(writers, writers.AssetPath, first).Failed());
    ASSERT_TRUE(writers.OtherSucceeded);
    EXPECT_NE(writers.FirstTemporary, writers.OtherTemporary);
    auto published = MetaFileIO::Read(writers, writers.AssetPath);
    ASSERT_TRUE(published.Succeeded());
    EXPECT_EQ(published.Value().AssetUUID, writers.OtherData.AssetUUID);
    ExpectNoTemporaryFiles("/textures");
    writers.Shutdown();
}

TEST_F(ModelImporterOutputTest, ConcurrentMetadataCreationKeepsOneIdentity)
{
    ASSERT_TRUE(Context.CreateDir(Path("/textures")).Succeeded());
    auto create = [&] { return MetaFileIO::GetOrCreate(Context, Path("/textures/shared.png"), "Test", 1); };
    auto first  = std::async(std::launch::async, create);
    auto second = std::async(std::launch::async, create);
    auto one    = first.get();
    auto two    = second.get();
    ASSERT_TRUE(one.Succeeded());
    ASSERT_TRUE(two.Succeeded());
    EXPECT_EQ(one.Value().AssetUUID, two.Value().AssetUUID);
    EXPECT_FALSE(one.Value().AssetUUID.is_nil());
}

class ModelImporterMetadataFailureTest : public ModelImporterOutputTest, public ::testing::WithParamInterface<FailurePoint>
{
};

TEST_P(ModelImporterMetadataFailureTest, TextureMetadataFailureStopsImportAndAllowsRetry)
{
    Config.Options.ImportMaterials = true;
    Config.Options.ImportTextures  = true;
    Context.Point                  = GetParam();
    Context.FailurePath            = "/textures/";
    Context.FailureWriteNumber     = 2; // Image publication succeeds; its metadata fails.
    const auto filename            = WriteTexturedGltf();
    ImportGltf(filename);
    EXPECT_EQ(Callbacks.Failed, 1);
    EXPECT_EQ(Callbacks.Completed, 0);
    EXPECT_FALSE(Exists("/meshes/triangle.zemesh"));
    EXPECT_FALSE(Exists("/textures/triangle/one_pixel_0.png.meta"));
    ExpectNoTemporaryFiles("/textures/triangle");
    Context.Point              = FailurePoint::None;
    Context.FailureWriteNumber = 0;
    ImportGltf(filename);
    EXPECT_EQ(Callbacks.Completed, 1) << Callbacks.Error;
    EXPECT_TRUE(Exists("/textures/triangle/one_pixel_0.png.meta"));
}

TEST_P(ModelImporterMetadataFailureTest, FailedMetadataUpdatePreservesPreviousIdentityAndHash)
{
    ASSERT_TRUE(Context.CreateDir(Path("/textures")).Succeeded());
    const auto path    = Path("/textures/shared.png");
    auto       initial = MetaFileIO::GetOrCreate(Context, path, "Test", 1);
    ASSERT_TRUE(initial.Succeeded());
    Context.WriteNumber        = 0;
    Context.FailureWriteNumber = 1;
    Context.Point              = GetParam();
    Context.FailurePath        = "/textures/";
    EXPECT_TRUE(MetaFileIO::GetOrCreate(Context, path, "Test", 2).Failed());
    auto old = MetaFileIO::Read(Context, path);
    ASSERT_TRUE(old.Succeeded());
    EXPECT_EQ(old.Value().SourceHash, 1u);
    EXPECT_EQ(old.Value().AssetUUID, initial.Value().AssetUUID);
    ExpectNoTemporaryFiles("/textures");
}

INSTANTIATE_TEST_SUITE_P(VFSStages, ModelImporterMetadataFailureTest, ::testing::Values(FailurePoint::Open, FailurePoint::Write, FailurePoint::ShortWrite, FailurePoint::Flush, FailurePoint::Close, FailurePoint::Rename));

class ModelImporterWriteFailureTest : public ModelImporterOutputTest, public ::testing::WithParamInterface<FailurePoint>
{
};

TEST_P(ModelImporterWriteFailureTest, MeshWriteFailureStopsImportAndAllowsRetry)
{
    const auto filename = WriteGltf();
    Context.Point       = GetParam();
    ImportGltf(filename);

    EXPECT_EQ(Callbacks.Failed, 1);
    EXPECT_EQ(Callbacks.Completed, 0);
    EXPECT_TRUE(Callbacks.Outputs.empty());
    EXPECT_LT(Callbacks.Progress, 1.0f);
    EXPECT_NE(Callbacks.Error.find("/meshes/triangle.zemesh"), std::string::npos);
    EXPECT_NE(Callbacks.Error.find("VFS error"), std::string::npos);
    EXPECT_FALSE(Exists("/meshes/triangle.zemesh"));
    ExpectNoTemporaryFiles("/meshes");
    EXPECT_EQ(Importer.Arena.m_current_offset, Importer.Arena.m_initial_current_offset);

    Context.Point = FailurePoint::None;
    ImportGltf(filename);
    EXPECT_EQ(Callbacks.Failed, 1);
    EXPECT_EQ(Callbacks.Completed, 1);
    EXPECT_TRUE(Exists("/meshes/triangle.zemesh"));
}

TEST_P(ModelImporterWriteFailureTest, FailedRecookPreservesExistingArtifact)
{
    const auto filename = WriteGltf();
    ImportGltf(filename);
    ASSERT_EQ(Callbacks.Completed, 1);
    const auto before = ReadFile("/meshes/triangle.zemesh");
    ASSERT_FALSE(before.empty());

    Context.Point = GetParam();
    ImportGltf(filename);
    EXPECT_EQ(Callbacks.Failed, 1);
    EXPECT_EQ(Callbacks.Completed, 1);
    EXPECT_EQ(before, ReadFile("/meshes/triangle.zemesh"));
    ExpectNoTemporaryFiles("/meshes");
}

TEST_P(ModelImporterWriteFailureTest, TextureFailureStopsImportAndPreservesExistingMeshAndTexture)
{
    Config.Options.ImportMaterials = Config.Options.ImportTextures = true;
    const auto filename                                            = WriteTexturedGltf();
    ImportGltf(filename);
    ASSERT_EQ(Callbacks.Completed, 1) << Callbacks.Error;
    const auto before_mesh    = ReadFile("/meshes/triangle.zemesh");
    const auto before_texture = ReadFile("/textures/triangle/one_pixel_0.png");
    ChangeTriangleVertices();
    Context.Point       = GetParam();
    Context.FailurePath = "/textures/";
    ImportGltf(filename);
    EXPECT_EQ(Callbacks.Failed, 1);
    EXPECT_EQ(Callbacks.Completed, 1);
    EXPECT_LT(Callbacks.Progress, 1.0f);
    EXPECT_EQ(ReadFile("/meshes/triangle.zemesh"), before_mesh);
    EXPECT_EQ(ReadFile("/textures/triangle/one_pixel_0.png"), before_texture);
    ExpectNoTemporaryFiles("/textures/triangle");
}

INSTANTIATE_TEST_SUITE_P(VFSStages, ModelImporterWriteFailureTest, ::testing::Values(FailurePoint::CreateDir, FailurePoint::Open, FailurePoint::Write, FailurePoint::ShortWrite, FailurePoint::Flush, FailurePoint::Close, FailurePoint::Rename));
