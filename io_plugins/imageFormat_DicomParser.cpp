#include "imageFormat_Dicom.hpp"
#include <clocale>
#include <boost/date_time/gregorian/gregorian.hpp>
#include <boost/endian/buffers.hpp>
#include <isis/core/common.hpp>
#include <dcmtk/dcmdata/dcdict.h>
#include <dcmtk/dcmdata/dcdicent.h>

namespace isis
{
namespace image_io
{

namespace _internal
{
util::istring id2Name( const uint16_t group, const uint16_t element ){
	char id_str[4+4+3+1];
	sprintf(id_str,"(%04x,%04x)",group,element);
	return id_str;
}
util::istring id2Name( const uint32_t id32 ){
	return id2Name((id32&0xFFFF0000)>>16,id32&0xFFFF);
}

template<typename ST, typename DT> bool try_cast( const ST &source, DT &dest )
{
	bool ret = true;

	try {
		dest = boost::lexical_cast<DT>( source );
	} catch ( boost::bad_lexical_cast e ) {
		ret = false;
	}

	return ret;
}
struct tag_length_visitor
{
	template<boost::endian::order Order> size_t operator()(const ExplicitVrTag<Order> *_tag)const{
		return _tag->length.value();
	}
	template<boost::endian::order Order> size_t operator()(const ImplicitVrTag<Order> *_tag)const{
		return _tag->length.value();
	}
};
struct tag_vr_visitor
{
	template<boost::endian::order Order> std::string operator()(const ExplicitVrTag<Order> *_tag)const{
		return std::string(_tag->vr,_tag->vr+2);
	}
	template<boost::endian::order Order> std::string operator()(const ImplicitVrTag<Order> *_tag)const{
		return "--";
	}
};
struct tag_id_visitor
{
	template<boost::endian::order Order> uint32_t operator()(const Tag<Order> *_tag)const{
		return _tag->getID32();
	}
};
size_t DicomElement::getLength()const{
	const size_t len=boost::apply_visitor(tag_length_visitor(),tag);
	return len;
}
size_t DicomElement::getPosition()const{
	return position;
}
util::istring DicomElement::getIDString()const{
	return id2Name(getID32());
}

uint32_t DicomElement::getID32()const{
	return boost::apply_visitor(tag_id_visitor(),tag);
}

std::string DicomElement::getVR()const{
	return boost::apply_visitor(tag_vr_visitor(),tag);
}
util::PropertyMap::PropPath DicomElement::getName()const{
	auto found=dicom_dict.find(getID32());
	if(found!=dicom_dict.end())
		return found->second;
	else{
		return util::istring( ImageFormat_Dicom::unknownTagName ) + getIDString().c_str();
	}
}

DicomElement::DicomElement(const data::ByteArray &_source, size_t _position, boost::endian::order _endian):source(_source),position(_position),endian(_endian){
	next(position);//trigger read by calling next without moving
}
DicomElement &DicomElement::next(){
	const size_t len=getLength();
	LOG_IF(len==0xffffffffffffffff,Debug,error) << "Doing next on " << getName() << " at " << position << " with an undefined length";
	return next(position+len+8);
}
DicomElement &DicomElement::next(size_t _position){
	position=_position;
	switch(endian){
		case boost::endian::order::big:
			tag=makeTag<boost::endian::order::big>();
			break;
		case boost::endian::order::little:
			tag=makeTag<boost::endian::order::little>();
			break;
	}
	return *this;
}


const uint8_t *DicomElement::data()const{
	return &source[position+2+2+2+2]; //offset by group-id, element-id, vr and length
}
util::ValueReference DicomElement::getValue(){
	util::ValueReference ret;
	const std::string vr=getVR();
	auto found_generator=generator_map.find(vr);
	if(found_generator!=generator_map.end()){
		auto generator=found_generator->second;;
		size_t mult=generator.value_size?getLength()/generator.value_size:1;
		
		if(mult==1)
			ret=generator.scalar(this);
		else if(generator.list)
			ret=generator.list(this);
		else { // fallback for non- supportet lists @todo
			assert(false);
		}

		LOG(Debug,verbose_info) << "Parsed " << getVR() << "-tag " << getName() << " "  << getIDString() << " at position " << position << " as "  << ret;
	} else {
		LOG(Debug,error) << "Could not find an interpreter for the VR " << getVR() << " of " << getName() << "/" << getIDString() << " at " << position ;
	}
	
	return ret;
}
DicomElement DicomElement::next(boost::endian::order endian)const{
	//@todo handle end of stream
	size_t nextpos=position+2+2+2+2+getLength();
	return DicomElement(source,nextpos,endian);
}
bool DicomElement::endian_swap()const{
	switch(endian){
		case boost::endian::order::big:   return (__BYTE_ORDER == __LITTLE_ENDIAN);
		case boost::endian::order::little:return (__BYTE_ORDER == __BIG_ENDIAN);
	}
}


template<typename T> std::list<T> dcmtkListString2list( DcmElement *elem )
{
	OFString buff;
	elem->getOFStringArray( buff );
	return util::stringToList<T>( std::string( buff.c_str() ), '\\' );
}

template <typename S, typename V> void arrayToVecPropImp( S *array, util::PropertyMap &dest, const util::PropertyMap::PropPath &name, size_t len )
{
	V vector;
	vector.copyFrom( array, array + len );
	dest.property( name ) = vector; //if Float32 is float its fine, if not we will get an linker error here
}
template <typename S> void arrayToVecProp( S *array, util::PropertyMap &dest, const util::PropertyMap::PropPath &name, size_t len )
{
	if( len <= 3 )arrayToVecPropImp<S, util::vector3<S> >( array, dest, name, len );
	else arrayToVecPropImp<S, util::vector4<S> >( array, dest, name, len );
}

template<typename T> bool noLatin( const T &t ) {return t >= 127;}
}


/**
 * Parses the Age String
 * A string of characters with one of the following formats -- nnnD, nnnW, nnnM, nnnY;
 * where nnn shall contain the number of days for D, weeks for W, months for M, or years for Y.
 * Example - "018M" would represent an age of 18 months.
 */
void ImageFormat_Dicom::parseAS( DcmElement *elem, const util::PropertyMap::PropPath &name, util::PropertyMap &map )
{
	uint16_t duration = 0;
	OFString buff;
	elem->getOFString( buff, 0 );
	static boost::numeric::converter <
	uint16_t, double,
			boost::numeric::conversion_traits<uint16_t, double>,
			boost::numeric::def_overflow_handler,
			boost::numeric::RoundEven<double>
			> double2uint16;

	if ( _internal::try_cast( buff.substr( 0, buff.find_last_of( "0123456789" ) + 1 ), duration ) ) {
		switch ( buff.at( buff.size() - 1 ) ) {
		case 'D':
		case 'd':
			break;
		case 'W':
		case 'w':
			duration *= 7;
			break;
		case 'M':
		case 'm':
			duration = double2uint16( 30.436875 * duration ); // year/12
			break;
		case 'Y':
		case 'y':
			duration = double2uint16( 365.2425 * duration ); //mean length of a year
			break;
		default:
			LOG( Runtime, warning )
					<< "Missing age-type-letter, assuming days";
		}
		map.setValueAs( name, duration );
		LOG( Debug, verbose_info )
				<< "Parsed age for " << name << "(" <<  buff << ")" << " as " << duration << " days";
	} else
		LOG( Runtime, warning )
				<< "Cannot parse age string \"" << buff << "\" in the field \"" << name << "\"";
}

/**
 * Parses the Time string
 * For duration (VR=TM):
 * A string of characters of the format hhmmss.frac; where hh contains hours (range "00" - "23"),
 * mm contains minutes (range "00" - "59"), ss contains seconds (range "00" - "59"), and frac contains
 * a fractional part of a second as small as 1 millionth of a second (range "000000" - "999999").
 * A 24 hour clock is assumed. Midnight can be represented by only "0000" since "2400" would violate the
 * hour range. The string may be padded with trailing spaces. Leading and embedded spaces are not allowed.
 * One or more of the components mm, ss, or frac may be unspecified as long as every component to the right
 * of an unspecified component is also unspecified. If frac is unspecified the preceding "." may not be included.
 * Frac shall be held to six decimal places or less to ensure its format conforms to the ANSI HISPP MSDS Time
 * common data type.
 * Examples:
 * - "070907.0705" represents a time of 7 hours, 9 minutes and 7.0705 seconds.
 * - "1010" represents a time of 10 hours, and 10 minutes.
 * - "021" is an invalid value.
 * For timestamp (VR=TM and DA) see http://dicom.nema.org/dicom/2013/output/chtml/part05/sect_6.2.html
 */
void ImageFormat_Dicom::parseTime( DcmElement *elem, const util::PropertyMap::PropPath &name, util::PropertyMap &map,uint16_t dstID )
{
	OFString buff;
	elem->getOFString( buff, 0 );
	
	util::PropertyValue &prop=map.setValueAs( name, buff.c_str()); // store string
	
	if ( prop.transform(dstID) ) { // try to convert it into timestamp or date
		LOG( Debug, verbose_info ) << "Parsed time for " << name << "(" <<  buff << ")" << " as " << map.property(name).toString(true);
	} else
		LOG( Runtime, warning ) << "Cannot parse Time string \"" << buff << "\" in the field \"" << name << "\"";
}

void ImageFormat_Dicom::parseScalar( DcmElement *elem, const util::PropertyMap::PropPath &name, util::PropertyMap &map )
{
	switch ( elem->getVR() ) {
	case EVR_AS: { // age string (nnnD, nnnW, nnnM, nnnY)
		parseAS( elem, name, map );
	}
	break;
	case EVR_DA: {
		parseTime( elem, name, map, util::Value<util::date>::staticID() );
	}
	break;
	case EVR_TM: {
		parseTime( elem, name, map, util::Value<util::timestamp>::staticID() ); //duration is for milliseconds stored as decimal number
	}
	break;
	case EVR_DT: {
		parseTime( elem, name, map, util::Value<util::timestamp>::staticID() );
	}
	break;
	case EVR_FL: {
		Float32 buff;
		elem->getFloat32( buff );
		map.setValueAs<float>( name, buff ); //if Float32 is float its fine, if not we will get an compiler error here
	}
	break;
	case EVR_FD: {
		Float64 buff;
		elem->getFloat64( buff );
		map.setValueAs<double>( name, buff ); //if Float64 is double its fine, if not we will get an compiler error here
	}
	break;
	case EVR_DS: { //Decimal String (can be floating point)
		OFString buff;
		elem->getOFString( buff, 0 );
		map.setValueAs<double>( name, std::stod( buff.c_str() ) );
	}
	break;
	case EVR_SL: { //signed long
		Sint32 buff;
		elem->getSint32( buff );
		map.setValueAs<int32_t>( name, buff ); //seems like Sint32 is not allways int32_t, so enforce it
	}
	break;
	case EVR_SS: { //signed short
		Sint16 buff;
		elem->getSint16( buff );
		map.setValueAs<int16_t>( name, buff );
	}
	break;
	case EVR_UL: { //unsigned long
		Uint32 buff;
		elem->getUint32( buff );
		map.setValueAs<uint32_t>( name, buff );
	}
	break;
	case EVR_US: { //unsigned short
		Uint16 buff;
		elem->getUint16( buff );
		map.setValueAs<uint16_t>( name, buff );
	}
	break;
	case EVR_IS: { //integer string
		OFString buff;
		elem->getOFString( buff, 0 );
		map.setValueAs<int32_t>( name, std::stoi( buff.c_str() ) );
	}
	break;
	case EVR_AE: //Application Entity (string)
	case EVR_CS: // Code String (string)
	case EVR_LT: //long text
	case EVR_SH: //short string
	case EVR_LO: //long string
	case EVR_ST: //short text
	case EVR_UT: //Unlimited Text
	case EVR_UI: //Unique Identifier [0-9\.]
	case EVR_AT: // @todo find a better way to interpret the value (see http://northstar-www.dartmouth.edu/doc/idl/html_6.2/Value_Representations.html)
	case EVR_PN: { //Person Name
		OFString buff;
		elem->getOFString( buff, 0 );
		map.setValueAs<std::string>( name, buff.c_str() );
	}
	break;
	case EVR_UN: //Unknown, see http://www.dabsoft.ch/dicom/5/6.2.2/
	case EVR_OB:{ //bytes .. if it looks like text, use it as text
		//@todo do a better sanity check
		Uint8 *buff;
		elem->getUint8Array( buff ); // get the raw data
		Uint32 len = elem->getLength();
		const size_t nonLat = std::count_if( buff, buff + len, _internal::noLatin<Uint8> );

		if( nonLat ) { // if its not "just text" encode it as base256
			LOG( Runtime, info ) << "Using " << len << " bytes from " << name << "("
								 << const_cast<DcmTag &>( elem->getTag() ).getVRName() << ") as base256 because there are "
								 << nonLat << " non latin characters in it";
			std::stringstream o;
			std::copy( buff, buff + len, std::ostream_iterator<Uint16>( o << std::hex ) );
			map.setValueAs<std::string>( name, o.str() ); //stuff it into a string
		} else
			map.setValueAs<std::string>( name, std::string( ( char * )buff, len ) ); //stuff it into a string
	}
	break;
	case EVR_OW: { //16bit words - parse as base256 strings
		Uint16 *buff;
		elem->getUint16Array( buff ); // get the raw data
		Uint32 len = elem->getLength();
		std::stringstream o;
		std::copy( buff, buff + len, std::ostream_iterator<Uint16>( o << std::hex ) );
		map.setValueAs<std::string>( name, o.str() ); //stuff it into a string
	}
	break;
	default: {
		OFString buff;
		elem->getOFString( buff, 0 );
		LOG( Runtime, notice ) << "Don't know how to handle Value Representation " << util::MSubject(const_cast<DcmTag &>( elem->getTag() ).getVRName()) << 
		" of " << std::make_pair(name ,buff);
	}
	break;
	}
}

void ImageFormat_Dicom::parseList( DcmElement *elem, const util::PropertyMap::PropPath &name, util::PropertyMap &map )
{
	OFString buff;
	size_t len = elem->getVM();

	switch ( elem->getVR() ) {
	case EVR_FL: {
		Float32 *buff;
		elem->getFloat32Array( buff );
		map.setValueAs( name, util::dlist( buff, buff + len ) );
	}
	break;
	case EVR_FD: {
		Float64 *buff;
		elem->getFloat64Array( buff );
		map.setValueAs( name, util::dlist( buff, buff + len ) );
	}
	break;
	case EVR_IS: {
		map.setValueAs( name, _internal::dcmtkListString2list<int>( elem ));
	}
	break;
	case EVR_SL: {
		Sint32 *buff;
		elem->getSint32Array( buff );
		map.setValueAs( name, util::ilist( buff, buff + len ));
	}
	break;
	case EVR_US: {
		Uint16 *buff;
		elem->getUint16Array( buff );
		map.setValueAs( name, util::ilist( buff, buff + len ));
	}
	break;
	case EVR_SS: {
		Sint16 *buff;
		elem->getSint16Array( buff );
		map.setValueAs( name, util::ilist( buff, buff + len ));
	}
	break;
	case EVR_CS: // Code String (string)
	case EVR_SH: //short string
	case EVR_LT: //long text
	case EVR_LO: //long string
	case EVR_DA: //date string
	case EVR_TM: //time string
	case EVR_UT: //Unlimited Text
	case EVR_ST: { //short text
		map.setValueAs( name, _internal::dcmtkListString2list<std::string>( elem ));
	}
	break;
	case EVR_DS: {
		map.setValueAs( name, _internal::dcmtkListString2list<double>( elem ));
	}
	break;
	case EVR_AS:
	case EVR_UL:
	case EVR_AE: //Application Entity (string)
	case EVR_UI: //Unique Identifier [0-9\.]
	case EVR_PN:
	default: {
		elem->getOFStringArray( buff );
		LOG( Runtime, notice ) << "Implement me "
							   << name << "("
							   << const_cast<DcmTag &>( elem->getTag() ).getVRName() << "):"
							   << buff;
	}
	break;
	}

	LOG( Debug, verbose_info ) << "Parsed the list " << name << " as " << map.property( name );
}

void ImageFormat_Dicom::parseCSA( DcmElement *elem, util::PropertyMap &map, std::list<util::istring> dialects )
{
	Uint8 *array;
	elem->getUint8Array( array );
	const size_t len = elem->getLength();

	for ( std::string::size_type pos = 0x10; pos < ( len - sizeof( Sint32 ) ); ) {
		pos += parseCSAEntry( array + pos, map, dialects );
	}
}
size_t ImageFormat_Dicom::parseCSAEntry( Uint8 *at, util::PropertyMap &map, std::list<util::istring> dialects )
{
	size_t pos = 0;
	const char *const name = ( char * )at + pos;
	pos += 0x40;
	if(name[0]==0)
		throw std::logic_error("empty CSA entry name");
	/*Sint32 &vm=*((Sint32*)array+pos);*/
	pos += sizeof( Sint32 );
	const char *const vr = ( char * )at + pos;
	pos += 0x4;
	/*Sint32 syngodt=endian<Uint8,Uint32>(array+pos);*/
	pos += sizeof( Sint32 );
	const Sint32 nitems = ((boost::endian::little_int32_buf_t*)( at + pos ))->value();
	pos += sizeof( Sint32 );
	static const std::string whitespaces( " \t\f\v\n\r" );
	
	if ( nitems ) {
		pos += sizeof( Sint32 ); //77
		util::slist ret;

		for ( unsigned short n = 0; n < nitems; n++ ) {
			Sint32 len = ((boost::endian::little_int32_buf_t*)( at + pos ))->value();
			pos += sizeof( Sint32 );//the length of this element
			pos += 3 * sizeof( Sint32 ); //whatever

			if ( !len )continue;

			if( (
					std::string( "MrPhoenixProtocol" ) != name  && std::string( "MrEvaProtocol" ) != name && std::string( "MrProtocol" ) != name
				) || checkDialect(dialects, "withExtProtocols") ) {
				const std::string insert( ( char * )at + pos );
				const std::string::size_type start = insert.find_first_not_of( whitespaces );

				if ( insert.empty() || start == std::string::npos ) {
					LOG( Runtime, verbose_info ) << "Skipping empty string for CSA entry " << name;
				} else {
					const std::string::size_type end = insert.find_last_not_of( whitespaces ); //strip spaces

					if( end == std::string::npos )
						ret.push_back( insert.substr( start, insert.size() - start ) ); //store the text if there is some
					else
						ret.push_back( insert.substr( start, end + 1 - start ) );//store the text if there is some
				}
			} else {
				LOG( Runtime, verbose_info ) << "Skipping " << name << " as its not requested by the dialect (use dialect \"withExtProtocols\" to get it)";
			}

			pos += (
					   ( len + sizeof( Sint32 ) - 1 ) / sizeof( Sint32 )
				   ) *
				   sizeof( Sint32 );//increment pos by len aligned to sizeof(Sint32)*/
		}

		try {
			util::PropertyMap::PropPath path;
			path.push_back( name );

			if ( ret.size() == 1 ) {
				if ( parseCSAValue( ret.front(), path , vr, map ) ) {
					LOG( Debug, verbose_info ) << "Found scalar entry " << path << ":" << map.property( path ) << " in CSA header";
				}
			} else if ( ret.size() > 1 ) {
				if ( parseCSAValueList( ret, path, vr, map ) ) {
					LOG( Debug, verbose_info ) << "Found list entry " << path << ":" << map.property( path ) << " in CSA header";
				}
			}
		} catch ( std::exception &e ) {
			LOG( Runtime, warning ) << "Failed to parse CSA entry " << std::make_pair( name, ret ) << " as " << vr << " (" << e.what() << ")";
		}
	} else {
		LOG( Debug, verbose_info ) << "Skipping empty CSA entry " << name;
		pos += sizeof( Sint32 );
	}

	return pos;
}

bool ImageFormat_Dicom::parseCSAValue( const std::string &val, const util::PropertyMap::PropPath &name, const util::istring &vr, util::PropertyMap &map )
{
	if ( vr == "IS" or vr == "SL" ) {
		map.setValueAs( name, std::stoi( val ));
	} else if ( vr == "UL" ) {
		map.setValueAs( name, boost::lexical_cast<uint32_t>( val ));
	} else if ( vr == "CS" or vr == "LO" or vr == "SH" or vr == "UN" or vr == "ST" or vr == "UT" ) {
		map.setValueAs( name, val );
	} else if ( vr == "DS" or vr == "FD" ) {
		map.setValueAs( name, std::stod( val ));
	} else if ( vr == "US" ) {
		map.setValueAs( name, boost::lexical_cast<uint16_t>( val ));
	} else if ( vr == "SS" ) {
		map.setValueAs( name, boost::lexical_cast<int16_t>( val ));
	} else if ( vr == "UT" or vr == "LT" ) {
		map.setValueAs( name, val);
	} else {
		LOG( Runtime, error ) << "Dont know how to parse CSA entry " << std::make_pair( name, val ) << " type is " << util::MSubject( vr );
		return false;
	}

	return true;
}
bool ImageFormat_Dicom::parseCSAValueList( const util::slist &val, const util::PropertyMap::PropPath &name, const util::istring &vr, util::PropertyMap &map )
{
	if ( vr == "IS" or vr == "SL" or vr == "US" or vr == "SS" ) {
		map.setValueAs( name, util::listToList<int32_t>( val.begin(), val.end() ) );
	} else if ( vr == "UL" ) {
		map.setValueAs( name, val); // @todo we dont have an unsigned int list
	} else if ( vr == "CS" or vr == "LO" or vr == "SH" or vr == "UN" or vr == "ST" or vr == "SL" ) {
		map.setValueAs( name, val );
	} else if ( vr == "DS" or vr == "FD" ) {
		map.setValueAs( name, util::listToList<double>( val.begin(), val.end() ) );
	} else if ( vr == "CS" ) {
		map.setValueAs( name, val );
	} else {
		LOG( Runtime, error ) << "Don't know how to parse CSA entry list " << std::make_pair( name, val ) << " type is " << util::MSubject( vr );
		return false;
	}

	return true;
}

DcmObject *ImageFormat_Dicom::dcmObject2PropMap( DcmObject *master_obj, util::PropertyMap &map, std::list<util::istring> dialects )const
{
// 	const std::string  old_loc=std::setlocale(LC_ALL,"C");
// 	DcmObject *img=nullptr;
// 	for ( DcmObject *obj = master_obj->nextInContainer( NULL ); obj; obj = master_obj->nextInContainer( obj ) ) {
// 		const DcmTagKey &tag = obj->getTag();
// 
// 		if ( tag == DcmTagKey( 0x7fe0, 0x0010 ) ){
// 			assert(!img);
// 			img=obj;
// 		} else if ( tag == DcmTagKey( 0x0029, 0x1010 ) || tag == DcmTagKey( 0x0029, 0x1020 ) ) { //CSAImageHeaderInfo
// 			boost::optional< util::PropertyValue& > known = map.queryProperty( "Private Code for (0029,1000)-(0029,10ff)" );
// 
// 			if( known && known->as<std::string>() == "SIEMENS CSA HEADER" ) {
// 				if(!checkDialect(dialects,"nocsa")){
// 					const util::PropertyMap::PropPath name = ( tag == DcmTagKey( 0x0029, 0x1010 ) ) ? "CSAImageHeaderInfo" : "CSASeriesHeaderInfo";
// 					DcmElement *elem = dynamic_cast<DcmElement *>( obj );
// 					try{
// 						parseCSA( elem, map.touchBranch( name ), dialects );
// 					} catch(std::exception &e){
// 						LOG( Runtime, error ) << "Error parsing CSA data ("<< util::MSubject(e.what()) <<"). Deleting " << util::MSubject(name);
// 					}
// 				}
// 			} else {
// 				LOG( Runtime, warning ) << "Ignoring entry " << tag.toString() << ", binary format " << *known << " is not known";
// 			}
// 		} else if ( tag == DcmTagKey( 0x0029, 0x0020 ) ) { //MedComHistoryInformation
// 			//@todo special handling needed
// 			LOG( Debug, info ) << "Ignoring MedComHistoryInformation at " << tag.toString();
// 		} else if ( obj->isLeaf() ) { // common case
// 			if ( obj->getTag() == DcmTag( 0x0008, 0x0032 ) ) {
// 				OFString buff;
// 				dynamic_cast<DcmElement *>( obj )->getOFString( buff, 0 );
// 
// 				if( buff.length() < 8 ) {
// 					LOG( Runtime, warning ) << "The Acquisition Time " << util::MSubject( buff ) << " is not precise enough, ignoring it";
// 					continue;
// 				}
// 			}
// 
// 			DcmElement *elem = dynamic_cast<DcmElement *>( obj );
// 			const size_t mult = obj->getVM();
// 
// 			if ( mult == 0 )
// 				LOG( Runtime, verbose_info ) << "Skipping empty Dicom-Tag " << util::MSubject( tag2Name( tag ) );
// 			else if ( mult == 1 )
// 				parseScalar( elem, _internal::tag2Name( tag ), map );
// 			else
// 				parseList( elem, tag2Name( tag ), map );
// 		} else {
// 			DcmObject *buff=dcmObject2PropMap( obj, map.touchBranch( tag2Name( tag ) ), dialects );
// 			assert(!(img && buff));
// 			if(buff)img=buff;
// 		}
// 	}
// 	std::setlocale(LC_ALL,old_loc.c_str());
// 	return img;
}

namespace _internal{
	template<typename T, typename LT> util::ValueReference list_generate(const DicomElement *e){
		size_t mult=e->getLength()/sizeof(T);
		assert(float(mult)*sizeof(T) == e->getLength());
		auto wrap=e->dataAs<T>(mult);
		return util::Value<std::list<LT>>(std::list<LT>(wrap.begin(),wrap.end()));
	}
	template<typename T> util::ValueReference scalar_generate(const DicomElement *e){
		assert(e->getLength()==sizeof(T));
		T *v=(T*)e->data();
		return util::Value<T>(e->endian_swap() ? data::endianSwap(*v):*v);
	}
	util::ValueReference string_generate(const DicomElement *e){
		//@todo http://dicom.nema.org/Dicom/2013/output/chtml/part05/sect_6.2.html#note_6.1-2-1
		const uint8_t *start=e->data();
		const uint8_t *end=start+e->getLength()-1;
		while(end>=start && (*end==' '|| *end==0)) //cut of trailing spaces and zeros
			--end;
		const std::string s(start,end+1);
		return util::Value<std::string>(s);
	}
	util::ValueReference parseAS(const _internal::DicomElement *e){
		util::ValueReference ret;
		uint16_t duration = 0;
		std::string buff=string_generate(e)->castTo<std::string>();

		static boost::numeric::converter <
		uint16_t, double,
				boost::numeric::conversion_traits<uint16_t, double>,
				boost::numeric::def_overflow_handler,
				boost::numeric::RoundEven<double>
				> double2uint16;

		if ( _internal::try_cast( buff.substr( 0, buff.find_last_of( "0123456789" ) + 1 ), duration ) ) {
			switch ( buff.at( buff.size() - 1 ) ) {
			case 'D':
			case 'd':
				break;
			case 'W':
			case 'w':
				duration *= 7;
				break;
			case 'M':
			case 'm':
				duration = double2uint16( 30.436875 * duration ); // year/12
				break;
			case 'Y':
			case 'y':
				duration = double2uint16( 365.2425 * duration ); //mean length of a year
				break;
			default:
				LOG( Runtime, warning )
						<< "Missing age-type-letter, assuming days";
			}
			LOG( Debug, verbose_info )
					<< "Parsed age for " << e->getName() << "(" <<  buff << ")" << " as " << duration << " days";
			ret=util::Value<uint16_t>(duration);
		} else
			LOG( Runtime, warning )
					<< "Cannot parse age string \"" << buff << "\" in the field \"" << e->getName() << "\"";
		return ret;
	}

	std::map<std::string,DicomElement::generator> DicomElement::generator_map={
		//"trivial" conversions
		{"FL",{scalar_generate<float>,   list_generate<float,   double>, sizeof(float )}},
		{"FD",{scalar_generate<double>,  list_generate<double,  double>, sizeof(double)}},
		{"SS",{scalar_generate<int16_t>, list_generate<int16_t, int32_t>,sizeof(int16_t)}},
		{"SL",{scalar_generate<int32_t>, list_generate<int32_t, int32_t>,sizeof(int32_t)}},
		{"US",{scalar_generate<uint16_t>,list_generate<uint16_t,int32_t>,sizeof(uint16_t)}},
		{"UL",{scalar_generate<uint32_t>,nullptr,                        sizeof(uint32_t)}},
		//"normal" string types
		{"LT",{string_generate,nullptr,0}},
		{"LO",{string_generate,nullptr,0}},
		{"UI",{string_generate,nullptr,0}},
		{"ST",{string_generate,nullptr,0}},
		{"SH",{string_generate,nullptr,0}},
		{"CS",{string_generate,nullptr,0}},
		{"PN",{string_generate,nullptr,0}},
		{"AE",{string_generate,nullptr,0}},
		//number strings (keep them as string, type-system will take care of the conversion if neccesary)
		{"IS",{string_generate,nullptr,0}},
		{"DS",{string_generate,nullptr,0}},
		//time strings
		{"DA",{[](const _internal::DicomElement *e){return string_generate(e)->copyByID(util::Value<util::date>::staticID());},nullptr,0}},
		{"TM",{[](const _internal::DicomElement *e){return string_generate(e)->copyByID(util::Value<util::timestamp>::staticID());},nullptr,0}},
		{"DT",{[](const _internal::DicomElement *e){return string_generate(e)->copyByID(util::Value<util::timestamp>::staticID());},nullptr,0}}
	};
}
}
}
