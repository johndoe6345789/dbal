/**
 * @file login_route_handler.cpp
 */
#include "login_route_handler.hpp"
#include "../shared/html_attr_escape.hpp"
#include "../shared/login_page_style.hpp"
#include "session_cookie.hpp"

#include <drogon/drogon.h>
#include <spdlog/spdlog.h>

namespace dbal::daemon::handlers::oidc {

namespace {

// Reads a `{"user":"...","pass":"..."}` blob from the clipboard (copied
// from vault.wardcrew.com), fills the form, and submits it -- the single
// central implementation of "Turbologin" now that every app's own login
// page redirects here instead of collecting a password itself.
constexpr const char* kTurboLoginScript = R"JS(
<script>
async function turboLogin() {
  const errEl = document.getElementById('turbo-error');
  errEl.style.display = 'none';
  try {
    const raw = await navigator.clipboard.readText();
    if (!raw.trim()) throw new Error('Clipboard is empty. Copy a Turbologin from vault.wardcrew.com first.');
    let data;
    try { data = JSON.parse(raw); } catch { throw new Error('Clipboard does not contain valid Turbologin JSON.'); }
    if (!data.user || !data.pass) throw new Error('Clipboard JSON is missing required fields (user, pass).');
    document.getElementById('username').value = data.user;
    document.getElementById('password').value = data.pass;
    document.getElementById('login-form').submit();
  } catch (e) {
    errEl.textContent = e.message || 'Could not read clipboard. Please allow clipboard access and try again.';
    errEl.style.display = 'block';
  }
}
</script>
)JS";

std::string renderLoginForm(const std::string& publicPathPrefix, const std::string& continuationToken,
                             const std::string& restartToken, const std::string& error = "",
                             const std::string& notice = "") {
    std::string errorHtml = error.empty() ? "" : "<div class=\"error\" role=\"alert\">" + error + "</div>";
    // status, not alert: an interrupted sign-in is information, and screen
    // readers should not announce it with the urgency of a rejected password.
    std::string noticeHtml = notice.empty() ? "" : "<div class=\"notice\" role=\"status\">" + notice + "</div>";
    using dbal::daemon::handlers::shared::htmlAttrEscape;
    std::string restartHtml = restartToken.empty() ? ""
        : "<input type=\"hidden\" name=\"restart\" value=\"" + htmlAttrEscape(restartToken) + "\">";
    return "<!doctype html><html><head><meta charset=\"utf-8\">"
           "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
           "<title>Sign in</title>" + dbal::daemon::handlers::shared::loginPageStyle() +
           "</head><body>"
           "<div class=\"card\">"
           "<p class=\"brand\">MetaBuilder SSO</p>"
           "<h1>Sign in</h1>" + noticeHtml + errorHtml +
           "<form id=\"login-form\" method=\"POST\" action=\"" + publicPathPrefix + "/oidc/login\">"
           "<input type=\"hidden\" name=\"continuation\" value=\"" + htmlAttrEscape(continuationToken) +
           "\">" + restartHtml +
           "<div class=\"field\"><label for=\"username\">Username</label>"
           "<input id=\"username\" type=\"text\" name=\"username\" autofocus autocomplete=\"username\"></div>"
           "<div class=\"field\"><label for=\"password\">Password</label>"
           "<input id=\"password\" type=\"password\" name=\"password\" autocomplete=\"current-password\"></div>"
           "<button type=\"submit\">Sign in</button>"
           "</form>"
           "<div class=\"divider\">or</div>"
           "<div class=\"error\" id=\"turbo-error\" role=\"alert\" style=\"display:none\"></div>"
           "<button type=\"button\" class=\"turbo\" onclick=\"turboLogin()\">\xE2\x9A\xA1 Turbologin</button>" +
           kTurboLoginScript +
           "<p class=\"footnote\">Signing in via OpenID Connect</p>"
           "</div></body></html>";
}

// Shown only when the flow cannot be resumed at all -- no live continuation
// and no live restart token, so the original /authorize parameters (client_id,
// redirect_uri, code_challenge) are gone and there is nothing to rebuild a
// login form from. Deliberately does not link to /oidc/authorize: called
// without those parameters it answers invalid_request, which would be a worse
// dead end than saying so plainly. The application that started the sign-in is
// the only party that can mint a fresh request.
std::string renderUnresumablePage(const std::string& detail) {
    return "<!doctype html><html><head><meta charset=\"utf-8\">"
           "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
           "<title>Sign-in expired</title>" + dbal::daemon::handlers::shared::loginPageStyle() +
           "</head><body>"
           "<div class=\"card\">"
           "<p class=\"brand\">MetaBuilder SSO</p>"
           "<h1>Sign-in expired</h1>"
           "<div class=\"notice\" role=\"status\">" + detail + "</div>"
           "<p class=\"footnote\">Return to the application and start signing in again.</p>"
           "</div></body></html>";
}
} // namespace

LoginRouteHandler::LoginRouteHandler(dbal::Client& client, dbal::oidc::OidcService& service,
                                      PendingAuthorizeStore& pendingStore,
                                      PendingAuthorizeStore& restartStore,
                                      std::string publicPathPrefix)
    : client_(client), service_(service), pending_store_(pendingStore),
      restart_store_(restartStore), public_path_prefix_(std::move(publicPathPrefix)) {}

void LoginRouteHandler::handleGet(
    const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& cb) const {
    std::string continuation = req->getParameter("continuation");
    std::string restart = req->getParameter("restart");

    // A continuation can go missing because the user sat on the form past its
    // TTL, reloaded a consumed one, or bookmarked this URL. The restart token
    // outlives it and still names the original authorize request, so in every
    // one of those cases a fresh form can be issued instead of a dead end.
    if (continuation.empty() && !restart.empty()) {
        if (auto original = restart_store_.peek(restart)) {
            continuation = pending_store_.store(*original);
        }
    }

    if (continuation.empty()) {
        auto resp = drogon::HttpResponse::newHttpResponse();
        resp->setStatusCode(drogon::k400BadRequest);
        resp->setContentTypeCode(drogon::CT_TEXT_HTML);
        resp->setBody(renderUnresumablePage(
            "This sign-in link is missing the token that identifies which "
            "application you were signing in to, and it could not be recovered."));
        cb(resp);
        return;
    }

    // Set by the POST handler's redirect below; the only value it ever sends.
    std::string notice = req->getParameter("notice") == "session_expired"
        ? "Your sign-in session expired, so we started it again. "
          "Please enter your details below."
        : "";

    auto resp = drogon::HttpResponse::newHttpResponse();
    resp->setContentTypeCode(drogon::CT_TEXT_HTML);
    resp->setBody(renderLoginForm(public_path_prefix_, continuation, restart, "", notice));
    cb(resp);
}

void LoginRouteHandler::handlePost(
    const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& cb) {

    std::string continuation = req->getParameter("continuation");
    std::string restart = req->getParameter("restart");
    std::string username = req->getParameter("username");
    std::string password = req->getParameter("password");

    auto pending = pending_store_.take(continuation);
    if (!pending) {
        // The continuation lapsed (5 min) or was already spent. The restart
        // token still holds the original authorize request, so mint a fresh
        // continuation and send the browser back to the form.
        //
        // 302 to the GET rather than re-rendering the form in this response:
        // the browser would otherwise be sitting on the result of a POST, and
        // reloading -- the reflex when a page says a session expired -- would
        // resubmit the password to a continuation that is already dead,
        // looping the user through this same message.
        if (!restart.empty()) {
            if (auto original = restart_store_.peek(restart)) {
                std::string fresh = pending_store_.store(*original);
                auto resp = drogon::HttpResponse::newHttpResponse();
                resp->setStatusCode(drogon::k302Found);
                resp->addHeader("Location", public_path_prefix_ + "/oidc/login?continuation=" + fresh +
                                                "&restart=" + restart + "&notice=session_expired");
                cb(resp);
                return;
            }
        }
        auto resp = drogon::HttpResponse::newHttpResponse();
        resp->setStatusCode(drogon::k400BadRequest);
        resp->setContentTypeCode(drogon::CT_TEXT_HTML);
        resp->setBody(renderUnresumablePage(
            "Your sign-in session expired and could not be restarted "
            "automatically, because the request that began it has also lapsed."));
        cb(resp);
        return;
    }

    auto verifyResult = client_.verifyCredential(username, password);
    if (verifyResult.isError() || !verifyResult.value()) {
        // Deliberately generic message — don't reveal whether the username exists.
        auto resp = drogon::HttpResponse::newHttpResponse();
        resp->setContentTypeCode(drogon::CT_TEXT_HTML);
        // Re-issue a fresh continuation token for the retry (the old one was consumed).
        std::string retryToken = pending_store_.store(*pending);
        // Carry the restart token through, or a retry that then sits idle past
        // the TTL loses the ability to restart that the first attempt had.
        resp->setBody(renderLoginForm(public_path_prefix_, retryToken, restart,
                                      "Invalid username or password"));
        cb(resp);
        return;
    }

    // Username-as-subject: simple for a credential-only login where there's
    // no separate profile/User linkage required for the "sub" claim.
    const std::string& userId = username;
    // Real tenant, sourced from Credential.tenantId (falls back to "system"
    // for un-migrated rows) -- this is what the multi-tenant JWT-claim
    // cross-check (server_routes.cpp) authorizes against, so it must reflect
    // the actual user, not a fixed default.
    auto tenantResult = client_.getCredentialTenantId(username);
    const std::string tenantId = tenantResult.isError() ? "system" : tenantResult.value();

    auto locationResult = service_.buildAuthorizeRedirect(*pending, userId, tenantId);
    if (locationResult.isError()) {
        spdlog::error("[oidc] Failed to issue authorization code: {}", locationResult.error().what());
        auto resp = drogon::HttpResponse::newHttpResponse();
        resp->setStatusCode(drogon::k500InternalServerError);
        resp->setBody("Failed to complete sign-in");
        cb(resp);
        return;
    }

    auto resp = drogon::HttpResponse::newHttpResponse();
    resp->setStatusCode(drogon::k302Found);
    resp->addHeader("Location", locationResult.value());

    // Browser-level SSO session so a subsequent /authorize call from a
    // *different* client can skip this login form entirely (see
    // OidcRouteHandler::handleAuthorize). Non-fatal if it fails to create --
    // the current login still succeeds, just without carrying over to other
    // apps this round.
    auto sessionResult = service_.createBrowserSession(userId, tenantId);
    if (sessionResult.hasValue()) {
        setSessionCookie(resp, sessionResult.value(), public_path_prefix_, service_.issuer());
    } else {
        spdlog::warn("[oidc] Failed to create browser session: {}", sessionResult.error().what());
    }

    cb(resp);
}

} // namespace dbal::daemon::handlers::oidc
