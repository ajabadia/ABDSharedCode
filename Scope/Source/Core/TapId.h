#pragma once

#include <string>
#include <string_view>
#include <cctype>

namespace abd::scope {

/**
 * ASCII-only lowercase helper (locale-independent, no UTF-8 surprises).
 */
inline std::string toLowerAscii(std::string_view s)
{
    std::string out;
    out.reserve(s.size());
    for (const char c : s)
        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    return out;
}

/**
 * Deterministic wire-protocol slug for a tap display name, e.g.
 *   "Master Out"   -> "master_out"
 *   "Osc 1 (DWGS)" -> "osc_1_dwgs"
 * Runs of non-alphanumeric characters collapse into a single underscore.
 *
 * Hosts that need a custom slug (e.g. "hardware_in") should register an
 * explicit id on the tap (see ScopeTap::setId / ScopeDataCollector::registerTap).
 */
inline std::string makeSlug(std::string_view name)
{
    std::string slug;
    slug.reserve(name.size());
    bool pendingSeparator = false;

    for (const char c : name)
    {
        const unsigned char uc = static_cast<unsigned char>(c);
        if (std::isalnum(uc))
        {
            if (pendingSeparator && !slug.empty())
                slug.push_back('_');
            pendingSeparator = false;
            slug.push_back(static_cast<char>(std::tolower(uc)));
        }
        else
        {
            pendingSeparator = true;
        }
    }

    return slug;
}

} // namespace abd::scope
