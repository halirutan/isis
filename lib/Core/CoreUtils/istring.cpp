/*
    <one line to give the program's name and a brief idea of what it does.>
    Copyright (C) <year>  <name of author>

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.

*/

#include "istring.hpp"
#ifdef _MSC_VER
#include "common.hpp"
#include <Windows.h>
#else
#include <strings.h>
#endif

namespace isis
{
namespace util
{
namespace _internal
{

std::locale const ichar_traits::loc = std::locale( "C" );

int ichar_traits::compare( const char *s1, const char *s2, size_t n )
{
#ifdef _MSC_VER
	// this _must_ be compare with a length - so never ever think about using lstrcmpiA
	switch(CompareString(
		LOCALE_INVARIANT, /*Well not exactly "C", but close enough */
		NORM_IGNORECASE, /*a bit redundant to LOCALE_INVARIANT but may be thats a good thing*/
		s1,n,
		s2,n
		)){
	case CSTR_LESS_THAN:return -1;break;
	case CSTR_EQUAL:return 0;break;
	case CSTR_GREATER_THAN:return 1;break;
	default:
		LOG(Debug,error) << "Wtf, string compare failed ? Not good!";
		break;
	}
	std::stringstream buff;
	buff << "String compare failed in " << __FILE__ << ":" << __LINE__;
	throw(std::logic_error(buff.str()));
	// oh my god what a mess .. :-(
	return 0;
#else
	return strncasecmp( s1, s2, n );
#endif
}

bool ichar_traits::eq( const char &c1, const char &c2 )
{
	return std::tolower( c1, loc ) == std::tolower( c2, loc );
}

bool ichar_traits::lt( const char &c1, const char &c2 )
{
	return std::tolower( c1, loc ) < std::tolower( c2, loc );
}

const char *ichar_traits::find( const char *s, size_t n, const char &a )
{
	for( size_t i = 0; i < n; i++, s++ ) {
		if( eq( *s, a ) )
			return s;
	}

	return NULL;
}


}
}
}

namespace boost
{
template<> isis::util::istring lexical_cast< isis::util::istring, std::string >( const std::string &arg ) {return isis::util::istring( arg.begin(), arg.end() );}
template<> std::string lexical_cast< std::string, isis::util::istring >( const isis::util::istring &arg ) {return std::string( arg.begin(), arg.end() );}
}