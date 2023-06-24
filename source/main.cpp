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

static std::unordered_set<std::string> whitelist_extensions = { "lua", "txt", "dat", "bsp"};

class GModDataPackProxy : public Detouring::ClassProxy<GModDataPack, GModDataPackProxy>
{
public:
	void Initialize( GarrysMod::Lua::ILuaBase *LUA )
	{
		lua = LUA;

		AddOrUpdateFile_original = FunctionPointers::GModDataPack_AddOrUpdateFile( );
		if( AddOrUpdateFile_original == nullptr )
			LUA->ThrowError( "failed to find GModDataPack::AddOrUpdateFile" );

		if( !Hook( AddOrUpdateFile_original, &GModDataPackProxy::AddOrUpdateFile ) )
			LUA->ThrowError( "failed to hook GModDataPack::AddOrUpdateFile" );
	}

	void Deinitialize( GarrysMod::Lua::ILuaBase *LUA )
	{
		UnHook( AddOrUpdateFile_original );
	}

	void AddOrUpdateFile( LuaFile *file, bool reload )
	{
		auto &self = Singleton;

		LuaHelpers::PushHookRun( self.lua, "AddOrUpdateCSLuaFile" );

		self.lua->PushString( file->path.c_str( ) );
		self.lua->PushBool( reload );

		bool shouldcall = true;
		if( LuaHelpers::CallHookRun( self.lua, 2, 1 ) )
			shouldcall = !self.lua->IsType( -1, GarrysMod::Lua::Type::BOOL ) || self.lua->GetBool( -1 );

		self.lua->Pop( 1 );

		if( shouldcall )
			return Call( self.AddOrUpdateFile_original, file, reload );
	}

	static GModDataPackProxy Singleton;

private:
	FunctionPointers::GModDataPack_AddOrUpdateFile_t AddOrUpdateFile_original = nullptr;
	GarrysMod::Lua::ILuaBase *lua = nullptr;
};

GModDataPackProxy GModDataPackProxy::Singleton;

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

LUA_FUNCTION_STATIC( Rename )
{
	LUA->CheckType( 1, GarrysMod::Lua::Type::STRING );
	LUA->CheckType( 2, GarrysMod::Lua::Type::STRING );

	std::string fname = LUA->GetString( 1 ), fnew = LUA->GetString( 2 );

	if( !IsPathAllowed( fname ) || !IsPathAllowed( fnew ) )
	{
		LUA->PushBool( false );
		return 1;
	}

	LUA->PushBool( filesystem->RenameFile( fname.c_str( ), fnew.c_str( ), "DATA" ) );
	return 1;
}

static void Initialize( GarrysMod::Lua::ILuaBase *LUA )
{
	filesystem = InterfacePointers::FileSystem( );
	if( filesystem == nullptr )
		LUA->ThrowError( "failed to initialize IFileSystem" );

	if( filesystem->FileExists( "lua/includes/init.lua", "MOD" ) )
	{
		if( filesystem->FileExists( "lua/includes/_init.lua", "MOD" ) )
		{
			Msg( "[gmsv_luapack_internal] 'lua/includes/_init.lua' already exists, deleting it!\n" );
			filesystem->RemoveFile( "lua/includes/_init.lua", "MOD" );
		}

		if( filesystem->RenameFile( "lua/includes/init.lua", "lua/includes/_init.lua", "MOD" ) )
			Msg( "[gmsv_luapack_internal] Renamed 'lua/includes/init.lua' to 'lua/includes/_init.lua'!\n" );
		else
			Warning( "[gmsv_luapack_internal] Failed to rename 'lua/includes/init.lua' to 'lua/includes/_init.lua'!\n" );
	}

	if( filesystem->FileExists( "lua/send.txt", "MOD" ) )
	{
		if( filesystem->FileExists( "lua/_send.txt", "MOD" ) )
		{
			Msg( "[gmsv_luapack_internal] 'lua/_send.txt' already exists, deleting it!\n" );
			filesystem->RemoveFile( "lua/_send.txt", "MOD" );
		}

		if( filesystem->RenameFile( "lua/send.txt", "lua/_send.txt", "MOD" ) )
			Msg( "[gmsv_luapack_internal] Renamed 'lua/send.txt' to 'lua/_send.txt'!\n" );
		else
			Warning( "[gmsv_luapack_internal] Failed to rename 'lua/send.txt' to 'lua/_send.txt'!\n" );
	}

	GModDataPackProxy::Singleton.Initialize( LUA );

	LUA->GetField( GarrysMod::Lua::INDEX_GLOBAL, "luapack" );
	if( !LUA->IsType( -1, GarrysMod::Lua::Type::TABLE ) )
	{
		GModDataPackProxy::Singleton.Deinitialize( LUA );
		LUA->ThrowError( "luapack table not found" );
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
