#include <DataStorage/io_application.hpp>
#include <DataStorage/io_factory.hpp>
#include <muParser/muParser.h>


using namespace isis;

class VoxelOp : public data::Chunk::VoxelOp<float>{
	mu::Parser parser;
	double voxBuff;
	util::FixedVector<double,4> posBuff;
public:
	VoxelOp(std::string expr){
		parser.SetExpr(expr);
		parser.DefineVar(std::string("vox"),&voxBuff);
		parser.DefineVar(std::string("pos_x"),&posBuff[data::rowDim]);
		parser.DefineVar(std::string("pos_y"),&posBuff[data::columnDim]);
		parser.DefineVar(std::string("pos_z"),&posBuff[data::sliceDim]);
		parser.DefineVar(std::string("pos_t"),&posBuff[data::timeDim]);
	}
	bool operator()(float& vox, const isis::util::FixedVector< size_t,4 >& pos){
		voxBuff=vox; //using parser.DefineVar every time would slow down the evaluation
		posBuff=pos;
		vox=parser.Eval();
	}

};

int main( int argc, char **argv )
{
	data::IOApplication app( "isis calc", true, true );
	app.parameters["voxelop"]=std::string("vox");
	app.parameters["voxelop"].setDescription("a term to evaluate the new value of each voxel. Available variables are: vox,pos_x,pos_y,pos_z,pos_t.");
	app.init( argc, argv, true ); // will exit if there is a problem


	std::list<data::Image> out;
	std::string op=app.parameters["voxelop"];

	VoxelOp vop(op);

	BOOST_FOREACH(data::TypedImage<float> img,app.images){
		std::cout << "Computing vox=(" << op << ") for each voxel of the " << img.getSizeAsString() << "-Image" << std::endl;
		img.foreachVoxel<float>(vop);
		out.push_back(img);
	}

	app.autowrite( out );
	return EXIT_SUCCESS;
}
