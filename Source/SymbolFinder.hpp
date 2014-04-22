#ifndef __SYMBOL_FINDER_HPP__
#define __SYMBOL_FINDER_HPP__

#include <stdint.h>
#include <stddef.h>
#ifndef _WIN32
#include <vector>
#endif

class SymbolFinder
{
public:
	SymbolFinder( );
	~SymbolFinder( );

	void *FindPattern( const void *handle, const uint8_t *pattern, size_t len );
	void *FindSymbol( const void *handle, const char *symbol );
	void *FindSymbolFromBinary( const char *name, const char *symbol );

	// data can be a symbol name (if appended by @) or a pattern
	void *Resolve( const void *handle, const uint8_t *data, size_t len = 0 );
	void *ResolveOnBinary( const char *name, const uint8_t *data, size_t len = 0 );

private:
	bool GetLibraryInfo( const void *handle, struct DynLibInfo &info );

#if __linux
	std::vector<struct LibSymbolTable *> symbolTables;
#elif __APPLE__
	std::vector<struct LibSymbolTable *> symbolTables;
	struct dyld_all_image_infos *m_ImageList;
	int m_OSXMajor;
	int m_OSXMinor;
#endif
};

#endif
