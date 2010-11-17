#include <DataStorage/io_interface.h>
#include <H5Cpp.h>

namespace isis
{
namespace image_io
{

class ImageFormat_hdf5: public FileFormat
{
protected:
	std::string suffixes()const {
		return std::string( ".h5" );
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
};
}
}
isis::image_io::FileFormat *factory()
{
	return new isis::image_io::ImageFormat_hdf5();
}
	