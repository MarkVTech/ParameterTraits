// g++ -std=c++17 -O2 property_traits_demo.cpp -o demo

#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <array>
#include <string_view>
#include <type_traits>
#include <cstring>
#include <iostream>

enum class PropertyID : uint16_t
{
    TemperatureSetpoint,
    DeviceVoltage,
    // add more...
};

struct TemperatureSetpoint
{
    float value;
};

struct DeviceVoltage
{
    int16_t value;
};

// -----------------------------
// Traits (compile-time metadata)
// -----------------------------
enum class StorageKind : uint8_t
{
    // Just a placeholder; we can add other types of storage.
    // Such as Non-Volatile (paired with a DB, eMRAM, or whatever).
    Volatile // In-memory
};

template <typename T>
struct PropertyTraits;

// TemperatureSetpoint specialization
template <>
struct PropertyTraits<TemperatureSetpoint>
{
    using UnderlyingType = float;

    static constexpr std::string_view     name    = "TemperatureSetpoint";
    static constexpr std::string_view     key     = "temperature";
    static constexpr StorageKind          storage = StorageKind::Volatile;
    static constexpr TemperatureSetpoint  default_v { 23.0f };

    static bool validate(const TemperatureSetpoint& t)
    {
        return t.value >= -50.0f && t.value <= 150.0f;
    }

    // Text-side helpers (optional)
    static bool parse(const char* in, TemperatureSetpoint& out)
    {
        if (!in) return false;
        char* end{};
        float v = std::strtof(in, &end);
        if (end == in) return false;
        out.value = v;
        return true;
    }

    static int serialize(const TemperatureSetpoint& in, char* out, size_t n)
    {
        return std::snprintf(out, n, "%.2f", in.value);
    }
};

// DeviceVoltage specialization
template <>
struct PropertyTraits<DeviceVoltage>
{
    using UnderlyingType = int16_t;

    static constexpr std::string_view name    = "DeviceVoltage";
    static constexpr std::string_view key     = "pressure";
    static constexpr StorageKind      storage = StorageKind::Volatile;
    static constexpr DeviceVoltage    default_v { 1013 };

    static bool validate(const DeviceVoltage& p)
    {
        return p.value > 0 && p.value < 20000;
    }

    static bool parse(const char* in, DeviceVoltage& out)
    {
        if (!in) return false;
        char* end{};
        long v = std::strtol(in, &end, 10);
        if (end == in) return false;
        out.value = static_cast<int16_t>(v);
        return true;
    }

    static int serialize(const DeviceVoltage& in, char* out, size_t n)
    {
        return std::snprintf(out, n, "%d", static_cast<int>(in.value));
    }
};

// -------------------------------------
// Runtime handler table (no switches!)
// -------------------------------------
struct PropertyHandler
{
    using ParseFn     = bool(*)(const char*, void*);
    using SerializeFn = int (*)(const void*, char*, size_t);
    using ValidateFn  = bool(*)(const void*);

    std::string_view name;
    std::string_view key;
    size_t           size;
    StorageKind      storage;
    ParseFn          parse;      // may be nullptr if you don't want text I/O
    SerializeFn      serialize;  // "
    ValidateFn       validate;
};

template <typename T>
constexpr PropertyHandler makeHandler()
{
    return {
        PropertyTraits<T>::name,
        PropertyTraits<T>::key,
        sizeof(T),
        PropertyTraits<T>::storage,
        [](const char* in, void* out) -> bool
        {
            return PropertyTraits<T>::parse
                       ? PropertyTraits<T>::parse(in, *static_cast<T*>(out))
                       : false;
        },
        [](const void* in, char* out, size_t n) -> int
        {
            return PropertyTraits<T>::serialize
                       ? PropertyTraits<T>::serialize(*static_cast<const T*>(in), out, n)
                       : -1;
        },
        [](const void* in) -> bool
        {
            return PropertyTraits<T>::validate(*static_cast<const T*>(in));
        }
    };
}

inline constexpr PropertyHandler kPropertyTable[] =
    {
        /* PropertyID::TemperatureSetpoint */ makeHandler<TemperatureSetpoint>(),
        /* PropertyID::DeviceVoltage    */ makeHandler<DeviceVoltage>(),
};

constexpr size_t kPropertyCount = sizeof(kPropertyTable) / sizeof(kPropertyTable[0]);

// Compute max property size at compile time
constexpr size_t compute_max_size()
{
    size_t m = 0;
    for (size_t i = 0; i < kPropertyCount; ++i)
    {
        m = kPropertyTable[i].size > m ? kPropertyTable[i].size : m;
    }
    return m;
}

constexpr size_t kMaxPropertySize = compute_max_size();

// -----------------------------------------
// In-RAM store (no persistence/backends)
// -----------------------------------------
struct PropertyStore
{
    struct Slot
    {
        std::array<std::uint8_t, kMaxPropertySize> buf{};
        size_t sz {0};
        bool   has_value {false};
    };

    std::array<Slot, kPropertyCount> slots{};

    const PropertyHandler& handler(PropertyID id) const
    {
        return kPropertyTable[static_cast<size_t>(id)];
    }

    bool setRaw(PropertyID id, const void* data, size_t sz)
    {
        const auto& h = handler(id);
        if (sz != h.size) return false;

        // validate without depending on alignment
        alignas(16) std::array<std::uint8_t, kMaxPropertySize> tmp{};
        std::memcpy(tmp.data(), data, sz);
        if (!h.validate(tmp.data())) return false;

        auto& s = slots[static_cast<size_t>(id)];
        std::memcpy(s.buf.data(), data, sz);
        s.sz = sz;
        s.has_value = true;
        return true;
    }

    bool getRaw(PropertyID id, void* out, size_t sz) const
    {
        const auto& s = slots[static_cast<size_t>(id)];
        if (!s.has_value || s.sz != sz) return false;
        std::memcpy(out, s.buf.data(), sz);
        return true;
    }

    template <typename T>
    bool set(PropertyID id, const T& v)
    {
        // caller is responsible for matching T to id
        return setRaw(id, &v, sizeof(T));
    }

    template <typename T>
    bool get(PropertyID id, T& out) const
    {
        return getRaw(id, &out, sizeof(T));
    }

    // Optional: set from text using trait parser
    bool setFromText(PropertyID id, const char* text)
    {
        const auto& h = handler(id);
        if (!h.parse) return false;

        // parse into a temp buffer to keep alignment safe
        alignas(16) std::array<std::uint8_t, kMaxPropertySize> tmp{};
        if (!h.parse(text, tmp.data())) return false;
        if (!h.validate(tmp.data())) return false;

        auto& s = slots[static_cast<size_t>(id)];
        std::memcpy(s.buf.data(), tmp.data(), h.size);
        s.sz = h.size;
        s.has_value = true;
        return true;
    }

    // Optional: get as text using trait serializer
    int getAsText(PropertyID id, char* out, size_t n) const
    {
        const auto& h = handler(id);
        const auto& s = slots[static_cast<size_t>(id)];
        if (!h.serialize || !s.has_value) return -1;
        return h.serialize(s.buf.data(), out, n);
    }
};

// -------------------------
// Tiny demo
// -------------------------
int main()
{
    PropertyStore store{};

    // Initialize with defaults
    TemperatureSetpoint t0 = PropertyTraits<TemperatureSetpoint>::default_v;
    DeviceVoltage    p0 = PropertyTraits<DeviceVoltage>::default_v;

    store.set(PropertyID::TemperatureSetpoint, t0);
    store.set(PropertyID::DeviceVoltage,    p0);

    // Override via text (Usefule for /devCLI/UI)
    store.setFromText(PropertyID::TemperatureSetpoint, "37.5");
    store.setFromText(PropertyID::DeviceVoltage,    "1015");

    // Read back
    TemperatureSetpoint t{};
    DeviceVoltage    p{};
    store.get(PropertyID::TemperatureSetpoint, t);
    store.get(PropertyID::DeviceVoltage,    p);

    char buf[64]{};
    int n1 = store.getAsText(PropertyID::TemperatureSetpoint, buf, sizeof(buf));
    std::cout << "TemperatureSetpoint = " << (n1 > 0 ? buf : "(err)") << " (float)\n";
    int n2 = store.getAsText(PropertyID::DeviceVoltage, buf, sizeof(buf));
    std::cout << "DeviceVoltage    = " << (n2 > 0 ? buf : "(err)") << " (int16)\n";

    // Validate guardrail demo (should fail)
    TemperatureSetpoint bad{ -1234.0f };
    bool ok = store.set(PropertyID::TemperatureSetpoint, bad);
    std::cout << "Setting invalid temperature setpoint accepted? " << (ok ? "yes" : "no") << "\n";

    return 0;
}
