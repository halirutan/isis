//
// C++ Interface: image
//
// Description:
//
//
// Author: Enrico Reimer<reimer@cbs.mpg.de>, (C) 2009
//
// Copyright: See COPYING file that comes with this distribution
//
//

#ifndef IMAGE_H
#define IMAGE_H

#include "chunk.hpp"

#include <set>
#include <boost/shared_ptr.hpp>
#include <vector>
#include <boost/foreach.hpp>
#include <boost/numeric/ublas/matrix.hpp>
#include <stack>
#include "sortedchunklist.hpp"

namespace isis
{
namespace data
{

class Image:
	public _internal::NDimensional<4>,
	public util::PropMap
{
public:
	enum orientation {axial, reversed_axial, sagittal, reversed_sagittal, coronal, reversed_coronal};

protected:
	_internal::SortedChunkList set;
	std::vector<boost::shared_ptr<Chunk> > lookup;
private:
	bool clean;
	size_t chunkVolume;

	/**
	 * Get the pointer to the chunk in the internal lookup-table at position at.
	 * The Chunk will only have metadata which are unique to it - so it might be invalid
	 * (run join on it using the image as parameter to insert all non-unique-metadata).
	 */
	const boost::shared_ptr<Chunk> &chunkPtrAt( size_t at )const;

	/**
	 * Computes chunk- and voxel- indices.
	 * The returned chunk-index applies to the lookup-table (chunkAt), and the voxel-index to this chunk.
	 * Behaviour will be undefined if:
	 * - the image is not clean (not indexed)
	 * - the image is empty
	 * - the coordinates are not in the image
	 *
	 * Additionally an error will be sent if Debug is enabled.
	 * \returns a std::pair\<chunk-index,voxel-index\>
	 */
	inline std::pair<size_t, size_t> commonGet ( size_t first, size_t second, size_t third, size_t fourth ) const {
		const size_t idx[] = {first, second, third, fourth};
		LOG_IF( ! clean, Debug, error )
				<< "Getting data from a non indexed image will result in undefined behavior. Run reIndex first.";
		LOG_IF( set.empty(), Debug, error )
				<< "Getting data from a empty image will result in undefined behavior.";
		LOG_IF( !rangeCheck( idx ), Debug, isis::error )
				<< "Index " << util::list2string( idx, idx + 4, "|" ) << " is out of range (" << sizeToString() << ")";
		const size_t index = dim2Index( idx );
		return std::make_pair( index / chunkVolume, index % chunkVolume );
	}


protected:
	static const char *needed;

	/**
	 * Search for a dimensional break in all stored chunks.
	 * This function searches for two chunks whose (geometrical) distance is more than twice
	 * the distance between the first and the second chunk. It wll assume a dimensional break
	 * at this position.
	 *
	 * Normally chunks are beneath each other (like characters in a text) so their distance is
	 * more or less constant. But if there is a dimensional break (analogous to the linebreak
	 * in a text) the distance between this particular chunks/characters is bigger than twice
	 * the normal distance
	 *
	 * For example for an image of 2D-chunks (slices) getChunkStride(1) will
	 * get the number of slices (size of third dim) and  getChunkStride(slices)
	 * will get the number of timesteps
	 * \param base_stride the base_stride for the iteration between chunks (1 for the first
	 * dimension, one "line" for the second and soon...)
	 * \returns the length of this chunk-"line" / the stride
	 */
	size_t getChunkStride( size_t base_stride = 1 );
	/**
	 * Access a chunk via index (and the lookup table)
	 * The Chunk will only have metadata which are unique to it - so it might be invalid
	 * (run join on it using the image as parameter to insert all non-unique-metadata).
	 */
	Chunk &chunkAt( size_t at );
public:
	/// Creates an empty Image object.
	Image();

	/**
	 * Copy constructor.
	 * Copies all elements, only the voxel-data (in the chunks) are referenced.
	 */
	Image( const Image &ref );

	/**
	 * Copy operator.
	 * Copies all elements, only the voxel-data (in the chunks) are referenced.
	 */
	Image &operator=( const Image &ref );

	bool checkMakeClean();
	/**
	 * This method returns a reference to the voxel value at the given coordinates.
	 *
	 * The voxel reference provides reading and writing access to the refered
	 * value.
	 *
	 * If the image is not clean, reIndex will be run.
	 * If the requested voxel is not of type T, an error will be raised.
	 *
	 * \param first The first coordinate in voxel space. Usually the x value / the read-encoded position..
	 * \param second The second coordinate in voxel space. Usually the y value / the phase-encoded position.
	 * \param third The third coordinate in voxel space. Ususally the z value / the time-encoded position.
	 * \param fourth The fourth coordinate in voxel space. Usually the time value.
	 *
	 * \returns A reference to the addressed voxel value. Reading and writing access
	 * is provided.
	 */
	template <typename T> T &voxel( size_t first, size_t second = 0, size_t third = 0, size_t fourth = 0 ) {
		checkMakeClean();
		const std::pair<size_t, size_t> index = commonGet( first, second, third, fourth );
		TypePtr<T> &data = chunkAt( index.first ).asTypePtr<T>();
		return data[index.second];
	}

	/**
	 * Get the value of the voxel value at the given coordinates.
	 *
	 * The voxel reference provides reading and writing access to the refered
	 * value.
	 *
	 * \param first The first coordinate in voxel space. Usually the x value / the read-encoded position..
	 * \param second The second coordinate in voxel space. Usually the y value / the phase-encoded position.
	 * \param third The third coordinate in voxel space. Ususally the z value / the time-encoded position.
	 * \param fourth The fourth coordinate in voxel space. Usually the time value.
	 *
	 * If the requested voxel is not of type T, an error will be raised.
	 *
	 * \returns A reference to the addressed voxel value. Only reading access is provided
	 */
	template <typename T> T voxel( size_t first, size_t second = 0, size_t third = 0, size_t fourth = 0 )const {
		const std::pair<size_t, size_t> index = commonGet( first, second, third, fourth );
		const TypePtr<T> &data = chunkPtrAt( index.first )->getTypePtr<T>();
		return data[index.second];
	}


	/**
	 * Get the type of the chunk with "biggest" type.
	 * Determines the minimum and maximum of the image, (and with that the types of these limits).
	 * If they are not the same, the type which can store the other type is selected.
	 * E.g. if min is "-5(int8_t)" and max is "1000(int16_t)" "int16_t" is selected.
	 * Warning: this will fail if min is "-5(int8_t)" and max is "70000(uint16_t)"
	 * \returns a number which is equal to the TypePtr::staticID of the selected type.
	 */
	unsigned short typeID() const;
	/// \returns the typename correspondig to the result of typeID
	std::string typeName() const;

	/**
	 * Get a chunk via index (and the lookup table).
	 * The returned chunk will be a cheap copy of the original chunk.
	 * If copy_metadata is true the metadata of the image is copied into the chunk.
	 */
	Chunk getChunkAt( size_t at, bool copy_metadata = true )const;

	/**
	 * Get the chunk that contains the voxel at the given coordinates.
	 *
	 * If the image is not clean, behaviour is undefined. (See Image::commonGet).
	 *
	 * \param first The first coordinate in voxel space. Usually the x value / the read-encoded position.
	 * \param second The second coordinate in voxel space. Usually the y value / the phase-encoded position.
	 * \param third The third coordinate in voxel space. Ususally the z value / the slice-encoded position.
	 * \param fourth The fourth coordinate in voxel space. Usually the time value.
	 * \param copy_metadata if true the metadata of the image are merged into the returned chunk
	 * \returns a copy of the chunk that contains the voxel at the given coordinates.
	 * (Reminder: Chunk-copies are cheap, so the data are NOT copied)
	 */
	const Chunk getChunk( size_t first, size_t second = 0, size_t third = 0, size_t fourth = 0, bool copy_metadata = true )const;

	/**
	 * Get the chunk that contains the voxel at the given coordinates.
	 * If the image is not clean Image::reIndex() will be run.
	 *
	 * \param first The first coordinate in voxel space. Usually the x value.
	 * \param second The second coordinate in voxel space. Usually the y value.
	 * \param third The third coordinate in voxel space. Ususally the z value.
	 * \param fourth The fourth coordinate in voxel space. Usually the time value.
	 * \param copy_metadata if true the metadata of the image are merged into the returned chunk
	 * \returns a copy of the chunk that contains the voxel at the given coordinates.
	 * (Reminder: Chunk-copies are cheap, so the data are NOT copied)
	 */
	Chunk getChunk( size_t first, size_t second = 0, size_t third = 0, size_t fourth = 0, bool copy_metadata = true );

	/**
	 * Get a sorted list of pointers to the chunks of the image.
	 * Note: this chunks only have metadata which are unique to them - so they might be invalid.
	 * (run join on copies of them using the image as parameter to insert all non-unique-metadata).
	 */
	std::vector<boost::shared_ptr<Chunk> > getChunkList();
	/// \copydoc getChunkList
	std::vector<boost::shared_ptr<const Chunk> > getChunkList()const;

	/**
	* Get the chunk that contains the voxel at the given coordinates in the given type.
	* If the accordant chunk has type T a cheap copy is returned.
	* Otherwise a MemChunk of the requested type is created from it.
	* In this case the minimum and maximum values of the image are computed and used for the MemChunk constructor.
	*
	* \param first The first coordinate in voxel space. Usually the x value.
	* \param second The second coordinate in voxel space. Usually the y value.
	* \param third The third coordinate in voxel space. Ususally the z value.
	* \param fourth The fourth coordinate in voxel space. Usually the time value.
	* \returns a (maybe converted) chunk containing the voxel value at the given coordinates.
	*/
	template<typename TYPE> Chunk getChunkAs( size_t first, size_t second = 0, size_t third = 0, size_t fourth = 0 )const {
		return getChunkAs<TYPE>( getScalingTo( TypePtr<TYPE>::staticID ), first, second, third, fourth );
	}
	/**
	 * Get the chunk that contains the voxel at the given coordinates in the given type (fast version).
	 * \copydetails getChunkAs
	 * This version does not compute the scaling, and thus is much faster.
	 * \param scaling the scaling (scale and offset) to be used if a conversion to the requested type is neccessary.
	 * \param first The first coordinate in voxel space. Usually the x value.
	 * \param second The second coordinate in voxel space. Usually the y value.
	 * \param third The third coordinate in voxel space. Ususally the z value.
	 * \param fourth The fourth coordinate in voxel space. Usually the time value.
	 * \returns a (maybe converted) chunk containing the voxel value at the given coordinates.
	 */
	template<typename TYPE> Chunk getChunkAs( const scaling_pair &scaling, size_t first, size_t second = 0, size_t third = 0, size_t fourth = 0 )const {
		Chunk ret = getChunk( first, second, third, fourth ); // get a cheap copy
		ret.makeOfTypeId( TypePtr<TYPE>::staticID, scaling ); // make it of type T
		return ret; //return that
	}

	///for each chunk get the scaling (and offset) which would be used in an conversion to the given type
	scaling_pair getScalingTo( unsigned short typeID, autoscaleOption scaleopt = autoscale )const;


	/**
	 * Insert a Chunk into the Image.
	 * The insertion is sorted and unique. So the Chunk will be inserted behind a geometrically "lower" Chunk if there is one.
	 * If there is allready a Chunk at the proposed position this Chunk wont be inserted.
	 *
	 * \param chunk The Chunk to be inserted
	 * \returns true if the Chunk was inserted, false otherwise.
	 */
	bool insertChunk( const Chunk &chunk );
	/**
	 * (Re)computes the image layout and metadata.
	 * The image will be "clean" on success.
	 * \returns true if the image was successfully reindexed and is valid, false otherwise.
	 */
	bool reIndex();

	/// \returns true if there is no chunk in the image
	bool empty()const;

	/**
	 * Get a list of the properties of the chunks for the given key
	 * \param key the name of the property to search for
	 * \param unique when true empty or consecutive duplicates wont be added
	 */
	std::list<util::PropertyValue> getChunksProperties( const util::PropMap::pname_type &key, bool unique = false )const;

	/// get the size of every voxel (in bytes)
	size_t bytes_per_voxel()const;

	/**
	 * Get the maximum and the minimum voxel value of the image.
	 * The results are stored as type T, if they dont fit an error ist send.
	 */
	template<typename T> void getMinMax( T &min, T &max )const {
		util::check_type<T>();// works only for T from _internal::types
		util::TypeReference _min, _max;
		getMinMax( _min, _max );
		min = _min->as<T>();
		max = _max->as<T>();
	}

	/// Get the maximum and the minimum voxel value of the image and store them as Type-object in the given references.
	void getMinMax( util::TypeReference &min, util::TypeReference &max )const;

	/**
	 * Compares the voxel-values of this image to the given.
	 * \returns the amount of the different voxels
	 */
	size_t cmp( const Image &comp )const;

	orientation getMainOrientation()const;
	/**
	 * Transforms the image coordinate system into an other system by multiplying
	 * the orientation matrix with a user defined transformation matrix. Additionally,
	 * the index origin will be transformed into the new coordinate system. This
	 * function only changes the
	 *
	 * <B>IMPORTANT!</B>: If you call this function with a matrix other than the
	 * identidy matrix, it's not guaranteed that the image is still in ISIS space
	 * according to the DICOM conventions. Eventuelly some ISIS algorithms that
	 * depend on correct image orientations won't work as expected. Use this method
	 * with caution!
	 */
	void transformCoords( boost::numeric::ublas::matrix<float> transform ) {
		isis::data::_internal::transformCoords( *this, transform );
	}

	/**
	 * Copy all voxel data of the image into memory.
	 * If neccessary a conversion into T is done using min/max of the image.
	 */
	template<typename T> void copyToMem( T *dst )const {
		if( checkMakeClean() ) {
			scaling_pair scale = getScalingTo( TypePtr<T>::staticID );
			// we could do this using makeOfTypeId - but this solution does not need any additional temporary memory
			BOOST_FOREACH( const boost::shared_ptr<Chunk> &ref, lookup ) {
				if( !ref->copyToMem<T>( dst, scale ) ) {
					LOG( Runtime, error ) << "Failed to copy raw data of type " << ref->typeName() << " from image into memory of type " << TypePtr<T>::staticName();
				}

				dst += ref->volume(); // increment the cursor
			}
		}
	}
	/**
	 * Ensure, the image has the type with the requested id.
	 * If the typeId of any chunk is not equal to the requested id, the data of the chunk is replaced by an converted version.
	 * The conversion is done using the value range of the image.
	 * \returns false if there was an error
	 */
	bool makeOfTypeId( unsigned short id );

	/**
	 * Automatically splice the given dimension and all dimensions above.
	 * e.g. spliceDownTo(sliceDim) will result in an image made of slices (aka 2d-chunks).
	 */
	size_t spliceDownTo( dimensions dim );
};

template<typename T> class MemImage: public Image
{
public:
	MemImage( const Image &src ) {
		operator=( src );
	}
	MemImage &operator=( const MemImage &ref ) { //use the copy for generic Images
		return operator=( static_cast<const Image &>( ref ) );
	}
	MemImage &operator=( const Image &ref ) { // copy the image, and make sure its of the given type
		Image::operator=( ref ); // ok we just copied the whole image
		//we want deep copies of the chunks, and we want them to be of type T
		struct : _internal::SortedChunkList::chunkPtrOperator {
			std::pair<util::TypeReference, util::TypeReference> scale;
			boost::shared_ptr<Chunk> operator()( const boost::shared_ptr< Chunk >& ptr ) {
				return boost::shared_ptr<Chunk>( new MemChunk<T>( *ptr, scale ) );
			}
		} conv_op;
		conv_op.scale = ref.getScalingTo( TypePtr<T>::staticID );
		LOG( Debug, info ) << "Computed scaling for conversion from source image: [" << conv_op.scale << "]";
		set.transform( conv_op );
		lookup = set.getLookup(); // the lookup table still points to the old chunks
		return *this;
	}
};

template<typename T> class TypedImage: public Image
{
public:
	TypedImage( const Image &src ): Image( src ) { // ok we just copied the whole image
		//but we want it to be of type T
		makeOfTypeId( TypePtr<T>::staticID );
	}
	TypedImage &operator=( const TypedImage &ref ) { //its already of the given type - so just copy it
		Image::operator=( ref );
		return *this;
	}
	TypedImage &operator=( const Image &ref ) { // copy the image, and make sure its of the given type
		Image::operator=( ref );
		makeOfTypeId( TypePtr<T>::staticID );
		return *this;
	}
};

class ImageList : public std::list< boost::shared_ptr<Image> >
{
public:
	ImageList();
	/**
	 * Create a number of images out of a Chunk list.
	 */
	ImageList( ChunkList src );
};
}
}

#endif // IMAGE_H
