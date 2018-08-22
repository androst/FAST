#ifndef CAMERA_ACCESS_HPP_
#define CAMERA_ACCESS_HPP_


#include "FAST/Data/DataTypes.hpp"

namespace fast {

class FAST_EXPORT  CameraAccess {
    public:
        CameraAccess(Vector3f* position, Vector3f* upVector, Vector3f* target);
        Affine3f getCameraTransformation() const;
        Vector3f getTargetPosition() const;
        Vector3f getPosition() const;
        Vector3f getUpVector() const;
        void setTargetPosition(Vector3f position);
        void setPosition(Vector3f position);
        void setUpVector(Vector3f upVector);
        typedef std::unique_ptr<CameraAccess> pointer;
    private:
        Vector3f* mPosition;
        Vector3f* mUpVector;
        Vector3f* mTarget;
};

}

#endif
