#include "editor/panels/FileBrowserPanel.hpp"
#include "editor/EditorUI.hpp"
#include "project/Project.hpp"
#include "scene/Scene.hpp"
#include "graphics/ResourceManager.hpp"
#include "graphics/Texture.hpp"

#include <imgui.h>
#include <filesystem>
#include <algorithm>
#include <cctype>
#include <cstring>

namespace ne {

namespace {

static std::string toLower(const std::string& str) {
    std::string lower = str;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c){ return std::tolower(c); });
    return lower;
}

const char* fileIcon(const std::filesystem::directory_entry& entry) {
    if (entry.is_directory()) return "[D]";
    auto ext = entry.path().extension().string();
    if (ext == ".obj" || ext == ".fbx" || ext == ".gltf") return "[3D]";
    if (ext == ".png" || ext == ".jpg" || ext == ".bmp")  return "[Img]";
    if (ext == ".vert" || ext == ".frag" || ext == ".glsl" || ext == ".spv") return "[Sh]";
    if (ext == ".lua")   return "[Lua]";
    if (ext == ".neproj") return "[Proj]";
    if (ext == ".scene") return "[Sc]";
    return "[F]";
}

} // namespace

void FileBrowserPanel::draw(EditorUI* editor, Project* project, Scene* scene, ResourceManager* resources) {
    ImGui::Begin("File Browser", &editor->showFileBrowser_);

    if (!project || !project->isLoaded()) {
        ImGui::TextDisabled("No project loaded.");
        ImGui::TextDisabled("Use File > New Project or Open Project to get started.");
        ImGui::End();
        return;
    }

    namespace fs = std::filesystem;
    fs::path root(project->rootPath());

    if (editor->currentBrowsePath_.empty() ||
        editor->currentBrowsePath_.find(project->rootPath()) == std::string::npos) {
        editor->currentBrowsePath_ = project->rootPath();
    }

    fs::path browsePath(editor->currentBrowsePath_);

    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    
    if (ImGui::Button(project->name().c_str())) {
        editor->currentBrowsePath_ = root.string();
    }

    if (browsePath != root) {
        std::vector<fs::path> components;
        fs::path p = browsePath;
        while (p != root && !p.empty() && p.string().find(root.string()) != std::string::npos) {
            components.push_back(p);
            p = p.parent_path();
        }
        std::reverse(components.begin(), components.end());

        for (const auto& comp : components) {
            ImGui::SameLine(0.0f, 4.0f);
            ImGui::TextDisabled(">");
            ImGui::SameLine(0.0f, 4.0f);
            if (ImGui::Button(comp.filename().string().c_str())) {
                editor->currentBrowsePath_ = comp.string();
            }
        }
    }
    ImGui::PopStyleColor();

    ImGui::SameLine(ImGui::GetWindowWidth() - 360.0f);
    ImGui::SetNextItemWidth(200.0f);
    ImGui::InputTextWithHint("##FileSearch", "Search files...", editor->fileBrowserSearchBuf_, sizeof(editor->fileBrowserSearchBuf_));

    ImGui::SameLine();
    ImGui::SetNextItemWidth(100.0f);
    ImGui::SliderFloat("##Zoom", &editor->fileBrowserZoom_, 0.5f, 3.0f, "Zoom %.1f");

    if (ImGui::IsWindowHovered()) {
        float scroll = ImGui::GetIO().MouseWheel;
        if (scroll != 0.0f && ImGui::GetIO().KeyCtrl) {
            editor->fileBrowserZoom_ += scroll * 0.15f; 
            editor->fileBrowserZoom_ = std::clamp(editor->fileBrowserZoom_, 0.5f, 3.0f);
        }
    }

    ImGui::Separator();

    try {
        std::vector<fs::directory_entry> dirs;
        std::vector<fs::directory_entry> files;
        std::string query = toLower(editor->fileBrowserSearchBuf_);

        if (query.empty()) {
            for (auto& entry : fs::directory_iterator(browsePath)) {
                auto name = entry.path().filename().string();
                if (name.empty() || name[0] == '.') continue;
                if (name == "build") continue;

                if (entry.is_directory()) dirs.push_back(entry);
                else files.push_back(entry);
            }
        } else {
            for (auto& entry : fs::recursive_directory_iterator(project->rootPath())) {
                auto name = entry.path().filename().string();
                if (name.empty() || name[0] == '.') continue;
                if (name == "build") continue;

                std::string nameLower = toLower(name);
                std::string extLower = toLower(entry.path().extension().string());
                
                if (nameLower.find(query) != std::string::npos || extLower.find(query) != std::string::npos) {
                    if (entry.is_directory()) dirs.push_back(entry);
                    else files.push_back(entry);
                }
            }
        }

        auto cmp = [](const fs::directory_entry& a, const fs::directory_entry& b) {
            return a.path().filename() < b.path().filename();
        };
        std::sort(dirs.begin(), dirs.end(), cmp);
        std::sort(files.begin(), files.end(), cmp);

        bool useGrid = editor->fileBrowserZoom_ > 1.0f;
        float iconSize = 64.0f * editor->fileBrowserZoom_;
        float cellSize = iconSize + 20.0f;

        if (useGrid) {
            float panelWidth = ImGui::GetContentRegionAvail().x;
            int columnCount = std::max(1, static_cast<int>(panelWidth / cellSize));
            ImGui::Columns(columnCount, nullptr, false);
        }

        std::string pathToDelete_;

        auto drawItemContextMenu = [&](const std::string& pathStr, const std::string& filename) {
            if (ImGui::BeginPopupContextItem()) {
                if (ImGui::MenuItem("Show in Explorer")) {
#ifdef _WIN32
                    std::string winPath = pathStr;
                    std::replace(winPath.begin(), winPath.end(), '/', '\\');
                    std::string cmd = "explorer /select,\"" + winPath + "\"";
                    std::system(cmd.c_str());
#endif
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Rename")) {
                    editor->fileToRename_ = pathStr;
                    std::strncpy(editor->fileRenameBuf_, filename.c_str(), sizeof(editor->fileRenameBuf_) - 1);
                    editor->fileRenameBuf_[sizeof(editor->fileRenameBuf_) - 1] = '\0';
                }
                if (ImGui::MenuItem("Delete")) {
                    pathToDelete_ = pathStr;
                }
                ImGui::EndPopup();
            }
        };

        auto handleInlineRename = [&](const std::string& pathStr, float width) {
            if (editor->fileToRename_ == pathStr) {
                ImGui::SetNextItemWidth(width);
                ImGui::SetKeyboardFocusHere();
                if (ImGui::InputText(("##rename_" + pathStr).c_str(), editor->fileRenameBuf_, sizeof(editor->fileRenameBuf_), ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll)) {
                    try { fs::rename(editor->fileToRename_, fs::path(editor->fileToRename_).parent_path() / editor->fileRenameBuf_); } catch(...) {}
                    editor->fileToRename_.clear();
                } else if (ImGui::IsItemDeactivated() && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
                    try { fs::rename(editor->fileToRename_, fs::path(editor->fileToRename_).parent_path() / editor->fileRenameBuf_); } catch(...) {}
                    editor->fileToRename_.clear();
                }
                return true;
            }
            return false;
        };

        char buffer[256];
        for (auto& d : dirs) {
            std::string pathStr = d.path().string();
            std::string filename = d.path().filename().string();
            if (useGrid) {
                ImGui::BeginGroup();
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0,0,0,0));
                ImGui::Button(("[DIR]\n" + filename).c_str(), ImVec2(iconSize, iconSize));
                ImGui::PopStyleColor();
                drawItemContextMenu(pathStr, filename);
                if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                    editor->currentBrowsePath_ = pathStr;
                
                if (!handleInlineRename(pathStr, iconSize)) {
                    ImGui::TextWrapped("%s", filename.c_str());
                }
                ImGui::EndGroup();
                ImGui::NextColumn();
            } else {
                if (!handleInlineRename(pathStr, -1.0f)) {
                    std::snprintf(buffer, sizeof(buffer), "[D] %s", filename.c_str());
                    if (ImGui::Selectable(buffer, false, ImGuiSelectableFlags_AllowDoubleClick)) {
                        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                            editor->currentBrowsePath_ = pathStr;
                    }
                    drawItemContextMenu(pathStr, filename);
                }
            }
        }
        
        for (auto& f : files) {
            std::string pathStr = f.path().string();
            std::string filename = f.path().filename().string();
            auto ext = f.path().extension().string();
            
            if (useGrid) {
                ImGui::BeginGroup();
                
                if (ext == ".png" || ext == ".jpg" || ext == ".bmp") {
                    AssetID id = resources->getOrRegister(f.path().string(), AssetType::Texture);
                    Texture* tex = resources->getTexture(id);
                    if (tex && editor->texCache_.get(tex)) {
                        ImGui::Image(editor->texCache_.get(tex), ImVec2(iconSize, iconSize));
                    } else {
                        ImGui::Button("[IMG]", ImVec2(iconSize, iconSize));
                    }
                } else if (ext == ".scene") {
                    ImGui::Button("[SCENE]", ImVec2(iconSize, iconSize));
                    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                        editor->loadScene(scene, resources, pathStr);
                    }
                } else if (ext == ".wav" || ext == ".mp3" || ext == ".ogg") {
                    ImGui::Button("[AUDIO]", ImVec2(iconSize, iconSize));
                } else if (ext == ".obj" || ext == ".fbx" || ext == ".gltf") {
                    ImGui::Button("[3D]", ImVec2(iconSize, iconSize));
                } else {
                    ImGui::Button("[FILE]", ImVec2(iconSize, iconSize));
                }

                if (ext == ".scene") {
                    if (ImGui::BeginDragDropSource()) {
                        ImGui::SetDragDropPayload("FILE_SCENE", pathStr.c_str(), pathStr.size() + 1);
                        ImGui::Text("Instantiate %s", filename.c_str());
                        ImGui::EndDragDropSource();
                    }
                } else if (ext == ".obj" || ext == ".fbx" || ext == ".gltf" || ext == ".png" || ext == ".jpg") {
                    if (ImGui::BeginDragDropSource()) {
                        AssetID id = project->assetRegistry().getID(pathStr);
                        if (id != kAssetInvalid) {
                            ImGui::SetDragDropPayload("ASSET_ID", &id, sizeof(AssetID));
                            ImGui::Text("Assign %s", filename.c_str());
                        }
                        ImGui::EndDragDropSource();
                    }
                }

                drawItemContextMenu(pathStr, filename);

                if (!handleInlineRename(pathStr, iconSize)) {
                    ImGui::TextWrapped("%s", filename.c_str());
                }
                ImGui::EndGroup();
                ImGui::NextColumn();
            } else {
                if (!handleInlineRename(pathStr, -1.0f)) {
                    std::snprintf(buffer, sizeof(buffer), "%s %s", fileIcon(f), filename.c_str());
                    if (ImGui::Selectable(buffer, false, ImGuiSelectableFlags_AllowDoubleClick)) {
                        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && ext == ".scene") {
                            editor->loadScene(scene, resources, pathStr);
                        }
                    }
                    drawItemContextMenu(pathStr, filename);
                }
                
                if (ext == ".scene") {
                    if (ImGui::BeginDragDropSource()) {
                        ImGui::SetDragDropPayload("FILE_SCENE", pathStr.c_str(), pathStr.size() + 1);
                        ImGui::Text("Instantiate %s", filename.c_str());
                        ImGui::EndDragDropSource();
                    }
                } else if (ext == ".obj" || ext == ".fbx" || ext == ".gltf" || ext == ".png" || ext == ".jpg") {
                    if (ImGui::BeginDragDropSource()) {
                        AssetID id = project->assetRegistry().getID(pathStr);
                        if (id != kAssetInvalid) {
                            ImGui::SetDragDropPayload("ASSET_ID", &id, sizeof(AssetID));
                            ImGui::Text("Assign %s", filename.c_str());
                        }
                        ImGui::EndDragDropSource();
                    }
                }
            }
        }
        
        if (!pathToDelete_.empty()) {
            try { fs::remove_all(pathToDelete_); } catch (...) {}
        }

        if (useGrid) {
            ImGui::Columns(1);
        }
    } catch (const fs::filesystem_error&) {
        ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "Error reading directory");
    }

    ImGui::End();
}

} // namespace ne
