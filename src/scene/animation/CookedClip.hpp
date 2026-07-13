#pragma once

// CookedClip — format runtime compact d'un clip d'animation compilé.
// Les pistes sont liées par indice d'os (aucun nom), les temps sont quantifiés
// sur 16 bits par page, les translations/scales sur 16 bits par composante avec
// bornes par piste, les rotations en smallest-three 48 bits. Un canal constant
// ne stocke qu'une clé. Les données ne sont jamais éditées : elles vivent dans
// le cache dérivé et sont régénérées par AnimationCooker.

#include "scene/animation/AnimationClip.hpp"
#include "scene/animation/Pose.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace saida {

// Une page couvre une tranche temporelle contiguë de la piste : les longues
// séquences gardent ainsi une précision de temps constante, et le sampler
// n'interpole jamais à travers une frontière (la première clé d'une page
// duplique la dernière de la précédente).
struct CookedPage {
    float startTime = 0.0f;
    float timeScale = 0.0f;  // secondes par pas quantifié (0 = clé unique)
    uint32_t firstKey = 0;
    uint32_t keyCount = 0;
};

struct CookedTrack {
    uint16_t boneIndex = 0;
    TrackTarget target = TrackTarget::Translation;
    TrackInterpolation interpolation = TrackInterpolation::Linear;
    uint32_t firstPage = 0;
    uint32_t pageCount = 0;
    // Bornes de déquantification des pistes vec3 (ignorées en rotation).
    glm::vec3 valueMin{0.0f};
    glm::vec3 valueScale{0.0f};
};

// Position de lecture mémorisée d'une piste : la lecture vers l'avant repart
// de la dernière clé trouvée (coût amorti O(1)), un seek retombe sur la
// recherche binaire puis remet le curseur en place.
struct CookedCursor {
    uint32_t page = 0;
    uint32_t key = 0;
};

class CookedClip {
public:
    static constexpr uint32_t kMagic = 0x434E4153;  // "SANC"
    static constexpr uint32_t kVersion = 1;
    static constexpr uint32_t kValuesPerKey = 3;    // u16 par clé (vec3 et quat)
    static constexpr float kTimeSteps = 65535.0f;   // pas de quantification par page

    // Échantillonne toutes les pistes à `time` (secondes, clampé aux clés).
    // `outPose` doit déjà contenir la pose de repos pour les canaux absents.
    // `cursors` (tableau de trackCount() entrées, optionnel) accélère la
    // lecture vers l'avant — l'appelant en possède la mémoire.
    void sample(float time, LocalPose& outPose, CookedCursor* cursors = nullptr) const;

    const std::string& name() const { return name_; }
    float duration() const { return duration_; }
    size_t trackCount() const { return tracks_.size(); }
    size_t keyCount() const { return times_.size(); }
    size_t pageCount() const { return pages_.size(); }
    size_t dataBytes() const;

    std::vector<uint8_t> serialize() const;
    static bool deserialize(const uint8_t* data, size_t size, CookedClip& out,
                            std::string* error = nullptr);

    // Encodage smallest-three : 3 × 15 bits de composantes + 2 bits d'indice de
    // la plus grande composante, répartis dans trois uint16.
    static void quantizeRotation(const glm::quat& q, uint16_t out[3]);
    static glm::quat dequantizeRotation(const uint16_t in[3]);

private:
    friend class AnimationCooker;

    void sampleTrack(const CookedTrack& track, float time,
                     CookedCursor& cursor, Transform& out) const;
    float keyTime(const CookedPage& page, uint32_t key) const;
    glm::vec3 keyVec3(const CookedTrack& track, uint32_t key) const;
    glm::quat keyQuat(uint32_t key) const;

    std::string name_;
    float duration_ = 0.0f;
    std::vector<CookedTrack> tracks_;
    std::vector<CookedPage> pages_;
    std::vector<uint16_t> times_;   // 1 u16 par clé, relatif à sa page
    std::vector<uint16_t> values_;  // kValuesPerKey u16 par clé
};

} // namespace saida
