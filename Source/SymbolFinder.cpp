#include "SymbolFinder.hpp"
#include <string>
#include <map>

#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN

#include <windows.h>

#elif defined __linux

#include <sys/stat.h>
#include <fcntl.h>
#include <dlfcn.h>
#include <elf.h>
#include <link.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#define PAGE_SIZE 4096
#define PAGE_ALIGN_UP( x ) ( ( x + PAGE_SIZE - 1 ) & ~( PAGE_SIZE - 1 ) )

#elif defined __APPLE__

#include <mach/task.h>
#include <mach-o/dyld_images.h>
#include <mach-o/loader.h>
#include <mach-o/nlist.h>
#include <string.h>
#include <sys/mman.h>

#endif

struct DynLibInfo
{
	void *baseAddress;
	size_t memorySize;
};

#if defined __linux || defined __APPLE__

struct LibSymbolTable
{
	std::map<std::string, void *> table;
	uintptr_t lib_base;
	uint32_t last_pos;
};

#endif

SymbolFinder::SymbolFinder( )
{

#ifdef __APPLE__

	Gestalt( gestaltSystemVersionMajor, &m_OSXMajor );
	Gestalt( gestaltSystemVersionMinor, &m_OSXMinor );

	if( ( m_OSXMajor == 10 && m_OSXMinor >= 6 ) || m_OSXMajor > 10 )
	{
		task_dyld_info_data_t dyld_info;
		mach_msg_type_number_t count = TASK_DYLD_INFO_COUNT;
		task_info( mach_task_self( ), TASK_DYLD_INFO, (task_info_t)&dyld_info, &count );
		m_ImageList = (struct dyld_all_image_infos *)dyld_info.all_image_info_addr;
	}
	else
	{
		struct nlist list[2];
		memset( list, 0, sizeof( list ) );
		list[0].n_un.n_name = (char *)"_dyld_all_image_infos";
		nlist( "/usr/lib/dyld", list );
		m_ImageList = (struct dyld_all_image_infos *)list[0].n_value;
	}

#endif

}

SymbolFinder::~SymbolFinder( )
{

#if defined __linux || defined __APPLE__

	for( size_t i = 0; i < symbolTables.size( ); ++i )
		delete symbolTables[i];

#endif

}

void *SymbolFinder::FindPattern( const void *handle, const uint8_t *pattern, size_t len )
{
	DynLibInfo lib;
	memset( &lib, 0, sizeof( DynLibInfo ) );
	if( !GetLibraryInfo( handle, lib ) )
		return NULL;

	uint8_t *ptr = reinterpret_cast<uint8_t *>( lib.baseAddress );
	uint8_t *end = ptr + lib.memorySize - len;
	bool found = false;
	while( ptr < end )
	{
		found = true;
		for( size_t i = 0; i < len; ++i )
			if( pattern[i] != '\x2A' && pattern[i] != ptr[i] )
			{
				found = false;
				break;
			}

		if( found )
			return ptr;

		++ptr;
	}

	return NULL;
}

void *SymbolFinder::FindSymbol( const void *handle, const char *symbol )
{

#ifdef _WIN32

	return (void *)GetProcAddress( (HMODULE)handle, symbol );

#elif defined __linux

	struct link_map *dlmap = (struct link_map *)handle;
	LibSymbolTable *libtable = NULL;
	for( size_t i = 0; i < symbolTables.size( ); ++i )
		if( symbolTables[i]->lib_base == dlmap->l_addr )
		{
			libtable = symbolTables[i];
			break;
		}

	if( libtable == NULL )
	{
		libtable = new LibSymbolTable( );
		libtable->lib_base = dlmap->l_addr;
		libtable->last_pos = 0;
		symbolTables.push_back( libtable );
	}

	std::map<std::string, void *> &table = libtable->table;
	void *symbol_ptr = table[symbol];
	if( symbol_ptr != NULL )
		return symbol_ptr;

	struct stat dlstat;
	int dlfile = open( dlmap->l_name, O_RDONLY );
	if( dlfile == -1 || fstat( dlfile, &dlstat ) == -1 )
	{
		close( dlfile );
		return NULL;
	}

	Elf32_Ehdr *file_hdr = (Elf32_Ehdr *)mmap( 0, dlstat.st_size, PROT_READ, MAP_PRIVATE, dlfile, 0 );
	uintptr_t map_base = (uintptr_t)file_hdr;
	close( dlfile );
	if( file_hdr == MAP_FAILED )
		return NULL;

	if( file_hdr->e_shoff == 0 || file_hdr->e_shstrndx == SHN_UNDEF )
	{
		munmap( file_hdr, dlstat.st_size );
		return NULL;
	}

	Elf32_Shdr *symtab_hdr = NULL, *strtab_hdr = NULL;
	Elf32_Shdr *sections = (Elf32_Shdr *)( map_base + file_hdr->e_shoff );
	uint16_t section_count = file_hdr->e_shnum;
	Elf32_Shdr *shstrtab_hdr = &sections[file_hdr->e_shstrndx];
	const char *shstrtab = (const char *)( map_base + shstrtab_hdr->sh_offset );
	for( uint16_t i = 0; i < section_count; i++ )
	{
		Elf32_Shdr &hdr = sections[i];
		const char *section_name = shstrtab + hdr.sh_name;
		if( strcmp( section_name, ".symtab" ) == 0 )
			symtab_hdr = &hdr;
		else if( strcmp( section_name, ".strtab" ) == 0 )
			strtab_hdr = &hdr;
	}

	if( symtab_hdr == NULL || strtab_hdr == NULL )
	{
		munmap( file_hdr, dlstat.st_size );
		return NULL;
	}

	Elf32_Sym *symtab = (Elf32_Sym *)( map_base + symtab_hdr->sh_offset );
	const char *strtab = (const char *)( map_base + strtab_hdr->sh_offset );
	uint32_t symbol_count = symtab_hdr->sh_size / symtab_hdr->sh_entsize;
	void *symbol_pointer = NULL;
	for( uint32_t i = libtable->last_pos; i < symbol_count; i++ )
	{
		Elf32_Sym &sym = symtab[i];
		unsigned char sym_type = ELF32_ST_TYPE( sym.st_info );
		const char *sym_name = strtab + sym.st_name;

		if( sym.st_shndx == SHN_UNDEF || ( sym_type != STT_FUNC && sym_type != STT_OBJECT ) )
			continue;

		void *symptr = (void *)( dlmap->l_addr + sym.st_value );
		table[sym_name] = symptr;
		if( strcmp( sym_name, symbol ) == 0 )
		{
			libtable->last_pos = ++i;
			symbol_pointer = symptr;
			break;
		}
	}

	munmap( file_hdr, dlstat.st_size );
	return symbol_pointer;

#elif defined __APPLE__

	uintptr_t dlbase = 0;
	uint32_t image_count = m_ImageList->infoArrayCount;
	struct segment_command *linkedit_hdr = NULL;
	struct symtab_command *symtab_hdr = NULL;
	for( uint32_t i = 1; i < image_count; i++ )
	{
		const struct dyld_image_info &info = m_ImageList->infoArray[i];
		void *h = dlopen( info.imageFilePath, RTLD_NOLOAD );
		if( h == handle )
		{
			dlbase = (uintptr_t)info.imageLoadAddress;
			dlclose( h );
			break;
		}

		dlclose( h );
	}

	if( dlbase == 0 )
		return NULL;

	LibSymbolTable *libtable = NULL;
	for( size_t i = 0; i < symbolTables.size( ); ++i )
		if( symbolTables[i]->lib_base == dlbase )
		{
			libtable = symbolTables[i];
			break;
		}

	if( libtable == NULL )
	{
		libtable = new LibSymbolTable( );
		libtable->lib_base = dlbase;
		libtable->last_pos = 0;
		symbolTables.push_back( libtable );
	}

	std::map<std::string, void *> &table = libtable->table;
	void *symbol_ptr = table[symbol];
	if( symbol_ptr != NULL )
		return symbol_ptr;

	struct mach_header *file_hdr = (struct mach_header *)dlbase;
	struct load_command *loadcmds = (struct load_command *)( dlbase + sizeof( struct mach_header ) );
	uint32_t loadcmd_count = file_hdr->ncmds;
	for( uint32_t i = 0; i < loadcmd_count; i++ )
	{
		if( loadcmds->cmd == LC_SEGMENT && linkedit_hdr == NULL )
		{
			struct segment_command *seg = (struct segment_command *)loadcmds;
			if( strcmp( seg->segname, "__LINKEDIT" ) == 0 )
			{
				linkedit_hdr = seg;
				if( symtab_hdr != NULL )
					break;
			}
		}
		else if( loadcmds->cmd == LC_SYMTAB )
		{
			symtab_hdr = (struct symtab_command *)loadcmds;
			if( linkedit_hdr != NULL )
				break;
		}

		loadcmds = (struct load_command *)( (uintptr_t)loadcmds + loadcmds->cmdsize );
	}

	if( linkedit_hdr == NULL || symtab_hdr == NULL || symtab_hdr->symoff == 0 || symtab_hdr->stroff == 0 )
		return 0;

	uintptr_t linkedit_addr = dlbase + linkedit_hdr->vmaddr;
	struct nlist *symtab = (struct nlist *)( linkedit_addr + symtab_hdr->symoff - linkedit_hdr->fileoff );
	const char *strtab = (const char *)( linkedit_addr + symtab_hdr->stroff - linkedit_hdr->fileoff );
	uint32_t symbol_count = symtab_hdr->nsyms;
	void *symbol_pointer = NULL;
	for( uint32_t i = libtable->last_pos; i < symbol_count; i++ )
	{
		struct nlist &sym = symtab[i];
		const char *sym_name = strtab + sym.n_un.n_strx + 1;
		if( sym.n_sect == NO_SECT )
			continue;

		void *symptr = (void *)( dlmap->l_addr + sym.st_value );
		table[sym_name] = symptr;
		if( strcmp( sym_name, symbol ) == 0 )
		{
			libtable->last_pos = ++i;
			symbol_pointer = symptr;
			break;
		}
	}

	return symbol_pointer;

#endif

}

void *SymbolFinder::FindSymbolFromBinary( const char *name, const char *symbol )
{

#ifdef _WIN32

	HMODULE binary = NULL;
	if( GetModuleHandleEx( NULL, name, &binary ) == TRUE && binary != NULL )
	{
		void *symbol_pointer = FindSymbol( binary, symbol );
		FreeModule( binary );
		return symbol_pointer;
	}

#elif defined __linux || defined __APPLE__

	void *binary = dlopen( name, RTLD_NOW | RTLD_LOCAL );
	if( binary != NULL )
	{
		void *symbol_pointer = FindSymbol( binary, symbol );
		dlclose( binary );
		return symbol_pointer;
	}

#endif

	return NULL;
}

void *SymbolFinder::Resolve( const void *handle, const uint8_t *data, size_t len )
{
	if( data[0] == '@' )
		return FindSymbol( handle, (const char *)++data );

	if( len != 0 )
		return FindPattern( handle, data, len );

	return NULL;
}

void *SymbolFinder::ResolveOnBinary( const char *name, const uint8_t *data, size_t len )
{

#ifdef _WIN32

	HMODULE binary = NULL;
	if( GetModuleHandleEx( NULL, name, &binary ) == TRUE && binary != NULL )
	{
		void *symbol_pointer = Resolve( binary, data, len );
		FreeModule( binary );
		return symbol_pointer;
	}

#elif defined __linux || defined __APPLE__

	void *binary = dlopen( name, RTLD_NOW | RTLD_LOCAL );
	if( binary != NULL )
	{
		void *symbol_pointer = Resolve( binary, data, len );
		dlclose( binary );
		return symbol_pointer;
	}

#endif

	return NULL;
}

bool SymbolFinder::GetLibraryInfo( const void *handle, DynLibInfo &lib )
{
	if( handle == NULL )
		return false;

#ifdef _WIN32

	MEMORY_BASIC_INFORMATION info;
	if( VirtualQuery( handle, &info, sizeof( MEMORY_BASIC_INFORMATION ) ) == FALSE )
		return false;

	uintptr_t baseAddr = reinterpret_cast<uintptr_t>( info.AllocationBase );

	IMAGE_DOS_HEADER *dos = reinterpret_cast<IMAGE_DOS_HEADER *>( baseAddr );
	IMAGE_NT_HEADERS *pe = reinterpret_cast<IMAGE_NT_HEADERS *>( baseAddr + dos->e_lfanew );
	IMAGE_FILE_HEADER *file = &pe->FileHeader;
	IMAGE_OPTIONAL_HEADER *opt = &pe->OptionalHeader;

	if( dos->e_magic != IMAGE_DOS_SIGNATURE || pe->Signature != IMAGE_NT_SIGNATURE || opt->Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC )
		return false;

	if( file->Machine != IMAGE_FILE_MACHINE_I386 )
		return false;

	if( ( file->Characteristics & IMAGE_FILE_DLL ) == 0 )
		return false;

	lib.memorySize = opt->SizeOfImage;

#elif defined __linux

	Dl_info info;
	if( dladdr( handle, &info ) == 0 )
		return false;

	if( info.dli_fbase == NULL || info.dli_fname == NULL )
		return false;

	uintptr_t baseAddr = reinterpret_cast<uintptr_t>( info.dli_fbase );
	Elf32_Ehdr *file = reinterpret_cast<Elf32_Ehdr *>( baseAddr );
	if( memcmp( ELFMAG, file->e_ident, SELFMAG ) != 0 )
		return false;

	if( file->e_ident[EI_VERSION] != EV_CURRENT )
		return false;

	if( file->e_ident[EI_CLASS] != ELFCLASS32 || file->e_machine != EM_386 || file->e_ident[EI_DATA] != ELFDATA2LSB )
		return false;

	if( file->e_type != ET_DYN )
		return false;

	uint16_t phdrCount = file->e_phnum;
	Elf32_Phdr *phdr = reinterpret_cast<Elf32_Phdr *>( baseAddr + file->e_phoff );
	for( uint16_t i = 0; i < phdrCount; ++i )
	{
		Elf32_Phdr &hdr = phdr[i];
		if( hdr.p_type == PT_LOAD && hdr.p_flags == ( PF_X | PF_R ) )
		{
			lib.memorySize = PAGE_ALIGN_UP( hdr.p_filesz );
			break;
		}
	}

#elif defined __APPLE__

	Dl_info info;
	if( dladdr( handle, &info ) == 0 )
		return false;

	if( info.dli_fbase == NULL || info.dli_fname == NULL )
		return false;

	uintptr_t baseAddr = (uintptr_t)info.dli_fbase;
	struct mach_header *file = (struct mach_header *)baseAddr;
	if( file->magic != MH_MAGIC )
		return false;

	if( file->cputype != CPU_TYPE_I386 || file->cpusubtype != CPU_SUBTYPE_I386_ALL )
		return false;

	if( file->filetype != MH_DYLIB )
		return false;

	uint32_t cmd_count = file->ncmds;
	struct segment_command *seg = (struct segment_command *)( baseAddr + sizeof( struct mach_header ) );

	for( uint32_t i = 0; i < cmd_count; ++i )
	{
		if( seg->cmd == LC_SEGMENT )
			lib.memorySize += seg->vmsize;

		seg = (struct segment_command *)( (uintptr_t)seg + seg->cmdsize );
	}

#endif

	lib.baseAddress = reinterpret_cast<void *>( baseAddr );
	return true;
}
