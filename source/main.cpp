#include <GarrysMod/Lua/Interface.h>
#include <GarrysMod/Lua/LuaInterface.h>
#include <lua.hpp>
#include <symbolfinder.hpp>
#include <GarrysMod/Interfaces.hpp>
#include <detours.h>
#include <cstdint>
#include <string>
#include <set>
#include <algorithm>
#include <interface.h>
#include <filesystem.h>
#include <eiface.h>

#if defined _WIN32 && _MSC_VER != 1600

#error The only supported compilation platform for this project on Windows is Visual Studio 2010 (for ABI reasons).

#elif defined __linux && (__GNUC__ != 4 || (__GNUC__ == 4 && __GNUC_MINOR__ < 4))

#error The only supported compilation platforms for this project on Linux are GCC 4.4 to 4.9 (for ABI reasons).

#elif defined __APPLE__

#include <AvailabilityMacros.h>

#if MAC_OS_X_VERSION_MIN_REQUIRED > 1050

#error The only supported compilation platform for this project on Mac OS X is GCC with Mac OS X 10.5 SDK (for ABI reasons).

#endif

#endif

class GModDataPack;

namespace Bootil
{

class Buffer
{
public:
	void **vtable;
	void *data;
	uint32_t size;
	uint32_t pos;
	uint32_t written;
};

class AutoBuffer : public Buffer
{ };

}

struct LuaFile // 116 bytes
{
	void **vtable;
	const char *path[7];
	const char *parent[7];
	const char *content[7];
	Bootil::AutoBuffer buffer;
	bool unkbool1;
	bool unkbool2;
};

namespace global
{

#if defined _WIN32

static const char *server_lib = "server.dll";

static const char *filesystem_dedicated_lib = "dedicated.dll";

static const char *FileSystemFactory_dedicated_sym = "\x55\x8B\xEC\x56\x8B\x75\x08\x68\x2A\x2A\x2A\x2A\x56\xE8";
static const size_t FileSystemFactory_dedicated_symlen = 14;

static const char *filesystem_lib = "filesystem_stdio.dll";

static const char *FileSystemFactory_sym = "@CreateInterface";
static const size_t FileSystemFactory_symlen = 0;

static const char *AddOrUpdateFile_sym = "\x55\x8B\xEC\x83\xEC\x18\x53\x56\x8B\x75\x08\x83\xC6\x04\x83\x7E";
static const size_t AddOrUpdateFile_symlen = 16;

#elif defined __linux

static const char *server_lib = "garrysmod/bin/server_srv.so";

static const char *filesystem_dedicated_lib = "dedicated_srv.so";

static const char *FileSystemFactory_dedicated_sym = "@_Z17FileSystemFactoryPKcPi";
static const size_t FileSystemFactory_dedicated_symlen = 0;

static const char *filesystem_lib = "filesystem_stdio.so";

static const char *FileSystemFactory_sym = "@CreateInterface";
static const size_t FileSystemFactory_symlen = 0;

static const char *AddOrUpdateFile_sym = "@_ZN12GModDataPack15AddOrUpdateFileEP7LuaFileb";
static const size_t AddOrUpdateFile_symlen = 0;

#elif defined __APPLE__

static const char *server_lib = "garrysmod/bin/server.dylib";

static const char *filesystem_dedicated_lib = "dedicated.dylib";

static const char *FileSystemFactory_dedicated_sym = "@__Z17FileSystemFactoryPKcPi";
static const size_t FileSystemFactory_dedicated_symlen = 0;

static const char *filesystem_lib = "filesystem_stdio.dylib";

static const char *FileSystemFactory_sym = "@_CreateInterface";
static const size_t FileSystemFactory_symlen = 0;

static const char *AddOrUpdateFile_sym = "@__ZN12GModDataPack15AddOrUpdateFileEP7LuaFileb";
static const size_t AddOrUpdateFile_symlen = 0;

#endif

static const char *hook_name = "AddOrUpdateCSLuaFile";

static GarrysMod::Lua::ILuaInterface *lua = nullptr;
static IFileSystem *filesystem = nullptr;

#if defined _WIN32

typedef void ( __thiscall *AddOrUpdateFile_t ) ( GModDataPack *self, LuaFile *file, bool force );

#elif defined __linux || defined __APPLE__

typedef void ( *AddOrUpdateFile_t ) ( GModDataPack *self, LuaFile *file, bool force );

#endif
 
static AddOrUpdateFile_t AddOrUpdateFile = nullptr;
static MologieDetours::Detour<AddOrUpdateFile_t> *AddOrUpdateFile_d = nullptr;

#if defined _WIN32

void __fastcall AddOrUpdateFile_h( GModDataPack *self, void *, LuaFile *file, bool reload )

#elif defined __linux || defined __APPLE__

void AddOrUpdateFile_h( GModDataPack *self, LuaFile *file, bool reload )

#endif

{
	helpers::PushHookRun( lua, hook_name );

	lua->PushString( file->path[0] );
	lua->PushBool( reload );

	bool dontcall = false;
	if( helpers::CallHookRun( lua, 2, 1 ) )
		dontcall = lua->IsType( -1, GarrysMod::Lua::Type::BOOL ) && !lua->GetBool( -1 );

	lua->Pop( 1 );

	if( !dontcall )
		return AddOrUpdateFile( self, file, reload );
}

static void Initialize( lua_State *state )
{
	lua = reinterpret_cast<GarrysMod::Lua::ILuaInterface *>( LUA );

	SourceSDK::FactoryLoader engine_loader( "engine", false, false, "bin/" );
	IVEngineServer *engine_server = engine_loader.GetInterface<IVEngineServer>(
		INTERFACEVERSION_VENGINESERVER_VERSION_21
	);
	if( engine_server == nullptr )
		LUA->ThrowError( "failed to retrieve server engine interface" );

	bool dedicated = engine_server->IsDedicatedServer( );

	SymbolFinder symfinder;

	CreateInterfaceFn factory = reinterpret_cast<CreateInterfaceFn>( symfinder.ResolveOnBinary(
		dedicated ? filesystem_dedicated_lib : filesystem_lib,
		dedicated ? FileSystemFactory_dedicated_sym : FileSystemFactory_sym,
		dedicated ? FileSystemFactory_dedicated_symlen : FileSystemFactory_symlen
	) );
	if( factory == nullptr )
		LUA->ThrowError( "unable to retrieve filesystem factory" );

	filesystem = static_cast<IFileSystem *>( factory( FILESYSTEM_INTERFACE_VERSION, nullptr ) );
	if( filesystem == nullptr )
		LUA->ThrowError( "failed to initialize IFileSystem" );

	AddOrUpdateFile = reinterpret_cast<AddOrUpdateFile_t>(
		symfinder.ResolveOnBinary( server_lib, AddOrUpdateFile_sym, AddOrUpdateFile_symlen )
	);
	if( AddOrUpdateFile == nullptr )
		LUA->ThrowError( "failed to find GModDataPack::AddOrUpdateFile" );

	AddOrUpdateFile_d = new( std::nothrow ) MologieDetours::Detour<AddOrUpdateFile_t>(
		AddOrUpdateFile, reinterpret_cast<AddOrUpdateFile_t>( AddOrUpdateFile_h )
	);
	if( AddOrUpdateFile_d == nullptr )
		LUA->ThrowError( "failed to detour GModDataPack::AddOrUpdateFile" );

	AddOrUpdateFile = AddOrUpdateFile_d->GetOriginalFunction( );
}

static void Deinitialize( lua_State *state )
{
	delete AddOrUpdateFile_d;
}

}

namespace luapack
{

static std::set<std::string> whitelist_extensions;

static std::set<std::string> whitelist_pathid;

static bool IsPathAllowed( std::string &filename )
{
	if( !V_RemoveDotSlashes( &filename[0], CORRECT_PATH_SEPARATOR, true ) )
		return false;

	filename.resize( std::strlen( filename.c_str( ) ) );

	const char *extension = V_GetFileExtension( filename.c_str( ) );
	if( extension == nullptr )
		return false;

	std::string ext = extension;
	std::transform( ext.begin( ), ext.end( ), ext.begin( ), tolower );
	return whitelist_extensions.find( ext ) != whitelist_extensions.end( );
}

inline bool IsPathIDAllowed( std::string &pathid )
{
	std::transform( pathid.begin( ), pathid.end( ), pathid.begin( ), tolower );
	return whitelist_pathid.find( pathid ) != whitelist_pathid.end( );
}

LUA_FUNCTION_STATIC( Rename )
{
	LUA->CheckType( 1, GarrysMod::Lua::Type::STRING );
	LUA->CheckType( 2, GarrysMod::Lua::Type::STRING );
	LUA->CheckType( 3, GarrysMod::Lua::Type::STRING );

	std::string fname = LUA->GetString( 1 ), fnew = LUA->GetString( 2 ), pathid = LUA->GetString( 3 );

	if( !IsPathAllowed( fname ) || !IsPathAllowed( fnew ) || !IsPathIDAllowed( pathid ) )
	{
		LUA->PushBool( false );
		return 1;
	}

	LUA->PushBool( global::filesystem->RenameFile( fname.c_str( ), fnew.c_str( ), pathid.c_str( ) ) );
	return 1;
}

static void Initialize( lua_State *state )
{
	LUA->GetField( GarrysMod::Lua::INDEX_GLOBAL, "luapack" );
	if( !LUA->IsType( -1, GarrysMod::Lua::Type::TABLE ) )
		LUA->ThrowError( "luapack table not found" );

	LUA->PushCFunction( Rename );
	LUA->SetField( -2, "Rename" );

	LUA->Pop( 1 );

	if( whitelist_extensions.empty( ) )
	{
		const std::string extensions[] = { "lua", "txt", "dat" };
		whitelist_extensions.insert( extensions, extensions + sizeof( extensions ) / sizeof( *extensions ) );
	}

	if( whitelist_pathid.empty( ) )
	{
		const std::string pathid[] = { "lsv", "lua", "data" };
		whitelist_pathid.insert( pathid, pathid + sizeof( pathid ) / sizeof( *pathid ) );
	}
}

static void Deinitialize( lua_State *state )
{
	LUA->GetField( GarrysMod::Lua::INDEX_GLOBAL, "luapack" );
	if( !LUA->IsType( -1, GarrysMod::Lua::Type::TABLE ) )
		LUA->ThrowError( "luapack table not found" );

	LUA->PushNil( );
	LUA->SetField( -2, "Rename" );

	LUA->Pop( 1 );
}

}

GMOD_MODULE_OPEN( )
{
	global::Initialize( state );
	luapack::Initialize( state );
	return 0;
}
 
GMOD_MODULE_CLOSE( )
{
	luapack::Deinitialize( state );
	global::Deinitialize( state );
	return 0;
}
