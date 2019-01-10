#include <GarrysMod/Lua/Interface.h>
#include <GarrysMod/Lua/Helpers.hpp>
#include <GarrysMod/Interfaces.hpp>

#include <scanning/symbolfinder.hpp>
#include <detouring/classproxy.hpp>

#include <cstdint>
#include <string>
#include <unordered_set>
#include <algorithm>
#include <cctype>

#include <interface.h>
#include <filesystem.h>
#include <eiface.h>

class GModDataPack;

namespace Bootil
{

class Buffer
{
public:
	virtual ~Buffer() = default; // For the VTable

	void *data;
	uint32_t size;
	uint32_t pos;
	uint32_t written;
};

class AutoBuffer : public Buffer
{
public:
	virtual ~AutoBuffer() = default; // For the VTable
};

}

class LuaFile // 116 bytes
{
public:
	virtual ~LuaFile() = default; // For the VTable

	std::string path;
	std::string parent;
	std::string content;
	Bootil::AutoBuffer buffer;
	bool unknown1;
	bool unknown2;
};

namespace luapack
{

#if defined _WIN32

static const char FileSystemFactory_dedicated_sym[] = "\x55\x8B\xEC\x68\x2A\x2A\x2A\x2A\xFF\x75\x08\xE8\x2A\x2A\x2A\x2A\x83\xC4\x08";
static const size_t FileSystemFactory_dedicated_symlen = sizeof( FileSystemFactory_dedicated_sym ) - 1;

static const char AddOrUpdateFile_sym[] = "\x55\x8B\xEC\x83\xEC\x18\x53\x56\x57\x8B\x7D\x08\x8B\xD9\x83\x7F\x18\x10";
static const size_t AddOrUpdateFile_symlen = sizeof( AddOrUpdateFile_sym ) - 1;

#elif defined __linux || defined __APPLE__

static const char FileSystemFactory_dedicated_sym[] = "@_Z17FileSystemFactoryPKcPi";
static const size_t FileSystemFactory_dedicated_symlen = 0;

static const char AddOrUpdateFile_sym[] = "@_ZN12GModDataPack15AddOrUpdateFileEP7LuaFileb";
static const size_t AddOrUpdateFile_symlen = 0;

#endif

static const std::string filesystem_dedicated_lib = Helpers::GetBinaryFileName( "dedicated", false );
static const std::string filesystem_lib = Helpers::GetBinaryFileName( "filesystem_stdio", false, false );
static IFileSystem *filesystem = nullptr;

static const std::string server_lib = Helpers::GetBinaryFileName( "server", false, true, "garrysmod/bin/" );

static const char FileSystemFactory_sym[] = "@CreateInterface";
static const size_t FileSystemFactory_symlen = 0;

static std::unordered_set<std::string> whitelist_extensions = { "lua", "txt", "dat" };
static std::unordered_set<std::string> whitelist_pathid = { "lsv", "lua", "data" };

class GModDataPackProxy : public Detouring::ClassProxy<GModDataPack, GModDataPackProxy>
{
private:

#if defined _WIN32

	typedef void( __thiscall *AddOrUpdateFile_t ) ( GModDataPack *self, LuaFile *file, bool force );

#elif defined __linux || defined __APPLE__

	typedef void( *AddOrUpdateFile_t ) ( GModDataPack *self, LuaFile *file, bool force );

#endif

	static AddOrUpdateFile_t AddOrUpdateFile_original;
	static const char hook_name[];
	static GarrysMod::Lua::ILuaBase *lua;

public:
	static void Initialize( GarrysMod::Lua::ILuaBase *LUA )
	{
		lua = LUA;

		SymbolFinder symfinder;

		AddOrUpdateFile_original = reinterpret_cast<AddOrUpdateFile_t>(
			symfinder.ResolveOnBinary( server_lib.c_str( ), AddOrUpdateFile_sym, AddOrUpdateFile_symlen )
		);
		if( AddOrUpdateFile_original == nullptr )
			LUA->ThrowError( "failed to find GModDataPack::AddOrUpdateFile" );

		if( !Hook( AddOrUpdateFile_original, &GModDataPackProxy::AddOrUpdateFile ) )
			LUA->ThrowError( "failed to hook GModDataPack::AddOrUpdateFile" );
	}

	static void Deinitialize( GarrysMod::Lua::ILuaBase *LUA )
	{
		UnHook( AddOrUpdateFile_original );
	}

	void AddOrUpdateFile( LuaFile *file, bool reload )
	{
		LuaHelpers::PushHookRun( lua, hook_name );

		lua->PushString( file->path.c_str( ) );
		lua->PushBool( reload );

		bool shouldcall = true;
		if( LuaHelpers::CallHookRun( lua, 2, 1 ) )
			shouldcall = !lua->IsType( -1, GarrysMod::Lua::Type::BOOL ) || lua->GetBool( -1 );

		lua->Pop( 1 );

		if( shouldcall )
			return Call( AddOrUpdateFile_original, file, reload );
	}

	static GModDataPackProxy Singleton;
};

GModDataPackProxy::AddOrUpdateFile_t GModDataPackProxy::AddOrUpdateFile_original = nullptr;
const char GModDataPackProxy::hook_name[] = "AddOrUpdateCSLuaFile";
GarrysMod::Lua::ILuaBase *GModDataPackProxy::lua = nullptr;

static bool IsPathAllowed( std::string &filename )
{
	if( !V_RemoveDotSlashes( &filename[0], CORRECT_PATH_SEPARATOR, true ) )
		return false;

	filename.resize( std::strlen( filename.c_str( ) ) );

	const char *extension = V_GetFileExtension( filename.c_str( ) );
	if( extension == nullptr )
		return false;

	std::string ext = extension;
	std::transform( ext.begin( ), ext.end( ), ext.begin( ), [] ( uint8_t c )
	{
		return static_cast<char>( std::tolower( c ) );
	} );
	return whitelist_extensions.find( ext ) != whitelist_extensions.end( );
}

inline bool IsPathIDAllowed( std::string &pathid )
{
	std::transform( pathid.begin( ), pathid.end( ), pathid.begin( ), [] ( uint8_t c )
	{
		return static_cast<char>( std::tolower( c ) );
	} );
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

	LUA->PushBool( filesystem->RenameFile( fname.c_str( ), fnew.c_str( ), pathid.c_str( ) ) );
	return 1;
}

static void Initialize( GarrysMod::Lua::ILuaBase *LUA )
{
	SourceSDK::FactoryLoader engine_loader( "engine", false, true, "bin/" );

	IVEngineServer *engine_server = engine_loader.GetInterface<IVEngineServer>(
		INTERFACEVERSION_VENGINESERVER
	);
	if( engine_server == nullptr )
		LUA->ThrowError( "failed to retrieve server engine interface" );

	bool isdedicated = engine_server->IsDedicatedServer( );

	SymbolFinder symfinder;

	CreateInterfaceFn factory = reinterpret_cast<CreateInterfaceFn>( symfinder.ResolveOnBinary(
		( isdedicated ? filesystem_dedicated_lib : filesystem_lib ).c_str( ),
		isdedicated ? FileSystemFactory_dedicated_sym : FileSystemFactory_sym,
		isdedicated ? FileSystemFactory_dedicated_symlen : FileSystemFactory_symlen
	) );
	if( factory == nullptr )
		LUA->ThrowError( "unable to retrieve filesystem factory" );

	filesystem = static_cast<IFileSystem *>( factory( FILESYSTEM_INTERFACE_VERSION, nullptr ) );
	if( filesystem == nullptr )
		LUA->ThrowError( "failed to initialize IFileSystem" );

	GModDataPackProxy::Singleton.Initialize( LUA );

	LUA->GetField( GarrysMod::Lua::INDEX_GLOBAL, "luapack" );
	if( !LUA->IsType( -1, GarrysMod::Lua::Type::TABLE ) )
		LUA->ThrowError( "luapack table not found" );

	LUA->PushCFunction( Rename );
	LUA->SetField( -2, "Rename" );

	LUA->Pop( 1 );
}

static void Deinitialize( GarrysMod::Lua::ILuaBase *LUA )
{
	LUA->GetField( GarrysMod::Lua::INDEX_GLOBAL, "luapack" );
	if( !LUA->IsType( -1, GarrysMod::Lua::Type::TABLE ) )
		LUA->ThrowError( "luapack table not found" );

	LUA->PushNil( );
	LUA->SetField( -2, "Rename" );

	LUA->Pop( 1 );

	GModDataPackProxy::Singleton.Deinitialize( LUA );
}

}

GMOD_MODULE_OPEN( )
{
	luapack::Initialize( LUA );
	return 0;
}
 
GMOD_MODULE_CLOSE( )
{
	luapack::Deinitialize( LUA );
	return 0;
}
