#pragma once

/**
 * @file socket_request.h
 * @brief Wire types for the JusticeFlow dashboard API gateway protocol.
 *
 * Protocol overview:
 *   All messages are framed as:
 *     [4 bytes LE uint32 = JSON length][JSON bytes]
 *
 *   Request JSON schema:
 *   {
 *     "request_id": <uint32>,          // caller-chosen correlation id
 *     "command":    <string>,           // one of CommandType names below
 *     "params":     { <string>:<string>, ... }  // optional command parameters
 *   }
 *
 *   Response JSON schema:
 *   {
 *     "request_id": <uint32>,          // echoed from request
 *     "status":     <int>,             // JusticeFlow::ResultCode value
 *     "message":    <string>,          // human-readable status / error detail
 *     "data":       <object|null>      // command-specific payload (see below)
 *   }
 *
 * Command catalogue (all read-only from dashboard perspective):
 *
 *   PING
 *     params:  (none)
 *     data:    { "pong": true, "uptime_seconds": <int> }
 *
 *   GET_AGENT_STATUS
 *     params:  (none)
 *     data:    { "agents": [ { "index":0, "name":"...", "status_code":0,
 *                               "error_detail":"...", "last_updated":<epoch> }, ... ] }
 *
 *   GET_AGENT_STATUS_BY_INDEX
 *     params:  { "index": "0"|"1"|"2" }   // 0=hotspot 1=priority 2=workload
 *     data:    { "index":0, "name":"...", "status_code":0,
 *                "error_detail":"...", "last_updated":<epoch> }
 *
 *   GET_ACTIVE_SESSIONS
 *     params:  (none)
 *     data:    { "active_sessions": <int> }
 *
 *   GET_CASE_LIST
 *     params:  { "limit":"100", "offset":"0" }   // optional, defaults shown
 *     data:    { "cases": [ { "case_id":..., "case_type":...,
 *                              "status":..., "registered_at":... }, ... ] }
 *
 *   GET_CASE_BY_ID
 *     params:  { "case_id": "<uuid-string>" }
 *     data:    { "case_id":..., "case_type":..., "status":..., ... }
 *
 *   GET_HOTSPOT_DATA
 *     params:  { "limit":"50" }                  // optional
 *     data:    { "hotspots": [ { "zone":..., "risk_level":...,
 *                                 "incident_count":..., "updated_at":... }, ... ] }
 *
 *   GET_PRIORITY_CASES
 *     params:  { "min_priority":"HIGH", "limit":"50" }   // optional
 *     data:    { "cases": [ ... ] }
 *
 *   GET_OFFICER_WORKLOAD
 *     params:  { "limit":"50" }                  // optional
 *     data:    { "officers": [ { "officer_id":..., "name":...,
 *                                 "rank":..., "active_cases":...,
 *                                 "duty_status":... }, ... ] }
 */

#include <cstdint>
#include <string>
#include <unordered_map>

#include "../../../common/constants.h"

namespace ipc
{

    // ────────────────────────────────────────────────────────────────────────────
    //  CommandType  –  the set of operations the dashboard may request
    // ────────────────────────────────────────────────────────────────────────────
    enum class CommandType
    {
        // ── System ──────────────────────────────────────────────────────────────
        PING, ///< Health-check / round-trip test

        // ── AI agent status (from SharedMemory) ─────────────────────────────────
        GET_AGENT_STATUS,          ///< All three agents' statuses
        GET_AGENT_STATUS_BY_INDEX, ///< One agent by index (0/1/2)

        // ── Session info (from SharedMemory) ────────────────────────────────────
        GET_ACTIVE_SESSIONS, ///< active_sessions counter from shared memory

        // ── Database read-only queries ───────────────────────────────────────────
        GET_CASE_LIST,        ///< Paginated case summary list
        GET_CASE_BY_ID,       ///< Single case by UUID
        GET_HOTSPOT_DATA,     ///< Crime hotspot analytics rows
        GET_PRIORITY_CASES,   ///< Cases filtered by minimum priority level
        GET_OFFICER_WORKLOAD, ///< Officers with workload stats

        // ── Sentinel ─────────────────────────────────────────────────────────────
        UNKNOWN ///< Unrecognised command string
    };

    // ────────────────────────────────────────────────────────────────────────────
    //  SocketRequest
    // ────────────────────────────────────────────────────────────────────────────
    struct SocketRequest
    {
        uint32_t request_id{0};
        CommandType command{CommandType::UNKNOWN};

        /// Optional key→value parameters; all values arrive as strings.
        /// Numeric params (e.g. "limit", "index") must be validated by dispatch.
        std::unordered_map<std::string, std::string> params;

        // ── Factory ─────────────────────────────────────────────────────────────

        /**
         * @brief Parses a JSON string into a SocketRequest.
         *
         * Accepts:
         *   { "request_id": 42, "command": "GET_CASE_BY_ID",
         *     "params": { "case_id": "some-uuid" } }
         *
         * The "params" key is optional; "request_id" defaults to 0 if absent.
         * An unrecognised "command" value produces CommandType::UNKNOWN (not an
         * error at this layer — the dispatcher will reject UNKNOWN commands).
         *
         * @param json_str  Raw JSON string from the wire.
         * @param out       Output SocketRequest populated on success.
         * @return ResultCode::OK           on success (even for UNKNOWN command).
         *         ResultCode::INVALID_INPUT  if json_str is not valid JSON or
         *                                    structurally malformed.
         */
        static JusticeFlow::ResultCode fromJson(const std::string &json_str,
                                                SocketRequest &out);

        /**
         * @brief Convenience: look up a param by key with a fallback default.
         * @return params[key] if present, otherwise default_val.
         */
        std::string param(const std::string &key,
                          const std::string &default_val = "") const;

        /**
         * @brief Parses an integer param, returning default_val on parse failure
         *        or if the key is absent.
         */
        int intParam(const std::string &key, int default_val = 0) const;
    };

    // ────────────────────────────────────────────────────────────────────────────
    //  SocketResponse
    // ────────────────────────────────────────────────────────────────────────────
    struct SocketResponse
    {
        uint32_t request_id{0};
        JusticeFlow::ResultCode status{JusticeFlow::ResultCode::OK};
        std::string message; ///< Human-readable status / error
        std::string payload; ///< JSON string to embed under "data"

        // ── Serialisation ────────────────────────────────────────────────────────

        /**
         * @brief Serialises the response to a JSON string ready for the wire.
         *
         * The "data" field is:
         *   - null                  if payload is empty
         *   - parsed JSON object    if payload is valid JSON
         *   - a JSON string         if payload is not valid JSON (fallback)
         *
         * @return UTF-8 JSON string.
         */
        std::string toJson() const;

        // ── Factories ────────────────────────────────────────────────────────────

        /**
         * @brief Constructs a success response.
         * @param req_id  Echoed request_id.
         * @param payload JSON string to embed in "data" (may be "{}").
         */
        static SocketResponse ok(uint32_t req_id,
                                 const std::string &payload);

        /**
         * @brief Constructs an error response.
         * @param req_id  Echoed request_id.
         * @param code    Non-OK ResultCode describing the failure category.
         * @param msg     Human-readable explanation for the dashboard.
         */
        static SocketResponse error(uint32_t req_id,
                                    JusticeFlow::ResultCode code,
                                    const std::string &msg);
    };

    // ────────────────────────────────────────────────────────────────────────────
    //  Utility: result code → string (for JSON "message" fields)
    // ────────────────────────────────────────────────────────────────────────────

    /**
     * @brief Returns a short English description of a ResultCode.
     *        Used when building error responses to avoid magic integers in logs.
     */
    const char *resultCodeToString(JusticeFlow::ResultCode code);

} // namespace ipc