/*
 * GuiNode.cpp
 *
 *  Created on: 21 Dec 2018
 *      Author: zal
 */

#include <odOsg/render/GuiNode.h>

#include <odOsg/GlmAdapter.h>
#include <odOsg/Utils.h>
#include <odOsg/render/GuiQuad.h>

#include <odCore/gui/Widget.h>

namespace odOsg
{

    class UpdateCallback : public osg::Callback
    {
    public:

        UpdateCallback(GuiNode *n)
        : mNode(n)
        , mLastSimTime(0.0)
        , mFirstUpdate(true)
        {
        }

        virtual bool run(osg::Object* object, osg::Object* data) override
        {
            osg::NodeVisitor *nv = data->asNodeVisitor();
            if(nv == nullptr)
            {
                return traverse(object, data);
            }

            const osg::FrameStamp *fs = nv->getFrameStamp();
            if(fs == nullptr)
            {
                return traverse(object, data);
            }

            double simTime = fs->getSimulationTime();

            if(mFirstUpdate)
            {
                mLastSimTime = simTime;
                mFirstUpdate = false;
            }

            if(mNode != nullptr)
            {
                mNode->update(simTime - mLastSimTime);
            }

            mLastSimTime = simTime;

            return traverse(object, data);
        }


    private:

        GuiNode *mNode;
        double mLastSimTime;
        bool mFirstUpdate;
    };


    GuiNode::GuiNode(osg::Group *guiRoot, odGui::Widget *w)
    : mGuiRoot(guiRoot)
    , mWidget(w)
    , mTransform(new osg::MatrixTransform)
    , mUpdateCallback(new UpdateCallback(this))
    {
        mTransform->addUpdateCallback(mUpdateCallback);

        mGuiRoot->addChild(mTransform);
    }

    GuiNode::~GuiNode()
    {
        if(mUpdateCallback != nullptr)
        {
            mTransform->removeUpdateCallback(mUpdateCallback);
        }

        mGuiRoot->removeChild(mTransform);
    }

    void GuiNode::setMatrix(const glm::mat4 &m)
    {
        mTransform->setMatrix(GlmAdapter::toOsg(m));
    }

    void GuiNode::setViewport(const glm::vec2 &offset, const glm::vec2 &size)
    {

    }

    void GuiNode::setOrthogonalMode()
    {

    }

    void GuiNode::setPerspectiveMode(float fov, float aspect)
    {

    }

    void GuiNode::setVisible(bool visible)
    {
        mTransform->setNodeMask(visible ? 1 : 0);
    }

    void GuiNode::setZIndex(int32_t zIndex)
    {
        mTransform->getOrCreateStateSet()->setRenderBinDetails(-zIndex, "RenderBin");
    }

    void GuiNode::reorderChildren()
    {
    }

    odRender::GuiQuad *GuiNode::createGuiQuad()
    {
        if(mGeode == nullptr)
        {
            mGeode = new osg::Geode;
            mTransform->addChild(mGeode);
        }

        mGuiQuads.push_back(od::make_refd<GuiQuad>());

        mGeode->addDrawable(mGuiQuads.back()->getOsgGeometry());

        return mGuiQuads.back();
    }

    void GuiNode::removeGuiQuad(odRender::GuiQuad *quad)
    {
        if(mGeode == nullptr)
        {
            return;
        }

        for(auto it = mGuiQuads.begin(); it != mGuiQuads.end(); ++it)
        {
            if((*it) == quad)
            {
                mGeode->removeDrawable((*it)->getOsgGeometry());
                mGuiQuads.erase(it);
                break;
            }
        }
    }

    odRender::ObjectNode *GuiNode::createObjectNode()
    {
        return nullptr;
    }

    void GuiNode::removeObjectNode(odRender::ObjectNode *node)
    {

    }

    void GuiNode::update(float relTime)
    {
        if(mWidget != nullptr)
        {
            mWidget->update(relTime);
        }
    }

}

