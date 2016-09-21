#include "fft.hpp"
#include "common.hpp"

#ifdef HAVE_GSL
#include "gsl/fft.hpp"
#endif // HAVE_GSL

#ifdef HAVE_CLFFT
#include "details/clfft.hxx"
#endif //HAVE_CLFFT

#ifdef HAVE_FFTW
#include "details/fftw.hxx"
#endif //HAVE_FFTW

isis::data::Chunk isis::math::fft(isis::data::Chunk data, bool inverse)
{
	switch(data.getTypeID()){
	case data::ValueArray<uint8_t>::staticID():
	case data::ValueArray<uint16_t>::staticID():
	case data::ValueArray<int8_t>::staticID():
	case data::ValueArray<int16_t>::staticID():
	case data::ValueArray<float>::staticID():
	case data::ValueArray<std::complex< float >>::staticID():
		return fft_single(data,inverse);
	default:
		return fft_double(data,inverse);
	}
}

isis::data::TypedChunk< std::complex< float > > isis::math::fft_single(isis::data::MemChunk< std::complex< float > > data, bool inverse)
{
#ifdef HAVE_CLFFT
	cl::fft(data,inverse);
#elif HAVE_FFTW
	fftw::fft(data,inverse);
#else
	LOG(Runtime,error) << "Sorry, no single precision fft support compiled in (enable clFFT and/or fftw)";
#endif
	return data;
}

isis::data::TypedChunk< std::complex< double > > isis::math::fft_double(isis::data::MemChunk< std::complex< double > > data, bool inverse)
{
#ifdef HAVE_FFTW
	fftw::fft(data,inverse);
#elif HAVE_GSL
	gsl::fft(data,inverse);
#else
	LOG(Runtime,error) << "Sorry, no double precision fft support compiled in (enable gsl and/or fftw)";
#endif
	return data;
}
