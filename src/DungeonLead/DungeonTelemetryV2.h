/*
 * Dungeon Lead - a derivative module for mod-playerbots (AzerothCore), adding autonomous 5-man
 * dungeon leadership. https://github.com/Sugyn/mod-playerbots-dungeon-lead
 *
 * Copyright (C) 2026 the Dungeon Lead contributors. Licensed under the GNU General Public
 * License, version 2, or (at your option) any later version - see LICENSE in this repository.
 *
 * DungeonTelemetryV2.h
 *
 * Structured telemetry, schema v2 (docs/telemetry-schema-v2.md): one JSON object per line in
 * DungeonLeadEvents.v2.jsonl, written next to the v1 CSV through the same bounded buffer. Lines
 * are built here as plain strings - no JSON DOM on the map threads. Also the position sampling
 * policy for `position_sample`. Dependency-free so tests/ can exercise it.
 */

#ifndef MOD_DUNGEONLEAD_TELEMETRYV2_H
#define MOD_DUNGEONLEAD_TELEMETRYV2_H

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>

namespace DungeonLeadKernel
{
    constexpr int kTelemetrySchemaVersion = 2;

    inline std::string JsonEscape(std::string const& s)
    {
        std::string out;
        out.reserve(s.size() + 2);
        for (unsigned char c : s)
        {
            switch (c)
            {
                case '"':  out += "\\\""; break;
                case '\\': out += "\\\\"; break;
                case '\n': out += "\\n"; break;
                case '\r': out += "\\r"; break;
                case '\t': out += "\\t"; break;
                default:
                    if (c < 0x20)
                    {
                        char buf[8];
                        std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                        out += buf;
                    }
                    else
                        out += char(c);
            }
        }
        return out;
    }

    // Builds one JSON object: Str/Num/Raw append a member, Open/Close a nested object. Raw takes an
    // already serialized JSON value (a nested object built by another JsonLine, an array).
    class JsonLine
    {
    public:
        JsonLine() : _s("{") {}

        JsonLine& Str(char const* key, std::string const& v)
        {
            Key(key);
            _s += '"';
            _s += JsonEscape(v);
            _s += '"';
            return *this;
        }
        JsonLine& Num(char const* key, int64_t v)
        {
            Key(key);
            _s += std::to_string(v);
            return *this;
        }
        JsonLine& Num(char const* key, uint64_t v)
        {
            Key(key);
            _s += std::to_string(v);
            return *this;
        }
        JsonLine& Num(char const* key, uint32_t v) { return Num(key, uint64_t(v)); }
        JsonLine& Num(char const* key, int32_t v) { return Num(key, int64_t(v)); }
        // Coordinates: two decimals is 1 cm, plenty for a map and stable for golden files.
        JsonLine& Num(char const* key, double v)
        {
            Key(key);
            if (!std::isfinite(v))
            {
                _s += "null";
                return *this;
            }
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%.2f", v);
            _s += buf;
            return *this;
        }
        JsonLine& Num(char const* key, float v) { return Num(key, double(v)); }
        JsonLine& Bool(char const* key, bool v)
        {
            Key(key);
            _s += v ? "true" : "false";
            return *this;
        }
        JsonLine& Raw(char const* key, std::string const& json)
        {
            Key(key);
            _s += json.empty() ? "null" : json;
            return *this;
        }
        JsonLine& Open(char const* key)
        {
            Key(key);
            _s += '{';
            _first = true;
            return *this;
        }
        JsonLine& Close()
        {
            _s += '}';
            _first = false;
            return *this;
        }
        // The finished object (without a newline).
        std::string Done() const { return _s + "}"; }

    private:
        void Key(char const* key)
        {
            if (!_first)
                _s += ',';
            _first = false;
            _s += '"';
            _s += key;
            _s += "\":";
        }

        std::string _s;
        bool _first = true;
    };

    // Leader position evidence for the replay map, throttled (schema v2, "position_sample"): a
    // sample when the leader moved >= minMoveYd since the last one, or every maxIntervalMs while it
    // keeps moving, and always on a state or route step change. Standing still emits nothing.
    struct PositionSampleFacts
    {
        bool haveLast = false;
        float lastX = 0.f, lastY = 0.f, lastZ = 0.f;
        uint32_t msSinceLast = 0;
        float x = 0.f, y = 0.f, z = 0.f;
        bool stateChanged = false;
        bool stepChanged = false;
    };

    struct PositionSamplePolicy
    {
        float minMoveYd = 3.0f;
        uint32_t maxIntervalMs = 3000;
    };

    inline bool ShouldSamplePosition(PositionSampleFacts const& f, PositionSamplePolicy const& p = {})
    {
        if (!f.haveLast || f.stateChanged || f.stepChanged)
            return true;
        float const dx = f.x - f.lastX, dy = f.y - f.lastY, dz = f.z - f.lastZ;
        float const moved = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (moved >= p.minMoveYd)
            return true;
        // still moving a little, but long enough since the last sample
        return moved >= 0.5f && f.msSinceLast >= p.maxIntervalMs;
    }
}

#endif
