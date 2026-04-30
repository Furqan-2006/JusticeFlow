/**
 * @file socket_request.cpp
 * @brief Request/response serialisation for the JusticeFlow dashboard gateway.
 *
 * JSON parsing strategy:
 *   This file intentionally avoids external JSON libraries to keep the build
 *   self-contained.  The incoming request schema is small and fixed, so a
 *   minimal hand-written parser is both faster and safer than pulling in a
 *   large dependency.  Responses are assembled via std::ostringstream; the
 *   payload field is always a pre-validated JSON string from the dispatch
 *   layer, so we embed it verbatim without re-parsing.
 *
 * If the project later adopts nlohmann/json project-wide, replace the
 * parseJson() helper and fromJson() body — the public API stays the same.
 */

#include "../include/socket_request.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>

namespace ipc
{

    // ────────────────────────────────────────────────────────────────────────────
    //  Minimal JSON helpers (request parsing only)
    // ────────────────────────────────────────────────────────────────────────────

    namespace
    {
        // Skip whitespace in-place.
        void skipWs(const std::string &s, size_t &pos)
        {
            while (pos < s.size() && std::isspace(static_cast<unsigned char>(s[pos])))
                ++pos;
        }

        // Expect a specific character; advance pos past it.
        // Throws std::runtime_error on mismatch.
        void expect(const std::string &s, size_t &pos, char ch)
        {
            skipWs(s, pos);
            if (pos >= s.size() || s[pos] != ch)
                throw std::runtime_error(std::string("expected '") + ch + "'");
            ++pos;
        }

        // Parse a JSON string value (must start at opening '"').
        // Returns the unescaped string; handles \" and \\ only (sufficient here).
        std::string parseString(const std::string &s, size_t &pos)
        {
            skipWs(s, pos);
            expect(s, pos, '"');
            std::string result;
            result.reserve(64);
            while (pos < s.size() && s[pos] != '"')
            {
                if (s[pos] == '\\' && pos + 1 < s.size())
                {
                    ++pos;
                    switch (s[pos])
                    {
                    case '"':
                        result += '"';
                        break;
                    case '\\':
                        result += '\\';
                        break;
                    case 'n':
                        result += '\n';
                        break;
                    case 'r':
                        result += '\r';
                        break;
                    case 't':
                        result += '\t';
                        break;
                    default:
                        result += s[pos];
                        break;
                    }
                }
                else
                {
                    result += s[pos];
                }
                ++pos;
            }
            expect(s, pos, '"'); // consume closing quote
            return result;
        }

        // Parse a JSON number or boolean as a string (for "request_id").
        std::string parsePrimitive(const std::string &s, size_t &pos)
        {
            skipWs(s, pos);
            size_t start = pos;
            while (pos < s.size() && s[pos] != ',' && s[pos] != '}' &&
                   s[pos] != ']' && !std::isspace(static_cast<unsigned char>(s[pos])))
                ++pos;
            return s.substr(start, pos - start);
        }

        // Parse a flat JSON object { "key":"val", ... } where all values are
        // strings or simple primitives.  Returns key→raw-value map.
        // Throws on structural errors.
        std::unordered_map<std::string, std::string>
        parseObject(const std::string &s, size_t &pos)
        {
            std::unordered_map<std::string, std::string> obj;
            skipWs(s, pos);
            expect(s, pos, '{');
            skipWs(s, pos);
            if (pos < s.size() && s[pos] == '}')
            {
                ++pos;
                return obj;
            }

            while (pos < s.size())
            {
                std::string key = parseString(s, pos);
                skipWs(s, pos);
                expect(s, pos, ':');
                skipWs(s, pos);

                std::string val;
                if (s[pos] == '"')
                    val = parseString(s, pos);
                else
                    val = parsePrimitive(s, pos);

                obj[key] = val;

                skipWs(s, pos);
                if (pos < s.size() && s[pos] == ',')
                {
                    ++pos;
                    skipWs(s, pos);
                    continue;
                }
                break;
            }
            expect(s, pos, '}');
            return obj;
        }

        // Convert command string from the wire to CommandType enum.
        CommandType toCommandType(const std::string &cmd)
        {
            if (cmd == "PING")
                return CommandType::PING;
            if (cmd == "GET_AGENT_STATUS")
                return CommandType::GET_AGENT_STATUS;
            if (cmd == "GET_AGENT_STATUS_BY_INDEX")
                return CommandType::GET_AGENT_STATUS_BY_INDEX;
            if (cmd == "GET_ACTIVE_SESSIONS")
                return CommandType::GET_ACTIVE_SESSIONS;
            if (cmd == "GET_CASE_LIST")
                return CommandType::GET_CASE_LIST;
            if (cmd == "GET_CASE_BY_ID")
                return CommandType::GET_CASE_BY_ID;
            if (cmd == "GET_HOTSPOT_DATA")
                return CommandType::GET_HOTSPOT_DATA;
            if (cmd == "GET_PRIORITY_CASES")
                return CommandType::GET_PRIORITY_CASES;
            if (cmd == "GET_OFFICER_WORKLOAD")
                return CommandType::GET_OFFICER_WORKLOAD;
            return CommandType::UNKNOWN;
        }

        // Escape a plain string so it is safe to embed in a JSON string literal.
        std::string jsonEscape(const std::string &s)
        {
            std::string out;
            out.reserve(s.size() + 8);
            for (unsigned char c : s)
            {
                switch (c)
                {
                case '"':
                    out += "\\\"";
                    break;
                case '\\':
                    out += "\\\\";
                    break;
                case '\n':
                    out += "\\n";
                    break;
                case '\r':
                    out += "\\r";
                    break;
                case '\t':
                    out += "\\t";
                    break;
                default:
                    if (c < 0x20)
                    {
                        // Control characters: emit \uXXXX
                        char buf[8];
                        snprintf(buf, sizeof(buf), "\\u%04x", c);
                        out += buf;
                    }
                    else
                    {
                        out += static_cast<char>(c);
                    }
                    break;
                }
            }
            return out;
        }

        // Quick structural check: does s look like a JSON object or array?
        bool looksLikeJson(const std::string &s)
        {
            for (char c : s)
            {
                if (std::isspace(static_cast<unsigned char>(c)))
                    continue;
                return (c == '{' || c == '[');
            }
            return false;
        }

    } // anonymous namespace

    // ────────────────────────────────────────────────────────────────────────────
    //  SocketRequest implementation
    // ────────────────────────────────────────────────────────────────────────────

    JusticeFlow::ResultCode SocketRequest::fromJson(const std::string &json_str,
                                                    SocketRequest &out)
    {
        try
        {
            size_t pos = 0;
            auto top = parseObject(json_str, pos);

            // request_id (optional, defaults to 0)
            if (top.count("request_id"))
            {
                out.request_id = static_cast<uint32_t>(
                    std::stoul(top.at("request_id")));
            }

            // command (required; UNKNOWN is accepted — dispatcher will reject it)
            if (top.count("command"))
                out.command = toCommandType(top.at("command"));
            else
                out.command = CommandType::UNKNOWN;

            // params (optional nested object)
            // Re-parse the original string for the "params" sub-object.
            {
                const std::string needle = "\"params\"";
                size_t ppos = json_str.find(needle);
                if (ppos != std::string::npos)
                {
                    ppos += needle.size();
                    // Find the colon
                    size_t colon = json_str.find(':', ppos);
                    if (colon != std::string::npos)
                    {
                        size_t obj_start = colon + 1;
                        // Skip whitespace to '{'
                        while (obj_start < json_str.size() &&
                               std::isspace(static_cast<unsigned char>(json_str[obj_start])))
                            ++obj_start;

                        if (obj_start < json_str.size() && json_str[obj_start] == '{')
                            out.params = parseObject(json_str, obj_start);
                    }
                }
            }

            return JusticeFlow::ResultCode::OK;
        }
        catch (const std::exception &)
        {
            return JusticeFlow::ResultCode::INVALID_INPUT;
        }
    }

    std::string SocketRequest::param(const std::string &key,
                                     const std::string &default_val) const
    {
        auto it = params.find(key);
        return (it != params.end()) ? it->second : default_val;
    }

    int SocketRequest::intParam(const std::string &key, int default_val) const
    {
        auto it = params.find(key);
        if (it == params.end())
            return default_val;
        try
        {
            return std::stoi(it->second);
        }
        catch (...)
        {
            return default_val;
        }
    }

    // ────────────────────────────────────────────────────────────────────────────
    //  SocketResponse implementation
    // ────────────────────────────────────────────────────────────────────────────

    std::string SocketResponse::toJson() const
    {
        std::ostringstream oss;
        oss << '{'
            << "\"request_id\":" << request_id << ','
            << "\"status\":" << static_cast<int>(status) << ','
            << "\"message\":\"" << jsonEscape(message) << "\","
            << "\"data\":";

        if (payload.empty())
        {
            oss << "null";
        }
        else if (looksLikeJson(payload))
        {
            // Embed pre-built JSON verbatim (no re-parsing overhead).
            oss << payload;
        }
        else
        {
            // Fallback: wrap in a string literal so the response is always valid JSON.
            oss << '"' << jsonEscape(payload) << '"';
        }

        oss << '}';
        return oss.str();
    }

    SocketResponse SocketResponse::ok(uint32_t req_id,
                                      const std::string &payload)
    {
        SocketResponse r;
        r.request_id = req_id;
        r.status = JusticeFlow::ResultCode::OK;
        r.message = "OK";
        r.payload = payload;
        return r;
    }

    SocketResponse SocketResponse::error(uint32_t req_id,
                                         JusticeFlow::ResultCode code,
                                         const std::string &msg)
    {
        SocketResponse r;
        r.request_id = req_id;
        r.status = code;
        r.message = msg;
        // payload intentionally left empty for errors
        return r;
    }

    // ────────────────────────────────────────────────────────────────────────────
    //  resultCodeToString
    // ────────────────────────────────────────────────────────────────────────────

    const char *resultCodeToString(JusticeFlow::ResultCode code)
    {
        using RC = JusticeFlow::ResultCode;
        switch (code)
        {
        case RC::OK:
            return "OK";
        case RC::AUTH_FAILED:
            return "AUTH_FAILED";
        case RC::RANK_INSUFFICIENT:
            return "RANK_INSUFFICIENT";
        case RC::SESSION_EXPIRED:
            return "SESSION_EXPIRED";
        case RC::JURISDICTION_DENIED:
            return "JURISDICTION_DENIED";
        case RC::NOT_FOUND:
            return "NOT_FOUND";
        case RC::ALREADY_EXISTS:
            return "ALREADY_EXISTS";
        case RC::INVALID_INPUT:
            return "INVALID_INPUT";
        case RC::INVALID_ARGUMENT:
            return "INVALID_ARGUMENT";
        case RC::DB_ERROR:
            return "DB_ERROR";
        case RC::FOREIGN_KEY_VIOLATION:
            return "FOREIGN_KEY_VIOLATION";
        case RC::FILE_SYSTEM_ERROR:
            return "FILE_SYSTEM_ERROR";
        case RC::INVALID_STATE:
            return "INVALID_STATE";
        case RC::DUTY_INACTIVE:
            return "DUTY_INACTIVE";
        case RC::RECORD_LOCKED:
            return "RECORD_LOCKED";
        case RC::ANALYSIS_FAILED:
            return "ANALYSIS_FAILED";
        case RC::THRESHOLD_NOT_MET:
            return "THRESHOLD_NOT_MET";
        default:
            return "UNKNOWN_ERROR";
        }
    }

} // namespace ipc