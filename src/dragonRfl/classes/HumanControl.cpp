/*
 * HumanControl.cpp
 *
 *  Created on: 2 Apr 2018
 *      Author: zal
 */

#include <dragonRfl/classes/HumanControl.h>

#include <vector>

#include <glm/gtc/constants.hpp>

#include <odCore/LevelObject.h>
#include <odCore/Level.h>
#include <odCore/Client.h>
#include <odCore/Server.h>

#include <odCore/anim/Skeleton.h>
#include <odCore/anim/SkeletonAnimationPlayer.h>

#include <odCore/rfl/Rfl.h>
#include <odCore/rfl/PrefetchProbe.h>

#include <odCore/input/InputManager.h>
#include <odCore/input/Keys.h>

#include <odCore/audio/SoundSystem.h>

#include <odCore/physics/PhysicsSystem.h>

#include <odCore/render/Renderer.h>

#include <dragonRfl/classes/TrackingCamera.h>

namespace dragonRfl
{

    static const float TURN_ANIM_THRESHOLD = glm::half_pi<float>(); // angular yaw speed at which turn animation is triggered (in rad/sec)


    HumanControl_Sv::HumanControl_Sv(odNet::ClientId clientId)
    : mClientId(clientId)
    , mYaw(0)
	, mPitch(0)
    , mState(State::Idling)
    , mLastUpdatedYaw(0)
    {
    }

    HumanControl_Sv::~HumanControl_Sv()
    {
    }

    void HumanControl_Sv::onLoaded()
    {
        // prefetch referenced assets
        odRfl::PrefetchProbe probe(getLevelObject().getClass()->getDependencyTable());
        mFields.probeFields(probe);

        // configure controls FIXME: these handlers are not memory safe because actions are not uniquely owned!
        auto actionHandler = std::bind(&HumanControl_Sv::_handleAction, this, std::placeholders::_1, std::placeholders::_2);
        auto analogActionHandler = std::bind(&HumanControl_Sv::_handleAnalogAction, this, std::placeholders::_1, std::placeholders::_2);

        auto &inputManager = getServer().getInputManagerForClient(mClientId);

        auto &forwardAction = inputManager.getAction(Action::Forward);
        forwardAction.setRepeatable(false);
        forwardAction.addCallback(actionHandler);

        auto &backwardAction = inputManager.getAction(Action::Backward);
        backwardAction.setRepeatable(false);
        backwardAction.addCallback(actionHandler);

        auto &attackAction = inputManager.getAction(Action::Attack_Primary);
        attackAction.setRepeatable(false);
        attackAction.addCallback(actionHandler);
        attackAction.setIgnoreUpEvents(true);

        auto &lookAction = inputManager.getAnalogAction(Action::Look);
        lookAction.addCallback(analogActionHandler);
    }

    void HumanControl_Sv::onSpawned()
    {
        auto &obj = getLevelObject();

    	Logger::verbose() << "Spawned Human Control at "
    			<< obj.getPosition().x << "/"
				<< obj.getPosition().y << "/"
				<< obj.getPosition().z;

    	glm::vec3 playerLookDirection = glm::eulerAngles(obj.getRotation());
    	mPitch = playerLookDirection.x;
    	mYaw = playerLookDirection.y;

        obj.setupRenderingAndPhysics(od::ObjectRenderMode::NOT_RENDERED, od::ObjectPhysicsMode::SOLID);
        obj.setupSkeleton();

        auto animPlayer = obj.getSkeletonAnimationPlayer();
        if(animPlayer != nullptr)
        {
            mCharacterController = std::make_shared<odPhysics::CharacterController>(getServer().getPhysicsSystem(), obj.getPhysicsHandle(), obj, 0.05, 0.3);

            animPlayer->setBoneAccumulator(mCharacterController, 0);
            animPlayer->setBoneModes({ odAnim::BoneMode::NORMAL,
                                       odAnim::BoneMode::NORMAL,
                                       odAnim::BoneMode::NORMAL }, 0);

            odAnim::AnimModes animModes;
            animModes.playbackType = odAnim::PlaybackType::LOOPING;
            animPlayer->playAnimation(mFields.readyAnim.getAsset(), animModes);

        }else
        {
            Logger::warn() << "Used Human Control class on object without skeleton";
        }

    	obj.setEnableUpdate(true);
    }

    void HumanControl_Sv::onUpdate(float relTime)
    {
        //auto guard = getServer().compensateLag(mClientId);
        auto &obj = getLevelObject();

        obj.setRotation(glm::quat(glm::vec3(0, mYaw, 0)));

        float yawSpeed = (mYaw - mLastUpdatedYaw)/relTime;
        mLastUpdatedYaw = mYaw;

        auto animPlayer = obj.getSkeletonAnimationPlayer();

        // handle state transitions that might happen during update
        switch(mState)
        {
        case State::Idling:
        case State::TurningLeft:
        case State::TurningRight:
            if(yawSpeed >= TURN_ANIM_THRESHOLD)
            {
                _playAnim(mFields.turnLeft, true, false);
                mState = State::TurningLeft;

            }else if(yawSpeed <= -TURN_ANIM_THRESHOLD)
            {
                _playAnim(mFields.turnRight, true, false);
                mState = State::TurningRight;

            }else if(mState != State::Idling)
            {
                if(animPlayer != nullptr && animPlayer->isPlaying())
                {
                    break; // wait till turn anim is done
                }

                _playAnim(mFields.readyAnim, true, false);
                mState = State::Idling;
            }
            break;

        default:
            break;
        }

        if(mCharacterController != nullptr)
        {
            mCharacterController->update(relTime);
        }
    }

    void HumanControl_Sv::_handleAction(Action action, odInput::ActionState state)
    {
        // TODO: we probably have to add the packet and view latency to these state changes
        float clientLag = getServer().getEstimatedClientLag(mClientId); // skip animations ahead by this time

        if(state == odInput::ActionState::BEGIN)
        {
            switch(action)
            {
            case Action::Forward:
                _playAnim(mFields.runAnim, false, true, clientLag);
                mState = State::RunningForward;
                break;

            case Action::Backward:
                _playAnim(mFields.runBackwards, false, true, clientLag);
                mState = State::RunningBackward;
                break;

            case Action::Attack_Primary:
                _playAnim(mFields.oneHandRH, false, false, clientLag);
                _attack();
                break;

            default:
                break;
            }

        }else
        {
            _playAnim(mFields.readyAnim, true, true, clientLag);
            mState = State::Idling;
        }
    }

    void HumanControl_Sv::_handleAnalogAction(Action action, const glm::vec2 &pos)
    {
        if(action == Action::Look)
        {
            glm::vec2 yawPitch = TrackingCamera_Cl::cursorPosToYawPitch(pos);
            mYaw = yawPitch.x;
            mPitch = yawPitch.y;
        }
    }

    void HumanControl_Sv::_attack()
    {
        Logger::info() << "Trying to attack...";

        auto &obj = getLevelObject();
        auto pos = obj.getPosition();
        odPhysics::ContactTestResultVector results;
        getServer().getPhysicsSystem().sphereTest(pos, 1.0, odPhysics::PhysicsTypeMasks::LevelObject, results);

        for(auto &result : results)
        {
            odPhysics::ObjectHandle *objectHandle = result.handle->asObjectHandle();
            if(objectHandle != nullptr && objectHandle->getLevelObject().getObjectId() != obj.getObjectId())
            {
                Logger::info() << "I, " << obj.getObjectId() << ", attacked " << objectHandle->getLevelObject().getObjectId();
            }
        }
    }

    void HumanControl_Sv::_playAnim(const odRfl::AnimRef &animRef, bool skeletonOnly, bool looping, float skipAheadTime)
    {
        odAnim::AnimModes modes;
        modes.playbackType = looping ? odAnim::PlaybackType::LOOPING : odAnim::PlaybackType::NORMAL;
        if(skeletonOnly)
        {
            modes.boneModes = { odAnim::BoneMode::NORMAL, odAnim::BoneMode::NORMAL, odAnim::BoneMode::NORMAL };

        }else
        {
            modes.boneModes = { odAnim::BoneMode::ACCUMULATE, odAnim::BoneMode::NORMAL, odAnim::BoneMode::ACCUMULATE};
        }

        auto animPlayer = getLevelObject().getSkeletonAnimationPlayer();
        if(animPlayer != nullptr)
        {
            animPlayer->playAnimation(animRef.getAsset(), modes);
            animPlayer->setBoneModes(modes.boneModes, 0);
            if(skipAheadTime > 0)
            {
                animPlayer->update(skipAheadTime);
            }
        }
    }


    HumanControl_Cl::HumanControl_Cl()
    {
    }

    HumanControl_Cl::~HumanControl_Cl()
    {
    }

    void HumanControl_Cl::onLoaded()
    {
        odRfl::PrefetchProbe probe(getLevelObject().getClass()->getDependencyTable());
        mFields.probeFields(probe);

        auto &obj = getLevelObject();
        obj.setSpawnStrategy(od::SpawnStrategy::Always);
        obj.setEnableStatePrediction(true);

        auto analogActionHandler = std::bind(&HumanControl_Cl::_handleAnalogAction, this, std::placeholders::_1, std::placeholders::_2);
        auto &lookAction = getClient().getInputManager().getAnalogAction(Action::Look);
        lookAction.addCallback(analogActionHandler);
    }

    void HumanControl_Cl::onSpawned()
    {
        auto &obj = getLevelObject();

    	obj.setupRenderingAndPhysics(od::ObjectRenderMode::NORMAL, od::ObjectPhysicsMode::SOLID);
        obj.setupSkeleton();

        auto animPlayer = obj.getSkeletonAnimationPlayer();
        if(animPlayer != nullptr)
        {
            odAnim::AnimModes modes;
            modes.playbackType = odAnim::PlaybackType::LOOPING;
            animPlayer->playAnimation(mFields.readyAnim.getAsset(), modes);
        }

        // create a tracking camera for me
        std::vector<std::shared_ptr<od::LevelObject>> foundCameras;
        obj.getLevel().findObjectsOfType(TrackingCamera::classId(), foundCameras);
        if(foundCameras.empty())
        {
            Logger::error() << "Found no Tracking Camera in level! This could be an error in level design.";

        }else
        {
            if(foundCameras.size() > 1)
            {
                Logger::warn() << "More than one Tracking Camera found in level! Ignoring all but one";
            }

            auto &cameraObject = *foundCameras.back();
            auto trackingCameraInstance = od::downcast<TrackingCamera_Cl>(cameraObject.getClassInstance());
            trackingCameraInstance->setObjectToTrack(&obj);
        }
    }

    void HumanControl_Cl::onTransformChanged()
    {
        auto &obj = getLevelObject();

        odAudio::SoundSystem *soundSystem = getClient().getSoundSystem();
        if(soundSystem != nullptr)
        {
            glm::vec3 pos = obj.getPosition();

            glm::quat lookDirection = obj.getRotation();
            glm::vec3 at = lookDirection * glm::vec3(0, 0, -1);
            glm::vec3 up = glm::vec3(0, 1, 0);

            soundSystem->setListenerPosition(pos);
            soundSystem->setListenerOrientation(at, up);
        }
    }

    void HumanControl_Cl::onUpdate(float relTime)
    {
    }

    void HumanControl_Cl::_handleAnalogAction(Action action, const glm::vec2 &pos)
    {
        if(action == Action::Look)
        {
            // TODO: we have to mark this as a prediction, and have to shift the server side calculation so it roughly matches up
            //  with this state update
            glm::vec2 yawPitch = TrackingCamera_Cl::cursorPosToYawPitch(pos);
            getLevelObject().setRotation(glm::quat(glm::vec3(0, yawPitch.x, 0)));
        }
    }


    HumanControlDummy_Cl::HumanControlDummy_Cl()
    {
    }

    HumanControlDummy_Cl::~HumanControlDummy_Cl()
    {
    }

    void HumanControlDummy_Cl::onSpawned()
    {
    	getLevelObject().setupRenderingAndPhysics(od::ObjectRenderMode::NORMAL, od::ObjectPhysicsMode::SOLID);
    }

}
