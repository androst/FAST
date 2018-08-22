#define _USE_MATH_DEFINES

#include "View.hpp"
#include "FAST/Data/Camera.hpp"
#include "FAST/Exception.hpp"
#include "FAST/DeviceManager.hpp"
#include "FAST/Visualization/SliceRenderer/SliceRenderer.hpp"
#include "FAST/Visualization/ImageRenderer/ImageRenderer.hpp"
#include "FAST/Visualization/VolumeRenderer/VolumeRenderer.hpp"
#include "FAST/Utility.hpp"
#include "SimpleWindow.hpp"
#include "FAST/Utility.hpp"
#include <QGLFunctions>

#if defined(__APPLE__) || defined(__MACOSX)
#include <OpenCL/cl_gl.h>
#include <OpenGL/OpenGL.h>
#else
#if _WIN32
#include <GL/gl.h>

#include <CL/cl_gl.h>
#else
#include <GL/glx.h>

#include <CL/cl_gl.h>
#endif
#endif

#include <QCursor>
#include <QThread>

using namespace fast;

void View::addRenderer(Renderer::pointer renderer) {
    // Can renderer be casted to volume renderer test:
    auto test = std::dynamic_pointer_cast<VolumeRenderer>(renderer);
	bool thisIsAVolumeRenderer = (bool)test;

	if(thisIsAVolumeRenderer)
		mVolumeRenderers.push_back(renderer);
	else
		mNonVolumeRenderers.push_back(renderer);
}

void View::removeAllRenderers() {
    mVolumeRenderers.clear();
    mNonVolumeRenderers.clear();
}

void View::setBackgroundColor(Color color) {
	mBackgroundColor = color;
}

QGLFormat View::getGLFormat() {
    QGLFormat qglFormat = QGLFormat::defaultFormat();
    qglFormat.setVersion(3,3);
    qglFormat.setProfile(QGLFormat::CoreProfile);
    return qglFormat;
}

View::View() {
    createInputPort<Camera>(0, false);

    mBackgroundColor = Color::White();
    zNear = 0.1;
    zFar = 1000;
    fieldOfViewY = 45;
    mIsIn2DMode = false;
    mScale2D = 1.0f;
    mLeftMouseButtonIsPressed = false;
    mRightButtonIsPressed = false;
    mQuit = false;
	mCameraSet = false;
    mAutoUpdateCamera = false;

    mFramerate = 60;
    // Set up a timer that will call update on this object at a regular interval
    timer = new QTimer(this);
    timer->start(1000/mFramerate); // in milliseconds
    timer->setSingleShot(false);
    connect(timer,SIGNAL(timeout()),this,SLOT(update()));

	NonVolumesTurn=true;

    QGLContext* context = new QGLContext(getGLFormat(), this);
    context->create(fast::Window::getMainGLContext());
    this->setContext(context);
    if(!context->isValid() || !context->isSharing()) {
        reportInfo() << "The custom Qt GL context is invalid!" << Reporter::end();
        exit(-1);
    }
}

void View::setCameraInputConnection(DataPort::pointer port) {
    setInputConnection(0, port);
}

void View::setLookAt(Vector3f cameraPosition, Vector3f targetPosition, Vector3f cameraUpVector, float z_near, float z_far) {
    mCameraPosition = cameraPosition;
    mRotationPoint = targetPosition;
    // Equations based on gluLookAt https://www.opengl.org/sdk/docs/man2/xhtml/gluLookAt.xml
    Vector3f F = targetPosition - cameraPosition;
    F.normalize();
    Vector3f up = cameraUpVector;
    up.normalize();
    Vector3f s = F.cross(up);
    Vector3f sNormalized = s;
    sNormalized.normalize();
    Vector3f u = sNormalized.cross(F);

    Matrix3f M;
    // First row
    M(0,0) = s[0];
    M(0,1) = s[1];
    M(0,2) = s[2];
    // Second row
    M(1,0) = u[0];
    M(1,1) = u[1];
    M(1,2) = u[2];
    // Third row
    M(2,0) = -F[0];
    M(2,1) = -F[1];
    M(2,2) = -F[2];

    // Must calculate this somehow
    zNear = z_near;
    zFar = z_far;

    m3DViewingTransformation = Affine3f::Identity();
    m3DViewingTransformation.rotate(M);
    m3DViewingTransformation.translate(-mCameraPosition);

    mCameraSet = true;
}

void View::quit() {
    mQuit = true;
}

bool View::hasQuit() const {
    return mQuit;
}

View::~View() {
    reportInfo() << "DESTROYING view object" << Reporter::end();
    quit();
}


void View::setMaximumFramerate(unsigned int framerate) {
    if(framerate == 0)
        throw Exception("Framerate cannot be 0.");

    mFramerate = framerate;
    timer->stop();
    timer->start(1000/mFramerate); // in milliseconds
    timer->setSingleShot(false);
}

void View::execute() {
}

void View::updateRenderersInput(uint64_t timestep, StreamingMode mode) {
    for(Renderer::pointer renderer : mNonVolumeRenderers) {
        for(int i = 0; i < renderer->getNrOfInputConnections(); ++i) {
            renderer->getInputPort(i)->getProcessObject()->update(timestep, mode);
        }
    }
    for(Renderer::pointer renderer : mVolumeRenderers) {
        for(int i = 0; i < renderer->getNrOfInputConnections(); ++i) {
            renderer->getInputPort(i)->getProcessObject()->update(timestep, mode);
        }
    }
}

void View::updateRenderers(uint64_t timestep, StreamingMode mode) {
    for(Renderer::pointer renderer : mNonVolumeRenderers) {
        renderer->execute();
    }
    for(Renderer::pointer renderer : mVolumeRenderers) {
        renderer->execute();
    }
}

void View::lockRenderers() {
    for(Renderer::pointer renderer : mNonVolumeRenderers) {
        renderer->lock();
    }
    for(Renderer::pointer renderer : mVolumeRenderers) {
        renderer->lock();
    }
}

void View::stopRenderers() {
    for(Renderer::pointer renderer : mNonVolumeRenderers) {
        renderer->stopPipeline();
    }
    for(Renderer::pointer renderer : mVolumeRenderers) {
        renderer->stopPipeline();
    }
}

void View::unlockRenderers() {
    for(Renderer::pointer renderer : mNonVolumeRenderers) {
        renderer->unlock();
    }
    for(Renderer::pointer renderer : mVolumeRenderers) {
        renderer->unlock();
    }
}

void View::getMinMaxFromBoundingBoxes(bool transform, Vector3f& min, Vector3f& max) {
    // Get bounding boxes of all objects
    BoundingBox box = mNonVolumeRenderers.at(0)->getBoundingBox(transform);
    Vector3f corner = box.getCorners().row(0);
    min[0] = corner[0];
    max[0] = corner[0];
    min[1] = corner[1];
    max[1] = corner[1];
    min[2] = corner[2];
    max[2] = corner[2];
    for (int i = 0; i < mNonVolumeRenderers.size(); i++) {
        // Apply transformation to all b boxes
        // Get max and min of x and y coordinates of the transformed b boxes
        BoundingBox box = mNonVolumeRenderers.at(i)->getBoundingBox(transform);
        MatrixXf corners = box.getCorners();
        //reportInfo() << box << Reporter::end();
        for (int j = 0; j < 8; j++) {
            for (uint k = 0; k < 3; k++) {
                if (corners(j, k) < min[k])
                    min[k] = corners(j, k);

                if (corners(j, k) > max[k])
                    max[k] = corners(j, k);
            }
        }
    }
}

void View::recalculateCamera() {
    if(mIsIn2DMode) {
        // TODO Initialize 2D
       // Initialize camera
        Vector3f min, max;
        getMinMaxFromBoundingBoxes(false, min, max);
        mBBMin = min;
        mBBMax = max;
        // Calculate area of each side of the resulting bounding box
        float area[3] = { (max[0] - min[0]) * (max[1] - min[1]), // XY plane
        (max[1] - min[1]) * (max[2] - min[2]), // YZ plane
        (max[2] - min[2]) * (max[0] - min[0]) };
        uint maxArea = 0;
        for (uint i = 1; i < 3; i++) {
            if (area[i] > area[maxArea])
                maxArea = i;
        }
        // Find rotation needed
        float angleX, angleY;
        uint xDirection;
        uint yDirection;
        uint zDirection;
        switch (maxArea) {
            case 0:
                xDirection = 0;
                yDirection = 1;
                zDirection = 2;
                angleX = 0;
                angleY = 0;
            break;
            case 1:
                // Rotate 90 degres around Y axis
                xDirection = 2;
                yDirection = 1;
                zDirection = 0;
                angleX = 0;
                angleY = 90;
            break;
            case 2:
                // Rotate 90 degres around X axis
                xDirection = 0;
                yDirection = 2;
                zDirection = 1;
                angleX = 90;
                angleY = 0;
            break;
        }
        // Max pos - half of the size
        Vector3f centroid;
        centroid[0] = max[0] - (max[0] - min[0]) * 0.5;
        centroid[1] = max[1] - (max[1] - min[1]) * 0.5;
        centroid[2] = max[2] - (max[2] - min[2]) * 0.5;

        // Rotate object if needed
        Eigen::Quaternionf Qx;
        Qx = Eigen::AngleAxisf(angleX*M_PI/180.0f, Vector3f::UnitX());
        Eigen::Quaternionf Qy;
        Qy = Eigen::AngleAxisf(angleY*M_PI/180.0f, Vector3f::UnitY());
        Eigen::Quaternionf Q = Qx*Qy;

        //reportInfo() << "Centroid set to: " << centroid.x() << " " << centroid.y() << " " << centroid.z() << Reporter::end();
        // Initialize rotation point to centroid of object
        mRotationPoint = centroid;
        // Calculate initiali translation of camera
        // Move centroid to z axis
        // Note: Centroid does not change after rotation
        //mCameraPosition[1] = height()*0.5 - centroid[1];
        // Calculate z distance
        mCameraPosition[2] = -centroid[2]; // first move objects to origo
        // Move objects away from camera so that we see everything
        float z_width = (max[xDirection] - min[xDirection]);
        float z_height = (max[yDirection] - min[yDirection]);
        //reportInfo() << "asd: " << z_width << " " << z_height << Reporter::end();
        float minimumTranslationToSeeEntireObject = (
                z_width < z_height ? z_height : z_width);
        float boundingBoxDepth = std::max(max[zDirection] - min[zDirection], 0.1f);
        //reportInfo() << "minimum translation to see entire object: " << minimumTranslationToSeeEntireObject  << Reporter::end();
        //reportInfo() << "half depth of bounding box " << boundingBoxDepth*0.5 << Reporter::end();
        mCameraPosition[2] += -minimumTranslationToSeeEntireObject
                - boundingBoxDepth * 0.5; // half of the depth of the bounding box
        //reportInfo() << "Camera pos set to: " << cameraPosition.x() << " " << cameraPosition.y() << " " << cameraPosition.z() << Reporter::end();
        zFar = 1;//(minimumTranslationToSeeEntireObject + boundingBoxDepth) * 2;
        zNear = -1;//std::min(minimumTranslationToSeeEntireObject * 0.5, 0.1);
        mCameraPosition[2] = 0;
        aspect = (float) (this->width()) / this->height();
        float orthoAspect = z_width / z_height;
        float scalingWidth = 1;
        float scalingHeight = 1;
        if(aspect > orthoAspect) {
            scalingWidth = aspect / orthoAspect;
        } else {
            scalingHeight = orthoAspect / aspect;
        }
        mLeft = min[xDirection]*scalingWidth;
        mRight = max[xDirection]*scalingWidth;
        mBottom = min[yDirection]*scalingHeight;
        mTop = max[yDirection]*scalingHeight;

        mCameraPosition[0] = mLeft + (mRight - mLeft)*0.5f - centroid[0]; // center camera
        mCameraPosition[1] = mBottom + (mTop - mBottom)*0.5f - centroid[1]; // center camera
        mCameraPosition[1] = mCameraPosition[1] - 2.0f*(mBottom + (mTop - mBottom)*0.5f); // Compensate for Y flipping

        m3DViewingTransformation = Affine3f::Identity();
        //m3DViewingTransformation.pretranslate(-mRotationPoint); // Move to rotation point
        //m3DViewingTransformation.prerotate(Q.toRotationMatrix()); // Rotate
        //m3DViewingTransformation.pretranslate(mRotationPoint); // Move back from rotation point
        m3DViewingTransformation.scale(Vector3f(1, -1, 1)); // Flip y
        m3DViewingTransformation.translate(mCameraPosition);
        /*
        std::cout << "Centroid: " << centroid.transpose() << std::endl;
        std::cout << "Camera pos: " << mCameraPosition.transpose() << std::endl;
        std::cout << "width and height: " << this->width() << " " << this->height() << std::endl;
        std::cout << zNear << " " << zFar << std::endl;
        std::cout << min[xDirection] << " " << max[xDirection] << std::endl;
        std::cout << min[yDirection] << " " << max[yDirection] << std::endl;
        std::cout << "Ortho params: " << mLeft << " " << mRight << " " << mBottom << " " << mTop << " " << scalingWidth << " " << scalingHeight << " " << zNear << " " << zFar << std::endl;
         */
        mPerspectiveMatrix = loadOrthographicMatrix(mLeft, mRight, mBottom, mTop, zNear, zFar);
    } else {
        // 3D Mode
        aspect = (float) (this->width()) / this->height();
        fieldOfViewX = aspect * fieldOfViewY;
        // Initialize camera
        // Get bounding boxes of all objects
        Vector3f min, max;
        getMinMaxFromBoundingBoxes(true, min, max);
        mBBMin = min;
        mBBMax = max;

        // Calculate area of each side of the resulting bounding box
        float area[3] = {(max[0] - min[0]) * (max[1] - min[1]), // XY plane
                         (max[1] - min[1]) * (max[2] - min[2]), // YZ plane
                         (max[2] - min[2]) * (max[0] - min[0])};
        uint maxArea = 0;
        for (uint i = 1; i < 3; i++) {
            if (area[i] > area[maxArea])
                maxArea = i;
        }
        // Find rotation needed
        float angleX, angleY;
        uint xDirection;
        uint yDirection;
        uint zDirection;
        switch (maxArea) {
            case 0:
                xDirection = 0;
                yDirection = 1;
                zDirection = 2;
                angleX = 0;
                angleY = 0;
                break;
            case 1:
                // Rotate 90 degres around Y axis
                xDirection = 2;
                yDirection = 1;
                zDirection = 0;
                angleX = 0;
                angleY = 90;
                break;
            case 2:
                // Rotate 90 degres around X axis
                xDirection = 0;
                yDirection = 2;
                zDirection = 1;
                angleX = 90;
                angleY = 0;
                break;
        }
        // Max pos - half of the size
        Vector3f centroid;
        centroid[0] = max[0] - (max[0] - min[0]) * 0.5;
        centroid[1] = max[1] - (max[1] - min[1]) * 0.5;
        centroid[2] = max[2] - (max[2] - min[2]) * 0.5;

        // Rotate object if needed
        Eigen::Quaternionf Qx;
        Qx = Eigen::AngleAxisf(angleX * M_PI / 180.0f, Vector3f::UnitX());
        Eigen::Quaternionf Qy;
        Qy = Eigen::AngleAxisf(angleY * M_PI / 180.0f, Vector3f::UnitY());
        Eigen::Quaternionf Q = Qx * Qy;

        //reportInfo() << "Centroid set to: " << centroid.x() << " " << centroid.y() << " " << centroid.z() << Reporter::end();
        // Initialize rotation point to centroid of object
        mRotationPoint = centroid;
        // Calculate initiali translation of camera
        // Move centroid to z axis
        // Note: Centroid does not change after rotation
        mCameraPosition[0] = -centroid[0];
        mCameraPosition[1] = -centroid[1];
        // Calculate z distance
        mCameraPosition[2] = -centroid[2]; // first move objects to origo
        // Move objects away from camera so that we see everything
        float z_width = (max[xDirection] - min[xDirection]) * 0.5
                        / tan(fieldOfViewX * 0.5);
        float z_height = (max[yDirection] - min[yDirection]) * 0.5
                         / tan(fieldOfViewY * 0.5);
        //reportInfo() << "asd: " << z_width << " " << z_height << Reporter::end();
        float minimumTranslationToSeeEntireObject = (
                z_width < z_height ? z_height : z_width);
        float boundingBoxDepth = (max[zDirection] - min[zDirection]);
        //reportInfo() << "minimum translation to see entire object: " << minimumTranslationToSeeEntireObject  << Reporter::end();
        //reportInfo() << "half depth of bounding box " << boundingBoxDepth*0.5 << Reporter::end();
        mCameraPosition[2] += -minimumTranslationToSeeEntireObject
                              - boundingBoxDepth * 0.5; // half of the depth of the bounding box
        //reportInfo() << "Camera pos set to: " << cameraPosition.x() << " " << cameraPosition.y() << " " << cameraPosition.z() << Reporter::end();
        zFar = (minimumTranslationToSeeEntireObject + boundingBoxDepth) * 2;
        zNear = std::min(minimumTranslationToSeeEntireObject * 0.5, 0.1);
        //reportInfo() << "set zFar to " << zFar << Reporter::end();
        //reportInfo() << "set zNear to " << zNear << Reporter::end();
        m3DViewingTransformation = Affine3f::Identity();
        m3DViewingTransformation.pretranslate(-mRotationPoint); // Move to rotation point
        m3DViewingTransformation.prerotate(Q.toRotationMatrix()); // Rotate
        m3DViewingTransformation.pretranslate(mRotationPoint); // Move back from rotation point
        m3DViewingTransformation.pretranslate(mCameraPosition);
        mCentroidZ = -centroid[2];
    }
}

void View::reinitialize() {
    initializeGL();
}

void View::setStreamingMode(StreamingMode mode) {
    mStreamingMode = mode;
}

void View::initializeGL() {
    for(auto renderer : getRenderers())
        renderer->initializeOpenGLFunctions();
    QGLFunctions *fun = Window::getMainGLContext()->functions();

    glViewport(0, 0, this->width(), this->height());
    glEnable(GL_TEXTURE_2D);
    // Enable transparency
    glEnable(GL_BLEND);
    // Update all renderes, so that getBoundingBox works
    for(unsigned int i = 0; i < mNonVolumeRenderers.size(); i++)
        mNonVolumeRenderers[i]->update(0, mStreamingMode);
    if(mNonVolumeRenderers.size() == 0)
        return;
    if(mIsIn2DMode) {
        glDisable(GL_DEPTH_TEST);
        recalculateCamera();
    } else {
        glEnable(GL_DEPTH_TEST);
        // 3D mode
        if(!mCameraSet && getNrOfInputConnections() == 0) {
            // If camera is not set explicitly by user, FAST has to calculate it
            recalculateCamera();
        } else {
            aspect = (float) (this->width()) / this->height();
            fieldOfViewX = aspect * fieldOfViewY;
        }
        mPerspectiveMatrix = loadPerspectiveMatrix(fieldOfViewY, aspect, zNear, zFar);
    }

	reportInfo() << "Finished initializing OpenGL" << Reporter::end();

}


void View::paintGL() {

	mRuntimeManager->startRegularTimer("paint");

    glClearColor(mBackgroundColor.getRedValue(), mBackgroundColor.getGreenValue(), mBackgroundColor.getBlueValue(), 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    if(mAutoUpdateCamera) {
        // If bounding box has changed, recalculate camera
        Vector3f min, max;
        getMinMaxFromBoundingBoxes(!mIsIn2DMode, min, max);
        if(mBBMin != min || mBBMax != max)
            recalculateCamera();
    }

    if(mIsIn2DMode) {

        mRuntimeManager->startRegularTimer("draw2D");
        for(unsigned int i = 0; i < mNonVolumeRenderers.size(); i++) {
            mNonVolumeRenderers[i]->draw(mPerspectiveMatrix, m3DViewingTransformation.matrix(), true);
            mNonVolumeRenderers[i]->postDraw();
        }
        mRuntimeManager->stopRegularTimer("draw2D");

    } else {
        if(getNrOfInputConnections() > 0) {
            // Has camera input connection, get camera
            Camera::pointer camera = getInputData<Camera>(0);
            CameraAccess::pointer access = camera->getAccess(ACCESS_READ);
            mRotationPoint = access->getCameraTransformation()*access->getTargetPosition();
        }

        mRuntimeManager->startRegularTimer("draw");
        for(unsigned int i = 0; i < mNonVolumeRenderers.size(); i++) {
            mNonVolumeRenderers[i]->draw(mPerspectiveMatrix, m3DViewingTransformation.matrix(), false);
            mNonVolumeRenderers[i]->postDraw();
        }
        mRuntimeManager->stopRegularTimer("draw");
    }

    glFinish();
	mRuntimeManager->stopRegularTimer("paint");
}

void View::renderVolumes()
{
    QGLFunctions *fun = Window::getMainGLContext()->functions();

		//Rendere to Back buffer
		fun->glBindFramebuffer(GL_FRAMEBUFFER,0);

		
		glMatrixMode(GL_PROJECTION);
		glPushMatrix();
		glMatrixMode(GL_MODELVIEW);
		glPushMatrix();

		//Update Camera Matrix for VolumeRendere
		GLfloat modelView[16];
		glGetFloatv(GL_MODELVIEW_MATRIX, modelView);
		std::static_pointer_cast<VolumeRenderer>(mVolumeRenderers[0])->setModelViewMatrix(modelView);

		if (mNonVolumeRenderers.size() > 0)
		{
			std::static_pointer_cast<VolumeRenderer>(mVolumeRenderers[0])->addGeometryColorTexture(renderedTexture0);
			std::static_pointer_cast<VolumeRenderer>(mVolumeRenderers[0])->addGeometryDepthTexture(renderedTexture1);
		}
		

		mRuntimeManager->startRegularTimer("draw");
		for(unsigned int i = 0; i < mVolumeRenderers.size(); i++) {
            mVolumeRenderers[i]->draw(mPerspectiveMatrix, m3DViewingTransformation.matrix(), false);
            mVolumeRenderers[i]->postDraw();
		}
		mRuntimeManager->stopRegularTimer("draw");
		glMatrixMode(GL_PROJECTION);
		glPopMatrix();
		glMatrixMode(GL_MODELVIEW);
		glPopMatrix();
		
}

void View::getDepthBufferFromGeo()
{

    QGLFunctions *fun = Window::getMainGLContext()->functions();
	/*Converting the depth buffer texture from GL format to CL format >>>*/
	glMatrixMode(GL_MODELVIEW);
	glPushMatrix();
	glMatrixMode(GL_PROJECTION);
	glPushMatrix();

	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glViewport(0,0,this->width(),this->height());
	glOrtho(0, this->width(), 0, this->height(), 0, 512);

	// Render to Second Texture
	fun->glBindFramebuffer(GL_FRAMEBUFFER, fbo2);

	fun->glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, renderedDepthText);
	int loc = fun->glGetUniformLocation(programGLSL, "renderedDepthText");
	fun->glUniform1i(loc, renderedDepthText);

	fun->glUseProgram(programGLSL);

	glBegin(GL_QUADS);
	glTexCoord2f(0, 0);
	glVertex2f(0,0);
	glTexCoord2f(1.0, 0);
	glVertex2f(this->width(),0);
	glTexCoord2f(1.0, 1.0);
	glVertex2f(this->width(), this->height());
	glTexCoord2f(0, 1.0);
	glVertex2f(0,  this->height());
	glEnd();

	fun->glUseProgram(0);

	glBindTexture(GL_TEXTURE_2D, 0);


	//Rendere to Back buffer
	fun->glBindFramebuffer(GL_FRAMEBUFFER,0);

	glMatrixMode(GL_MODELVIEW);
	glPopMatrix();
	glMatrixMode(GL_PROJECTION);
	glPopMatrix();

}

void View::resizeGL(int width, int height) {
    glViewport(0, 0, width, height);
    if(mIsIn2DMode) {
        // TODO the aspect ratio of the viewport and the orhto projection (left, right, bottom, top) has to match.
        aspect = (float)width/height;
        float orthoAspect = (mRight - mLeft) / (mTop - mBottom);
        float scalingWidth = 1;
        float scalingHeight = 1;
        if(aspect > orthoAspect) {
            scalingWidth = aspect / orthoAspect;
        } else {
            scalingHeight = orthoAspect / aspect;
        }
        mPerspectiveMatrix = loadOrthographicMatrix(mLeft*scalingWidth, mRight*scalingWidth, mBottom*scalingHeight, mTop*scalingHeight, zNear, zFar);
    } else {
        aspect = (float)width/height;
        fieldOfViewX = aspect*fieldOfViewY;
        mPerspectiveMatrix = loadPerspectiveMatrix(fieldOfViewY, aspect, zNear, zFar);
    }
}

void View::keyPressEvent(QKeyEvent* event) {
    switch(event->key()) {
        case Qt::Key_R:
            recalculateCamera();
            break;
    }
}

void View::mouseMoveEvent(QMouseEvent* event) {
	if(mRightButtonIsPressed) {
        const float deltaX = event->x() - previousX;
        const float deltaY = event->y() - previousY;
        float actualMovementX, actualMovementY;
        if(mIsIn2DMode) {
            actualMovementX = deltaX*((mRight - mLeft)/width());
            actualMovementY = deltaY*((mTop - mBottom)/height());
        } else {
            float viewportWidth = std::tan((fieldOfViewX * M_PI / 180.0f) * 0.5f) * fabs(-mCameraPosition.z() + mCentroidZ) * 2.0f;
            float viewportHeight = std::tan((fieldOfViewY * M_PI / 180.0f) * 0.5f) * fabs(-mCameraPosition.z() + mCentroidZ) * 2.0f;
            actualMovementX = deltaX * viewportWidth / width();
            actualMovementY = deltaY * viewportHeight / height();
        }
        mCameraPosition[0] += actualMovementX;
        mCameraPosition[1] -= actualMovementY;
        m3DViewingTransformation.pretranslate(Vector3f(actualMovementX, -actualMovementY,0));
		previousX = event->x();
		previousY = event->y();
	} else if(mLeftMouseButtonIsPressed && !mIsIn2DMode) {
	    // 3D rotation
		int cx = width()/2;
		int cy = height()/2;

		if(event->x() == cx && event->y() == cy){ //The if cursor is in the middle
			return;
		}

		int diffx=event->x()-cx; //check the difference between the current x and the last x position
		int diffy=event->y()-cy; //check the difference between the current y and the last y position
		QCursor::setPos(mapToGlobal(QPoint(cx,cy)));
        Eigen::Quaternionf Qx;
        float sensitivity = 0.01;
        Qx = Eigen::AngleAxisf(sensitivity*diffx, Vector3f::UnitY());
        Eigen::Quaternionf Qy;
        Qy = Eigen::AngleAxisf(sensitivity*diffy, Vector3f::UnitX());
        Eigen::Quaternionf Q = Qx*Qy;
        Vector3f newRotationPoint = m3DViewingTransformation*mRotationPoint; // Move rotation point to new position
        m3DViewingTransformation.pretranslate(-newRotationPoint); // Move to rotation point
        m3DViewingTransformation.prerotate(Q.toRotationMatrix()); // Rotate
        m3DViewingTransformation.pretranslate(newRotationPoint); // Move back
	}
}

void View::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        mLeftMouseButtonIsPressed = true;
        // Move cursor to center of window
        int cx = width() / 2;
        int cy = height() / 2;
        QCursor::setPos(mapToGlobal(QPoint(cx, cy)));
    } else if (event->button() == Qt::RightButton) {
        previousX = event->x();
        previousY = event->y();
        mRightButtonIsPressed = true;
    }
}

void View::wheelEvent(QWheelEvent* event) {
    if(mIsIn2DMode) {
        // TODO Should instead increase the size of the orhtograpic projection here
        // TODO the aspect ratio of the viewport and the orhto projection (left, right, bottom, top) has to match.
        aspect = (float)width()/height();
        float orthoAspect = (mRight - mLeft) / (mTop - mBottom);
        float scalingWidth = 1;
        float scalingHeight = 1;
        if(aspect > orthoAspect) {
            scalingWidth = aspect / orthoAspect;
        } else {
            scalingHeight = orthoAspect / aspect;
        }
        // Zoom towards center
        if(event->delta() > 0) {
            float size = (mRight - mLeft)*0.05f;
            mLeft = mLeft + size;
            mRight = mRight - size;
            size = (mTop - mBottom)*0.05f;
            mBottom = mBottom + size;
            mTop = mTop - size;
        } else if(event->delta() < 0) {
            float size = (mRight - mLeft)*0.05f;
            mLeft = mLeft - size;
            mRight = mRight + size;
            size = (mTop - mBottom)*0.05f;
            mBottom = mBottom - size;
            mTop = mTop + size;
        }
        mPerspectiveMatrix = loadOrthographicMatrix(mLeft*scalingWidth, mRight*scalingWidth, mBottom*scalingHeight, mTop*scalingHeight, zNear, zFar);
    } else {
        if(event->delta() > 0) {
            mCameraPosition[2] += (zFar - zNear) * 0.05f;
            m3DViewingTransformation.pretranslate(Vector3f(0, 0, (zFar - zNear) * 0.05f));
        } else if(event->delta() < 0) {
            mCameraPosition[2] += -(zFar - zNear) * 0.05f;
            m3DViewingTransformation.pretranslate(Vector3f(0, 0, -(zFar - zNear) * 0.05f));
        }
    }
}

void View::mouseReleaseEvent(QMouseEvent* event) {
    if(event->button() == Qt::LeftButton) {
        mLeftMouseButtonIsPressed = false;
    } else if(event->button() == Qt::RightButton) {
        mRightButtonIsPressed = false;
    }
}


void View::set2DMode() {
    mIsIn2DMode = true;
}

void View::set3DMode() {
    mIsIn2DMode = false;
}

/********************************************************************************************************/
/********************************************************************************************************/
/********************************************************************************************************/
/********************************************************************************************************/

void View::initShader()
{

    QGLFunctions *fun = Window::getMainGLContext()->functions();
	//Read our shaders into the appropriate buffers
	std::string vertexSource =		"#version 120\nuniform sampler2D renderedDepthText;\n void main(){ gl_Position = gl_ProjectionMatrix * gl_ModelViewMatrix * gl_Vertex; \ngl_TexCoord[0] = gl_MultiTexCoord0;}\n";//Get source code for vertex shader.
	std::string fragmentSource =	"#version 120\nuniform sampler2D renderedDepthText;\nvoid main(){ \ngl_FragColor = texture2D(renderedDepthText, gl_TexCoord[0].st); }\n";//Get source code for fragment shader.

	//Create an empty vertex shader handle
	GLuint vertexShader = fun->glCreateShader(GL_VERTEX_SHADER);

	//Send the vertex shader source code to GL
	//Note that std::string's .c_str is NULL character terminated.
	const GLchar *source = (const GLchar *)vertexSource.c_str();
	fun->glShaderSource(vertexShader, 1, &source, 0);

	//Compile the vertex shader
	fun->glCompileShader(vertexShader);

	GLint isCompiled = 0;
	fun->glGetShaderiv(vertexShader, GL_COMPILE_STATUS, &isCompiled);
	if(isCompiled == GL_FALSE)
	{
		GLint maxLength = 0;
		fun->glGetShaderiv(vertexShader, GL_INFO_LOG_LENGTH, &maxLength);

		//The maxLength includes the NULL character
		std::vector<GLchar> infoLog(maxLength);
		fun->glGetShaderInfoLog(vertexShader, maxLength, &maxLength, &infoLog[0]);

		//We don't need the shader anymore.
		fun->glDeleteShader(vertexShader);

		//Use the infoLog as you see fit.

		//In this simple program, we'll just leave
		return;
	}

	//Create an empty fragment shader handle
	GLuint fragmentShader = fun->glCreateShader(GL_FRAGMENT_SHADER);

	//Send the fragment shader source code to GL
	//Note that std::string's .c_str is NULL character terminated.
	source = (const GLchar *)fragmentSource.c_str();
	fun->glShaderSource(fragmentShader, 1, &source, 0);

	//Compile the fragment shader
	fun->glCompileShader(fragmentShader);

	fun->glGetShaderiv(fragmentShader, GL_COMPILE_STATUS, &isCompiled);
	if(isCompiled == GL_FALSE)
	{
		GLint maxLength = 0;
		fun->glGetShaderiv(fragmentShader, GL_INFO_LOG_LENGTH, &maxLength);

		//The maxLength includes the NULL character
		std::vector<GLchar> infoLog(maxLength);
		fun->glGetShaderInfoLog(fragmentShader, maxLength, &maxLength, &infoLog[0]);

		//We don't need the shader anymore.
		fun->glDeleteShader(fragmentShader);
		//Either of them. Don't leak shaders.
		fun->glDeleteShader(vertexShader);

		//Use the infoLog as you see fit.

		//In this simple program, we'll just leave
		return;
}

//Vertex and fragment shaders are successfully compiled.
//Now time to link them together into a program.
//Get a program object.
programGLSL = fun->glCreateProgram();

//Attach our shaders to our program
fun->glAttachShader(programGLSL, vertexShader);
fun->glAttachShader(programGLSL, fragmentShader);

//Link our program
fun->glLinkProgram(programGLSL);

//Note the different functions here: glGetProgram* instead of glGetShader*.
GLint isLinked = 0;
fun->glGetProgramiv(programGLSL, GL_LINK_STATUS, (int *)&isLinked);
if(isLinked == GL_FALSE)
{
	GLint maxLength = 0;
	fun->glGetProgramiv(programGLSL, GL_INFO_LOG_LENGTH, &maxLength);

	//The maxLength includes the NULL character
	std::vector<GLchar> infoLog(maxLength);
	fun->glGetProgramInfoLog(programGLSL, maxLength, &maxLength, &infoLog[0]);

	//We don't need the program anymore.
	fun->glDeleteProgram(programGLSL);
	//Don't leak shaders either.
	fun->glDeleteShader(vertexShader);
	fun->glDeleteShader(fragmentShader);

	//Use the infoLog as you see fit.

	//In this simple program, we'll just leave
	return;
}

//Always detach shaders after a successful link.
fun->glDetachShader(programGLSL, vertexShader);
fun->glDetachShader(programGLSL, fragmentShader);
}

std::vector<Renderer::pointer> View::getRenderers() {
    std::vector<Renderer::pointer> newList = mNonVolumeRenderers;
    newList.insert(newList.cend(), mVolumeRenderers.begin (), mVolumeRenderers.end());
    return newList;
}

void View::resetRenderers() {
    for(Renderer::pointer renderer : mNonVolumeRenderers) {
        renderer->reset();
    }
    for(Renderer::pointer renderer : mVolumeRenderers) {
        renderer->reset();
    }
}

Vector4f View::getOrthoProjectionParameters() {
    return Vector4f(mLeft, mRight, mBottom, mTop);
}

void View::setAutoUpdateCamera(bool autoUpdate) {
    mAutoUpdateCamera = autoUpdate;
}

/********************************************************************************************************/
/********************************************************************************************************/
/********************************************************************************************************/
/********************************************************************************************************/
