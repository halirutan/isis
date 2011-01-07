#include <DataStorage/io_interface.h>
#include <DataStorage/typeptr_base.hpp>
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
	data::Chunk copyChunkFromDataSet(const H5::DataSet &data,const H5::DataType &type){
		const unsigned short id= typeMap.hdf5Type2isisType(type);
		const H5::DataSpace space=data.getSpace();
		hsize_t dim=space.getSimpleExtentNdims();
		util::FixedVector<hsize_t,4> dims;
		if(dim >= 2 && dim <= 4){
			LOG(Debug,info) << "Loading HDF5 dataset as " << util::getTypeMap()[id];
			dims.fill(1);
			space.getSimpleExtentDims(&dims[0],NULL);
			data::_internal::TypePtrBase::Reference buff= data::_internal::TypePtrBase::createById(id << 8,dims.product()); // we need the pointer here, hence the "<< 8"
 			data.read(buff->getRawAddress().lock().get(),type);
		} else {
			LOG(Runtime,error) << "Cannot load " << dim << "D data.";
		}
	}
	void loadTree(const H5::CommonFG &data,const H5std_string& prefix){
		const size_t ocount=data.getNumObjs();
		for(size_t i=0;i<ocount;i++){
			const H5std_string name=prefix +"/"+data.getObjnameByIdx(i);
			switch(data.getObjTypeByIdx(i)){
				case H5G_GROUP:
					loadTree(data.openGroup(name),name);
					break;
				case H5G_DATASET:
					H5::DataSet obj=data.openDataSet(name);
					H5::DataSpace space=obj.getSpace();
					if(space.getSimpleExtentNdims()<2){
						LOG(Runtime,warning) << "Entry " << util::MSubject(name) << " doesn't look like an image (less than 2D), ignoring it.";
						continue;
					}

					switch(obj.getTypeClass()){
					case H5T_INTEGER:
// 						std::cout << "IntType" << obj.getIntType().getPrecision() << (obj.getIntType().getSign() ? " signed":" unsigned") << std::endl;
						copyChunkFromDataSet(obj,obj.getIntType());
						break;
					case H5T_FLOAT:
// 						std::cout << "FloatType " << obj.getFloatType().getPrecision() << std::endl;
						copyChunkFromDataSet(obj,obj.getFloatType());
						break;
					case H5T_ARRAY:
					case H5T_TIME:
					case H5T_STRING:
					case H5T_BITFIELD:
					case H5T_COMPOUND:
					case H5T_ENUM:
					case H5T_VLEN:
						LOG(Runtime,warning) << "Entry " << util::MSubject(name) << " has an unsupportet object class, ignoring it.";
						break;
					default:
						LOG(Runtime,error) << "Sorry invalid object class " << util::MSubject(obj.getTypeClass()) << " ignoring " << util::MSubject(name);
						break;
					}
					break;
			}
			
		}
	}
public:
	std::string name()const {
		return "hdf5";
	}

	int load ( data::ChunkList &chunks, const std::string &filename, const std::string &dialect )  throw( std::runtime_error & ) {
		H5::H5File file( filename, H5F_ACC_RDONLY );
		loadTree(file,"");
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
	