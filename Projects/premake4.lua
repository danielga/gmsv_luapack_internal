GARRYSMOD_INCLUDES_PATH = "../gmod-module-base/include"
PROJECT_FOLDER = os.get() .. "/" .. _ACTION
SOURCE_FOLDER = "../Source/"

solution("gmsv_luapack_internal")
	language("C++")
	location(PROJECT_FOLDER)
	flags({"NoPCH", "ExtraWarnings"})

	if os.is("macosx") then
		platforms({"Universal32"})
	else
		platforms({"x32"})
	end

	configurations({"Debug", "Release"})

	configuration("Debug")
		defines({"DEBUG"})
		flags({"Symbols"})
		targetdir(PROJECT_FOLDER .. "/Debug")
		objdir(PROJECT_FOLDER .. "/Intermediate")

	configuration("Release")
		defines({"NDEBUG"})
		flags({"Optimize", "EnableSSE"})
		targetdir(PROJECT_FOLDER .. "/Release")
		objdir(PROJECT_FOLDER .. "/Intermediate")

	project("gmsv_luapack_internal")
		kind("SharedLib")
		defines({"GMMODULE"})
		includedirs({SOURCE_FOLDER, GARRYSMOD_INCLUDES_PATH})
		files({SOURCE_FOLDER .. "*.cpp", SOURCE_FOLDER .. "*.hpp", SOURCE_FOLDER .. "*.c", SOURCE_FOLDER .. "*.h", SOURCE_FOLDER .. "MologieDetours/hde.cpp", SOURCE_FOLDER .. "MologieDetours/detours.h"})
		vpaths({["Header files/*"] = {SOURCE_FOLDER .. "**.hpp", SOURCE_FOLDER .. "**.h"}, ["Source files/*"] = {SOURCE_FOLDER .. "**.cpp", SOURCE_FOLDER .. "**.c", SOURCE_FOLDER .. "MologieDetours/**.cpp"}})
		
		targetprefix("gmsv_") -- Just to remove prefixes like lib from Linux
		targetname("luapack_internal")

		configuration("windows")
			targetsuffix("_win32")

		configuration("linux")
			targetsuffix("_linux")
			targetextension(".dll") -- Derp Garry, WHY

		configuration("macosx")
			targetsuffix("_mac")
			targetextension(".dll") -- Derp Garry, WHY