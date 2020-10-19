
#include <odCore/state/StateManager.h>

#include <algorithm>

#include <odCore/Level.h>
#include <odCore/LevelObject.h>

#include <odCore/net/DownlinkConnector.h>
#include <odCore/net/UplinkConnector.h>

namespace odState
{

    static const TickNumber TICK_CAPACITY = 16;

    /**
     * @brief An RAII object that disables state updates on the StateManager as long as it lives.
     */
    class ApplyGuard
    {
    public:

        ApplyGuard(StateManager &sm)
        : mStateManager(sm)
        {
            mStateManager.mIgnoreStateUpdates = true;
        }

        ApplyGuard(const ApplyGuard &g) = delete;

        ~ApplyGuard()
        {
            mStateManager.mIgnoreStateUpdates = false;
        }


    private:

        StateManager &mStateManager;
    };


    StateManager::StateManager(od::Level &level)
    : mLevel(level)
    , mIgnoreStateUpdates(false)
    {
    }

    void StateManager::setUplinkConnector(std::shared_ptr<odNet::UplinkConnector> c)
    {
        mUplinkConnectorForAck = c;
    }

    TickNumber StateManager::getLatestTick()
    {
        std::lock_guard<std::mutex> lock(mSnapshotMutex);

        return mSnapshots.empty() ? INVALID_TICK : mSnapshots.back().tick;
    }

    double StateManager::getLatestRealtime()
    {
        std::lock_guard<std::mutex> lock(mSnapshotMutex);

        return mSnapshots.empty() ? 0.0 : mSnapshots.back().realtime;
    }

    void StateManager::objectStatesChanged(od::LevelObject &object, const od::ObjectStates &newStates)
    {
        if(mIgnoreStateUpdates) return;
        auto &storedStates = mCurrentUpdateChangeMap[object.getObjectId()].baseStates;
        storedStates.merge(storedStates, newStates);
    }

    void StateManager::objectCustomStateChanged(od::LevelObject &object)
    {
        OD_UNIMPLEMENTED();
    }

    void StateManager::incomingObjectStatesChanged(TickNumber tick, od::LevelObjectId objectId, const od::ObjectStates &states)
    {
        auto snapshotIt = _getSnapshot(tick, mIncomingSnapshots, true);
        auto &baseStates = snapshotIt->changes[objectId].baseStates;
        baseStates.merge(baseStates, states);
        _commitIncomingIfComplete(tick, snapshotIt);
    }

    void StateManager::incomingObjectCustomStateChanged(TickNumber tick, od::LevelObjectId objectId)
    {
        OD_UNIMPLEMENTED();
    }

    void StateManager::confirmIncomingSnapshot(TickNumber tick, double time, size_t changeCount, TickNumber referenceTick)
    {
        auto stagedSnapshot = _getSnapshot(tick, mIncomingSnapshots, true); // TODO: clean up incoming snapshots that have never been confirmed
        stagedSnapshot->realtime = time;
        stagedSnapshot->targetDiscreteChangeCount = changeCount;
        stagedSnapshot->confirmed = true;
        stagedSnapshot->referenceSnapshot = referenceTick;

        _commitIncomingIfComplete(tick, stagedSnapshot);
    }

    void StateManager::commit(double realtime)
    {
        std::lock_guard<std::mutex> lock(mSnapshotMutex);

        if(mSnapshots.size() >= TICK_CAPACITY)
        {
            // TODO: reclaim the discarded change map so we don't allocate a new one for every commit
            mSnapshots.pop_front();
        }

        TickNumber nextTick = mSnapshots.empty() ? FIRST_TICK : mSnapshots.back().tick + 1;
        mSnapshots.emplace_back(nextTick);

        auto &newSnapshot = mSnapshots.back();
        newSnapshot.changes = mCurrentUpdateChangeMap;
        newSnapshot.realtime = realtime;
    }

    void StateManager::apply(double realtime)
    {
        std::lock_guard<std::mutex> lock(mSnapshotMutex);
        ApplyGuard applyGuard(*this);

        if(mSnapshots.empty())
        {
            // can't apply anything on an empty timeline. we are done right away.
            return;
        }

        // find the first snapshots with a time later than the requested one
        auto pred = [](double realtime, Snapshot &snapshot) { return realtime < snapshot.realtime; };
        auto it = std::upper_bound(mSnapshots.begin(), mSnapshots.end(), realtime, pred);

        if(it == mSnapshots.end())
        {
            // the latest snapshot is older than the requested time -> extrapolate
            //  TODO: extrapolation not implemented. applying latest snapshot verbatim for now
            for(auto &objChange : mSnapshots.back().changes)
            {
                od::LevelObject *obj = mLevel.getLevelObjectById(objChange.first);
                if(obj == nullptr) continue;

                obj->setStates(objChange.second.baseStates, true);
            }

        }else if(it == mSnapshots.begin())
        {
            // we only have one snapshot in the timeline, and it's later than the requested time.
            //  extrapolating here is probably unnecessary, so we just apply the snapshot as if it happened right now.
            for(auto &objChange : it->changes)
            {
                od::LevelObject *obj = mLevel.getLevelObjectById(objChange.first);
                if(obj == nullptr) continue;

                obj->setStates(objChange.second.baseStates, true);
            }

        }else
        {
            Snapshot &a = *(it-1);
            Snapshot &b = *it;

            double delta = (realtime - a.realtime)/(b.realtime - a.realtime);

            for(auto &objChange : a.changes)
            {
                od::LevelObject *obj = mLevel.getLevelObjectById(objChange.first);
                if(obj == nullptr) continue;

                auto changeInB = b.changes.find(objChange.first);
                if(changeInB == b.changes.end())
                {
                    // no corresponding change in B. this should not happen, as
                    //  all snapshots reflect all changes since load. for now, assume steady state
                    obj->setStates(objChange.second.baseStates, true);

                }else
                {
                    // TODO: a flag indicating that a state has not changed since the last snapshot might improve performance a tiny bit by ommitting the lerp.
                    //  we still have to store full snapshots, as we must move through the timeline arbitrarily, and for every missing state, we'd have to
                    //  search previous and intermediate snapshots to recover the original state
                    od::ObjectStates lerped;
                    lerped.lerp(objChange.second.baseStates, changeInB->second.baseStates, delta);
                    obj->setStates(lerped, true);
                }
            }
        }
    }

    void StateManager::sendSnapshotToClient(TickNumber tickToSend, odNet::DownlinkConnector &c, TickNumber referenceSnapshot)
    {
        std::lock_guard<std::mutex> lock(mSnapshotMutex);

        auto toSend = _getSnapshot(tickToSend, mSnapshots, false);
        if(toSend == mSnapshots.end())
        {
            throw od::Exception("Snapshot with given tick not available for sending");
        }

        size_t discreteChangeCount = 0;

        auto reference = (referenceSnapshot != INVALID_TICK) ? _getSnapshot(referenceSnapshot, mSnapshots, false) : mSnapshots.end();

        for(auto &stateChange : toSend->changes)
        {
            od::ObjectStates filteredChange = stateChange.second.baseStates;
            if(reference != mSnapshots.end())
            {
                auto referenceChange = reference->changes.find(stateChange.first);
                if(referenceChange != reference->changes.end())
                {
                    filteredChange.deltaEncode(referenceChange->second.baseStates, filteredChange);
                }
            }

            size_t changeCount = filteredChange.countStatesWithValue();
            if(changeCount > 0)
            {
                c.objectStatesChanged(tickToSend, stateChange.first, filteredChange);
            }

            discreteChangeCount += changeCount;
        }

        c.confirmSnapshot(tickToSend, toSend->realtime, discreteChangeCount, referenceSnapshot);
    }

    StateManager::SnapshotIterator StateManager::_getSnapshot(TickNumber tick, std::deque<Snapshot> &snapshots, bool createIfNotFound)
    {
        auto pred = [](Snapshot &snapshot, TickNumber tick) { return snapshot.tick < tick; };
        auto it = std::lower_bound(snapshots.begin(), snapshots.end(), tick, pred);

        if(it == snapshots.end() || it->tick != tick)
        {
            if(createIfNotFound)
            {
                return snapshots.emplace(it, tick);

            }else
            {
                return snapshots.end();
            }

        }else
        {
            return it;
        }
    }

    void StateManager::_commitIncomingIfComplete(TickNumber tick, SnapshotIterator incomingSnapshot)
    {
        if(!incomingSnapshot->confirmed)
        {
            return;
        }

        // even though we might count the total discrete changes more than once here, this
        //  should happen only rarely in case a confirmation packet arrives earlier than a change.
        //  however, doing it this way reduces the amount of dependent states in our program, which is a plus
        size_t discreteChangeCount = 0;
        for(auto &change : incomingSnapshot->changes)
        {
            discreteChangeCount += change.second.baseStates.countStatesWithValue();
        }

        if(incomingSnapshot->targetDiscreteChangeCount == discreteChangeCount)
        {
            // this snapshot is complete! move it to the timeline

            std::lock_guard<std::mutex> lock(mSnapshotMutex);

            if(mSnapshots.size() >= TICK_CAPACITY)
            {
                mSnapshots.pop_front();
            }

            // undo delta-encoding by merging incoming with the reference snapshot (only if this is not a full snapshot)
            if(incomingSnapshot->referenceSnapshot != INVALID_TICK)
            {
                auto referenceSnapshot = _getSnapshot(incomingSnapshot->referenceSnapshot, mSnapshots, false);
                if(referenceSnapshot == mSnapshots.end())
                {
                    throw od::Exception("Reference snapshot no longer contained in timeline");
                }

                for(auto &baseChange : referenceSnapshot->changes)
                {
                    auto &deltaChange = incomingSnapshot->changes[baseChange.first];
                    deltaChange.baseStates.merge(baseChange.second.baseStates, deltaChange.baseStates);
                }
            }

            auto snapshot = _getSnapshot(tick, mSnapshots, true);
            if(snapshot->confirmed) throw od::Exception("Re-committing snapshot");
            *snapshot = std::move(*incomingSnapshot);
            mIncomingSnapshots.erase(incomingSnapshot);

            if(mUplinkConnectorForAck != nullptr)
            {
                mUplinkConnectorForAck->acknowledgeSnapshot(tick);
            }
        }
    }

}
