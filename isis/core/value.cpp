#include "value.hpp"

#include <sstream>

namespace isis{
namespace util{


ValueNew::ValueNew(const ValueTypes &v):ValueTypes(v){print(true,std::cout<<"Copy created ")<< std::endl;}

ValueNew::ValueNew(ValueTypes &&v):ValueTypes(v){print(true,std::cout<<"Move created ")<< std::endl;}
ValueNew::ValueNew():ValueTypes(){}

std::string ValueNew::toString(bool with_typename)const{
	std::stringstream o;
	print(with_typename,o);
	return o.str();
}

}}
