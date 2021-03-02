/*
 * Object.h
 *
 *  Created on: 8 Feb 2018
 *      Author: zal
 */

#ifndef OBJECT_H_
#define OBJECT_H_

#include <memory>
#include <array>

#include <glm/vec3.hpp>
#include <glm/gtc/quaternion.hpp>

#include <odCore/BoundingBox.h>
#include <odCore/BoundingSphere.h>
#include <odCore/IdTypes.h>
#include <odCore/Message.h>
#include <odCore/ObjectStates.h>
#include <odCore/ObjectRecord.h>

#include <odCore/anim/AnimModes.h>

#include <odCore/db/Class.h>

#include <odCore/state/Event.h>

namespace odAnim
{
    class Skeleton;
    class SkeletonAnimationPlayer;
}

namespace odDb
{
    class Animation;
    class Model;
}

namespace odRender
{
    class Handle;
}

namespace odPhysics
{
    class ObjectHandle;
}

namespace od
{
    class ObjectLightReceiver;
    class Level;
    class Layer;

    enum class SpawnStrategy
    {
        Never,
        WhenInSight,
        Always
    };

    enum class ObjectRenderMode
    {
        NOT_RENDERED,
        NORMAL,
        NO_LIGHTING
    };

    enum class ObjectPhysicsMode
    {
        NO_PHYSICS,
        SOLID
    };

    class LevelObject
    {
    public:

        /**
         * Since many objects can be created from the same object records, the \c id argument is used instead of the ID stored in the record passed here.
         */
        LevelObject(Level &level, uint16_t recordIndex, ObjectRecordData &record, LevelObjectId id, std::shared_ptr<odDb::Class> dbClass);
        LevelObject(LevelObject &&obj) = delete;
        virtual ~LevelObject();

        inline uint16_t getRecordIndex() const { return mRecordIndex; }
        inline LevelObjectId getObjectId() const { return mId; }
        inline std::shared_ptr<odDb::Class> getClass() { return mClass; }
        inline std::shared_ptr<odDb::Model> getModel() { return mModel; }
        inline odRfl::ClassBase *getClassInstance() { return mRflClassInstance.get(); }
        inline odRfl::SpawnableClass *getSpawnableClassInstance() { return mSpawnableClass; }
        inline Level &getLevel() { return mLevel; }
        inline glm::vec3 getPosition() const { return mStates.position.get(); }
        inline glm::vec3 getScale() const { return mStates.scale.get(); }
        inline glm::quat getRotation() const { return mStates.rotation.get(); }
        inline void setSpawnStrategy(SpawnStrategy s) { mSpawnStrategy = s; }
        inline SpawnStrategy getSpawnStrategy() const { return mSpawnStrategy; }
        inline bool isSpawned() const { return mIsSpawned; }
        inline const std::vector<LevelObjectId> &getLinkedObjects() const { return mLinkedObjects; }
        inline Layer *getLightSourceLayer() { return mLightingLayer; }
        inline bool isVisible() const { return mStates.visibility.get(); }
        inline bool isScaled() const { return (getScale() != glm::vec3(1,1,1)); }
        inline void setAssociateWithCeiling(bool b) { mAssociateWithCeiling = b; }
        inline Layer *getAssociatedLayer() const { return mAssociatedLayer; } ///< @return The layer this object is associated with, or nullptr if none
        inline bool isStatePredictionEnabled() const { return mEnableStatePrediction; }
        inline ObjectStates &getStates() { return mStates; }
        inline const ObjectStates &getStates() const { return mStates; }

        inline std::shared_ptr<odRender::Handle> getRenderHandle() { return mRenderHandle; }
        inline std::shared_ptr<odPhysics::ObjectHandle> getPhysicsHandle() { return mPhysicsHandle; }
        inline std::shared_ptr<odAnim::Skeleton> getSkeleton() { return mSkeleton; }
        inline std::shared_ptr<odAnim::SkeletonAnimationPlayer> getSkeletonAnimationPlayer() { return mSkeletonAnimationPlayer; }

        void spawn();
        void despawn();

        // these are convenience methods that create and pass an ObjectStates object to setStates().
        void setPosition(const glm::vec3 &v);
        void setRotation(const glm::quat &q);
        void setPositionRotation(const glm::vec3 &v, const glm::quat &q);
        void setScale(const glm::vec3 &s);
        void setVisible(bool v);
        void setRunning(bool b);

        /**
         * @brief Applies the passed states to this object and notifies the StateManager about any changes.
         */
        void setStates(const ObjectStates &states);

        /**
         * @brief Same as setStates(), but will not notify the StateManager of the change.
         *
         * This is basically only used by the state manager itself.
         */
        void setStatesUntracked(const ObjectStates &states);

        /**
         * @brief Will cause the RFL instance's states to be probed and all changed states to be added to the snapshot.
         */
        void extraStatesDirty();

        void setExtraStatesUntracked(const odState::StateBundleBase &states);

        odState::StateBundleBase *getExtraStates();

        /**
         * If enabled, any updates to networked states of this object are
         * considered to be predictions and are handled via a consensus
         * algorithm to smoothly compensate prediction errors.
         */
        void setEnableStatePrediction(bool b);

        /**
         * @brief Enables or disables updates for this object.
         *
         * Changing this in the update hook will not prevent the postUpdate hook to be called. The change
         * will only become effective with the next update.
         */
        void setEnableUpdate(bool enable);

        /**
         * @brief Called each tick during the update stage.
         * @param relTime  The time passed since the last update, in seconds.
         */
        void update(float relTime);

        /**
         * @brief Called after everything in the level has been updated and a snapshot is about to occur.
         *
         * This is where an object can perform updates that only need to reflect the final level state for this tick.
         * These updates should not affect other objects.
         *
         * One example for what might happen here is updating the object's associated layer, which only needs to consider
         * the final position assigned during this tick.
         */
        void postUpdate(float relTime);

        void setAssociatedLayer(od::Layer *l);

        /**
         * @brief Attaches this object to target object.
         *
         * Attaches this LevelObject to the LevelObject \c target. Any transformation applied to \c target will also be applied to
         * \c this. This only happens in a relative context. For instance, any offset between this and \c target will be maintained
         * unless \c ignoreTranslation is true and the scale ration between these two object will be maintained unless
         * \c ignoreScale is true.
         *
         * If \c ignoreTranslation is true, translation changes applied to \c target will not be transferred to this object.
         * If \c ignoreRotation is true, rotation  changesapplied to \c target will not be transferred to this object.
         * If \c ignoreScale is true, scaling changes applied to \c target will not be transferred to this object.
         *
         * If \c ignoreTranslation, \c ignoreRotation and \c ignoreScale are all true, this method will panic.
         *
         * Calling this with \c target as \c nullptr is equivalent to calling \c detach().
         *
         * If this is called on a LevelObject that is already attached to something, it will be detached first.
         *
         * If the attachment target despawns, this object will automatically be detached.
         */
        void attachTo(LevelObject *target, bool ignoreTranslation, bool ignoreRotation, bool ignoreScale);

        /**
         * @brief Attaches this object to bone/channel of target character object.
         *
         * Attaches this LevelObject to the channel with index \c channelIndex in LevelObject \c target.
         * Any transformation applied to the respective channel in \c target will also be applied to
         * \c this. Any rotations transferred from the channel to this will pivot relative to the channel's origin.
         * If \c target has no skeleton or no channel with the given index, this method will panic.
         *
         * If \c clearOffset is true, \c this will be moved to target channel prior to attachment so their transformation will be kept equal
         * for the duration of the attachment.
         *
         * If this is called on a LevelObject that is already attached to something, it will be detached first.
         */
        void attachToChannel(LevelObject *target, size_t channelIndex, bool clearOffset = false);

        /**
         * @brief Detaches this object from any objects. Will do nothing if this object was not attached to anything.
         */
        void detach();

        void receiveMessage(LevelObject &sender, od::Message message);
        void receiveMessageWithoutDispatch(LevelObject &sender, od::Message message);
        void messageAllLinkedObjects(od::Message message);

        void requestDestruction();

        /**
         * @brief Calculates a axis-aligned bounding box for this object from it's model, taking position, rotation and scale into account.
         */
        AxisAlignedBoundingBox getBoundingBox();

        /**
         * @brief Calculates a bounding sphere for this object from it's model, taking position and scale into account.
         *
         * Note that rotation does not affect bounding spheres.
         */
        BoundingSphere getBoundingSphere();

        void updateAssociatedLayer(bool callChangedHook = true);

        void setRflClassInstance(std::unique_ptr<odRfl::ClassBase> instance);

        void setupRenderingAndPhysics(ObjectRenderMode renderMode, ObjectPhysicsMode physicsMode);
        void setupSkeleton();

        void playAnimation(std::shared_ptr<odDb::Animation> anim, const odAnim::AnimModes &modes);
        void playAnimationUntracked(std::shared_ptr<odDb::Animation> anim, const odAnim::AnimModes &modes);

        /**
         * This returns true if the event was handled (an event being ignored counts as handled in this context).
         * However, sometimes events can not be processed immediately, like when an object is not being run.
         * In that case, this will return false to indicate event processing should be delayed. The same event
         * will be dispatched later (with a timeDelta reflecting that delay).
         *
         * @param timeDelta  Number of seconds that have elapsed since this event should have happened (always >= 0)
         *
         * @return true if the event was handled, false if handling needs to be retried later.
         */
        bool handleEvent(const odState::EventVariant &event, float timeDelta);


    private:

        friend class HandleVisitor;

        Level &mLevel;

        // loaded from the object record:
        uint16_t mRecordIndex;
        LevelObjectId mId;
        std::shared_ptr<odDb::Class> mClass;
        std::shared_ptr<odDb::Model> mModel;
        Layer *mLightingLayer;
        std::vector<LevelObjectId> mLinkedObjects; // this is sorta abused, since during load it stores the indices instead. those are later translated

        ObjectStates mStates;

        bool mIsSpawned;
        SpawnStrategy mSpawnStrategy;

        Layer *mAssociatedLayer;
        bool mAssociateWithCeiling;

        std::unique_ptr<odRfl::ClassBase> mRflClassInstance;
        odRfl::SpawnableClass *mSpawnableClass; // downcast version of mRflClassInstance, so we don't have to cast for every call to Spawnable methods

        bool mRunObjectAi;
        bool mEnableUpdate;
        bool mEnableStatePrediction;

        ObjectRenderMode mRenderMode;
        ObjectPhysicsMode mPhysicsMode;
        std::shared_ptr<odRender::Handle> mRenderHandle;
        std::shared_ptr<odPhysics::ObjectHandle> mPhysicsHandle;
        std::unique_ptr<ObjectLightReceiver> mLightReceiver;
        std::shared_ptr<odAnim::Skeleton> mSkeleton;
        std::shared_ptr<odAnim::SkeletonAnimationPlayer> mSkeletonAnimationPlayer;
    };

}



#endif /* OBJECT_H_ */
