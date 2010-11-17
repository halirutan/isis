#include <DataStorage/io_interface.h>
#include "imageFormat_hdf5_typeMapper.hpp"
#include <H5Cpp.h>

namespace isis
{
namespace image_io
{

class ImageFormat_hdf5: public FileFormat
{
	_internal::TypeMapper typeMap;
protected:
	std::string suffixes()const {
		return std::string( ".h5" );
	}
	H5::DataSet imageToDataSet(const data::Image& img,H5::CommonFG &group){
// 		group.createDataSet("Data",typeMap.isisType2hdf5Type(img.typeID()));
	}
public:
	std::string name()const {
		return "hdf5";
	}

	int load ( data::ChunkList &chunks, const std::string &filename, const std::string &dialect )  throw( std::runtime_error & ) {
		return 0; //return data::ChunkList();
	}

	void write( const data::Image &image, const std::string &filename, const std::string &dialect )  throw( std::runtime_error & ) {
	}
	void write(const isis::data::ImageList& images, const std::string& filename, const std::string& dialect)throw( std::runtime_error & ) {
		H5::H5File file( filename, H5F_ACC_TRUNC );
	}
};
}
}
isis::image_io::FileFormat *factory()
{
	return new isis::image_io::ImageFormat_hdf5();
}
	