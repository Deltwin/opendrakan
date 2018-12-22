/*
 * Renderer.cpp
 *
 *  Created on: 12 Nov 2018
 *      Author: zal
 */


#include <odOsg/Renderer.h>

#include <memory>

#include <osg/FrontFace>
#include <osgGA/TrackballManipulator>
#include <osgViewer/ViewerEventHandlers>

#include <odCore/Logger.h>
#include <odCore/LevelObject.h>
#include <odCore/OdDefines.h>

#include <odCore/render/Light.h>
#include <odCore/render/RendererEventListener.h>

#include <odOsg/ObjectNode.h>
#include <odOsg/ModelNode.h>
#include <odOsg/LayerNode.h>
#include <odOsg/Geometry.h>
#include <odOsg/Image.h>
#include <odOsg/Texture.h>
#include <odOsg/GlmAdapter.h>
#include <odOsg/Camera.h>
#include <odOsg/GuiNode.h>

namespace odOsg
{

    Renderer::Renderer()
    : mShaderFactory("resources/shader_src")
    , mEventListener(nullptr)
    , mLightingEnabled(true)
    {
        mViewer = new osgViewer::Viewer;

        mCamera = new Camera(mViewer->getCamera());

        osg::ref_ptr<osgViewer::StatsHandler> statsHandler(new osgViewer::StatsHandler);
        statsHandler->setKeyEventPrintsOutStats(0);
        statsHandler->setKeyEventTogglesOnScreenStats(osgGA::GUIEventAdapter::KEY_F1);
        mViewer->addEventHandler(statsHandler);

        mViewer->setKeyEventSetsDone(osgGA::GUIEventAdapter::KEY_Escape);

        mSceneRoot = new osg::Group;
        mViewer->setSceneData(mSceneRoot);

        mObjects = new osg::Group;
        mObjects->getOrCreateStateSet()->setAttribute(new osg::FrontFace(osg::FrontFace::CLOCKWISE));
        mSceneRoot->addChild(mObjects);

        mLayers = new osg::Group;
        mSceneRoot->addChild(mLayers);

        // set up root state
        osg::StateSet *ss = mSceneRoot->getOrCreateStateSet();
        ss->setAttribute(mShaderFactory.getProgram("default"));
        ss->setMode(GL_CULL_FACE, osg::StateAttribute::ON);

        mGlobalLightDiffuse   = new osg::Uniform("layerLightDiffuse",   osg::Vec3(0.0, 0.0, 0.0));
        mGlobalLightAmbient   = new osg::Uniform("layerLightAmbient",   osg::Vec3(0.0, 0.0, 0.0));
        mGlobalLightDirection = new osg::Uniform("layerLightDirection", osg::Vec3(0.0, 1.0, 0.0));
        ss->addUniform(mGlobalLightDiffuse);
        ss->addUniform(mGlobalLightAmbient);
        ss->addUniform(mGlobalLightDirection);

        mLocalLightsColor     = new osg::Uniform(osg::Uniform::FLOAT_VEC3, "objectLightDiffuse", OD_MAX_LIGHTS);
        mLocalLightsIntensity = new osg::Uniform(osg::Uniform::FLOAT, "objectLightIntensity", OD_MAX_LIGHTS);
        mLocalLightsRadius    = new osg::Uniform(osg::Uniform::FLOAT, "objectLightRadius", OD_MAX_LIGHTS);
        mLocalLightsPosition  = new osg::Uniform(osg::Uniform::FLOAT_VEC3, "objectLightPosition", OD_MAX_LIGHTS);
        ss->addUniform(mLocalLightsColor);
        ss->addUniform(mLocalLightsIntensity);
        ss->addUniform(mLocalLightsRadius);
        ss->addUniform(mLocalLightsPosition);

        _setupGuiStuff();
    }

    Renderer::~Renderer()
    {
        if(mViewer != nullptr)
        {
            Logger::warn() << "Render thread was not stopped when renderer was destroyed";
            mViewer->setDone(true);
        }

        // note: we need to do this even if the render thread already left it's thread function, or else it will std::terminate() us
        if(mRenderThread.joinable())
        {
            mRenderThread.join();
        }
    }

    void Renderer::onStart()
    {
        mRenderThread = std::thread(&Renderer::_threadedRender, this);
    }

    void Renderer::onEnd()
    {
        if(mRenderThread.joinable() && mViewer != nullptr)
        {
            mViewer->setDone(true);
            mRenderThread.join();
        }
    }

    void Renderer::setRendererEventListener(odRender::RendererEventListener *listener)
    {
        mEventListener = listener;
    }

    void Renderer::setEnableLighting(bool b)
    {
        mLightingEnabled = b;

        if(!b)
        {
            mGlobalLightAmbient->set(osg::Vec3(1.0, 1.0, 1.0));
        }
    }

    bool Renderer::isLightingEnabled() const
    {
        return mLightingEnabled;
    }

    odRender::Light *Renderer::createLight(od::LevelObject *obj)
    {
        od::RefPtr<odRender::Light> light = new odRender::Light(obj);

        mLights.push_back(light);

        return light;
    }

    odRender::ObjectNode *Renderer::createObjectNode(od::LevelObject &obj)
    {
        return new ObjectNode(this, mObjects);
    }

    odRender::ModelNode *Renderer::createModelNode(odDb::Model *model)
    {
        return new ModelNode(this, model);
    }

    odRender::LayerNode *Renderer::createLayerNode(od::Layer *layer)
    {
        return new LayerNode(this, layer, mLayers);
    }

    odRender::Image *Renderer::createImage(odDb::Texture *dbTexture)
    {
        return new Image(dbTexture);
    }

    odRender::Texture *Renderer::createTexture(odRender::Image *image)
    {
        Image *odOsgImage = dynamic_cast<Image*>(image);
        if(odOsgImage == nullptr)
        {
            throw od::Exception("Tried to create texture from non-odOsg image");
        }

        return new Texture(odOsgImage);
    }

    od::RefPtr<odRender::GuiNode> Renderer::createGuiNode()
    {
        return od::RefPtr<odRender::GuiNode>(new GuiNode);
    }

    odRender::GuiNode *Renderer::getGuiRootNode()
    {
        return mGuiRootNode;
    }

    odRender::Camera *Renderer::getCamera()
    {
        return mCamera;
    }

    void Renderer::applyLayerLight(const osg::Matrix &viewMatrix, const osg::Vec3 &diffuse, const osg::Vec3 &ambient, const osg::Vec3 &direction)
    {
        if(!mLightingEnabled)
        {
            return;
        }

        mGlobalLightDiffuse->set(diffuse);
        mGlobalLightAmbient->set(ambient);

        osg::Vec4 dirCs = osg::Vec4(direction, 0.0) * viewMatrix;
        mGlobalLightDirection->set(osg::Vec3(dirCs.x(), dirCs.y(), dirCs.z()));
    }

    void Renderer::applyToLightUniform(const osg::Matrix &viewMatrix, odRender::Light *light, size_t index)
    {
        if(index >= OD_MAX_LIGHTS)
        {
            throw od::InvalidArgumentException("Tried to apply light at out-of-bounds index");
        }

        if(!mLightingEnabled)
        {
            return;
        }

        mLocalLightsColor->setElement(index, GlmAdapter::toOsg(light->getColor()));
        mLocalLightsIntensity->setElement(index, light->getIntensityScaling());
        mLocalLightsRadius->setElement(index, light->getRadius());

        osg::Vec3 posWs = GlmAdapter::toOsg(light->getLevelObject()->getPosition());
        osg::Vec4 dirCs = osg::Vec4(posWs, 1.0) * viewMatrix;
        mLocalLightsPosition->setElement(index, osg::Vec3(dirCs.x(), dirCs.y(), dirCs.z()));
    }

    void Renderer::applyNullLight(size_t index)
    {
        if(index >= OD_MAX_LIGHTS)
        {
            throw od::InvalidArgumentException("Tried to apply null light at out-of-bounds index");
        }

        if(!mLightingEnabled)
        {
            return;
        }

        mLocalLightsColor->setElement(index, osg::Vec3(0.0, 0.0, 0.0));
        mLocalLightsIntensity->setElement(index, 0.0f);
    }

    void Renderer::getLightsIntersectingSphere(const od::BoundingSphere &sphere, std::vector<odRender::Light*> &lights)
    {
        // TODO: organize lights in a structure with efficient spatial search
        //  for now, just use a brute force technique by iterating over all registered lights.

        for(auto it = mLights.begin(); it != mLights.end(); ++it)
        {
            odRender::Light *l = *it;

            if(l->affects(sphere))
            {
                lights.push_back(l);
            }
        }
    }

    void Renderer::setFreeLook(bool f)
    {
        mCamera->setIgnoreViewChanges(f);

        if(f)
        {
            mViewer->setCameraManipulator(new osgGA::TrackballManipulator, true);

        }else
        {
            mViewer->setCameraManipulator(nullptr, false);
        }
    }

    void Renderer::_setupGuiStuff()
    {
        mGuiCamera = new osg::Camera;
        mGuiCamera->setReferenceFrame(osg::Transform::ABSOLUTE_RF);
        mGuiCamera->setProjectionMatrix(osg::Matrix::ortho2D(-1, 1, -1, 1));
        mGuiCamera->setViewMatrix(osg::Matrix::identity());
        mGuiCamera->setClearMask(GL_DEPTH_BUFFER_BIT);
        mGuiCamera->setRenderOrder(osg::Camera::POST_RENDER);
        mGuiCamera->setAllowEventFocus(false);
        mSceneRoot->addChild(mGuiCamera);

        mGuiRoot = new osg::Group;
        mGuiRoot->setCullingActive(false);
        osg::StateSet *ss = mGuiRoot->getOrCreateStateSet();
        ss->setMode(GL_BLEND, osg::StateAttribute::ON);
        ss->setMode(GL_DEPTH_TEST, osg::StateAttribute::OFF);
        ss->setMode(GL_CULL_FACE, osg::StateAttribute::OFF);
        mGuiCamera->addChild(mGuiRoot);

        mGuiRootNode = new GuiNode();
        mGuiRoot->addChild(mGuiRootNode->getOsgNode());
    }

    void Renderer::_threadedRender()
    {
        static const double maxFrameRate = 60;

        mViewer->realize();

        double simTime = 0;
        double frameTime = 0;
        while(!mViewer->done())
        {
            double minFrameTime = (maxFrameRate > 0.0) ? (1.0/maxFrameRate) : 0.0;
            osg::Timer_t startFrameTick = osg::Timer::instance()->tick();

            {
                std::lock_guard<std::mutex> lock(mRenderMutex);

                mViewer->advance(simTime);
                mViewer->eventTraversal();
                mViewer->updateTraversal();
                mViewer->renderingTraversals();
            }

            osg::Timer_t endFrameTick = osg::Timer::instance()->tick();
            frameTime = osg::Timer::instance()->delta_s(startFrameTick, endFrameTick);
            simTime += frameTime;
            if(frameTime < minFrameTime)
            {
                simTime += (minFrameTime-frameTime);
                std::this_thread::sleep_for(std::chrono::microseconds(1000000*static_cast<size_t>(minFrameTime-frameTime)));
            }
        }

        mViewer = nullptr;

        if(mEventListener != nullptr)
        {
            mEventListener->onRenderWindowClosed();
        }

        Logger::verbose() << "Render thread terminated";
    }

}
