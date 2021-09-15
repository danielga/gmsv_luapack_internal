#include <GarrysMod/Lua/Interface.h>
#include <GarrysMod/Lua/Helpers.hpp>
#include <GarrysMod/FunctionPointers.hpp>
#include <GarrysMod/InterfacePointers.hpp>

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
static IFileSystem *filesystem = nullptr;

static std::unordered_set<std::string> whitelist_extensions = { "lua", "txt", "dat" };
static std::unordered_set<std::string> whitelist_pathid = { "lsv", "lua", "data" };

class GModDataPackProxy : public Detouring::ClassProxy<GModDataPack, GModDataPackProxy>
{
private:
	static FunctionPointers::GModDataPack_AddOrUpdateFile_t AddOrUpdateFile_original;
	static const char hook_name[];
	static GarrysMod::Lua::ILuaBase *lua;

public:
	static void Initialize( GarrysMod::Lua::ILuaBase *LUA )
	{
		lua = LUA;

		AddOrUpdateFile_original = FunctionPointers::GModDataPack_AddOrUpdateFile( );
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

FunctionPointers::GModDataPack_AddOrUpdateFile_t GModDataPackProxy::AddOrUpdateFile_original = nullptr;
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
	filesystem = InterfacePointers::FileSystem( );
	if( filesystem == nullptr )
		LUA->ThrowError( "failed to initialize IFileSystem" );

	GModDataPackProxy::Singleton.Initialize( LUA );

	LUA->GetField( GarrysMod::Lua::INDEX_GLOBAL, "luapack" );
	if( !LUA->IsType( -1, GarrysMod::Lua::Type::TABLE ) )
	{
		GModDataPackProxy::Singleton.Deinitialize(LUA);
		LUA->ThrowError("luapack table not found");
	}

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
