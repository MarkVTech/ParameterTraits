// g++ -std=c++17 -O2 parameters_min.cpp -o demo

#include <cstdint>
#include <cstddef>
#include <string_view>
#include <cstdio>
#include <cstdlib>
#include <iostream>

// --------------------
// Parameter identities
// --------------------
enum class ParameterID : uint16_t
{
    TemperatureSetpoint,
    HighTemperatureAlarm
};

// --------------------
// Parameter types
// --------------------
struct TemperatureSetpoint
{
    float value;
};

struct HighTemperatureAlarm
{
    float threshold;
};

// --------------------
// ParameterTraits<T>
// --------------------
template <typename T>
struct ParameterTraits;

// TemperatureSetpoint
template <>
struct ParameterTraits<TemperatureSetpoint>
{
    using UnderlyingType = float;

    static constexpr std::string_view name = "TemperatureSetpoint";
    static constexpr TemperatureSetpoint default_v { 37.5f };

    static bool validate(const TemperatureSetpoint& x)
    {
        return x.value >= 0.0f && x.value <= 100.0f;
    }

    static bool parse(const char* in, TemperatureSetpoint& out)
    {
        if (!in) return false;
        char* end{};
        float v = std::strtof(in, &end);
        if (end == in) return false;
        out.value = v;
        return validate(out);
    }

    static int serialize(const TemperatureSetpoint& x, char* out, size_t n)
    {
        return std::snprintf(out, n, "%.2f", x.value);
    }
};

// HighTemperatureAlarm
template <>
struct ParameterTraits<HighTemperatureAlarm>
{
    using UnderlyingType = float;

    static constexpr std::string_view name = "HighTemperatureAlarm";
    static constexpr HighTemperatureAlarm default_v { 80.0f };

    static bool validate(const HighTemperatureAlarm& x)
    {
        return x.threshold >= 0.0f && x.threshold <= 150.0f;
    }

    static bool parse(const char* in, HighTemperatureAlarm& out)
    {
        if (!in) return false;
        char* end{};
        float v = std::strtof(in, &end);
        if (end == in) return false;
        out.threshold = v;
        return validate(out);
    }

    static int serialize(const HighTemperatureAlarm& x, char* out, size_t n)
    {
        return std::snprintf(out, n, "%.2f", x.threshold);
    }
};

// --------------------
// Simple demo main
// --------------------
int main()
{
    // Defaults
    TemperatureSetpoint sp = ParameterTraits<TemperatureSetpoint>::default_v;
    HighTemperatureAlarm hi = ParameterTraits<HighTemperatureAlarm>::default_v;

    // Parse from text (like CLI input)
    ParameterTraits<TemperatureSetpoint>::parse("42.0", sp);
    ParameterTraits<HighTemperatureAlarm>::parse("85.5", hi);

    // Validate
    std::cout << ParameterTraits<TemperatureSetpoint>::name
              << " valid? " << (ParameterTraits<TemperatureSetpoint>::validate(sp) ? "yes" : "no") << "\n";
    std::cout << ParameterTraits<HighTemperatureAlarm>::name
              << " valid? " << (ParameterTraits<HighTemperatureAlarm>::validate(hi) ? "yes" : "no") << "\n";

    // Serialize to text
    char buf[32];
    int n1 = ParameterTraits<TemperatureSetpoint>::serialize(sp, buf, sizeof(buf));
    std::cout << "Setpoint: " << (n1 > 0 ? buf : "(err)") << "\n";
    int n2 = ParameterTraits<HighTemperatureAlarm>::serialize(hi, buf, sizeof(buf));
    std::cout << "High alarm: " << (n2 > 0 ? buf : "(err)") << "\n";

    // Show validation failure
    TemperatureSetpoint bad{ -10.0f };
    std::cout << "Bad setpoint valid? "
              << (ParameterTraits<TemperatureSetpoint>::validate(bad) ? "yes" : "no") << "\n";

    return 0;
}
