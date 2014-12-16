GARRYSMOD_MODULE_BASE_FOLDER = "../gmod-module-base"
SCANNING_FOLDER = "../scanning"
DETOURING_FOLDER = "../detouring"
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
			SCANNING_FOLDER,
			DETOURING_FOLDER
		})
		files({
			SOURCE_FOLDER .. "/*.cpp",
			SOURCE_FOLDER .. "/*.c",
			SCANNING_FOLDER .. "/SymbolFinder.cpp",
			DETOURING_FOLDER .. "/hde.cpp"
		})
		vpaths({
			["Source files"] = {
				SOURCE_FOLDER .. "/**.cpp",
				SOURCE_FOLDER .. "/**.c",
				SCANNING_FOLDER .. "/**.cpp",
				DETOURING_FOLDER .. "/**.cpp"
			}
		})
		
		targetprefix("")
		targetextension(".dll")

		filter("action:gmake")
			linkoptions({"-static-libgcc", "-static-libstdc++"})

		configuration("windows")
			targetsuffix("_win32")

		configuration("linux")
			links({"dl"})
			buildoptions({"-std=c++11"})
			targetsuffix("_linux")

		configuration("macosx")
			links({"dl"})
			buildoptions({"-std=c++11"})
			targetsuffix("_mac")
