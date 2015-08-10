#include <GarrysMod/Lua/Interface.h>
#include <GarrysMod/Lua/LuaInterface.h>
#include <lua.hpp>
#include <symbolfinder.hpp>
#include <detours.h>
#include <cstdint>
#include <string>
#include <sstream>
#include <unordered_set>
#include <algorithm>

#undef GetCurrentDirectory

#include <interface.h>
#include <filesystem.h>

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

static const char *dedicated_lib = "dedicated.dll";
static const char *server_lib = "server.dll";

static const char *FileSystemFactory_sym = "\x55\x8B\xEC\x56\x8B\x75\x08\x68\x2A\x2A\x2A\x2A\x56\xE8";
static const size_t FileSystemFactory_symlen = 14;

static const char *AddOrUpdateFile_sym = "\x55\x8B\xEC\x83\xEC\x18\x53\x56\x8B\x75\x08\x83\xC6\x04\x83\x7E";
static const size_t AddOrUpdateFile_symlen = 16;

#elif defined __linux

static const char *dedicated_lib = "dedicated_srv.so";
static const char *server_lib = "garrysmod/bin/server_srv.so";

static const char *FileSystemFactory_sym = "@_Z17FileSystemFactoryPKcPi";
static const size_t FileSystemFactory_symlen = 0;

static const char *AddOrUpdateFile_sym = "@_ZN12GModDataPack15AddOrUpdateFileEP7LuaFileb";
static const size_t AddOrUpdateFile_symlen = 0;

#elif defined __APPLE__

static const char *dedicated_lib = "dedicated.dylib";
static const char *server_lib = "garrysmod/bin/server.dylib";

static const char *FileSystemFactory_sym = "@__Z17FileSystemFactoryPKcPi";
static const size_t FileSystemFactory_symlen = 0;

static const char *AddOrUpdateFile_sym = "@__ZN12GModDataPack15AddOrUpdateFileEP7LuaFileb";
static const size_t AddOrUpdateFile_symlen = 0;

#endif

static GarrysMod::Lua::ILuaInterface *lua = nullptr;
static IFileSystem *filesystem = nullptr;
static char fullpath[260] = { 0 };

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
	lua->ReferencePush( 1 );
	lua->GetField( GarrysMod::Lua::INDEX_GLOBAL, "hook" );
	lua->GetField( -1, "Call" );
	lua->PushString( "AddOrUpdateCSLuaFile" );
	lua->PushNil( );
	lua->PushString( file->path[0] );
	lua->PushBool( reload );

	bool dontcall = false;
	if( lua->PCall( 4, 1, -6 ) == 0 )
		dontcall = lua->IsType( -1, GarrysMod::Lua::Type::BOOL ) && !lua->GetBool( -1 );
	else
		lua->Msg( "\n[ERROR] %s\n\n", lua->GetString( -1 ) );

	lua->Pop( 3 );

	if( !dontcall )
		return AddOrUpdateFile( self, file, reload );
}

static void Initialize( lua_State *state )
{
	lua = reinterpret_cast<GarrysMod::Lua::ILuaInterface *>( LUA );

	SymbolFinder symfinder;

	CreateInterfaceFn factory = static_cast<CreateInterfaceFn>(
		symfinder.ResolveOnBinary( dedicated_lib, FileSystemFactory_sym, FileSystemFactory_symlen )
	);
	if( factory == nullptr )
		LUA->ThrowError( "nable to retrieve dedicated factory" );

	filesystem = static_cast<IFileSystem *>( factory( FILESYSTEM_INTERFACE_VERSION, nullptr ) );
	if( filesystem == nullptr )
		LUA->ThrowError( "failed to initialize IFileSystem" );

	if( !filesystem->GetCurrentDirectory( fullpath, sizeof( fullpath ) ) )
		LUA->ThrowError( "unable to retrieve current directory" );

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

static bool IsPathAllowed( std::string &filename )
{
	static const std::unordered_set<std::string> whitelist_extensions = {
		"lua", "txt", "dat"
	};

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
	static const std::unordered_set<std::string> whitelist_pathid = {
		"lsv", "lua", "data"
	};

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
	global::Deinitialize( state );
	return 0;
}
