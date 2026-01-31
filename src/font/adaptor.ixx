module;

#include <freetype/freetype.h>
#include <msdfgen/msdfgen-ext.h>

export module mo_yanxi.msdf_adaptor;

export auto adopt_msdfgen_hld(FT_Face face){
	return msdfgen::adoptFreetypeFont(face);
}