--
-- Generates dist/: the single-header build of AIMD (dist/aimd.h) and a copy of its two shader files.
-- Run with `xmake amalgamate`. Building the demo runs it too, so dist/ never falls behind Include/ and Source/.
--

local kPreamble = [[
/**
 * AIMD -- single-header build. Generated from Include/ and Source/ by Scripts/amalgamate.lua: do not edit.
 *
 * In exactly one C++ file (C++17):
 *     #define AIMD_IMPLEMENTATION
 *     #include "aimd.h"
 * Everywhere else, C or C++, include it without the define.
 *
 * Needs agfx (<agfx/agfx.h>). The shaders are the two files next to this one: AIMD.hlsl and AIMDDebug.hlsli.
 */]]

-- Lines of the sources that make no sense once everything is in one file.
local kDroppedLines = {
    ["#pragma once"] = true,
    ["#include <aimd/aimd.h>"] = true,
    ['#include "aimd_internal.h"'] = true,
}

local kImplementationFiles = {
    "Source/aimd_internal.h",
    "Source/aimd.cpp",
    "Source/aimd_gpu.cpp",
    "Source/aimd_shapes.cpp",
}

local kShaderFiles = {
    "Shaders/AIMD.hlsl",
    "Shaders/AIMDDebug.hlsli",
}

local function readSource(root, file)
    local lines = {}
    for line in io.readfile(path.join(root, file)):gsub("\r\n", "\n"):gmatch("([^\n]*)\n?") do
        if not kDroppedLines[line] then
            table.insert(lines, line)
        end
    end
    return (table.concat(lines, "\n"):gsub("\n\n\n+", "\n\n"):gsub("^%s+", ""):gsub("%s+$", ""))
end

-- Leaves the file untouched when nothing changed, so builds don't see a new timestamp.
local function writeIfChanged(file, content)
    if os.isfile(file) and io.readfile(file) == content then
        return false
    end
    io.writefile(file, content)
    return true
end

function main()
    local root = os.projectdir()
    local dist = path.join(root, "dist")
    os.mkdir(dist)

    local parts = {
        kPreamble,
        "#ifndef AIMD_H\n#define AIMD_H",
        readSource(root, "Include/aimd/aimd.h"),
        "#endif // AIMD_H",
        "#if defined(AIMD_IMPLEMENTATION) && !defined(AIMD_IMPLEMENTATION_INCLUDED)\n" ..
        "#define AIMD_IMPLEMENTATION_INCLUDED\n\n" ..
        "#ifndef __cplusplus\n" ..
        "#error \"AIMD_IMPLEMENTATION must be defined in a C++ file\"\n" ..
        "#endif",
    }
    for _, file in ipairs(kImplementationFiles) do
        table.insert(parts, "// " .. string.rep("-", 116) .. "\n// " .. file .. "\n// " .. string.rep("-", 116))
        table.insert(parts, readSource(root, file))
    end
    table.insert(parts, "#endif // AIMD_IMPLEMENTATION")

    local changed = {}
    if writeIfChanged(path.join(dist, "aimd.h"), table.concat(parts, "\n\n") .. "\n") then
        table.insert(changed, "aimd.h")
    end
    for _, file in ipairs(kShaderFiles) do
        if writeIfChanged(path.join(dist, path.filename(file)), io.readfile(path.join(root, file))) then
            table.insert(changed, path.filename(file))
        end
    end

    if #changed > 0 then
        cprint("${bright green}amalgamate:${clear} updated dist/%s", table.concat(changed, ", dist/"))
    end
end
