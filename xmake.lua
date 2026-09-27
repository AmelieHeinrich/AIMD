set_project("AIMD")
set_version("0.1.0")

add_rules("mode.debug", "mode.release")
add_rules("plugin.compile_commands.autoupdate", { outputdir = "." })

set_languages("c11", "c++20")
set_warnings("all")

add_requires("libsdl3", "glm")
add_requires("imgui v1.92.7", { configs = { sdl3 = true } })

--
-- agfx: graphics abstraction, one backend per platform
--
target("agfx")
    set_kind("static")
    set_warnings("none")
    add_includedirs("ThirdParty", { public = true })
    add_headerfiles("ThirdParty/agfx/agfx.h", "ThirdParty/agfx/agfx.hpp", "ThirdParty/agfx/agfx_native.h")

    if is_plat("macosx") then
        add_files("ThirdParty/agfx/agfx_metal4.mm")
        add_mxflags("-fobjc-arc")
        add_frameworks("Metal", "QuartzCore", "Foundation", { public = true })
    elseif is_plat("windows") then
        add_files("ThirdParty/agfx/agfx_d3d12.cpp")
        add_includedirs("ThirdParty/agfx/d3dx12")
        add_syslinks("d3d12", "dxgi", "dxguid", { public = true })
        add_linkdirs("Binaries", { public = true })
        add_links("WinPixEventRuntime", { public = true })
    elseif is_plat("linux") then
        -- agfx_vulkan.cpp includes vk/volk.c itself
        add_files("ThirdParty/agfx/agfx_vulkan.cpp")
        add_includedirs("ThirdParty/agfx/vk")
        add_syslinks("dl", "pthread", { public = true })
    end
target_end()

--
-- agfx_shader: runtime HLSL compiler (DXC + Metal Shader Converter / SPIR-V). Only the demo uses it.
--
target("agfx_shader")
    set_kind("static")
    set_warnings("none")
    add_includedirs("ThirdParty", { public = true })

    if is_plat("macosx") then
        add_files("ThirdParty/agfx_shader/agfx_shader_compiler_mac.mm")
        add_mxflags("-fobjc-arc")
        add_linkdirs("Binaries", { public = true })
        add_links("dxcompiler", "metalirconverter", { public = true })
    elseif is_plat("windows") then
        add_files("ThirdParty/agfx_shader/agfx_shader_compiler_windows.cpp")
        add_linkdirs("Binaries", { public = true })
        add_links("dxcompiler", { public = true })
    elseif is_plat("linux") then
        add_files("ThirdParty/agfx_shader/agfx_shader_compiler_linux.cpp")
        add_files("ThirdParty/agfx_shader/spirv_reflect/spirv_reflect.c")
        add_syslinks("dl", { public = true })
    end
target_end()

--
-- aimd: the debug renderer library, from Include/ and Source/. For projects that consume AIMD through xmake;
-- everyone else can drop dist/ into their tree instead.
--
target("aimd")
    set_kind("static")
    add_deps("agfx")
    add_includedirs("Include", { public = true })
    add_headerfiles("Include/(aimd/*.h)")
    add_files("Source/*.cpp")
target_end()

--
-- aimd_demo: SDL3 + ImGui showcase. Uses the single-header build from dist/, regenerated before every build.
--
target("aimd_demo")
    set_kind("binary")
    add_deps("agfx", "agfx_shader")
    add_packages("libsdl3", "imgui", "glm")
    add_defines("GLM_FORCE_DEPTH_ZERO_TO_ONE", "GLM_FORCE_RADIANS")
    add_includedirs("dist", "ThirdParty/agfx_imgui")
    add_files("Demo/*.cpp")
    add_files("ThirdParty/agfx_imgui/imgui_impl_agfx.cpp")
    set_rundir("$(projectdir)")

    if is_plat("macosx") then
        add_rpathdirs("@executable_path")
    elseif is_plat("linux") then
        add_rpathdirs("$ORIGIN")
    end

    before_build(function (target)
        import("amalgamate", { rootdir = path.join(os.projectdir(), "Scripts") })()
    end)

    -- Runtime shader compiler libraries next to the executable
    after_build(function (target)
        local patterns = {
            macosx = { "libdxcompiler.dylib", "libmetalirconverter.dylib" },
            windows = { "dxcompiler.dll", "dxil.dll", "WinPixEventRuntime.dll" },
            linux = { "libdxcompiler.so" },
        }
        for _, name in ipairs(patterns[target:plat()] or {}) do
            os.cp(path.join(os.projectdir(), "Binaries", name), target:targetdir())
        end
    end)
target_end()

--
-- xmake amalgamate: regenerates dist/ (aimd.h, AIMD.hlsl, AIMDDebug.hlsli) from Include/, Source/ and Shaders/
--
task("amalgamate")
    set_category("action")
    on_run(function ()
        import("amalgamate", { rootdir = path.join(os.projectdir(), "Scripts") })()
    end)
    set_menu({ usage = "xmake amalgamate", description = "Regenerate the single-header build in dist/", options = {} })
task_end()
