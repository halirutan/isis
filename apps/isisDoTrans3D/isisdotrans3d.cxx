/****************************************************************
 *
 *  Copyright (C) 2010 Max Planck Institute for Human Cognitive and Brain Sciences, Leipzig
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

#include <boost/algorithm/string.hpp>
#include <boost/foreach.hpp>

#include <itkImageFileReader.h>
#include <itkImageFileWriter.h>
#include <itkImageSeriesWriter.h>

#include <itkNearestNeighborInterpolateImageFunction.h>
#include <itkBSplineInterpolateImageFunction.h>
#include <itkLinearInterpolateImageFunction.h>
#include <itkWindowedSincInterpolateImageFunction.h>

#include <itkResampleImageFilter.h>
#include <itkWarpImageFilter.h>
#include <itkCastImageFilter.h>

#include <itkTileImageFilter.h>

#include <itkTransformFileReader.h>

//isis includes
#include "CoreUtils/log.hpp"
#include "DataStorage/io_factory.hpp"
#include "DataStorage/image.hpp"
#include "CoreUtils/application.hpp"
#include "Adapter/itkAdapter.hpp"

#include "extITK/isisTimeStepExtractionFilter.hpp"
#include "isisTransformMerger3D.hpp"
#include "extITK/isisIterationObserver.hpp"

//via command parser include
#include <viaio/option.h>
#include <viaio/mu.h> //this is required for VNumber
//command line parser options

VDictEntry TYPInterpolator[] = { {"Linear", 0}, {"BSpline", 1}, {"NearestNeighbor", 2}, {NULL}};

static VString in_filename = NULL;
static VString out_filename = NULL;
static VArgVector trans_filename;
static VString vtrans_filename;
static VString template_filename = NULL;
static VBoolean in_found, out_found, trans_found;
static VShort interpolator_type;
static VArgVector resolution;
static VBoolean fmri;
static VBoolean use_inverse = false;
static VShort number_threads = 1;

static VOptionDescRec options[] = {
	//requiered inputs
	{"in", VStringRepn, 1, &in_filename, &in_found, 0, "the input image filename"}, {
		"out", VStringRepn, 1,
		&out_filename, &out_found, 0, "the output image filename"
	},

	//non-required inputs
	{"itktrans", VStringRepn, 0, &trans_filename, &trans_found, 0, "the itk transform filename"},
	{
		"interpolator", VShortRepn,
		1, &interpolator_type, VOptionalOpt, TYPInterpolator, "The interpolator used to resample the image"
	}, {"ref", VStringRepn, 1,
		&template_filename, VOptionalOpt, 0, "The template image"
	   }, {"reso", VFloatRepn, 0, ( VPointer ) &resolution,
		   VOptionalOpt, 0, "The output resolution. One value for isotrop output"
		  },
	{
		"fmri", VBooleanRepn, 1, &fmri,
		VOptionalOpt, 0, "Input and output image file are functional data"
	}, {"trans", VStringRepn, 1,
		&vtrans_filename, VOptionalOpt, 0, "Vector deformation field"
	   }, {"use_inverse", VBooleanRepn, 1, &use_inverse,
		   VOptionalOpt, 0, "Using the inverse of the transform"
		  }, {"j" , VShortRepn, 1, &number_threads, VOptionalOpt, 0 , "Number of threads"}

};

#include <boost/concept_check.hpp>
int main(

	int argc, char *argv[] )
{
	// show revision information string constant
	std::cout << "Core Version: " << isis::util::Application::getCoreVersion() << std::endl;
	isis::util::enable_log<isis::util::DefaultMsgPrint>( isis::error );
	isis::data::enable_log<isis::util::DefaultMsgPrint>( isis::error );
	isis::image_io::enable_log<isis::util::DefaultMsgPrint>( isis::error );

	// DANGER! Kids don't try this at home! VParseCommand modifies the values of argc and argv!!!
	if ( !VParseCommand( VNumber( options ), options, &argc, argv ) || !VIdentifyFiles( VNumber( options ), options, "in",
			&argc, argv, 0 ) || !VIdentifyFiles( VNumber ( options ), options, "out", &argc, argv, -1 ) ) {
		VReportUsage( argv[0], VNumber( options ), options, NULL );
		exit( 1 );
	}

	// VParseCommand reduces the argv vector to the name of the program and  unknown command line parameters.
	if ( argc > 1 ) {
		VReportBadArgs( argc, argv );
		VReportUsage( argv[0], VNumber( options ), options, NULL );
		exit( 1 );
	}

	//typedef section
	typedef float PixelType;
	const unsigned int Dimension = 3;
	const unsigned int fmriDimension = 4;
	typedef itk::Vector<float, 3> VectorType;
	typedef itk::Image<VectorType, 3> DeformationFieldType;
	typedef itk::Image<PixelType, Dimension> InputImageType;
	typedef itk::Image<PixelType, Dimension> OutputImageType;
	typedef itk::Image<PixelType, fmriDimension> FMRIInputType;
	typedef itk::Image<PixelType, fmriDimension> FMRIOutputType;
	typedef itk::ResampleImageFilter<InputImageType, OutputImageType> ResampleImageFilterType;
	typedef itk::WarpImageFilter<OutputImageType, OutputImageType, DeformationFieldType> WarpImageFilterType;
	typedef itk::CastImageFilter<InputImageType, OutputImageType> CastImageFilterType;
	typedef itk::ImageFileReader<InputImageType> ImageReaderType;
	typedef itk::ImageFileWriter<OutputImageType> ImageFileWriter;
	typedef itk::ImageFileReader<FMRIInputType> FMRIImageReaderType;
	typedef itk::ImageFileWriter<FMRIOutputType> FMRIImageWriterType;
	typedef itk::ImageFileReader<DeformationFieldType> DeformationFieldReaderType;
	typedef itk::ImageSeriesWriter<FMRIOutputType, OutputImageType> ImageSeriesWriterType;
	typedef isis::extitk::TimeStepExtractionFilter<FMRIInputType, InputImageType> TimeStepExtractionFilterType;
	typedef itk::TileImageFilter<OutputImageType, FMRIOutputType> TileImageFitlerType;
	typedef const itk::Transform<double, Dimension, Dimension>* ConstTransformPointer;
	typedef itk::Transform<double, Dimension, Dimension>* TransformPointer;
	itk::Transform<double, Dimension, Dimension>::ParametersType parameters;
	typedef itk::LinearInterpolateImageFunction<OutputImageType, double> LinearInterpolatorType;
	typedef itk::NearestNeighborInterpolateImageFunction<OutputImageType, double> NearestNeighborInterpolatorType;
	typedef itk::BSplineInterpolateImageFunction<OutputImageType, double> BSplineInterpolatorType;
	LinearInterpolatorType::Pointer linearInterpolator = LinearInterpolatorType::New();
	NearestNeighborInterpolatorType::Pointer nearestNeighborInterpolator = NearestNeighborInterpolatorType::New();
	BSplineInterpolatorType::Pointer bsplineInterpolator = BSplineInterpolatorType::New();
	itk::TransformFileReader::Pointer transformFileReader = itk::TransformFileReader::New();
	ResampleImageFilterType::Pointer resampler = ResampleImageFilterType::New();
	WarpImageFilterType::Pointer warper = WarpImageFilterType::New();
	CastImageFilterType::Pointer caster = CastImageFilterType::New();
	isis::extitk::ProcessUpdate::Pointer progressObserver = isis::extitk::ProcessUpdate::New();
	TimeStepExtractionFilterType::Pointer timeStepExtractionFilter = TimeStepExtractionFilterType::New();
	isis::extitk::TransformMerger3D *transformMerger = new isis::extitk::TransformMerger3D;
	DeformationFieldReaderType::Pointer deformationFieldReader = DeformationFieldReaderType::New();
	ImageReaderType::Pointer reader = ImageReaderType::New();
	ImageReaderType::Pointer templateReader = ImageReaderType::New();
	ImageFileWriter::Pointer writer = ImageFileWriter::New();
	ImageFileWriter::Pointer testwriter = ImageFileWriter::New();
	FMRIImageReaderType::Pointer fmriReader = FMRIImageReaderType::New();
	FMRIImageWriterType::Pointer fmriWriter = FMRIImageWriterType::New();
	ImageSeriesWriterType::Pointer seriesWriter = ImageSeriesWriterType::New();
	TileImageFitlerType::Pointer tileImageFilter = TileImageFitlerType::New();
	OutputImageType::SpacingType outputSpacing;
	OutputImageType::SizeType outputSize;
	OutputImageType::PointType outputOrigin;
	OutputImageType::DirectionType outputDirection;
	OutputImageType::SizeType fmriOutputSize;
	OutputImageType::SpacingType fmriOutputSpacing;
	OutputImageType::DirectionType fmriOutputDirection;
	OutputImageType::PointType fmriOutputOrigin;
	InputImageType::Pointer tmpImage = InputImageType::New();
	FMRIInputType::Pointer fmriImage = FMRIInputType::New();
	InputImageType::Pointer inputImage = InputImageType::New();
	InputImageType::Pointer templateImage = InputImageType::New();
	isis::adapter::itkAdapter *fixedAdapter = new isis::adapter::itkAdapter;
	isis::adapter::itkAdapter *movingAdapter = new isis::adapter::itkAdapter;
	//transform object used for inverse transform
	itk::MatrixOffsetTransformBase<double, Dimension, Dimension>::Pointer transform = itk::MatrixOffsetTransformBase<double, Dimension, Dimension>::New();

	if ( !trans_filename.number and !vtrans_filename ) {
		std::cout << "No transform specified!!" << std::endl;
		return EXIT_FAILURE;
	}

	resampler->SetNumberOfThreads( number_threads );
	warper->SetNumberOfThreads( number_threads );
	progress_timer time;
	isis::data::ImageList tmpList;

	//if template file is specified by the user
	if ( template_filename ) {
		tmpList = isis::data::IOFactory::load( template_filename, "" );
		LOG_IF( tmpList.empty(), isis::DataLog, isis::error ) << "Template image is empty!";
		templateImage = fixedAdapter->makeItkImageObject<InputImageType>( tmpList.front()  );
	}

	//setting up the output resolution
	if ( resolution.number ) {
		if ( static_cast<unsigned int> ( resolution.number ) < Dimension ) {
			//user has specified less than 3 resolution values->sets isotrop resolution with the first typed value
			outputSpacing.Fill( ( ( VFloat * ) resolution.vector )[0] );
		}

		if ( resolution.number >= 3 ) {
			//user has specified at least 3 values -> sets anisotrop resolution
			for ( unsigned int i = 0; i < 3; i++ ) {
				outputSpacing[i] = ( ( VFloat * ) resolution.vector )[i];
			}
		}
	}

	if ( !resolution.number ) {
		if ( template_filename ) {
			outputSpacing = templateImage->GetSpacing();
			outputSize = templateImage->GetLargestPossibleRegion().GetSize();
		}

		if ( !template_filename ) {
			outputSpacing = inputImage->GetSpacing();
			outputSize = inputImage->GetLargestPossibleRegion().GetSize();
		}
	}

	isis::data::ImageList inList = isis::data::IOFactory::load( in_filename, "", "" );
	BOOST_FOREACH( isis::data::ImageList::reference ref, inList ) {
		if ( tmpList.front()->hasProperty( "Vista/extent" ) ) {
			ref->setProperty<std::string>( "Vista/extent", tmpList.front()->getProperty<std::string>( "Vista/extent" ) );
		}

		if ( tmpList.front()->hasProperty( "Vista/ca" ) && tmpList.front()->hasProperty( "Vista/cp" ) ) {
			std::vector< std::string > caTuple;
			std::vector< std::string > cpTuple;
			std::string ca = tmpList.front()->getProperty<std::string>( "Vista/ca" );
			std::string cp = tmpList.front()->getProperty<std::string>( "Vista/cp" );
			isis::util::fvector4 oldVoxelSize = tmpList.front()->getProperty<isis::util::fvector4>( "voxelSize" );
			boost::algorithm::split( caTuple, ca, boost::algorithm::is_any_of( " " ) );
			boost::algorithm::split( cpTuple, cp, boost::algorithm::is_any_of( " " ) );

			for ( size_t dim = 0; dim < 3; dim++ ) {
				float caFloat = boost::lexical_cast<float>( caTuple[dim] );
				float cpFloat = boost::lexical_cast<float>( cpTuple[dim] );
				float catmp = caFloat * ( oldVoxelSize[dim] / outputSpacing[dim] );
				float cptmp = cpFloat * ( oldVoxelSize[dim] / outputSpacing[dim] );
				caTuple[dim] = std::string( boost::lexical_cast<std::string>( catmp ) );
				cpTuple[dim] = std::string( boost::lexical_cast<std::string>( cptmp ) );
			}

			std::string newCa = caTuple[0] + std::string( " " ) + caTuple[1] + std::string( " " ) + caTuple[2];
			std::string newCp = cpTuple[0] + std::string( " " ) + cpTuple[1] + std::string( " " ) + cpTuple[2];
			ref->setProperty<std::string>( "Vista/ca", newCa );
			ref->setProperty<std::string>( "Vista/cp", newCp );
		}
	}

	if ( !fmri ) {
		LOG_IF( inList.empty(), isis::DataLog, isis::error ) << "Input image is empty!";
		inputImage = movingAdapter->makeItkImageObject<InputImageType>( inList.front() );
	}

	if ( fmri ) {
		LOG_IF( inList.empty(), isis::DataLog, isis::error ) << "Input image is empty!";
		fmriImage = movingAdapter->makeItkImageObject<FMRIInputType>( inList.front() );
	}

	if ( !template_filename ) {
		outputDirection = inputImage->GetDirection();
		outputOrigin = inputImage->GetOrigin();
	} else {
		outputDirection = templateImage->GetDirection();
		outputOrigin = templateImage->GetOrigin();
	}

	if ( trans_filename.number ) {
		unsigned int number_trans = trans_filename.number;

		if ( number_trans > 1 ) {
			std::cout << "More than one transform is set. This is not possible, yet!" << std::endl;

			for ( unsigned int i = 0; i < number_trans; i++ ) {
				itk::TransformFileReader::TransformListType *transformList =
					new itk::TransformFileReader::TransformListType;
				transformFileReader->SetFileName( ( ( VStringConst * ) trans_filename.vector )[i] );
				transformFileReader->Update();
				transformList = transformFileReader->GetTransformList();
				itk::TransformFileReader::TransformListType::const_iterator ti = transformList->begin();
				transformMerger->push_back( ( *ti ).GetPointer() );
			}

			transformMerger->setTemplateImage<InputImageType>( templateImage );
			transformMerger->merge();
			warper->SetDeformationField( transformMerger->getTransform() );
		}

		if ( number_trans == 1 ) {
			transformFileReader->SetFileName( ( ( VStringConst * ) trans_filename.vector )[0] );
			transformFileReader->Update();
			itk::TransformFileReader::TransformListType *transformList = transformFileReader->GetTransformList();
			itk::TransformFileReader::TransformListType::const_iterator ti;
			ti = transformList->begin();

			//setting up the resample object
			if ( use_inverse ) {
				transform->SetParameters( static_cast<TransformPointer>( ( *ti ).GetPointer() )->GetInverseTransform()->GetParameters() );
				transform->SetFixedParameters( static_cast<TransformPointer>( ( *ti ).GetPointer() )->GetInverseTransform()->GetFixedParameters() );
				resampler->SetTransform( transform );
			}

			if ( !use_inverse ) {
				resampler->SetTransform( static_cast<ConstTransformPointer> ( ( *ti ).GetPointer() ) );
			}
		}
	}

	if ( vtrans_filename ) {
		deformationFieldReader->SetFileName( vtrans_filename );
		deformationFieldReader->Update();
	}

	if ( resolution.number && template_filename ) {
		for ( unsigned int i = 0; i < 3; i++ ) {
			//output spacing = (template size / output resolution) * template resolution
			outputSize[i] = ( ( templateImage->GetLargestPossibleRegion().GetSize()[i] ) / outputSpacing[i] )
							* templateImage->GetSpacing()[i];
		}
	}

	if ( resolution.number && !template_filename ) {
		for ( unsigned int i = 0; i < 3; i++ ) {
			//output spacing = (moving size / output resolution) * moving resolution
			if ( !fmri ) {
				outputSize[i] = ( ( inputImage->GetLargestPossibleRegion().GetSize()[i] ) / outputSpacing[i] )
								* inputImage->GetSpacing()[i];
			}

			if ( fmri ) {
				outputSize[i] = ( ( fmriImage->GetLargestPossibleRegion().GetSize()[i] ) / outputSpacing[i] )
								* fmriImage->GetSpacing()[i];
			}
		}
	}

	//setting up the interpolator
	switch ( interpolator_type ) {
	case 0:
		resampler->SetInterpolator( linearInterpolator );
		warper->SetInterpolator( linearInterpolator );
		break;
	case 1:
		resampler->SetInterpolator( bsplineInterpolator );
		warper->SetInterpolator( bsplineInterpolator );
		break;
	case 2:
		resampler->SetInterpolator( nearestNeighborInterpolator );
		warper->SetInterpolator( nearestNeighborInterpolator );
		break;
	}

	if ( !fmri ) {
		writer->SetFileName( out_filename );

		if ( !vtrans_filename && trans_filename.number == 1 ) {
			resampler->AddObserver( itk::ProgressEvent(), progressObserver );
			resampler->SetInput( inputImage );
			resampler->SetOutputSpacing( outputSpacing );
			resampler->SetSize( outputSize );
			resampler->SetOutputOrigin( outputOrigin );
			resampler->SetOutputDirection( outputDirection );
			resampler->Update();
			isis::data::ImageList imgList = movingAdapter->makeIsisImageObject<OutputImageType>( resampler->GetOutput() );
			isis::data::IOFactory::write( imgList, out_filename, "" , "" );
			// DEBUG
			//          writer->SetInput(resampler->GetOutput());
			//          writer->Update();
		}

		if ( vtrans_filename or trans_filename.number > 1 ) {
			warper->AddObserver( itk::ProgressEvent(), progressObserver );
			warper->SetOutputDirection( outputDirection );
			warper->SetOutputOrigin( outputOrigin );
			warper->SetOutputSize( outputSize );
			warper->SetOutputSpacing( outputSpacing );
			warper->SetInput( inputImage );

			if ( trans_filename.number == 0 ) {
				warper->SetDeformationField( deformationFieldReader->GetOutput() );
			}

			warper->Update();
			isis::data::ImageList imgList = movingAdapter->makeIsisImageObject<OutputImageType>( warper->GetOutput() );
			isis::data::IOFactory::write( imgList, out_filename, "", "" );
		}
	}

	if ( fmri ) {
		timeStepExtractionFilter->SetInput( fmriImage );
		fmriWriter->SetFileName( out_filename );

		if ( template_filename ) {
			fmriOutputOrigin = templateImage->GetOrigin();
			fmriOutputDirection = templateImage->GetDirection();
		}

		for ( unsigned int i = 0; i < 4; i++ ) {
			if ( resolution.number ) {
				fmriOutputSpacing[i] = outputSpacing[i];
				fmriOutputSize[i] = outputSize[i];
			} else {
				if ( !template_filename ) {
					fmriOutputSpacing[i] = fmriImage->GetSpacing()[i];
					fmriOutputSize[i] = fmriImage->GetLargestPossibleRegion().GetSize()[i];
				}

				if ( template_filename ) {
					fmriOutputSpacing[i] = templateImage->GetSpacing()[i];
					fmriOutputSize[i] = templateImage->GetLargestPossibleRegion().GetSize()[i];
				}
			}

			if ( !template_filename ) {
				fmriOutputOrigin[i] = fmriImage->GetOrigin()[i];

				for ( unsigned int j = 0; j < 3; j++ ) {
					fmriOutputDirection[j][i] = fmriImage->GetDirection()[j][i];
				}
			}
		}

		if( fmriOutputSpacing[3] == 0 ) { fmriOutputSpacing[3] = 1; }

		if ( trans_filename.number ) {
			resampler->SetOutputDirection( fmriOutputDirection );
			resampler->SetOutputSpacing( fmriOutputSpacing );
			resampler->SetSize( fmriOutputSize );
			resampler->SetOutputOrigin( fmriOutputOrigin );
		}

		if ( vtrans_filename ) {
			//warper->AddObserver(itk::ProgressEvent(), progressObserver);
			warper->SetOutputDirection( fmriOutputDirection );
			warper->SetOutputOrigin( fmriOutputOrigin );
			warper->SetOutputSize( fmriOutputSize );
			warper->SetOutputSpacing( fmriOutputSpacing );
			warper->SetInput( inputImage );

			if ( trans_filename.number == 0 ) {
				warper->SetDeformationField( deformationFieldReader->GetOutput() );
			}
		}

		itk::FixedArray<unsigned int, 4> layout;
		layout[0] = 1;
		layout[1] = 1;
		layout[2] = 1;
		layout[3] = 0;
		const unsigned int numberOfTimeSteps = fmriImage->GetLargestPossibleRegion().GetSize()[3];
		OutputImageType::Pointer tileImage;
		std::cout << std::endl;
		//      isis::data::ImageList inList = isis::data::IOFactory::load( in_filename, "" );
		inputImage = movingAdapter->makeItkImageObject<InputImageType>( inList.front() );
		FMRIOutputType::DirectionType direction4D;

		for ( size_t i = 0; i < 3; i++ ) {
			for ( size_t j = 0; j < 3; j++ ) {
				direction4D[i][j] = fmriOutputDirection[i][j];
			}
		}

		direction4D[3][3] = 1;

		for ( unsigned int timestep = 0; timestep < numberOfTimeSteps; timestep++ ) {
			std::cout << "Resampling timestep: " << timestep << "...\r" << std::flush;
			timeStepExtractionFilter->SetRequestedTimeStep( timestep );
			timeStepExtractionFilter->Update();
			tmpImage = timeStepExtractionFilter->GetOutput();
			tmpImage->SetDirection( inputImage->GetDirection() );
			tmpImage->SetOrigin( inputImage->GetOrigin() );

			if ( trans_filename.number ) {
				resampler->SetInput( tmpImage );
				resampler->Update();
				tileImage = resampler->GetOutput();
			}

			if ( vtrans_filename ) {
				warper->SetInput( tmpImage );
				warper->Update();
				tileImage = warper->GetOutput();
			}

			tileImage->Update();
			tileImage->DisconnectPipeline();
			tileImageFilter->PushBackInput( tileImage );
		}

		tileImageFilter->SetLayout( layout );
		tileImageFilter->GetOutput()->SetDirection( direction4D );
		tileImageFilter->Update();
		isis::data::ImageList imgList = movingAdapter->makeIsisImageObject<FMRIOutputType>( tileImageFilter->GetOutput() );
		isis::data::IOFactory::write( imgList, out_filename, "" , "" );
	}

	std::cout << std::endl << "Done.    " << std::endl;
	return 0;
}
