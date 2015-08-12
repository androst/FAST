#include "Segmentation.hpp"

namespace fast {

void Segmentation::createFromImage(Image::pointer image) {
    create(image->getSize(), TYPE_UINT8, 1);

    SceneGraph::setParentNode(mPtr.lock(), image);
    setSpacing(image->getSpacing());
}

Segmentation::Segmentation() {
}

Segmentation::~Segmentation() {
    freeAll();
}

}
