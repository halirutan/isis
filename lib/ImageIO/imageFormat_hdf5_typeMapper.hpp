#ifndef HDF5_TYPEMAPPER_HPP
#define HDF5_TYPEMAPPER_HPP

#include <map>
#include <boost/shared_ptr.hpp>
#include <H5Cpp.h>

namespace isis{
namespace image_io{
namespace _internal{

class TypeMapper{
public:
	typedef std::map<unsigned short,boost::shared_ptr<H5::DataType> > TypeMap;
	TypeMapper();
	unsigned short hdf5Type2isisType(const H5::AbstractDs &hdfType);
	const H5::DataType & isisType2hdf5Type(unsigned short isisType);
	struct Type2hdf5Mapping{
		struct {
			std::map<unsigned char,unsigned short> unsignedMap,signedMap; // precission => type
		}integerType;
		struct {
			std::map<unsigned char,unsigned short> map; // precission => type
		}floatType;
		struct {
			std::map<unsigned char,unsigned short> map; // charset => type (currently only H5T_CSET_ASCII)
		}strType;
	};
private:
	Type2hdf5Mapping type2hfd_map;
};

}}}
#endif //HDF5_TYPEMAPPER_HPP