#include "ViewerCoreBase.hpp"

namespace isis
{
namespace viewer
{

ViewerCoreBase::ViewerCoreBase( data::Image image )
	: m_CurrentTimestep( 0 ),
	  m_CurrentImage( image )
{
	setCurrentImage( image );
}
void ViewerCoreBase::addImageList( const std::list< data::Image > imageList )
{
	if( !imageList.empty() ) {
		BOOST_FOREACH( std::list< data::Image >::const_reference imageRef, imageList ) {
			m_DataContainer.addImage( imageRef );
		}
	} else {
		LOG( Runtime, warning ) << "The image list passed to the core is empty!";
	}

	m_CurrentImage = getDataContainer()[m_DataContainer.size() - 1];

}



void ViewerCoreBase::setImageList( const std::list< data::Image > imageList )
{
	if( !imageList.empty() ) {
		m_DataContainer.clear();
		BOOST_FOREACH( std::list< data::Image >::const_reference imageRef, imageList ) {
			m_DataContainer.addImage( imageRef );
		}
	} else {
		LOG( Runtime, warning ) << "The image list passed to the core is empty!";
	}

	m_CurrentImage = getDataContainer()[m_DataContainer.size() - 1];
}


}
} // end namespace