
#include <Windows.h>
//#include "Application.h"
#include <iostream>
#include "core/lattice.h"     // greg::QuadMesh, roundLattice, makeCube
#include "core/patch_mesh.h"  // greg::PatchMesh

using namespace greg;

#ifdef _DEBUG
int main()
{
#else
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
{
#endif
	// main.cpp の頭あたりで一時的に
	auto cube = greg::makeCube();
	auto pm = greg::roundLattice(cube);
	std::cout << "patches: " << pm.patches.size()   // 6
		<< " edges: " << pm.edges.size()     // 12（立方体の共有辺）
		<< std::endl;

	/*auto& app = Application::Instance();
	if (!app.Init()) {
		return -1;
	}
	app.Run();
	app.Terminate();*/
	return 0;
}