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
	class Hdf5Chunk:public data::Chunk{
	public:
		Hdf5Chunk(unsigned short id,const util::FixedVector<hsize_t,4> &sizes,const H5::DataSet &src):
			Chunk(data::_internal::TypePtrBase::createById(id << 8,sizes.product()),sizes[3],sizes[2],sizes[1],sizes[0])
		{}
		void* getRawAddress(){
			return asTypePtrBase().getRawAddress().lock().get();
		}
	};
protected:
	std::string suffixes()const {
		return std::string( ".h5" );
	}
	H5::DataSet imageToDataSet(const data::Image& img,H5::CommonFG &group){
// 		group.createDataSet("Data",typeMap.isisType2hdf5Type(img.typeID()));
	}
	data::Chunk copyChunkFromDataSet(const H5::DataSet &src,const std::string &name){
			const unsigned short id= typeMap.hdf5Type2isisType(src); //@todo deal with non available types
			const H5::DataSpace space=src.getSpace();
			util::FixedVector<hsize_t,4> sizes;

			LOG(Debug,info) << "Loading HDF5 dataset " << util::MSubject( name ) << " as " << util::getTypeMap()[id];
			sizes.fill(1);
			space.getSimpleExtentDims(&sizes[4-space.getSimpleExtentNdims()],NULL);

			Hdf5Chunk ret(id,sizes,src);//create a fitting chunk
			src.read(ret.getRawAddress(),src.getDataType()); //read the data into it

			for(size_t i=src.getNumAttrs();i;--i){
				const H5::Attribute attr=src.openAttribute(i-1);
				
				const H5std_string name=attr.getName();
				const unsigned short id=typeMap.hdf5Type2isisType(attr);
				std::cout << "Attribute "<< name << " has type " << util::getTypeMap(true,false)[id] << std::endl;
			}

			return ret;
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
					if(space.getSimpleExtentNdims()>4){
						LOG(Runtime,warning) << "Entry " << util::MSubject(name) << " has more than 4 dimensions, ignoring it.";
						continue;
					}

					switch(obj.getTypeClass()){
					case H5T_INTEGER:
// 						std::cout << "IntType" << obj.getIntType().getPrecision() << (obj.getIntType().getSign() ? " signed":" unsigned") << std::endl;
						copyChunkFromDataSet(obj,name);
						break;
					case H5T_FLOAT:
// 						std::cout << "FloatType " << obj.getFloatType().getPrecision() << std::endl;
						copyChunkFromDataSet(obj,name);
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
	