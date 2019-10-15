#include <FAST/Visualization/SimpleWindow.hpp>
#include <FAST/Importers/VTKMeshFileImporter.hpp>
#include <FAST/Importers/ImageFileImporter.hpp>
#include <FAST/Visualization/ImageRenderer/ImageRenderer.hpp>
#include <FAST/Streamers/MeshFileStreamer.hpp>
#include "FAST/Testing.hpp"
#include "VertexRenderer.hpp"

namespace fast {

TEST_CASE("VertexRenderer on LV surface model", "[fast][VertexRenderer][visual]") {
    CHECK_NOTHROW(
            VTKMeshFileImporter::pointer importer = VTKMeshFileImporter::New();
            importer->setFilename(Config::getTestDataPath() + "Surface_LV.vtk");
            VertexRenderer::pointer renderer = VertexRenderer::New();
            renderer->addInputConnection(importer->getOutputPort());
            SimpleWindow::pointer window = SimpleWindow::New();
            window->addRenderer(renderer);
            window->setTimeout(1000);
            window->start();
    );
}

TEST_CASE("VertexRenderer 2D", "[fast][VertexRenderer][visual]") {

    ImageFileImporter::pointer importer = ImageFileImporter::New();
    importer->setFilename(Config::getTestDataPath()+"US/CarotidArtery/Right/US-2D_0.mhd");
    ImageRenderer::pointer imageRenderer = ImageRenderer::New();
    imageRenderer->addInputConnection(importer->getOutputPort());

    Mesh::pointer mesh = Mesh::New();
    std::vector<MeshVertex> vertices = {
            MeshVertex(Vector3f(1, 5, 0)),
            MeshVertex(Vector3f(10, 10, 0)),
            MeshVertex(Vector3f(20, 20, 0)),
    };
    mesh->create(vertices);

    VertexRenderer::pointer renderer = VertexRenderer::New();
    renderer->addInputData(mesh);
    renderer->setColor(0, Color::Red());

    SimpleWindow::pointer window = SimpleWindow::New();
    window->addRenderer(imageRenderer);
    window->addRenderer(renderer);
    window->setTimeout(500);
    window->set2DMode();
    window->start();
}



TEST_CASE("VertexRenderer 3D", "[fast][VertexRenderer][visual]") {
    Config::setStreamingMode(STREAMING_MODE_NEWEST_FRAME_ONLY);
    // Setup streaming
    auto streamer = MeshFileStreamer::New();
    streamer->setFilenameFormat("/home/androst/EchoBot_Recordings/2019-08-17-212249 TestDump004/PointClouds/#.vtk");
    streamer->enableLooping();
    streamer->setSleepTime(15);

    VertexRenderer::pointer renderer = VertexRenderer::New();
    renderer->addInputConnection(streamer->getOutputPort());
    renderer->setDefaultSize(1.5);

    SimpleWindow::pointer window = SimpleWindow::New();
    window->addRenderer(renderer);
    window->setTimeout(30000);
    window->start();
}


}
