/****************************************************************
 *
 * Copyright (C) 2010 Max Planck Institute for Human Cognitive and Brain Sciences, Leipzig
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 3
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 *
 * Author: Erik Tuerke, tuerke@cbs.mpg.de, 2010
 *
 *****************************************************************/


#ifndef VTKADAPTER_HPP_
#define VTKADAPTER_HPP_

#include "DataStorage/image.hpp"
#include "CoreUtils/log.hpp"

//external includes
#include <boost/shared_ptr.hpp>
#include <boost/foreach.hpp>

//vtk includes
#include <vtkImageData.h>
#include <vtkImageImport.h>
#include <vtkSmartPointer.h>

//TODO chunk handling

namespace isis
{
namespace adapter
{

/**
  * VTKAdapter is able of taking an isis image object and return a vector of vtkSmartpointers on vtkImageData objects.
  */
class vtkAdapter
{
public:
	typedef  std::pair< util::TypeReference, util::TypeReference> ScalingType;
	/**
	* Gets a std::vector of vtkSmartpointers on vtkImageData objects.
	*/
	static std::vector<vtkSmartPointer<vtkImageData> >makeVtkImageObject( const boost::shared_ptr<data::Image>, ScalingType & );
private:
protected:
	//should not be loaded directly
	vtkAdapter( const boost::shared_ptr<data::Image> );
	vtkAdapter( const vtkAdapter & ) {};


};



}
}// end namespace
#endif //VTKADAPTER_HPP_
