#include "editor/BuildExporter.hpp"

#include "core/Log.hpp"
#include "project/Project.hpp"

#include <filesystem>
#include <fstream>
#include <system_error>

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#endif

namespace ne {

namespace {
namespace fs = std::filesystem;

// Recursively copy a directory if it exists. Returns false on hard error.
bool copyTree(const fs::path& src, const fs::path& dst, std::string& log) {
    std::error_code ec;
    if (!fs::exists(src)) return true;  // nothing to copy is not an error
    fs::create_directories(dst, ec);
    fs::copy(src, dst,
             fs::copy_options::recursive | fs::copy_options::overwrite_existing,
             ec);
    if (ec) {
        log += "  ! failed to copy " + src.string() + " -> " + dst.string() +
               " (" + ec.message() + ")\n";
        return false;
    }
    log += "  copied " + src.filename().string() + "/\n";
    return true;
}

// Is `child` inside `parent` (or equal)? Used to reject dangerous output dirs.
bool isInside(const fs::path& child, const fs::path& parent) {
    const fs::path c = child.lexically_normal();
    const fs::path p = parent.lexically_normal();
    auto it = c.begin();
    for (auto pit = p.begin(); pit != p.end(); ++pit, ++it) {
        if (it == c.end() || *it != *pit) return false;
    }
    return true;
}

} // namespace

BuildExporter::Result BuildExporter::exportWindowsBuild(const Project& project,
                                                        const Options& options) {
    Result r;
    auto fail = [&](const std::string& msg) -> Result {
        r.success = false;
        r.error = msg;
        r.log += "ERROR: " + msg + "\n";
        Log::error("Build: ", msg);
        return r;
    };

    if (!project.isLoaded()) return fail("no project loaded");

    const fs::path projectRoot = fs::path(project.rootPath()).lexically_normal();

    // Resolve output directory (relative paths are anchored to the project root).
    fs::path outDir = options.outputDir.empty() ? fs::path("build/export")
                                                : fs::path(options.outputDir);
    if (outDir.is_relative()) outDir = projectRoot / outDir;
    outDir = outDir.lexically_normal();
    r.outputDir = outDir.string();

    // Guard: never export into the project root or inside its asset folders —
    // that would copy a tree into itself.
    if (outDir == projectRoot)
        return fail("output directory must not be the project root itself");
    if (isInside(outDir, fs::path(project.assetsDir())) ||
        isInside(outDir, fs::path(project.scenesDir())) ||
        isInside(outDir, fs::path(project.scriptsDir())))
        return fail("output directory must not be inside assets/scenes/scripts");

    std::error_code ec;
    fs::create_directories(outDir, ec);
    if (ec) return fail("cannot create output directory: " + outDir.string());
    r.log += "Output: " + outDir.string() + "\n";

    const fs::path binDir = fs::path(NE_RUNTIME_DIR);
    const fs::path runtimeExe = binDir / "NextEngineRuntime.exe";
    if (!fs::exists(runtimeExe))
        return fail("runtime template not found — build the NextEngineRuntime "
                    "target first: " + runtimeExe.string());

    const std::string gameName = project.name().empty() ? "Game" : project.name();
    const fs::path gameExe = outDir / (gameName + ".exe");

    // 1. Runtime exe (renamed to the game name).
    fs::copy_file(runtimeExe, gameExe, fs::copy_options::overwrite_existing, ec);
    if (ec) return fail("failed to copy runtime exe: " + ec.message());
    r.gameExe = gameExe.string();
    r.log += "  exe -> " + gameExe.filename().string() + "\n";

    // 2. GLFW runtime DLL.
    const fs::path glfwDll = binDir / "glfw3.dll";
    if (fs::exists(glfwDll)) {
        fs::copy_file(glfwDll, outDir / "glfw3.dll",
                      fs::copy_options::overwrite_existing, ec);
        if (ec) return fail("failed to copy glfw3.dll: " + ec.message());
        r.log += "  glfw3.dll\n";
    } else {
        r.log += "  ! glfw3.dll not found in build dir (may already be on PATH)\n";
    }

    // 3. Compiled SPIR-V shaders.
    if (!copyTree(binDir / "shaders", outDir / "shaders", r.log))
        return fail("failed to copy shaders");

    // 4. Project data, in a flat layout so Project::load() sets rootPath = outDir
    //    and the AssetRegistry resolves every asset relative to it.
    if (!copyTree(project.assetsDir(),  outDir / "assets",  r.log) ||
        !copyTree(project.scenesDir(),  outDir / "scenes",  r.log) ||
        !copyTree(project.scriptsDir(), outDir / "scripts", r.log))
        return fail("failed to copy project data");

    // .neproj + asset registry (the runtime loads the project from these).
    const fs::path neproj = project.filePath();
    if (fs::exists(neproj)) {
        fs::copy_file(neproj, outDir / neproj.filename(),
                      fs::copy_options::overwrite_existing, ec);
        if (ec) return fail("failed to copy .neproj: " + ec.message());
        r.log += "  " + neproj.filename().string() + "\n";
    } else {
        return fail("project file missing: " + neproj.string());
    }
    const fs::path registry = projectRoot / "asset_registry.json";
    if (fs::exists(registry)) {
        fs::copy_file(registry, outDir / "asset_registry.json",
                      fs::copy_options::overwrite_existing, ec);
        if (ec) r.log += "  ! failed to copy asset_registry.json\n";
        else r.log += "  asset_registry.json\n";
    }

    // 5. Boot manifest (read by NextEngineRuntime at startup).
    {
        std::ofstream manifest(outDir / "game.ne", std::ios::trunc);
        if (!manifest) return fail("cannot write boot manifest game.ne");
        manifest << "# NextEngine game boot manifest\n";
        manifest << "project=" << neproj.filename().string() << "\n";
        manifest << "main_scene=" << options.mainScene << "\n";
    }
    r.log += "  game.ne (main_scene=" + options.mainScene + ")\n";

    r.success = true;
    r.log += "Build succeeded.\n";
    Log::info("Build succeeded: ", gameExe.string());

    if (options.launchAfterBuild) launch(r.gameExe);
    return r;
}

bool BuildExporter::launch(const std::string& exePath) {
#ifdef _WIN32
    const fs::path exe(exePath);
    const std::string dir = exe.parent_path().string();
    HINSTANCE h = ::ShellExecuteA(nullptr, "open", exePath.c_str(), nullptr,
                                  dir.c_str(), SW_SHOWNORMAL);
    if (reinterpret_cast<INT_PTR>(h) <= 32) {
        Log::error("Build: failed to launch ", exePath);
        return false;
    }
    return true;
#else
    (void)exePath;
    return false;
#endif
}

void BuildExporter::openInExplorer(const std::string& dir) {
#ifdef _WIN32
    ::ShellExecuteA(nullptr, "open", dir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#else
    (void)dir;
#endif
}

} // namespace ne
