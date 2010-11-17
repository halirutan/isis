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
	unsigned short hdf5Type2isisType(const H5::DataType &hdfType);
	const H5::DataType & isisType2hdf5Type(unsigned short isisType);
private:
	TypeMap types;
};

}}}
#endif //HDF5_TYPEMAPPER_HPP