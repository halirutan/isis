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

// generic version - does nothing
template<int TYPE_GROUP,typename ISIS_TYPE> struct genH5DataType{
	void operator()(TypeMapper::TypeMap &map){};
};

// we not not (yet) support bool - do nothing
template<> struct genH5DataType<g_integer,bool>{
	void operator()(TypeMapper::TypeMap &map){};
};

// integer version - creates a fitting H5::IntType
template<typename ISIS_TYPE> struct genH5DataType<g_integer,ISIS_TYPE>{
	void operator()(TypeMapper::TypeMap &map){
		H5::IntType *ins=new H5::IntType;

		std::string order;
		ins->setOrder(H5T_ORDER_LE);
		ins->getOrder(order);
		std::cout << "Created integer type from " << data::TypePtr<ISIS_TYPE>::staticName() <<  " << with ordering: " << order <<  std::endl;

		ins->setSign(std::numeric_limits<ISIS_TYPE>::is_signed ? H5T_SGN_2:H5T_SGN_NONE);
		ins->setSize(sizeof(ISIS_TYPE));
		map[(unsigned short)data::TypePtr<ISIS_TYPE>::staticID].reset(ins);
	};
};

// float version - creates a fitting H5::FloatType
template<> struct genH5DataType<g_float,float>{
	void operator()(TypeMapper::TypeMap &map){
		map[(unsigned short)data::TypePtr<float>::staticID].reset(new H5::FloatType(H5::PredType::NATIVE_FLOAT));
	};
};
template<> struct genH5DataType<g_float,double>{
	void operator()(TypeMapper::TypeMap &map){
		map[(unsigned short)data::TypePtr<double>::staticID].reset(new H5::FloatType(H5::PredType::NATIVE_DOUBLE));
	};
};

///generate a TypeConverter for conversions from any SRC from the "types" list
struct TypeMapOp {
	TypeMapper::TypeMap &m_map;
	TypeMapOp( TypeMapper::TypeMap &map ): m_map( map ) {}
	template<typename ISIS_TYPE> void operator()( ISIS_TYPE ) {
		std::cout << "Creating a map for " << data::TypePtr<ISIS_TYPE>::staticName() <<  std::endl;
		if(boost::is_arithmetic<int>::value){ // ok its a number
			if(std::numeric_limits<ISIS_TYPE>::is_integer)
				genH5DataType<g_integer,ISIS_TYPE>()(m_map);
			else{ // to me this looks like a float
				genH5DataType<g_float,ISIS_TYPE>()(m_map);
			}
		} else {
			// well ... big things to come
		}
	}
};
	
TypeMapper::TypeMapper()
{
	boost::mpl::for_each<util::_internal::types>( TypeMapOp( types ) );
	LOG( Debug, info ) << "mapping " << types.size() << " isis types to hdf5-types";
}

short unsigned int TypeMapper::hdf5Type2isisType(const H5::DataType& hdfType)
{
	// search the typemap for a hdf5 type which is equal to the given type
	// H5::DataType is not less compareable so we cannot use a map here
	BOOST_FOREACH(TypeMap::value_type &ref, types){ 
		const H5::DataType &reftype=*(ref.second);
		if(reftype==hdfType)
			return ref.first;
	}
	// no type found - lets get out of here
	image_io::FileFormat::throwGenericError(
		std::string("Cannot map hdf5 datatype ") + hdfType.fromClass() + " to a isis datatype"
	); // @todo figure out how to get the "name" of a hdf5 type
	// return something - so the compiler does not complain
	return std::numeric_limits<unsigned short>::max();
}
const H5::DataType& TypeMapper::isisType2hdf5Type(short unsigned int isisType)
{
	TypeMap::iterator found=types.find(isisType);
	if(found==types.end()){ // no type found - lets get out of here
		image_io::FileFormat::throwGenericError(
			std::string("Cannot map isis datatype ") + util::getTypeMap( false,true)[isisType] + " to a hdf5 datatype"
		);
	}
	return *(found->second);
}


}}}