#pragma once

// ─────────────────────────────────────────────────────────────────────────────
// NextEngine reflection — the "API the LLM reads instead of the source".
//
// A behaviour/node declares its fields ONCE in a static `describe()`:
//
//     void RotatorBehaviour::describe(ne::reflect::TypeBuilder<RotatorBehaviour>& t) {
//         t.doc("Spins its node around a local axis.");
//         t.property("axis",  &RotatorBehaviour::axis);
//         t.property("speed", &RotatorBehaviour::speed).range(0, 360).tooltip("deg/s");
//         t.signal("fullRotation", &RotatorBehaviour::fullRotation);   // named Signal<>
//         t.slot("reset", &RotatorBehaviour::reset);                   // invocable method
//     }
//
// From that single declaration we get, for free:
//   • automatic save()/load() (no hand-written JSON boilerplate),
//   • a compact machine-readable manifest (`TypeRegistry::manifest()`),
//   • named, data-wireable signals/slots (used by the M2 SignalWiring layer).
//
// Header-only descriptors via member pointers — no external codegen step (keeps
// the MSYS2 toolchain happy). This header pulls in nlohmann/json, so it is meant
// to be included by leaf gameplay .cpp files (and the few headers that use the
// NE_REFLECT_* macros), never by widely-shared engine headers.
// ─────────────────────────────────────────────────────────────────────────────

#include "core/ReflectionFwd.hpp"
#include "core/Signal.hpp"

#include <nlohmann/json.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <functional>
#include <memory>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace ne::reflect {

using json = nlohmann::json;

// ── value <-> json conversion + a manifest "kind" name ───────────────────────
template <typename M, typename Enable = void>
struct Traits {
    static const char* kind() {
        if constexpr (std::is_same_v<M, bool>) return "bool";
        else if constexpr (std::is_floating_point_v<M>) return "float";
        else if constexpr (std::is_integral_v<M>) return "int";
        else if constexpr (std::is_same_v<M, std::string>) return "string";
        else return "json";
    }
    static void to(json& j, const M& v) { j = v; }
    static void from(const json& j, M& v) { v = j.get<M>(); }
};

template <typename E>
struct Traits<E, std::enable_if_t<std::is_enum_v<E>>> {
    static const char* kind() { return "enum"; }
    static void to(json& j, const E& v) { j = static_cast<int>(v); }
    static void from(const json& j, E& v) { v = static_cast<E>(j.get<int>()); }
};

template <>
struct Traits<glm::vec3> {
    static const char* kind() { return "vec3"; }
    static void to(json& j, const glm::vec3& v) { j = json::array({v.x, v.y, v.z}); }
    static void from(const json& j, glm::vec3& v) {
        if (j.is_array() && j.size() == 3)
            v = {j[0].get<float>(), j[1].get<float>(), j[2].get<float>()};
    }
};

template <>
struct Traits<glm::vec4> {
    static const char* kind() { return "vec4"; }
    static void to(json& j, const glm::vec4& v) { j = json::array({v.x, v.y, v.z, v.w}); }
    static void from(const json& j, glm::vec4& v) {
        if (j.is_array() && j.size() == 4)
            v = {j[0].get<float>(), j[1].get<float>(), j[2].get<float>(), j[3].get<float>()};
    }
};

template <>
struct Traits<glm::quat> {
    static const char* kind() { return "quat"; }
    // stored x,y,z,w (matches the transform.rotation convention in the serializer)
    static void to(json& j, const glm::quat& v) { j = json::array({v.x, v.y, v.z, v.w}); }
    static void from(const json& j, glm::quat& v) {
        if (j.is_array() && j.size() == 4)
            v = glm::quat(j[3].get<float>(), j[0].get<float>(), j[1].get<float>(), j[2].get<float>());
    }
};

// ── argument packing for signals/slots (JSON-friendly payloads) ──────────────
template <typename V>
inline void pushArg(json& arr, const V& v) {
    if constexpr (std::is_same_v<V, bool>) arr.push_back(v);
    else if constexpr (std::is_arithmetic_v<V>) arr.push_back(v);
    else if constexpr (std::is_enum_v<V>) arr.push_back(static_cast<long long>(v));
    else if constexpr (std::is_same_v<V, std::string>) arr.push_back(v);
    else arr.push_back(nullptr);  // pointers / structs carry no JSON payload
}

template <typename A>
inline std::decay_t<A> getArg(const json& arr, std::size_t i) {
    using T = std::decay_t<A>;
    if (i >= arr.size()) return T{};
    const json& e = arr[i];
    if constexpr (std::is_same_v<T, bool>) return e.is_boolean() ? e.get<bool>() : T{};
    else if constexpr (std::is_arithmetic_v<T>) return e.is_number() ? e.get<T>() : T{};
    else if constexpr (std::is_enum_v<T>) return e.is_number() ? static_cast<T>(e.get<int>()) : T{};
    else if constexpr (std::is_same_v<T, std::string>) return e.is_string() ? e.get<std::string>() : T{};
    else return T{};
}

// ── descriptors ──────────────────────────────────────────────────────────────
struct PropertyDesc {
    std::string name;
    std::string kind;       // "float" | "int" | "bool" | "string" | "vec3" | ... | "asset"
    std::string tooltip;
    bool hasRange = false;
    double min = 0.0;
    double max = 0.0;
    std::vector<std::string> enumLabels;  // for kind == "enum"
    std::function<void(const void*, json&)> get;
    std::function<void(void*, const json&)> set;
};

struct SignalDesc {
    std::string name;
    int arity = 0;
    // Subscribe `sink` (receives a JSON array of the emitted args) to this object's
    // signal; returns a lifetime-managed Connection. Filled by TypeBuilder::signal.
    std::function<Connection(void*, std::function<void(const json&)>)> connect;
    // Fire the signal with a JSON array of args (value-typed args only; pointer
    // args receive defaults). Filled by TypeBuilder::signal.
    std::function<void(void*, const json&)> emit;
};

struct SlotDesc {
    std::string name;
    int arity = 0;
    // Invoke the method with a JSON array of args. Filled by TypeBuilder::slot.
    std::function<void(void*, const json&)> invoke;
};

// Fluent handle returned by property() for optional metadata.
struct PropertyRef {
    PropertyDesc* d = nullptr;
    PropertyRef& range(double lo, double hi) { d->hasRange = true; d->min = lo; d->max = hi; return *this; }
    PropertyRef& tooltip(std::string s) { d->tooltip = std::move(s); return *this; }
    PropertyRef& asset() { d->kind = "asset"; return *this; }
    PropertyRef& enumValues(std::vector<std::string> labels) { d->enumLabels = std::move(labels); return *this; }
};

struct TypeDesc {
    std::string name;
    std::string category;  // "behaviour" | "node"
    std::string doc;
    std::vector<PropertyDesc> properties;
    std::vector<SignalDesc> signals;
    std::vector<SlotDesc> slots;

    const PropertyDesc* findProperty(const std::string& n) const {
        for (const auto& p : properties) if (p.name == n) return &p;
        return nullptr;
    }
    const SignalDesc* findSignal(const std::string& n) const {
        for (const auto& s : signals) if (s.name == n) return &s;
        return nullptr;
    }
    const SlotDesc* findSlot(const std::string& n) const {
        for (const auto& s : slots) if (s.name == n) return &s;
        return nullptr;
    }

    // Write every property into `j` (top-level keys, matching the legacy format).
    void saveTo(const void* obj, json& j) const {
        for (const auto& p : properties) { json v; p.get(obj, v); j[p.name] = std::move(v); }
    }
    // Read every property that is present in `j` (missing keys keep defaults).
    void loadFrom(void* obj, const json& j) const {
        for (const auto& p : properties) {
            auto it = j.find(p.name);
            if (it != j.end()) p.set(obj, *it);
        }
    }

    json manifest() const;  // defined in Reflection.cpp
};

// ── builder ──────────────────────────────────────────────────────────────────
template <typename T>
class TypeBuilder {
public:
    explicit TypeBuilder(TypeDesc& d) : d_(d) {}

    TypeBuilder& doc(std::string s) { d_.doc = std::move(s); return *this; }

    template <typename M>
    PropertyRef property(std::string name, M T::*ptr) {
        PropertyDesc pd;
        pd.name = std::move(name);
        pd.kind = Traits<M>::kind();
        pd.get = [ptr](const void* o, json& j) { Traits<M>::to(j, static_cast<const T*>(o)->*ptr); };
        pd.set = [ptr](void* o, const json& j) { Traits<M>::from(j, static_cast<T*>(o)->*ptr); };
        d_.properties.push_back(std::move(pd));
        return PropertyRef{&d_.properties.back()};
    }

    template <typename... A>
    void signal(std::string name, Signal<A...> T::*ptr) {
        SignalDesc s;
        s.name = std::move(name);
        s.arity = static_cast<int>(sizeof...(A));
        s.connect = [ptr](void* o, std::function<void(const json&)> sink) -> Connection {
            T* t = static_cast<T*>(o);
            return (t->*ptr).connect([sink = std::move(sink)](A... args) {
                json arr = json::array();
                (pushArg(arr, args), ...);
                sink(arr);
            });
        };
        s.emit = [ptr](void* o, const json& arr) {
            T* t = static_cast<T*>(o);
            emitImpl(t, ptr, arr, std::index_sequence_for<A...>{});
        };
        d_.signals.push_back(std::move(s));
    }

    template <typename... A>
    void slot(std::string name, void (T::*method)(A...)) {
        SlotDesc s;
        s.name = std::move(name);
        s.arity = static_cast<int>(sizeof...(A));
        s.invoke = [method](void* o, const json& arr) {
            T* t = static_cast<T*>(o);
            invokeImpl(t, method, arr, std::index_sequence_for<A...>{});
        };
        d_.slots.push_back(std::move(s));
    }

private:
    template <typename... A, std::size_t... I>
    static void invokeImpl(T* t, void (T::*method)(A...), const json& arr, std::index_sequence<I...>) {
        (t->*method)(getArg<A>(arr, I)...);
    }

    template <typename... A, std::size_t... I>
    static void emitImpl(T* t, Signal<A...> T::*ptr, const json& arr, std::index_sequence<I...>) {
        (t->*ptr).emit(getArg<A>(arr, I)...);
    }

    TypeDesc& d_;
};

// ── global registry (the manifest source) ────────────────────────────────────
class TypeRegistry {
public:
    static TypeRegistry& instance();

    // Insert or replace a type entry; returns a stable reference.
    TypeDesc& add(const std::string& name);
    const TypeDesc* find(const std::string& name) const;

    // Compact manifest. Empty filter = everything; otherwise "behaviour"/"node".
    json manifest(const std::string& categoryFilter = "") const;
    json manifestFor(const std::string& typeName) const;

private:
    std::unordered_map<std::string, std::unique_ptr<TypeDesc>> types_;
};

// ── per-type cached descriptor (used by auto save/load, registry-independent) ─
template <typename T>
const TypeDesc& localDesc() {
    static const TypeDesc desc = [] {
        TypeDesc d;
        TypeBuilder<T> b(d);
        T::describe(b);
        return d;
    }();
    return desc;
}

template <typename T>
void saveObject(const T& obj, json& j) { localDesc<T>().saveTo(&obj, j); }

template <typename T>
void loadObject(T& obj, const json& j) { localDesc<T>().loadFrom(&obj, j); }

} // namespace ne::reflect

// ─────────────────────────────────────────────────────────────────────────────
// Macros placed in a class body. They wire typeName() + auto serialization to the
// reflected descriptor. Put them in the class's *header* and define describe() in
// the .cpp (which includes this header).
// ─────────────────────────────────────────────────────────────────────────────
#define NE_REFLECT_BEHAVIOUR(Type, NameStr)                                        \
public:                                                                             \
    static constexpr const char* reflectName() { return NameStr; }                  \
    const char* typeName() const override { return NameStr; }                       \
    static void describe(ne::reflect::TypeBuilder<Type>& t);                         \
    void save(nlohmann::json& j) const override { ne::reflect::saveObject(*this, j); } \
    void load(const nlohmann::json& j) override { ne::reflect::loadObject(*this, j); }

#define NE_REFLECT_NODE(Type, NameStr)                                              \
public:                                                                             \
    static constexpr const char* reflectName() { return NameStr; }                  \
    const char* typeName() const override { return NameStr; }                       \
    static void describe(ne::reflect::TypeBuilder<Type>& t);                         \
    void serialize(nlohmann::json& j, ne::ResourceManager& r) const override {       \
        ne::Node::serialize(j, r);                                                   \
        ne::reflect::saveObject(*this, j);                                           \
    }                                                                                \
    void deserialize(const nlohmann::json& j, ne::ResourceManager& r) override {     \
        ne::Node::deserialize(j, r);                                                 \
        ne::reflect::loadObject(*this, j);                                           \
    }
