#include "types.hpp"

namespace isis{
namespace util{

#define setName(type,name) template<> std::string _internal::name_visitor::operator()<type>(const type&)const{return name;}

setName( int8_t, "s8bit" );
setName( uint8_t, "u8bit" );

setName( int16_t, "s16bit" );
setName( uint16_t, "u16bit" );

setName( int32_t, "s32bit" );
setName( uint32_t, "u32bit" );

setName( int64_t, "s64bit" );
setName( uint64_t, "u64bit" );

setName( float, "float" );
setName( double, "double" );

setName( color24, "color24" );
setName( color48, "color48" );

setName( fvector3, "fvector3" );
setName( fvector4, "fvector4" );
setName( dvector3, "dvector3" );
setName( dvector4, "dvector4" );
setName( ivector3, "ivector3" );
setName( ivector4, "ivector4" );
 
setName( ilist, "list<int32_t>" );
setName( dlist, "list<double>" );
setName( slist, "list<string>" );

setName( std::complex<float>, "complex<float>" );
setName( std::complex<double>, "complex<double>" );

setName( std::string, "string" );
// setName( Selection, "selection" );

setName( timestamp, "timestamp" );
setName( duration, "duration" );
setName( date, "date" );

#undef setName

}
}
