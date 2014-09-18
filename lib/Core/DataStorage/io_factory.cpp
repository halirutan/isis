//
// C++ Implementation: io_factory
//
// Description:
//
//
// Author: Enrico Reimer<reimer@cbs.mpg.de>, (C) 2009
//
// Copyright: See COPYING file that comes with this distribution
//
//

#include "io_factory.hpp"
#ifdef WIN32
#include <windows.h>
#include <Winbase.h>
#define BOOST_FILESYSTEM_VERSION 3 
#include <boost/filesystem/path.hpp>
#else
#include <dlfcn.h>
#endif
#include <iostream>
#include <vector>
#include <algorithm>

#include "../CoreUtils/log.hpp"
#include "common.hpp"
#include <boost/regex.hpp>
#include <boost/system/error_code.hpp>
#include <boost/algorithm/string.hpp>
#include "../CoreUtils/singletons.hpp"

namespace isis
{
namespace data
{
API_EXCLUDE_BEGIN;
/// @cond _internal
namespace _internal
{
struct pluginDeleter {
	void *m_dlHandle;
	std::string m_pluginName;
	pluginDeleter( void *dlHandle, std::string pluginName ): m_dlHandle( dlHandle ), m_pluginName( pluginName ) {}
	void operator()( image_io::FileFormat *format ) {
		delete format;
#ifdef WIN32

		if( !FreeLibrary( ( HINSTANCE )m_dlHandle ) )
#else
		if ( dlclose( m_dlHandle ) != 0 )
#endif
			std::cerr << "Failed to release plugin " << m_pluginName << " (was loaded at " << m_dlHandle << ")";

		//we cannot use LOG here, because the loggers are gone allready
	}
};
struct dialect_missing {
	util::istring dialect;
	std::string filename;
	bool operator()( IOFactory::FileFormatList::reference ref )const {
		const util::istring dia = ref->dialects( filename );
		std::list<util::istring> splitted = util::stringToList<util::istring>( dia, ' ' );
		const bool ret = ( std::find( splitted.begin(), splitted.end(), dialect ) == splitted.end() );
		LOG_IF( ret, image_io::Runtime, warning ) << ref->getName() << " does not support the requested dialect " << util::MSubject( dialect );
		return ret;
	}
};

}
/// @endcond _internal
API_EXCLUDE_BEGIN;

IOFactory::IOFactory()
{
	const char *env_path = getenv( "ISIS_PLUGIN_PATH" );
	const char *env_home = getenv( "HOME" );

	if( env_path ) {
		findPlugins( boost::filesystem::path( env_path ).native() );
	}

	if( env_home ) {
		const boost::filesystem::path home = boost::filesystem::path( env_home ) / "isis" / "plugins";

		if( boost::filesystem::exists( home ) ) {
			findPlugins( home.native() );
		} else {
			LOG( Runtime, info ) << home << " does not exist. Won't check for plugins there";
		}
	}

#ifdef WIN32
	TCHAR lpExeName[2048];
	DWORD lExeName = GetModuleFileName( NULL, lpExeName, 2048 );
	bool w32_path_ok = false;

	if( lExeName == 0 ) {
		LOG( Runtime, error ) << "Failed to get the process name " << util::MSubject( util::getLastSystemError() );
	} else if( lExeName < 2048 ) {
		lpExeName[lExeName] = '\0';
		boost::filesystem::path prog_name( lpExeName );

		if( boost::filesystem::exists( prog_name ) ) {
			w32_path_ok = true;
			LOG( Runtime, info ) << "Determined the path of the executable as " << util::MSubject( prog_name.remove_filename().directory_string() ) << " will search for plugins there..";
			findPlugins( prog_name.remove_filename().directory_string() );
		}
	} else
		LOG( Runtime, error ) << "Sorry, the path of the process is to long (must be less than 2048 characters) ";

	LOG_IF( !w32_path_ok, Runtime, warning ) << "Could not determine the path of the executable, won't search for plugins there..";
#else
	findPlugins( std::string( PLUGIN_PATH ) );
#endif
}

bool IOFactory::registerFileFormat( const FileFormatPtr plugin )
{
	if ( !plugin )return false;

	io_formats.push_back( plugin );
	std::list<util::istring> suffixes = plugin->getSuffixes(  );
	LOG( Runtime, info )
			<< "Registering " << ( plugin->tainted() ? "tainted " : "" ) << "io-plugin "
			<< util::MSubject( plugin->getName() )
			<< " with supported suffixes " << suffixes;
	for( util::istring & it :  suffixes ) {
		io_suffix[it].push_back( plugin );
	}
	return true;
}

unsigned int IOFactory::findPlugins( const std::string &path )
{
	boost::filesystem::path p( path );

	if ( !exists( p ) ) {
		LOG( Runtime, warning ) << util::MSubject( p ) << " not found";
		return 0;
	}

	if ( !boost::filesystem::is_directory( p ) ) {
		LOG( Runtime, warning ) << util::MSubject( p ) << " is no directory";
		return 0;
	}

	LOG( Runtime, info )   << "Scanning " << util::MSubject( p ) << " for plugins";
	boost::regex pluginFilter( std::string( "^" ) + DL_PREFIX + "isisImageFormat_" + "[[:word:]]+" + DL_SUFFIX + "$", boost::regex::perl | boost::regex::icase );
	unsigned int ret = 0;

	for ( boost::filesystem::directory_iterator itr( p ); itr != boost::filesystem::directory_iterator(); ++itr ) {
		if ( boost::filesystem::is_directory( *itr ) )continue;

		if ( boost::regex_match( itr->path().filename().string(), pluginFilter ) ) {
			const std::string pluginName = itr->path().native();
#ifdef WIN32
			HINSTANCE handle = LoadLibrary( pluginName.c_str() );
#else
			void *handle = dlopen( pluginName.c_str(), RTLD_NOW );
#endif

			if ( handle ) {
#ifdef WIN32
				image_io::FileFormat* ( *factory_func )() = ( image_io::FileFormat * ( * )() )GetProcAddress( handle, "factory" );
#else
				image_io::FileFormat* ( *factory_func )() = ( image_io::FileFormat * ( * )() )dlsym( handle, "factory" );
#endif

				if ( factory_func ) {
					FileFormatPtr io_class( factory_func(), _internal::pluginDeleter( handle, pluginName ) );

					if ( registerFileFormat( io_class ) ) {
						io_class->plugin_file = pluginName;
						ret++;
					} else {
						LOG( Runtime, warning ) << "failed to register plugin " << util::MSubject( pluginName );
					}
				} else {
#ifdef WIN32
					LOG( Runtime, warning )
							<< "could not get format factory function from " << util::MSubject( pluginName );
					FreeLibrary( handle );
#else
					LOG( Runtime, warning )
							<< "could not get format factory function from " << util::MSubject( pluginName ) << ":" << util::MSubject( dlerror() );
					dlclose( handle );
#endif
				}
			} else
#ifdef WIN32
				LOG( Runtime, warning ) << "Could not load library " << util::MSubject( pluginName );

#else
				LOG( Runtime, warning ) << "Could not load library " << util::MSubject( pluginName ) << ":" <<  util::MSubject( dlerror() );
#endif
		} else {
			LOG( Runtime, verbose_info ) << "Ignoring " << *itr << " because it doesn't match " << pluginFilter.str();
		}
	}

	return ret;
}

IOFactory &IOFactory::get()
{
	return util::Singletons::get<IOFactory, INT_MAX>();
}

std::list<Chunk> IOFactory::loadFile( const boost::filesystem::path &filename, util::istring suffix_override, util::istring dialect )
{
	FileFormatList formatReader;
	formatReader = getFileFormatList( filename.string(), suffix_override, dialect );
	const util::istring with_dialect = dialect.empty() ?
									   util::istring( "" ) : util::istring( " with dialect \"" ) + dialect + "\"";

	if ( formatReader.empty() ) {
		if( !boost::filesystem::exists( filename ) ) {
			LOG( Runtime, error ) << util::MSubject( filename )
								  << " does not exist as file, and no suitable plugin was found to generate data from "
								  << ( suffix_override.empty() ? util::istring( "that name" ) : util::istring( "the suffix \"" ) + suffix_override + "\"" );
		} else if( suffix_override.empty() ) {
			LOG( Runtime, error ) << "No plugin found to read " << filename << with_dialect;
		} else {
			LOG( Runtime, error ) << "No plugin supporting the requested suffix " << suffix_override << with_dialect << " was found";
		}
	} else {
		for( FileFormatList::const_reference it :  formatReader ) {
			LOG( ImageIoDebug, info )
					<< "plugin to load file" << with_dialect << " " << util::MSubject( filename ) << ": " << it->getName();

			try {
				std::list<data::Chunk> loaded=it->load( filename.native(), dialect, m_feedback );
				for( Chunk & ref :  loaded ) {
					ref.refValueAsOr( "source", filename.native() ); // set source to filename or leave it if its already set
				}
				return loaded;
			} catch ( std::runtime_error &e ) {
				if( suffix_override.empty() ) {
					LOG( Runtime, formatReader.size() > 1 ? warning : error )
							<< "Failed to load " <<  filename << " using " <<  it->getName() << with_dialect << " ( " << e.what() << " )";
				} else {
					LOG( Runtime, warning )
							<< "The enforced format " << it->getName()  << " failed to read " << filename << with_dialect
							<< " ( " << e.what() << " ), maybe it just wasn't the right format";
				}
			}
		}
		LOG_IF( boost::filesystem::exists( filename ) && formatReader.size() > 1, Runtime, error ) << "No plugin was able to load: "   << util::MSubject( filename ) << with_dialect;
	}
	
	return std::list<Chunk>();//no plugin of proposed list could load file
}


IOFactory::FileFormatList IOFactory::getFileFormatList( std::string filename, util::istring suffix_override, util::istring dialect )
{
	std::list<std::string> ext;
	FileFormatList ret;
	_internal::dialect_missing remove_op;

	if( suffix_override.empty() ) { // detect suffixes from the filename
		const boost::filesystem::path fname( filename );
		ext = util::stringToList<std::string>( fname.filename().string(), '.' ); // get all suffixes

		if( !ext.empty() )ext.pop_front(); // remove the first "suffix" - actually the basename
	} else ext = util::stringToList<std::string>( suffix_override, '.' );

	while( !ext.empty() ) {
		const util::istring wholeName( util::listToString( ext.begin(), ext.end(), ".", "", "" ).c_str() ); // (re)construct the rest of the suffix
		const std::map<util::istring, FileFormatList>::iterator found = get().io_suffix.find( wholeName );

		if( found != get().io_suffix.end() ) {
			LOG( Debug, verbose_info ) << found->second.size() << " plugins support suffix " << wholeName;
			ret.insert( ret.end(), found->second.begin(), found->second.end() );
		}

		ext.pop_front();
	}

	if( dialect.empty() ) {
		LOG_IF( ret.size() > 1, Debug, info ) << "No dialect given. Will use all " << ret.size() << " plugins";
	} else {//remove everything which lacks the dialect if there was some given
		remove_op.dialect = dialect;
		remove_op.filename = filename;
		ret.remove_if( remove_op );
		LOG( Debug, info ) << "Removed everything which does not support the dialect " << util::MSubject( dialect ) << " on " << filename << "(" << ret.size() << " plugins left)";
	}

	return ret;
}

std::list< Image > IOFactory::chunkListToImageList( std::list<Chunk> &src, optional< util::slist& > rejected )
{
	// throw away invalid chunks
	size_t errcnt=0;
	for(std::list<Chunk>::iterator i=src.begin();i!=src.end();){
		if(!i->isValid()){
			LOG(image_io::Runtime, error ) << "Rejecting invalid chunk. Missing properties: " << i->getMissing();
			errcnt++;
			if(rejected)
				rejected->push_back(i->getValueAs<std::string>("source")); 
			src.erase(i++);//we must increment i before the removal, otherwise it will be invalid
		} else
			i++;
	}

	std::list< Image > ret;

	while ( !src.empty() ) {
		LOG( Debug, info ) << src.size() << " Chunks left to be distributed.";
		size_t before = src.size();

		Image buff( src, rejected );

		if ( buff.isClean() ) {
			if( buff.isValid() ) { //if the image was successfully indexed and is valid, keep it
				ret.push_back( buff );
				LOG( Runtime, info ) << "Image " << ret.size() << " with size " << util::MSubject( buff.getSizeAsString() ) << " done.";
			} else {
				LOG_IF( !buff.getMissing().empty(), Runtime, error )
						<< "Cannot insert image. Missing properties: " << buff.getMissing();
				errcnt += before - src.size();
			}
		} else
			LOG( Runtime, info ) << "Dropping non clean Image";
	}

	LOG_IF( errcnt, Runtime, warning ) << "Dropped " << errcnt << " chunks because they didn't form valid images";
	return ret;
}

std::list< Chunk > IOFactory::loadChunks( const std::string& path, isis::util::istring suffix_override, isis::util::istring dialect, optional< isis::util::slist& > rejected  )
{
	const boost::filesystem::path p( path );
	return boost::filesystem::is_directory( p ) ?
		get().loadPath( p, suffix_override, dialect, rejected) :
		get().loadFile( p, suffix_override, dialect );
}

std::list< Image > IOFactory::load ( const util::slist &paths, util::istring suffix_override, util::istring dialect, optional< isis::util::slist& > rejected )
{
	std::list<Chunk> chunks;
	size_t loaded = 0;
	for( const std::string & path :  paths ) {
		std::list<Chunk> loaded=loadChunks( path , suffix_override, dialect, rejected );
		chunks.splice(chunks.end(),loaded);
	}
	const std::list<data::Image> images = chunkListToImageList( chunks, rejected );
	LOG( Runtime, info )
			<< "Generated " << images.size() << " images out of " << loaded << " chunks loaded from " << paths;

	// store paths of red, but rejected chunks
	std::set<std::string> image_rej;
	for(const data::Chunk &ch :  chunks ){
		image_rej.insert(ch.getValueAs<std::string>("source"));
	}
	if(rejected)
		rejected->insert(rejected->end(),image_rej.begin(),image_rej.end());
	
	return images;
}

std::list<data::Image> IOFactory::load( const std::string &path, util::istring suffix_override, util::istring dialect, optional< isis::util::slist& > rejected )
{
	return load( util::slist( 1, path ), suffix_override, dialect );
}

std::list<Chunk> IOFactory::loadPath( const boost::filesystem::path& path, util::istring suffix_override, util::istring dialect, optional< util::slist&> rejected )
{
	std::list<Chunk> ret;

	if( m_feedback ) {
		const size_t length = std::distance( boost::filesystem::directory_iterator( path ), boost::filesystem::directory_iterator() ); //@todo this will also count directories
		m_feedback->show( length, std::string( "Reading " ) + util::Value<std::string>( length ).toString( false ) + " files from " + path.native() );
	}

	for ( boost::filesystem::directory_iterator i( path ); i != boost::filesystem::directory_iterator(); ++i )  {
		if ( boost::filesystem::is_directory( *i ) )continue;

		std::list<Chunk> loaded= loadFile( *i, suffix_override, dialect );

		if(rejected && loaded.empty()){
			rejected->push_back(boost::filesystem::path(*i).native());
		}

		if( m_feedback )
			m_feedback->progress();
		
		ret.splice(ret.end(),loaded);
	}

	if( m_feedback )
		m_feedback->close();

	return ret;
}

bool IOFactory::write( const data::Image &image, const std::string &path, util::istring suffix_override, util::istring dialect )
{
	return write( std::list<data::Image>( 1, image ), path, suffix_override, dialect );
}


bool IOFactory::write( std::list< isis::data::Image > images, const std::string &path, util::istring suffix_override, util::istring dialect )
{
	const FileFormatList formatWriter = get().getFileFormatList( path, suffix_override, dialect );

	for( std::list<data::Image>::reference ref :  images ) {
		ref.checkMakeClean();
	}

	if( formatWriter.size() ) {
		for( FileFormatList::const_reference it :  formatWriter ) {
			LOG( Debug, info )
					<< "plugin to write to " <<  path << ": " << it->getName()
					<<  ( dialect.empty() ?
						  util::istring( "" ) :
						  util::istring( " using dialect: " ) + dialect
						);

			try {
				it->write( images, path, dialect, get().m_feedback );
				LOG( Runtime, info )
						<< images.size()
						<< " images written to " << path << " using " <<  it->getName()
						<<  ( dialect.empty() ?
							  util::istring( "" ) :
							  util::istring( " and dialect: " ) + dialect
							);
				return true;
			} catch ( std::runtime_error &e ) {
				LOG( Runtime, warning )
						<< "Failed to write " <<  images.size()
						<< " images to " << path << " using " <<  it->getName() << " (" << e.what() << ")";
			}
		}
	} else {
		LOG( Runtime, error ) << "No plugin found to write to: " << path; //@todo error message missing
	}

	return false;
}
void IOFactory::setProgressFeedback( boost::shared_ptr<util::ProgressFeedback> feedback )
{
	IOFactory &This = get();

	if( This.m_feedback )This.m_feedback->close();

	This.m_feedback = feedback;
}

IOFactory::FileFormatList IOFactory::getFormats()
{
	return get().io_formats;
}


}
} // namespaces data isis
