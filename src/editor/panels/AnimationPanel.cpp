#include "editor/panels/AnimationPanel.hpp"

#include "graphics/ResourceManager.hpp"
#include "project/AssetRegistry.hpp"
#include "project/Project.hpp"
#include "scene/Scene.hpp"
#include "scene/animation/AnimGraphAsset.hpp"
#include "scene/animation/AnimStateMachine.hpp"
#include "scene/animation/AnimationClip.hpp"
#include "scene/animation/Animator.hpp"
#include "scene/animation/ClipNode.hpp"
#include "scene/animation/ClipView.hpp"

#include <imgui.h>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace saida {

namespace {

struct FoundAnimator {
    Animator* animator = nullptr;
    std::string nodeName;
};

// L'Animator piloté : celui du nœud sélectionné en priorité, sinon le premier
// de la scène de preview (mode importer), sinon le premier de la scène.
FoundAnimator findAnimator(EditorUI* editor, Scene* scene) {
    FoundAnimator found;
    if (Node* selected = editor->selectedNode()) {
        if (auto* anim = selected->getBehaviour<Animator>()) {
            return {anim, selected->name()};
        }
    }
    Scene* scenes[2] = {editor->previewScene(), scene};
    for (Scene* candidate : scenes) {
        if (!candidate || found.animator) continue;
        candidate->traverse([&](Node& node, const glm::mat4&) {
            if (!found.animator) {
                if (auto* anim = node.getBehaviour<Animator>()) {
                    found.animator = anim;
                    found.nodeName = node.name();
                }
            }
        });
    }
    return found;
}

std::vector<std::string> sortedClipNames(const Animator& animator) {
    std::vector<std::string> names;
    names.reserve(animator.clips().size());
    for (const auto& [name, clip] : animator.clips()) names.push_back(name);
    std::sort(names.begin(), names.end());
    return names;
}

// Clé de sous-asset d'un clip de la bibliothèque ("model.glb#Run"), retrouvée
// via le registre. Fallback "#nom" si le clip n'est pas enregistré : playView
// résout par le nom après '#', la vue reste donc jouable localement.
std::string subAssetKeyFor(const AnimationClip* clip, const std::string& clipName,
                           ResourceManager* resources) {
    if (resources && resources->registry()) {
        const AssetID id = resources->animationId(clip);
        if (id != kAssetInvalid) {
            const std::string key = resources->registry()->getPath(id);
            if (!key.empty()) return key;
        }
    }
    return "#" + clipName;
}

// Les diagnostics d'un asset, affichés en jaune (warning) ou rouge (erreur).
void drawDiagnostics(const std::vector<AssetDiagnostic>& diagnostics) {
    for (const auto& d : diagnostics) {
        const bool isError = d.severity == AssetDiagnostic::Severity::Error;
        ImGui::PushStyleColor(ImGuiCol_Text, isError ? ImVec4(1.0f, 0.4f, 0.4f, 1.0f)
                                                     : ImVec4(1.0f, 0.85f, 0.4f, 1.0f));
        ImGui::TextWrapped("%s %s", isError ? "[erreur]" : "[warn]", d.message.c_str());
        ImGui::PopStyleColor();
    }
}

} // namespace

// Scan (mis en cache) des .sclip/.sgraph du projet, chemins projet-relatifs.
void AnimationPanel::refreshAssetList(EditorUI* editor, Project* project) {
    const double now = ImGui::GetTime();
    if (editor->animAssetScanTime_ >= 0.0 && now - editor->animAssetScanTime_ < 2.0) return;
    editor->animAssetScanTime_ = now;
    editor->animAssetFiles_.clear();
    if (!project || !project->isLoaded()) return;

    const std::filesystem::path root(project->rootPath());
    std::error_code ec;
    std::filesystem::recursive_directory_iterator it(
        root, std::filesystem::directory_options::skip_permission_denied, ec);
    for (; !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
        const auto& entry = *it;
        if (entry.is_directory(ec)) {
            const std::string dir = entry.path().filename().string();
            if (dir == "build" || dir == ".git" || dir == ".saida") it.disable_recursion_pending();
            continue;
        }
        const std::string ext = entry.path().extension().string();
        if (ext == ".sclip" || ext == ".sgraph") {
            editor->animAssetFiles_.push_back(
                entry.path().lexically_relative(root).generic_string());
        }
    }
    std::sort(editor->animAssetFiles_.begin(), editor->animAssetFiles_.end());
}

static void drawPlaybackSection(Animator* animator) {
    if (!ImGui::CollapsingHeader("Lecture", ImGuiTreeNodeFlags_DefaultOpen)) return;

    const std::vector<std::string> clipNames = sortedClipNames(*animator);
    const std::string& current = animator->currentClip();

    ImGui::SetNextItemWidth(-1);
    if (ImGui::BeginCombo("##PlayClip", current.empty() ? "(clip)" : current.c_str())) {
        for (const auto& name : clipNames) {
            if (ImGui::Selectable(name.c_str(), name == current)) animator->play(name);
        }
        ImGui::EndCombo();
    }

    ClipNode* node = animator->activeClipNode();
    if (!node) {
        if (animator->rootNode())
            ImGui::TextDisabled("Le graphe actif n'est pas un clip simple.");
        return;
    }

    static float speed = 1.0f;
    const bool playing = speed != 0.0f;
    if (ImGui::Button(playing ? "Pause" : "Lecture", ImVec2(80, 0))) {
        speed = playing ? 0.0f : 1.0f;
        node->setPlaybackSpeed(speed);
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(90);
    if (ImGui::DragFloat("##Speed", &speed, 0.01f, -4.0f, 4.0f, "x%.2f"))
        node->setPlaybackSpeed(speed);
    ImGui::SameLine();

    // Le scrub se fait dans la fenêtre jouable (plage d'une vue comprise).
    float time = node->time();
    const float start = node->rangeStart();
    const float end = node->rangeEnd() > start ? node->rangeEnd() : start + 1.0f;
    ImGui::SetNextItemWidth(-1);
    if (ImGui::SliderFloat("##Time", &time, start, end, "%.2fs")) node->setTime(time);
}

void AnimationPanel::drawClipViewSection(EditorUI* editor, Animator* animator, ResourceManager* resources,
                         Project* project) {
    if (!ImGui::CollapsingHeader("Clip View (.sclip)", ImGuiTreeNodeFlags_DefaultOpen)) return;

    const std::vector<std::string> clipNames = sortedClipNames(*animator);
    if (clipNames.empty()) {
        ImGui::TextDisabled("Aucun clip dans cet Animator.");
        return;
    }

    ImGui::InputText("Nom", editor->animViewName_, sizeof(editor->animViewName_));

    editor->animViewSourceIndex_ =
        std::clamp(editor->animViewSourceIndex_, 0, int(clipNames.size()) - 1);
    const std::string& sourceName = clipNames[size_t(editor->animViewSourceIndex_)];
    if (ImGui::BeginCombo("Source", sourceName.c_str())) {
        for (int i = 0; i < int(clipNames.size()); ++i) {
            if (ImGui::Selectable(clipNames[size_t(i)].c_str(),
                                  i == editor->animViewSourceIndex_)) {
                editor->animViewSourceIndex_ = i;
                editor->animViewEnd_ = 0.0f;  // re-cadre la plage sur le nouveau clip
            }
        }
        ImGui::EndCombo();
    }

    const AnimationClip* sourceClip = animator->clips().at(sourceName);
    const float duration = sourceClip->duration();
    if (editor->animViewEnd_ <= 0.0f) editor->animViewEnd_ = duration;
    editor->animViewStart_ = std::clamp(editor->animViewStart_, 0.0f, duration);
    editor->animViewEnd_ = std::clamp(editor->animViewEnd_, 0.0f, duration);

    ImGui::DragFloatRange2("Plage", &editor->animViewStart_, &editor->animViewEnd_, 0.01f,
                           0.0f, duration, "début %.2fs", "fin %.2fs");
    ImGui::Checkbox("Boucle", &editor->animViewLoop_);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(90);
    ImGui::DragFloat("Vitesse", &editor->animViewSpeed_, 0.01f, -4.0f, 4.0f, "x%.2f");

    // La vue est construite champ à champ puis validée contre la source : les
    // diagnostics s'affichent en direct, l'enregistrement est bloqué en erreur.
    ClipView view;
    view.name = editor->animViewName_;
    view.source = subAssetKeyFor(sourceClip, sourceName, resources);
    view.hasRange = editor->animViewStart_ > 0.0f || editor->animViewEnd_ < duration;
    view.rangeStart = editor->animViewStart_;
    view.rangeEnd = editor->animViewEnd_;
    view.loop = editor->animViewLoop_;
    view.speed = editor->animViewSpeed_;

    std::vector<AssetDiagnostic> diags = view.validate(sourceClip);
    if (view.name.empty())
        diags.push_back({"clipview.name.missing", AssetDiagnostic::Severity::Error, "/name",
                         "le nom de la vue est vide"});
    drawDiagnostics(diags);

    const bool valid = !hasErrors(diags);
    if (ImGui::Button("Jouer la vue", ImVec2(120, 0)) && valid) {
        animator->playView(view, 0.0f);
        editor->animStatus_ = "Vue '" + view.name + "' jouée.";
    }
    ImGui::SameLine();
    const bool canSave = valid && project && project->isLoaded();
    if (!canSave) ImGui::BeginDisabled();
    if (ImGui::Button("Enregistrer .sclip", ImVec2(150, 0))) {
        const std::filesystem::path dir =
            std::filesystem::path(project->rootPath()) / "assets" / "animation";
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        const std::string path = (dir / (view.name + ".sclip")).generic_string();
        if (view.saveFile(path)) {
            resources->loadClipView(path);
            editor->animStatus_ = "Enregistré : assets/animation/" + view.name + ".sclip";
            editor->animAssetScanTime_ = -1.0;  // force le rescan de la liste
        } else {
            editor->animStatus_ = "Échec d'écriture de " + path;
        }
    }
    if (!canSave) ImGui::EndDisabled();
    if (!project || !project->isLoaded()) {
        ImGui::SameLine();
        ImGui::TextDisabled("(projet requis pour enregistrer)");
    }
}

void AnimationPanel::drawAssetsSection(EditorUI* editor, Animator* animator, ResourceManager* resources,
                       Project* project) {
    if (!ImGui::CollapsingHeader("Assets du projet", ImGuiTreeNodeFlags_DefaultOpen)) return;
    if (!project || !project->isLoaded()) {
        ImGui::TextDisabled("Aucun projet ouvert.");
        return;
    }

    refreshAssetList(editor, project);
    if (ImGui::SmallButton("Rescanner")) editor->animAssetScanTime_ = -1.0;

    if (editor->animAssetFiles_.empty()) {
        ImGui::TextDisabled("Aucun .sclip/.sgraph dans le projet.");
        return;
    }

    for (const std::string& rel : editor->animAssetFiles_) {
        ImGui::PushID(rel.c_str());
        ImGui::TextUnformatted(rel.c_str());
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 130);

        const std::string abs =
            (std::filesystem::path(project->rootPath()) / rel).generic_string();
        const bool isClipView = rel.size() > 6 && rel.rfind(".sclip") == rel.size() - 6;

        if (isClipView) {
            if (ImGui::SmallButton("Jouer")) {
                const AssetID id = resources->loadClipView(abs);
                const ClipView* loaded = resources->getClipView(id);
                if (loaded) {
                    animator->playView(*loaded, 0.0f);
                    editor->animStatus_ = "Vue '" + loaded->name + "' jouée depuis " + rel;
                } else {
                    editor->animStatus_ = "Fichier invalide : " + rel + " (voir le log)";
                }
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Éditer")) {
                const AssetID id = resources->loadClipView(abs);
                if (const ClipView* loaded = resources->getClipView(id)) {
                    std::snprintf(editor->animViewName_, sizeof(editor->animViewName_), "%s",
                                  loaded->name.c_str());
                    editor->animViewStart_ = loaded->effectiveStart();
                    editor->animViewEnd_ = loaded->hasRange ? loaded->rangeEnd : 0.0f;
                    editor->animViewLoop_ = loaded->loop;
                    editor->animViewSpeed_ = loaded->speed;
                    editor->animStatus_ = "Vue '" + loaded->name + "' chargée dans l'éditeur.";
                }
            }
        } else {
            if (ImGui::SmallButton("Appliquer")) {
                const AssetID id = resources->loadAnimGraph(abs);
                const AnimGraphAsset* graph = resources->getAnimGraph(id);
                std::vector<AssetDiagnostic> diags;
                if (graph && animator->setGraph(*graph, &diags)) {
                    editor->animAppliedGraphId_ = id;
                    editor->animStatus_ = "Graphe '" +
                        (graph->name.empty() ? rel : graph->name) + "' appliqué.";
                } else {
                    editor->animStatus_ = "Échec d'application de " + rel +
                        (diags.empty() ? " (voir le log)" : " : " + diags.front().message);
                }
            }
        }
        ImGui::PopID();
    }
}

void AnimationPanel::drawGraphSection(EditorUI* editor, Animator* animator, ResourceManager* resources) {
    const AnimGraphAsset* graph = resources->getAnimGraph(editor->animAppliedGraphId_);
    auto* sm = dynamic_cast<AnimStateMachine*>(animator->rootNode());
    if (!graph || !sm) return;
    if (!ImGui::CollapsingHeader("Graphe actif", ImGuiTreeNodeFlags_DefaultOpen)) return;

    ImGui::Text("Graphe : %s", graph->name.empty() ? "(sans nom)" : graph->name.c_str());
    ImGui::Text("État : %s",
                sm->currentState() ? sm->currentState()->name().c_str() : "(aucun)");
    ImGui::Separator();

    // Les paramètres du graphe pilotent le blackboard en direct.
    for (const auto& p : graph->parameters) {
        ImGui::PushID(p.name.c_str());
        if (p.type == AnimParamType::Bool) {
            bool value = animator->blackboard().getBool(p.name);
            if (ImGui::Checkbox(p.name.c_str(), &value)) animator->setBool(p.name, value);
        } else {
            float value = animator->blackboard().getFloat(p.name);
            if (ImGui::DragFloat(p.name.c_str(), &value, 0.01f)) {
                if (p.type == AnimParamType::Int) value = float(int(value));
                animator->setFloat(p.name, value);
            }
        }
        ImGui::PopID();
    }
}

void AnimationPanel::draw(EditorUI* editor, Scene* scene, ResourceManager* resources,
                          Project* project) {
    if (!editor->showAnimation_) return;

    bool open = editor->showAnimation_;
    if (!ImGui::Begin("Animation", &open)) {
        ImGui::End();
        editor->showAnimation_ = open;
        return;
    }

    FoundAnimator found = findAnimator(editor, scene);
    if (!found.animator || !found.animator->rig()) {
        ImGui::TextDisabled("Aucun Animator dans la scène.");
        ImGui::TextDisabled("Sélectionne un nœud animé ou importe un modèle glTF.");
        ImGui::End();
        editor->showAnimation_ = open;
        return;
    }

    ImGui::Text("Animator : %s", found.nodeName.c_str());
    ImGui::Separator();

    drawPlaybackSection(found.animator);
    drawClipViewSection(editor, found.animator, resources, project);
    drawAssetsSection(editor, found.animator, resources, project);
    drawGraphSection(editor, found.animator, resources);

    if (!editor->animStatus_.empty()) {
        ImGui::Separator();
        ImGui::TextWrapped("%s", editor->animStatus_.c_str());
    }

    ImGui::End();
    editor->showAnimation_ = open;
}

} // namespace saida
