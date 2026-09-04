#pragma once

#include <string>

/**
 * What build is actually running.
 *
 * The version used to be a string literal repeated in four files, which
 * answered "which release is this meant to be" and not "which code is
 * running" -- so a deploy that silently kept an old image looked identical
 * to one that landed. DBAL_GIT_COMMIT and DBAL_BUILT_AT are injected by
 * CMake at build time; when they are absent (a local build straight from
 * the source tree) they read "unknown", which is honest rather than
 * misleading.
 */
namespace dbal {

inline constexpr const char* kServiceName = "DBAL Daemon";
inline constexpr const char* kVersion = "1.2.1";

inline const char* gitCommit() {
#ifdef DBAL_GIT_COMMIT
    return DBAL_GIT_COMMIT;
#else
    return "unknown";
#endif
}

inline const char* builtAt() {
#ifdef DBAL_BUILT_AT
    return DBAL_BUILT_AT;
#else
    return "unknown";
#endif
}

}  // namespace dbal
