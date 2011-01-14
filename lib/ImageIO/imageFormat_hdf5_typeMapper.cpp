#include <DataStorage/common.hpp>
#include "imageFormat_hdf5_typeMapper.hpp"
#include <CoreUtils/types.hpp>
#include <DataStorage/typeptr.hpp>
#include <DataStorage/io_interface.h>

#include <boost/mpl/for_each.hpp>
#include <boost/foreach.hpp>
#include <boost/type_traits/is_arithmetic.hpp>

namespace isis{
namespace image_io{
namespace _internal{

enum type_goups {g_integer,g_float,g_complex};

//////////////////////////////////////////////////////////////////////////////////
// get Types by their properties - hfd5 like duck-typing
//////////////////////////////////////////////////////////////////////////////////

// generic version - does nothing
template<bool INTEGER,typename ISIS_TYPE> struct getArithType{
	void operator()(TypeMapper::Type2hdf5Mapping &map){};
};

// we not not (yet) support bool - do nothing
template<> struct getArithType<true,bool>{
	void operator()(TypeMapper::Type2hdf5Mapping &map){};
};

// integer version - describe it by bitsize and sign
template<typename ISIS_TYPE> struct getArithType<true,ISIS_TYPE>{
	void operator()(TypeMapper::Type2hdf5Mapping &map){
		const unsigned char bitsize = sizeof(ISIS_TYPE)*8;
		LOG(Debug,verbose_info)
			<< "Mapping " << util::Type<ISIS_TYPE>::staticName() << " to a "
			<< (std::numeric_limits< ISIS_TYPE >::is_signed ? "signed ":"unsigned ")
			<< (int)bitsize << "bit H5::IntType";
		(std::numeric_limits< ISIS_TYPE >::is_signed ? map.integerType.signedMap:map.integerType.unsignedMap)[bitsize]=util::Type<ISIS_TYPE>::staticID;
	}
};

// float version - describe it by bitsize 
template<typename ISIS_TYPE> struct getArithType<false,ISIS_TYPE>{
	void operator()(TypeMapper::Type2hdf5Mapping &map){
		const unsigned char bitsize = sizeof(ISIS_TYPE)*8;
		LOG(Debug,verbose_info) << "Mapping " << util::Type<ISIS_TYPE>::staticName() << " to a " << (int)bitsize << "bit H5::FloatType";
		map.floatType.map[bitsize]=util::Type<ISIS_TYPE>::staticID;
	};
};


template<bool IS_ARITH,typename ISIS_TYPE> struct getType{
	void operator()(TypeMapper::Type2hdf5Mapping &map){};
};
template<typename ISIS_TYPE> struct getType<true,ISIS_TYPE>{
	void operator()(TypeMapper::Type2hdf5Mapping &map){
		getArithType<std::numeric_limits<ISIS_TYPE>::is_integer,ISIS_TYPE>()(map);
	};
};
template<> struct getType<false,std::basic_string< char > >{
	void operator()(TypeMapper::Type2hdf5Mapping &map){ // @todo check if this is ok on windows
		map.strType.map[H5T_CSET_ASCII]=util::Type<std::basic_string< char > >::staticID;
	};
};

/////////////////////////////////////////////////////////////////////////////////////
// in medias res
//////////////////////////////////////////////////////////////////////////////////////

///generate a Type2hdf5Mapping-entry for conversions from any SRC from the "types" list
struct TypeMapOp {
	TypeMapper::Type2hdf5Mapping &m_map;
	TypeMapOp( TypeMapper::Type2hdf5Mapping &map ): m_map( map ) {}
	template<typename ISIS_TYPE> void operator()( ISIS_TYPE ) {
		getType<boost::is_arithmetic<ISIS_TYPE>::value,ISIS_TYPE>()(m_map);
	}
};
	
TypeMapper::TypeMapper()
{
	boost::mpl::for_each<util::_internal::types>( TypeMapOp( type2hfd_map ) );
}

/**
 * Get the corresponding isis-typeID for a hdf5 type
 * \returns Type::staticID for the type fitting the given hdf5 type, zero if no type was found.
 */
short unsigned int TypeMapper::hdf5Type2isisType(const H5::AbstractDs & ds)
{
// @todo handle case when the given hdf5 type has a different byte order
// 		H5T_order_t order;
// #if __BYTE_ORDER == __LITTLE_ENDIAN
// 		order = H5T_ORDER_LE;
// #elif __BYTE_ORDER == __BIG_ENDIAN
// 		order = H5T_ORDER_BE;
// #elif __BYTE_ORDER == __PDP_ENDIAN
// 		order = H5T_ORDER_VAX;
// #else
// #error "Sorry your endianess is not supported. What the heck are you comiling on ??"
// #endif

	// find a isis type which is duck-equal to the given hdf5 type
	switch(ds.getTypeClass()){
		case H5T_INTEGER:{
			const H5::IntType t=ds.getIntType();
			switch(t.getSign()){
			case H5T_SGN_2:
				return type2hfd_map.integerType.signedMap[t.getPrecision()];
				break;
			case H5T_SGN_NONE:
				return type2hfd_map.integerType.unsignedMap[t.getPrecision()];
				break;
			} // @todo hanle other cases
		}break;
		case H5T_FLOAT:{
			const H5::FloatType t=ds.getFloatType();
			return type2hfd_map.floatType.map[t.getPrecision()];
		}break;
		case H5T_STRING:{
			const H5::StrType t=ds.getStrType();
			const H5T_cset_t cset=t.getCset();
			return type2hfd_map.strType.map[cset];
		}break;
	}
	return 0; // nothing found
}
const H5::DataType& TypeMapper::isisType2hdf5Type(short unsigned int isisType)
{
/*	TypeMap::iterator found=types.find(isisType);
	if(found==types.end()){ // no type found - lets get out of here
		image_io::FileFormat::throwGenericError(
			std::string("Cannot map isis datatype ") + util::getTypeMap( false,true)[isisType] + " to a hdf5 datatype"
		);
	}
	return *(found->second);*/
}


}}}