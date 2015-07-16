#include <GarrysMod/Lua/Interface.h>
#include <GarrysMod/Lua/LuaInterface.h>
#include <lua.hpp>
#include <symbolfinder.hpp>
#include <detours.h>
#include <sha1.h>
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

namespace Global
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

LUA_FUNCTION_STATIC( ErrorTraceback )
{
	GarrysMod::Lua::ILuaInterface *lua = static_cast<GarrysMod::Lua::ILuaInterface *>( LUA );

	std::string spaces( 2, ' ' );
	std::ostringstream stream;
	stream << LUA->GetString( 1 );

	lua_Debug dbg = { 0 };
	for( int32_t lvl = 1; lua->GetStack( lvl, &dbg ) == 1; ++lvl, memset( &dbg, 0, sizeof( dbg ) ) )
	{
		if( lua->GetInfo( "Sln", &dbg ) == 0 )
			break;

		stream
			<< '\n'
			<< spaces
			<< lvl
			<< ". "
			<< ( dbg.name == nullptr ? "unknown" : dbg.name )
			<< " - "
			<< dbg.short_src
			<< ':'
			<< dbg.currentline;
		spaces += ' ';
	}

	LUA->PushString( stream.str( ).c_str( ) );
	return 1;
}

LUA_FUNCTION_STATIC( hook_Call )
{
	int32_t args = LUA->Top( );

	LUA->PushSpecial( GarrysMod::Lua::SPECIAL_GLOB );
	LUA->GetField( -1, "hook" );
	LUA->GetField( -1, "Call" );

	for( int32_t i = 1; i <= args; ++i )
		LUA->Push( i );

	LUA->Call( args, 1 );
	return 1;
}

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
	lua->PushCFunction( ErrorTraceback );
	lua->PushCFunction( hook_Call );
	lua->PushString( "AddOrUpdateCSLuaFile" );
	lua->PushNil( );
	lua->PushString( file->path[0] );
	lua->PushBool( reload );

	bool dontcall = false;
	if( lua->PCall( 4, 1, -6 ) == 0 )
		dontcall = lua->IsType( -1, GarrysMod::Lua::Type::BOOL ) && !lua->GetBool( -1 );
	else
		lua->Msg( "\n[ERROR] %s\n\n", lua->GetString( -1 ) );

	lua->Pop( 2 );

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

namespace LuaPack
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

	LUA->PushBool( Global::filesystem->RenameFile( fname.c_str( ), fnew.c_str( ), pathid.c_str( ) ) );
	return 1;
}

static void Initialize( lua_State *state )
{
	LUA->PushSpecial( GarrysMod::Lua::SPECIAL_GLOB );

	LUA->GetField( -1, "luapack" );
	if( !LUA->IsType( -1, GarrysMod::Lua::Type::TABLE ) )
		LUA->ThrowError( "luapack table not found" );

	LUA->PushCFunction( Rename );
	LUA->SetField( -2, "Rename" );

	LUA->Pop( 2 );
}

static void Deinitialize( lua_State * )
{ }

}

namespace Hasher
{

struct UserData
{
	SHA1_CTX *pcontext;
	uint8_t type;
	SHA1_CTX context;
};

static const char *metaname = "SHA1";
static const uint8_t metaid = 130;
static const char *invalid_error = "invalid SHA1";

static SHA1_CTX *Get( lua_State *state, int32_t index )
{
	if( !LUA->IsType( index, metaid ) )
		static_cast<GarrysMod::Lua::ILuaInterface *>( LUA )->TypeError( metaname, index );

	SHA1_CTX *hasher = static_cast<UserData *>( LUA->GetUserdata( index ) )->pcontext;
	if( hasher == nullptr )
		LUA->ArgError( index, invalid_error );

	return hasher;
}

inline void Create( lua_State *state )
{
	UserData *udata = static_cast<UserData *>( LUA->NewUserdata( sizeof( UserData ) ) );
	udata->pcontext = &udata->context;
	udata->type = metaid;

	SHA1Init( udata->pcontext );

	LUA->CreateMetaTableType( metaname, metaid );
	LUA->SetMetaTable( -2 );
}

LUA_FUNCTION_STATIC( Constructor )
{
	Create( state );
	return 1;
}

LUA_FUNCTION_STATIC( Update )
{
	SHA1_CTX *hasher = Get( state, 1 );
	LUA->CheckType( 2, GarrysMod::Lua::Type::STRING );

	uint32_t len = 0;
	const uint8_t *data = reinterpret_cast<const uint8_t *>( LUA->GetString( 2, &len ) );

	SHA1Update( hasher, const_cast<uint8_t *>( data ), len );

	return 0;
}

LUA_FUNCTION_STATIC( Final )
{
	SHA1_CTX *hasher = Get( state, 1 );

	uint8_t digest[20] = { 0 };
	SHA1Final( hasher, digest );

	LUA->PushString( reinterpret_cast<char *>( digest ), sizeof( digest ) );
	return 1;
}

static void Initialize( lua_State *state )
{
	LUA->PushSpecial( GarrysMod::Lua::SPECIAL_GLOB );

	LUA->PushCFunction( Constructor );
	LUA->SetField( -2, metaname );

	LUA->Pop( 1 );

	LUA->CreateMetaTableType( metaname, metaid );

	LUA->Push( -1 );
	LUA->SetField( -2, "__index" );

	LUA->PushCFunction( Update );
	LUA->SetField( -2, "Update" );

	LUA->PushCFunction( Final );
	LUA->SetField( -2, "Final" );

	LUA->Pop( 1 );
}

static void Deinitialize( lua_State *state )
{
	LUA->PushSpecial( GarrysMod::Lua::SPECIAL_GLOB );

	LUA->PushNil( );
	LUA->SetField( -2, metaname );

	LUA->Pop( 1 );

	LUA->PushSpecial( GarrysMod::Lua::SPECIAL_REG );

	LUA->PushNil( );
	LUA->SetField( -2, metaname );

	LUA->Pop( 1 );
}

}

GMOD_MODULE_OPEN( )
{
	Global::Initialize( state );
	LuaPack::Initialize( state );
	Hasher::Initialize( state );
	return 0;
}
 
GMOD_MODULE_CLOSE( )
{
	Hasher::Deinitialize( state );
	LuaPack::Deinitialize( state );
	Global::Deinitialize( state );
	return 0;
}