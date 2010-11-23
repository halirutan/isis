#include <DataStorage/io_application.hpp>
#include <DataStorage/io_factory.hpp>
#include <boost/foreach.hpp>


using namespace isis;

int main( int argc, char **argv )
{
	data::IOApplication app( "isis data converter", true, true );
	app.parameters["tr"] = 0.;
	app.parameters["tr"].needed() = false;
	app.parameters["tr"].setDescription( "Repetition time in s" );
	app.init( argc, argv ); // will exit if there is a problem

	if( app.parameters["tr"]->as<uint16_t>() > 0 ) {
		BOOST_FOREACH( data::ImageList::const_reference ref, app.images ) {
			ref->setProperty<uint16_t>( "repetitionTime", app.parameters["tr"]->as<uint16_t>() * 1000 );
		}
	}

	app.autowrite( app.images );
	return EXIT_SUCCESS;
}
