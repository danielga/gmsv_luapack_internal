#include <GarrysMod/Lua/Interface.h>
#include <GarrysMod/Lua/LuaInterface.h>
#include <SymbolFinder.hpp>
#include <detours.h>
#include <stdexcept>
#include <string>
#include <sstream>
#include <stdint.h>
#include <sha1.h>

#define HASHER_METATABLE "SHA1"
#define HASHER_TYPE 31

#define THROW_ERROR( error ) ( LUA->ThrowError( error ), 0 )
#define LUA_ERROR( ) THROW_ERROR( LUA->GetString( ) )

#define GET_USERDATA( index ) reinterpret_cast<GarrysMod::Lua::UserData *>( LUA->GetUserdata( index ) )
#define GET_HASHER( index ) reinterpret_cast<SHA1_CTX *>( GET_USERDATA( index )->data )
#define VALIDATE_HASHER( hasher ) if( hasher == nullptr ) return THROW_ERROR( HASHER_METATABLE " object is not valid" )

#if defined _WIN32

	#define GOOD_SEPARATOR '\\'
	#define BAD_SEPARATOR '/'

	#define PARENT_DIRECTORY "..\\"
	#define CURRENT_DIRECTORY ".\\"

	#define __FASTCALL __fastcall
	#define __THISCALL __thiscall

	#define SERVER_BINARY "server.dll"

	#define ADDORUPDATEFILE_SYM "\x55\x8B\xEC\x83\xEC\x18\x53\x56\x8B\x75\x08\x83\xC6\x04\x83\x7E"
	#define ADDORUPDATEFILE_SYMLEN 16

#elif defined __linux || defined __APPLE__

	#define GOOD_SEPARATOR '/'
	#define BAD_SEPARATOR '\\'

	#define PARENT_DIRECTORY "../"
	#define CURRENT_DIRECTORY "./"

	#define __CDECL __attribute__((cdecl))

	#if defined __linux

		#define SERVER_BINARY "garrysmod/bin/server_srv.so"

		#define SYMBOL_PREFIX "@"

	#else

		#define SERVER_BINARY "garrysmod/bin/server.dylib"

		#define SYMBOL_PREFIX "@_"

	#endif

	#define ADDORUPDATEFILE_SYM SYMBOL_PREFIX "_ZN12GModDataPack15AddOrUpdateFileEP7LuaFileb"
	#define ADDORUPDATEFILE_SYMLEN 0

#endif

class GModDataPack;

struct LuaFile
{
	uint32_t skip;
	const char *path;
};

static GarrysMod::Lua::ILuaInterface *lua = nullptr;

LUA_FUNCTION_STATIC( ErrorTraceback )
{
	GarrysMod::Lua::ILuaInterface *lua = static_cast<GarrysMod::Lua::ILuaInterface *>( LUA );

	std::string spaces( 2, ' ' );
	std::ostringstream stream;
	stream << LUA->GetString( 1 ) << '\n';

	lua_Debug dbg = { 0 };
	for( int level = 1; lua->GetStack( level, &dbg ) == 1; ++level, memset( &dbg, 0, sizeof( dbg ) ) )
	{
		if( level != 1 )
			stream << '\n';

		lua->GetInfo( "Sln", &dbg );
		stream
			<< spaces
			<< level
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

LUA_FUNCTION_STATIC( HookRun )
{
	int args = LUA->Top( );

	LUA->PushSpecial( GarrysMod::Lua::SPECIAL_GLOB );
	LUA->GetField( -1, "hook" );
	LUA->GetField( -1, "Run" );

	for( int i = 1; i <= args; ++i )
		LUA->Push( i );

	LUA->Call( args, 1 );
	return 1;
}

#if defined _WIN32

typedef void ( __THISCALL *AddOrUpdateFile_t ) ( GModDataPack *self, LuaFile *file, bool force );

#elif defined __linux || defined __APPLE__

typedef void ( __CDECL *AddOrUpdateFile_t ) ( GModDataPack *self, LuaFile *file, bool force );

#endif
 
static AddOrUpdateFile_t AddOrUpdateFile = nullptr;
static MologieDetours::Detour<AddOrUpdateFile_t> *AddOrUpdateFile_d = nullptr;

#if defined _WIN32

void __FASTCALL AddOrUpdateFile_h( GModDataPack *self, void *, LuaFile *file, bool b )

#elif defined __linux || defined __APPLE__

void __CDECL AddOrUpdateFile_h( GModDataPack *self, LuaFile *file, bool b )

#endif

{
	lua->PushCFunction( ErrorTraceback );
	lua->PushCFunction( HookRun );
	lua->PushString( "AddOrUpdateCSLuaFile" );
	lua->PushString( file->path );
	lua->PushBool( b );

	bool dontcall = false;
	if( lua->PCall( 3, 1, -5 ) == 0 )
		dontcall = lua->IsType( -1, GarrysMod::Lua::Type::BOOL ) && !lua->GetBool( -1 );
	else
		lua->Msg( "\n[ERROR] %s\n\n", lua->GetString( -1 ) );

	lua->Pop( 1 );

	if( !dontcall )
		return AddOrUpdateFile( self, file, b );
}

LUA_FUNCTION_STATIC( hasher__new )
{
	try
	{
		SHA1_CTX *context = new SHA1_CTX;
		SHA1Init( context );

		void *luadata = LUA->NewUserdata( sizeof( GarrysMod::Lua::UserData ) );
		GarrysMod::Lua::UserData *userdata = reinterpret_cast<GarrysMod::Lua::UserData *>( luadata );
		userdata->data = context;
		userdata->type = HASHER_TYPE;

		LUA->CreateMetaTableType( HASHER_METATABLE, HASHER_TYPE );
		LUA->SetMetaTable( -2 );

		return 1;
	}
	catch( std::exception &e )
	{
		LUA->PushString( e.what( ) );
	}
 
	return LUA_ERROR( );
}

LUA_FUNCTION_STATIC( hasher__gc )
{
	LUA->CheckType( 1, HASHER_TYPE );

	GarrysMod::Lua::UserData *userdata = GET_USERDATA( 1 );
	SHA1_CTX *hasher = reinterpret_cast<SHA1_CTX *>( userdata->data );
	VALIDATE_HASHER( hasher );

	userdata->data = 0;

	delete hasher;
	return 0;
}

LUA_FUNCTION_STATIC( hasher_update )
{
	LUA->CheckType( 1, HASHER_TYPE );
	LUA->CheckType( 2, GarrysMod::Lua::Type::STRING );

	SHA1_CTX *hasher = GET_HASHER( 1 );
	VALIDATE_HASHER( hasher );

	uint32_t len = 0;
	const uint8_t *data = reinterpret_cast<const uint8_t *>( LUA->GetString( 2, &len ) );

	SHA1Update( hasher, const_cast<uint8_t *>( data ), len );

	return 0;
}

LUA_FUNCTION_STATIC( hasher_final )
{
	LUA->CheckType( 1, HASHER_TYPE );

	SHA1_CTX *hasher = GET_HASHER( 1 );
	VALIDATE_HASHER( hasher );

	uint8_t digest[20];
	SHA1Final( hasher, digest );

	LUA->PushString( reinterpret_cast<const char *>( digest ), sizeof( digest ) );
	return 1;
}

static void SubstituteChar( std::string &path, char part, char sub )
{
	size_t pos = path.find( part );
	while( pos != path.npos )
	{
		path.erase( pos, 1 );
		path.insert( pos, 1, sub );
		pos = path.find( part, pos + 1 );
	}
}

static void RemovePart( std::string &path, const char *part )
{
	size_t len = strlen( part ), pos = path.find( part );
	while( pos != path.npos )
	{
		path.erase( pos, len );
		pos = path.find( part, pos );
	}
}

static bool HasWhitelistedExtension( const std::string &path )
{
	size_t extstart = path.rfind( '.' );
	if( extstart != path.npos )
	{
		size_t lastslash = path.rfind( GOOD_SEPARATOR );
		if( lastslash != path.npos && lastslash > extstart )
			return false;

		std::string ext = path.substr( extstart + 1 );
		return ext == "txt" || ext == "dat" || ext == "lua";
	}

	return false;
}

static bool Rename( const char *f, const char *t )
{
	std::string from = "garrysmod";
	from += GOOD_SEPARATOR;
	from += f;

	SubstituteChar( from, BAD_SEPARATOR, GOOD_SEPARATOR );
	if( !HasWhitelistedExtension( from ) )
		return false;

	std::string to = "garrysmod";
	to += GOOD_SEPARATOR;
	to += t;

	SubstituteChar( to, BAD_SEPARATOR, GOOD_SEPARATOR );
	if( !HasWhitelistedExtension( to ) )
		return false;

	RemovePart( from, PARENT_DIRECTORY );
	RemovePart( from, CURRENT_DIRECTORY );

	RemovePart( to, PARENT_DIRECTORY );
	RemovePart( to, CURRENT_DIRECTORY );

	return rename( from.c_str( ), to.c_str( ) ) == 0;
}

LUA_FUNCTION_STATIC( luapack_rename )
{
	LUA->CheckType( 1, GarrysMod::Lua::Type::STRING );
	LUA->CheckType( 2, GarrysMod::Lua::Type::STRING );

	LUA->PushBool( Rename( LUA->GetString( 1 ), LUA->GetString( 2 ) ) );
	return 1;
}

GMOD_MODULE_OPEN( )
{
	lua = reinterpret_cast<GarrysMod::Lua::ILuaInterface *>( LUA );

	try
	{
		lua->PushSpecial( GarrysMod::Lua::SPECIAL_GLOB );

		lua->GetField( -1, "luapack" );
		if( !lua->IsType( -1, GarrysMod::Lua::Type::TABLE ) )
			throw std::runtime_error( "luapack table not found" );

		lua->PushCFunction( luapack_rename );
		lua->SetField( -2, "Rename" );

		lua->PushCFunction( hasher__new );
		lua->SetField( -2, HASHER_METATABLE );

		LUA->CreateMetaTableType( HASHER_METATABLE, HASHER_TYPE );

		LUA->Push( -1 );
		LUA->SetField( -2, "__index" );

		LUA->PushCFunction( hasher__gc );
		LUA->SetField( -2, "__gc" );

		LUA->PushCFunction( hasher_update );
		LUA->SetField( -2, "Update" );

		LUA->PushCFunction( hasher_final );
		LUA->SetField( -2, "Final" );

		SymbolFinder symfinder;

		AddOrUpdateFile = reinterpret_cast<AddOrUpdateFile_t>( symfinder.ResolveOnBinary( SERVER_BINARY, ADDORUPDATEFILE_SYM, ADDORUPDATEFILE_SYMLEN ) );
		if( AddOrUpdateFile == nullptr )
			throw std::runtime_error( "GModDataPack::AddOrUpdateFile detour failed" );

		AddOrUpdateFile_d = new MologieDetours::Detour<AddOrUpdateFile_t>( AddOrUpdateFile, reinterpret_cast<AddOrUpdateFile_t>( AddOrUpdateFile_h ) );

		AddOrUpdateFile = AddOrUpdateFile_d->GetOriginalFunction( );

		lua->Msg( "[luapack_internal] GModDataPack::AddOrUpdateFile detoured.\n" );

		return 0;
	}
	catch( std::exception &e )
	{
		LUA->PushString( e.what( ) );
	}
 
	return LUA_ERROR( );
}
 
GMOD_MODULE_CLOSE( )
{
	(void)state;
	delete AddOrUpdateFile_d;
	return 0;
}