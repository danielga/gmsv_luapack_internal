GARRYSMOD_MODULE_BASE_FOLDER = "../gmod-module-base"
SOURCE_FOLDER = "../Source"
PROJECT_FOLDER = os.get() .. "/" .. _ACTION

solution("gmsv_luapack_internal")
	language("C++")
	location(PROJECT_FOLDER)
	warnings("Extra")
	flags({"NoPCH", "StaticRuntime"})
	platforms({"x86"})
	configurations({"Release", "Debug"})

	filter("platforms:x86")
		architecture("x32")

	filter("configurations:Release")
		optimize("On")
		vectorextensions("SSE2")
		objdir(PROJECT_FOLDER .. "/Intermediate")
		targetdir(PROJECT_FOLDER .. "/Release")

	filter({"configurations:Debug"})
		flags({"Symbols"})
		objdir(PROJECT_FOLDER .. "/Intermediate")
		targetdir(PROJECT_FOLDER .. "/Debug")

	project("gmsv_luapack_internal")
		kind("SharedLib")
		defines({"GMMODULE"})
		includedirs({
			SOURCE_FOLDER,
			GARRYSMOD_MODULE_BASE_FOLDER .. "/include"
		})
		files({
			SOURCE_FOLDER .. "/*.cpp",
			SOURCE_FOLDER .. "/*.hpp",
			SOURCE_FOLDER .. "/*.c",
			SOURCE_FOLDER .. "/*.h",
			SOURCE_FOLDER .. "/MologieDetours/hde.cpp",
			SOURCE_FOLDER .. "/MologieDetours/detours.h"
		})
		vpaths({
			["Header files"] = {
				SOURCE_FOLDER .. "/**.hpp",
				SOURCE_FOLDER .. "/**.h"
			},
			["Source files"] = {
				SOURCE_FOLDER .. "/**.cpp",
				SOURCE_FOLDER .. "/**.c",
				SOURCE_FOLDER .. "/MologieDetours/**.cpp"
			}
		})
		
		targetprefix("")
		targetextension(".dll")

		configuration("windows")
			targetsuffix("_win32")

		configuration("linux")
			targetsuffix("_linux")

		configuration("macosx")
			targetsuffix("_mac")