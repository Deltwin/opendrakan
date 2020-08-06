
#include <odOsg/render/Group.h>

#include <algorithm>

#include <odCore/Exception.h>
#include <odCore/Downcast.h>

#include <odOsg/render/Handle.h>
#include <odOsg/GlmAdapter.h>

namespace odOsg
{

    Group::Group(osg::Group *parent)
    : mParentGroup(parent)
    , mTransform(new osg::MatrixTransform())
    {
        if(mParentGroup != nullptr)
        {
            mParentGroup->addChild(mTransform);
        }
    }

    Group::~Group()
    {
        if(mParentGroup != nullptr)
        {
            mParentGroup->removeChild(mTransform);
        }
    }

    void Group::addHandle(std::shared_ptr<odRender::Handle> handle)
    {
        OD_CHECK_ARG_NONNULL(handle);

        auto myHandle = od::confident_downcast<Handle>(handle);
        mHandles.push_back(myHandle);

        mTransform->addChild(myHandle->getOsgNode());
    }

    void Group::removeHandle(std::shared_ptr<odRender::Handle> handle)
    {
        OD_CHECK_ARG_NONNULL(handle);

        auto myHandle = od::confident_downcast<Handle>(handle);

        auto it = std::find(mHandles.begin(), mHandles.end(), myHandle);
        if(it != mHandles.end())
        {
            mTransform->removeChild(myHandle->getOsgNode());
            mHandles.erase(it);
        }
    }

    void Group::setMatrix(const glm::mat4 &m)
    {
        osg::Matrix osgMatrix = GlmAdapter::toOsg(m);
        mTransform->setMatrix(osgMatrix);
    }

    void Group::setVisible(bool visible)
    {
        int mask = visible ? -1 : 0;
        mTransform->setNodeMask(mask);
    }

}
