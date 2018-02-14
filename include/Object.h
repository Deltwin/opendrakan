/*
 * Object.h
 *
 *  Created on: 8 Feb 2018
 *      Author: zal
 */

#ifndef OBJECT_H_
#define OBJECT_H_

#include <osg/Group>
#include <osg/PositionAttitudeTransform>

#include "db/Class.h"

namespace od
{

    class Level;

    typedef uint32_t ObjectId;

    class Object : public osg::Group
    {
    public:

        Object(Level &level);

        inline ObjectId getObjectId() const { return mId; }
        inline ClassPtr getClass() { return mClass; }

        void loadFromRecord(DataReader dr);

        // override osg::Group
		virtual const char *libraryName() const override { return "od";    }
        virtual const char *className()   const override { return "Object"; }

    private:

        Level &mLevel;
        ObjectId mId;
        ClassPtr mClass;
        std::unique_ptr<RflClass> mRflClassInstance;
        uint32_t mFlags;
        uint16_t mInitialEventCount;
    };

    typedef osg::ref_ptr<od::Object> ObjectPtr;
}



#endif /* OBJECT_H_ */
