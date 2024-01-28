add_rules("mode.debug", "mode.release")
add_rules("plugin.vsxmake.autoupdate")

set_languages("c++20")

target("HeaderTool")

    if (is_mode("debug")) then
		add_defines("DEBUG")
	end

    set_kind("binary")
    add_headerfiles("src/**h")
    add_files("src/**.cpp")