#include "value.hpp"

#include <sstream>

namespace isis::util{

namespace _internal{
/**
 * Generic value operation class.
 * This generic class does nothing, and the ()-operator will allways fail with an error send to the debug-logging.
 * It has to be (partly) specialized for the regarding type.
 */
template<typename OPERATOR,bool modifying,bool enable> struct type_op
{
	typedef typename OPERATOR::result_type result_type;
	typedef std::integral_constant<bool,enable> enabled;
	typename std::conditional<modifying,typename OPERATOR::first_argument_type,const typename OPERATOR::first_argument_type>::type lhs;

	result_type operator()( lhs &first, const ValueNew &second )const {
		LOG( Debug, error ) << "operator " << typeid(OPERATOR).name() << " is not supportet for " << first.getTypeName()  << " and "<< second.typeName();
		throw std::domain_error("operation not available");
	}
};

/**
* Half-generic value operation class.
* This generic class does math operations on Values by converting the second Value-object to the type of the first Value-object. Then:
* - if the conversion was successfull (the second value can be represented in the type of the first) the "inRange"-operation is used
* - if the conversion failed with an positive or negative overflow (the second value is to high/low to fit into the type of the first) a info sent to the debug-logging and the posOverflow/negOverflow operation is used
* - if there is no known conversion from second to first an error is sent to the debug-logging and std::domain_error is thrown.
* \note The functions (posOverflow,negOverflow) here are only stubs and will allways throw std::domain_error.
* \note inRange will return OPERATOR()(first,second)
* These class can be further specialized for the regarding operation.
*/
template<typename OPERATOR,bool modifying> struct type_op<OPERATOR,modifying,true>
{
	virtual ~type_op(){}
	typename std::conditional<modifying, typename OPERATOR::first_argument_type,const typename OPERATOR::first_argument_type>::type lhs;
	typename OPERATOR::second_argument_type rhs;
	typename OPERATOR::result_type result_type;
	typedef std::integral_constant<bool,true> enabled;

	virtual result_type posOverflow()const {throw std::domain_error("positive overflow");}
	virtual result_type negOverflow()const {throw std::domain_error("negative overflow");}
	virtual result_type inRange( lhs &first, const rhs &second )const {
		return OPERATOR()(first,second);
	}
	result_type operator()(lhs &first, const ValueNew &second )const {
		// ask second for a converter from itself to Value<T>
		const ValueNew::Converter conv = second.getConverterTo( ValueNew::staticIndex<rhs>() );

		if ( conv ) {
			//try to convert second into T and handle results
			rhs buff;

			switch ( conv->convert( second, buff ) ) {
				case boost::numeric::cPosOverflow:return posOverflow();
				case boost::numeric::cNegOverflow:return negOverflow();
				case boost::numeric::cInRange:
					LOG_IF(second.isFloat() && second.as<float>()!=static_cast<ValueBase&>(buff).as<float>(), Debug,warning) //we can't really use Value<T> yet, so make it ValueBase
								<< "Using " << second.toString( true ) << " as " << buff.toString( true ) << " for operation on " << first.toString( true )
								<< " you might loose precision";
					return inRange( first, buff );
			}
		}
		throw std::domain_error(rhs::staticName()+" not convertible to "+second.getTypeName());
	}
};

}

ValueNew::ValueNew(const ValueTypes &v):ValueTypes(v){print(true,std::cout<<"Copy created ")<< std::endl;}

ValueNew::ValueNew(ValueTypes &&v):ValueTypes(v){print(true,std::cout<<"Move created ")<< std::endl;}
ValueNew::ValueNew():ValueTypes(){}

std::string ValueNew::toString(bool with_typename)const{
	std::stringstream o;
	print(with_typename,o);
	return o.str();
}

std::string ValueNew::typeName() const {
	return std::visit(_internal::name_visitor(),static_cast<const ValueTypes&>(*this));
}

const _internal::ValueConverterMap &ValueNew::converters(){
	static _internal::ValueConverterMap ret; //@todo not using class Singleton because ValueArrayConverterMap is hidden
	return ret;
}

const ValueNew::Converter &ValueNew::getConverterTo(unsigned short ID) const {
	const auto f1 = converters().find( index() );
	assert( f1 != converters().end() );
	const auto f2 = f1->second.find( ID );
	assert( f2 != f1->second.end() );
	return f2->second;
}

ValueNew ValueNew::createByID(unsigned short ID) {
	const auto f1 = converters().find(ID);
	assert( f1 != converters().end() );
	const auto f2 = f1->second.find( ID );
	assert( f2 != f1->second.end() );
	return f2->second->create();//trivial conversion to itself should always be there
}

ValueNew ValueNew::copyByID(unsigned short ID) const{
	const Converter &conv = getConverterTo( ID );
	ValueNew to;

	if ( conv ) {
		switch ( conv->generate( *this, to ) ) {
			case boost::numeric::cPosOverflow:
				LOG( Runtime, error ) << "Positive overflow when converting " << MSubject( toString( true ) ) << " to " << MSubject( getTypeMap( true, false )[ID] ) << ".";
				break;
			case boost::numeric::cNegOverflow:
				LOG( Runtime, error ) << "Negative overflow when converting " << MSubject( toString( true ) ) << " to " << MSubject( getTypeMap( true, false )[ID] ) << ".";
				break;
			case boost::numeric::cInRange:
				break;
		}

		return to; // return the generated Value-Object - wrapping it into Reference
	} else {
		LOG( Runtime, error ) << "I don't know any conversion from " << MSubject( toString( true ) ) << " to " << MSubject( getTypeMap( true, false )[ID] );
		return createByID(ID); // return an empty Reference
	}
}

bool ValueNew::fitsInto(unsigned short ID) const { //@todo find a better way to do this
	const Converter &conv = getConverterTo( ID );
	ValueNew to = createByID(ID);

	if ( conv ) {
		return ( conv->generate( *this, to ) ==  boost::numeric::cInRange );
	} else {
		LOG( Runtime, info )
			<< "I dont know any conversion from "
			<< MSubject( toString( true ) ) << " to " << MSubject( getTypeMap( true, false )[ID] );
		return false; // return an empty Reference
	}
}

bool ValueNew::convert(const ValueNew &from, ValueNew &to) {
	const Converter &conv = from.getConverterTo( to.index() );

	if ( conv ) {
		switch ( conv->convert( from, to ) ) {
			case boost::numeric::cPosOverflow:
				LOG( Runtime, error ) << "Positive overflow when converting " << from.toString( true ) << " to " << to.typeName() << ".";
				break;
			case boost::numeric::cNegOverflow:
				LOG( Runtime, error ) << "Negative overflow when converting " << from.toString( true ) << " to " << to.typeName() << ".";
				break;
			case boost::numeric::cInRange:
				return true;
				break;
		}
	} else {
		LOG( Runtime, error )
			<< "I don't know any conversion from "
			<< MSubject( from.toString( true ) ) << " to " << MSubject( to.typeName() );
	}

	return false;
}

bool ValueNew::gt(const ValueNew &ref) const {
	return false;
}

}
