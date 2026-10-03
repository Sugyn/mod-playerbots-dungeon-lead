/*
 * Dungeon Lead - a derivative module for mod-playerbots (AzerothCore), adding autonomous 5-man
 * dungeon leadership. https://github.com/Sugyn/mod-playerbots-dungeon-lead
 *
 * Copyright (C) 2026 the Dungeon Lead contributors. Licensed under the GNU General Public
 * License, version 2, or (at your option) any later version - see LICENSE in this repository.
 *
 * DungeonTelemetryBuffer.h
 *
 * Bounded in-memory queue between the threads that produce telemetry lines (bot AI updates run
 * on map-update threads) and the one that writes them to disk (the world thread, every couple of
 * seconds). Producers only take a short lock to append a ready-formatted line; no file I/O ever
 * happens under it. When full, new lines are counted as dropped instead of blocking or growing
 * without limit - the writer reports the count, so a gap is never silent.
 * Dependency-free so tests/ can exercise it.
 */

#ifndef MOD_DUNGEONLEAD_TELEMETRYBUFFER_H
#define MOD_DUNGEONLEAD_TELEMETRYBUFFER_H

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace DungeonLeadKernel
{
    enum class TelemetryFile : uint8_t
    {
        Sessions,  // DungeonLeadSessions.csv - one row per event
        Runs,      // DungeonLeadRuns.csv - one row per finished run
        Debug,     // DungeonLeadDebug.log - "startdungeon debug" dumps
    };

    struct TelemetryLine
    {
        TelemetryFile file = TelemetryFile::Sessions;
        std::string text;  // complete line, newline included
    };

    class TelemetryBuffer
    {
    public:
        explicit TelemetryBuffer(size_t capacity) : _capacity(capacity) {}

        // False (and counted as dropped) when the buffer is full.
        bool Push(TelemetryFile file, std::string text)
        {
            std::lock_guard<std::mutex> lock(_mutex);
            if (_lines.size() >= _capacity)
            {
                ++_dropped;
                return false;
            }
            _lines.push_back({file, std::move(text)});
            return true;
        }

        // Takes everything queued so far (and the drop count since the last drain).
        std::vector<TelemetryLine> Drain(uint64_t& droppedOut)
        {
            std::vector<TelemetryLine> out;
            std::lock_guard<std::mutex> lock(_mutex);
            out.swap(_lines);
            droppedOut = _dropped;
            _dropped = 0;
            return out;
        }

        size_t Size()
        {
            std::lock_guard<std::mutex> lock(_mutex);
            return _lines.size();
        }

    private:
        size_t const _capacity;
        std::mutex _mutex;
        std::vector<TelemetryLine> _lines;
        uint64_t _dropped = 0;
    };
}

#endif
