/*
 * Client.cpp
 *
 *  Created on: Oct 6, 2019
 *      Author: zal
 */

#include <odCore/Client.h>

#include <chrono>
#include <cmath>

#include <odCore/Level.h>
#include <odCore/LevelObject.h>

#include <odCore/render/Renderer.h>

#include <odCore/input/InputManager.h>
#include <odCore/input/RawActionListener.h>

#include <odCore/physics/bullet/BulletPhysicsSystem.h>

#include <odCore/net/ClientConnector.h>
#include <odCore/net/ServerConnector.h>

#include <odCore/state/StateManager.h>


namespace od
{

    /**
     * @brief ClientConnector for connecting a local client to the server.
     */
    class LocalClientConnector final : public odNet::ClientConnector
    {
    public:

        LocalClientConnector(Client &client)
        : mClient(client)
        {
        }

        virtual void loadLevel(const std::string &path) override
        {
            od::FilePath lvlPath(path, mClient.getEngineRootDir());
            mClient.loadLevel(lvlPath);
        }

        virtual void objectTransformed(odState::TickNumber tick, LevelObjectId id, const od::ObjectTransform &tf) override
        {
            mClient.getStateManager().advanceUntil(tick);

            auto &obj = _getObjectById(id);
            mClient.getStateManager().objectTransformed(obj, tf, tick);
        }

        virtual void objectVisibilityChanged(odState::TickNumber tick, od::LevelObjectId id, bool visible) override
        {
            mClient.getStateManager().advanceUntil(tick);

            auto &obj = _getObjectById(id);
            mClient.getStateManager().objectVisibilityChanged(obj, visible, tick);
        }

        virtual void spawnObject(od::LevelObjectId id)
        {
            _getObjectById(id).spawned();
        }

        virtual void despawnObject(od::LevelObjectId id)
        {
            _getObjectById(id).despawned();
        }

        virtual void destroyObject(od::LevelObjectId id)
        {
             _getObjectById(id).requestDestruction();
        }


    private:

        od::LevelObject &_getObjectById(od::LevelObjectId id)
        {
            if(mClient.getLevel() == nullptr)
            {
                throw od::Exception("No level loaded");
            }

            auto object = mClient.getLevel()->getLevelObjectById(id);
            if(object == nullptr)
            {
                throw od::Exception("Invalid level object ID");
            }

            return *object;
        }

        Client &mClient;

    };



    Client::Client(odDb::DbManager &dbManager, odRfl::RflManager &rflManager, odRender::Renderer &renderer)
    : mDbManager(dbManager)
    , mRflManager(rflManager)
    , mRenderer(renderer)
    , mEngineRoot(".")
    , mIsDone(false)
    {
        mPhysicsSystem = std::make_unique<odBulletPhysics::BulletPhysicsSystem>(&renderer);
        mInputManager = std::make_unique<odInput::InputManager>();

        mActionListener = mInputManager->createRawActionListener();
        mActionListener->callback = [this](odInput::ActionCode code, odInput::ActionState state)
        {
            if(this->mServerConnector != nullptr)
            {
                this->mServerConnector->actionTriggered(code, state);
            }
        };
    }

    Client::~Client()
    {
    }

    std::unique_ptr<odNet::ClientConnector> Client::createLocalConnector()
    {
        return std::make_unique<LocalClientConnector>(*this);
    }

    void Client::setServerConnector(std::unique_ptr<odNet::ServerConnector> connector)
    {
        mServerConnector = std::move(connector);
    }

    void Client::loadLevel(const FilePath &lvlPath)
    {
        Logger::verbose() << "Client loading level " << lvlPath;

        Engine engine(*this);

        mLevel = std::make_unique<Level>(engine);
        mLevel->loadLevel(lvlPath.adjustCase(), mDbManager);

        mStateManager = std::make_unique<odState::StateManager>(*mLevel);

        mLevel->spawnAllObjects();
    }

    void Client::run()
    {
        Logger::info() << "OpenDrakan client starting...";

        mRenderer.setup();

        Logger::info() << "Client set up. Starting render loop";

        using namespace std::chrono;
        auto lastTime = high_resolution_clock::now();
        float tickInterval = (1.0/60.0);
        float lerpTime = 0.1;
        odState::TickNumber currentTick = 0;
        float tickTimeOffset = 0;
        while(!mIsDone.load(std::memory_order_relaxed))
        {
            auto now = high_resolution_clock::now();
            auto duration = duration_cast<microseconds>(now - lastTime);
            lastTime = now;
            float relTime = duration.count() * 1e-6;
            tickTimeOffset += relTime;

            if(mLevel != nullptr)
            {
                mLevel->update(relTime);
            }

            mPhysicsSystem->update(relTime);
            mInputManager->update(relTime);

            // if our tick value has gone out of reach of the state manager (because of clock skew, for instance),
            //  reset it using the target lerp time
            if(currentTick < mStateManager->getOldestTick() || currentTick > mStateManager->getLatestTick())
            {
                int ticksToMoveBack = std::ceil(lerpTime/tickInterval);
                tickTimeOffset = ticksToMoveBack*tickInterval - lerpTime;
                currentTick = mStateManager->getLatestTick() - ticksToMoveBack;

            }else
            {
                while(tickTimeOffset >= tickInterval)
                {
                    ++currentTick;
                    tickTimeOffset -= tickInterval;
                }
            }

            float tickLerp = tickTimeOffset/tickInterval;
            Logger::info() << "client tick: " << currentTick << " + " << tickLerp;
            mStateManager->apply(currentTick, tickLerp);

            mRenderer.frame(relTime);
        }

        Logger::info() << "Shutting down client gracefully";
    }

}
