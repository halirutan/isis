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
		const unsigned char bitsize = sizeof(ISIS_TYPE);
		LOG(Debug,verbose_info) << "Mapping " << util::Type<ISIS_TYPE>::staticName() << " to a " << bitsize << "bit H5::IntType";
		(std::numeric_limits< ISIS_TYPE >::is_signed ? map.integerType.signedMap:map.integerType.unsignedMap)[bitsize]=util::Type<ISIS_TYPE>::staticID;
	}
};

// float version - describe it by bitsize 
template<typename ISIS_TYPE> struct getArithType<false,ISIS_TYPE>{
	void operator()(TypeMapper::Type2hdf5Mapping &map){
		const unsigned char bitsize = sizeof(ISIS_TYPE);
		LOG(Debug,verbose_info) << "Mapping " << util::Type<ISIS_TYPE>::staticName() << " to a " << bitsize << "bit H5::FloatType";
		map.floatType.map[bitsize]=util::Type<ISIS_TYPE>::staticID;
	};
};


///generate a Type2hdf5Mapping-entry for conversions from any SRC from the "types" list
struct TypeMapOp {
	TypeMapper::Type2hdf5Mapping &m_map;
	TypeMapOp( TypeMapper::Type2hdf5Mapping &map ): m_map( map ) {}
	template<typename ISIS_TYPE> void operator()( ISIS_TYPE ) {
		std::cout << "Creating a map for " << data::TypePtr<ISIS_TYPE>::staticName() <<  std::endl;
		if(boost::is_arithmetic<int>::value){ // ok its a number
			getArithType<std::numeric_limits<ISIS_TYPE>::is_integer,ISIS_TYPE>()(m_map);
		} else {
			// well ... big things to come
		}
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
short unsigned int TypeMapper::hdf5Type2isisType(const H5::DataType& hdfType)
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
	if(hdfType.detectClass(H5T_INTEGER)){
		const H5::IntType &t=dynamic_cast<const H5::IntType&>(hdfType);
		switch(t.getSign()){
		case H5T_SGN_2:
			return type2hfd_map.integerType.signedMap[t.getPrecision()];
			break;
		case H5T_SGN_NONE:
			return type2hfd_map.integerType.unsignedMap[t.getPrecision()];
			break;
		}
	} else if(hdfType.detectClass(H5T_FLOAT)){
		const H5::FloatType &t=dynamic_cast<const H5::FloatType&>(hdfType);
		return type2hfd_map.floatType.map[t.getPrecision()];
	} else if(hdfType.detectClass(H5T_STRING)){
		const H5::StrType &t=dynamic_cast<const H5::StrType&>(hdfType);
		return type2hfd_map.strType.map[t.getCset()];
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