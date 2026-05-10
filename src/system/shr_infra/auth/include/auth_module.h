/**
 * @file auth_module.h
 * @brief Umbrella header for the Auth & Session subsystem
 *
 * Exports the public AuthManager interface to external code.
 *
 * Internal components (SessionStore, DutyCache, TokenGenerator) are
 * private implementation details and should NOT be included directly.
 *
 * # Public API
 *
 * AuthManager::getInstance()                    — singleton access
 * AuthManager::login()                          — authenticate officer
 * AuthManager::validateToken()                  — validate session on each request
 * AuthManager::validateRank()                   — check permission level
 * AuthManager::isDutyActive()                   — check duty status (cached)
 * AuthManager::refreshSession()                 — reset idle timeout
 * AuthManager::logout()                         — destroy session
 *
 * # Usage Pattern
 *
 * // In os_layer/threading/worker.cpp:
 * #include "shr_infra/auth/include/auth_module.h"
 *
 * SessionContext session;
 * JusticeFlow::ResultCode res =
 *     auth::AuthManager::getInstance().validateToken(token, session);
 *
 * if (res != JusticeFlow::ResultCode::OK) {
 *     // Reject request
 *     return;
 * }
 *
 * // Process request with authenticated session
 *
 * @author Furqan
 */

#pragma once

#include "auth_manager.h"