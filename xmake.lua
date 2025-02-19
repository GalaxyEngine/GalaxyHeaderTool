add_rules("mode.debug", "mode.release")
add_rules("plugin.vsxmake.autoupdate")

-- Runtime mode configuration
if is_plat("windows") then
    set_runtimes(is_mode("debug") and "MDd" or "MD")
end

set_languages("c++20")

-- Custom repo
add_repositories("galaxy-repo https://github.com/GalaxyEngine/xmake-repo")
-- Packages
add_requires("cpp_serializer")

target("GalaxyHeaderTool")
    set_kind("binary")

    if (is_mode("debug")) then
		add_defines("DEBUG")
	end

    add_headerfiles("src/**h")
    add_headerfiles("src/**inl")
    add_files("src/**.cpp")
    
    add_packages("cpp_serializer")

target_end()
