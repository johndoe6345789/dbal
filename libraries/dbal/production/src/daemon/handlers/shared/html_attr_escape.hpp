/**
 * @file html_attr_escape.hpp
 * @brief Escaping for values interpolated into HTML attributes on the
 *        server-rendered login pages.
 *
 * The CAS and SAML login handlers each carried a private copy of this; the
 * OIDC one carried none, so its form reflected the continuation token from the
 * query string straight into a hidden input's value. Anything reaching a
 * `value="..."` on those pages comes from a URL a user can be sent, so it is
 * attacker-controlled until escaped, whether or not it later fails a lookup.
 *
 * New call sites should use this header rather than adding a fourth copy. The
 * CAS and SAML duplicates are still in place and can be collapsed onto this.
 */
#pragma once

#include <string>

namespace dbal::daemon::handlers::shared {

inline std::string htmlAttrEscape(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    for (char c : in) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            case '\'': out += "&#39;"; break;
            default: out += c;
        }
    }
    return out;
}

} // namespace dbal::daemon::handlers::shared
