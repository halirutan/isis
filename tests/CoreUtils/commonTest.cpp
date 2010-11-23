#define BOOST_TEST_MODULE TypeTest
#define NOMINMAX 1
#include <boost/test/included/unit_test.hpp>
#include <CoreUtils/common.hpp>
#include <CoreUtils/istring.hpp>
#include <boost/foreach.hpp>

namespace isis
{
namespace test
{

// TestCase object instantiation
BOOST_AUTO_TEST_CASE( fuzzy_equal_test )
{
	float a0 = 0.000000000000001;
	float b0 = 0;
	float a1 = 1.000000000000001;
	float b1 = 1;
	float a2 = 1000000000000001;
	float b2 = 1000000000000000;
	float a3 = 1000000000000001;
	float b3 = 100000000000000;
	double a4 = 1.000000000000001;
	double b4 = 1;
	float a5 = 1000000000000001;
	float b5 = -1000000000000000;
	float a6 = 0.000000000000001;
	float b6 = -0.000000000000001;
	BOOST_CHECK( util::fuzzyEqual( a0, b0 ) );
	BOOST_CHECK( util::fuzzyEqual( a1, b1 ) );
	BOOST_CHECK( util::fuzzyEqual( a2, b2 ) );
	BOOST_CHECK( !util::fuzzyEqual( a3, b3 ) );
	BOOST_CHECK( !util::fuzzyEqual( a4, b4 ) ); //double has double precision - so equality will be less fuzzy
	BOOST_CHECK( !util::fuzzyEqual( a5, b5 ) );
	BOOST_CHECK( util::fuzzyEqual( a6, b6 ) );
}

BOOST_AUTO_TEST_CASE( basic_string2list_test )
{
	std::list<std::string> serg=util::string2list<std::string>( std::string( "Val1,Val2,Val3" ), ',' );
	const char* list[]={"Val1","Val2","Val3"};
	size_t at=0;
	BOOST_FOREACH(const std::string &ref,serg){
		BOOST_CHECK_EQUAL(ref,list[at++]);
	}

	at=0;
	std::list<util::istring> ierg=util::string2list<util::istring>( util::istring( "Val1,Val2,Val3" ), ',' );
	BOOST_FOREACH(const util::istring &ref,ierg){
		BOOST_CHECK_EQUAL(ref,list[at++]);
	}
}

}}
