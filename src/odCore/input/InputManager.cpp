/*
 * InputManager.cpp
 *
 *  Created on: 29 Dec 2018
 *      Author: zal
 */

#include <odCore/input/InputManager.h>

#include <odCore/gui/Gui.h>

#include <odCore/input/InputListener.h>
#include <odCore/input/RawActionListener.h>

namespace odInput
{

    InputManager::InputManager()
    : mMouseMoved(false)
    {
    }

    InputManager::~InputManager()
    {
    }

    void InputManager::mouseMoved(float absX, float absY)
    {
        std::lock_guard<std::mutex> lock(mInputEventQueueMutex);
        mMouseMoveTarget = {absX, absY};
        mMouseMoved = true;
    }

    void InputManager::mouseButtonDown(int buttonCode)
    {
        std::lock_guard<std::mutex> lock(mInputEventQueueMutex);
        mMouseButtonQueue.push_back(std::make_pair(buttonCode, false));
    }

    void InputManager::mouseButtonUp(int buttonCode)
    {
        std::lock_guard<std::mutex> lock(mInputEventQueueMutex);
        mMouseButtonQueue.push_back(std::make_pair(buttonCode, true));
    }

    void InputManager::keyDown(Key key)
    {
        std::lock_guard<std::mutex> lock(mInputEventQueueMutex);
        mKeyQueue.push_back(std::make_pair(key, false));
    }

    void InputManager::keyUp(Key key)
    {
        std::lock_guard<std::mutex> lock(mInputEventQueueMutex);
        mKeyQueue.push_back(std::make_pair(key, true));
    }

    void InputManager::bindActionToKey(std::shared_ptr<IAction> action, Key key)
    {
        Binding &binding = mBindings[key];
        for(auto &a : binding.actions)
        {
            if(a.expired())
            {
                a = action;
                return;
            }
        }

        throw od::Exception("Exceeded maximum number of actions per key");
    }

    void InputManager::unbindActionFromKey(std::shared_ptr<IAction> action, Key key)
    {
        auto it = mBindings.find(key);
        if(it == mBindings.end())
        {
            return;
        }

        for(auto &a : it->second.actions)
        {
            auto boundAction = a.lock();
            if(boundAction == action)
            {
                a.reset();
            }
        }
    }

    std::shared_ptr<InputListener> InputManager::createInputListener()
    {
        auto listener = std::make_shared<InputListener>();
        mInputListeners.emplace_back(listener);
        return listener;
    }

    std::shared_ptr<RawActionListener> InputManager::createRawActionListener()
    {
        auto listener = std::make_shared<RawActionListener>();
        mRawActionListeners.emplace_back(listener);
        return listener;
    }

    void InputManager::update(float relTime)
    {
        std::lock_guard<std::mutex> lock(mInputEventQueueMutex);

        if(mMouseMoved)
        {
            _processMouseMove(mMouseMoveTarget);
            mMouseMoved = false;
        }

        for(auto &key : mKeyQueue)
        {
            if(key.second)
            {
                _processKeyUp(key.first);

            }else
            {
                _processKeyDown(key.first);
            }
        }
        mKeyQueue.clear();

        for(auto &button : mMouseButtonQueue)
        {
            if(button.second)
            {
                _processMouseUp(button.first);

            }else
            {
                _processMouseDown(button.first);
            }
        }
        mMouseButtonQueue.clear();
    }

    void InputManager::_processMouseMove(glm::vec2 pos)
    {
        _forEachInputListener([=](auto listener){ listener.mouseMoveEvent(pos); });
    }

    void InputManager::_processMouseDown(int buttonCode)
    {
        _forEachInputListener([=](auto listener){ listener.mouseButtonEvent(buttonCode, false); });
    }

    void InputManager::_processMouseUp(int buttonCode)
    {
        _forEachInputListener([=](auto listener){ listener.mouseButtonEvent(buttonCode, true); });
    }

    void InputManager::_processKeyDown(Key key)
    {
        _forEachInputListener([=](auto listener){ listener.keyEvent(key, false); });

        auto it = mBindings.find(key);
        if(it == mBindings.end())
        {
            return; // no bindings for this key
        }

        ActionState state = it->second.down ? ActionState::REPEAT : ActionState::BEGIN;

        it->second.down = true;

        for(auto &boundAction : it->second.actions)
        {
            if(!boundAction.expired())
            {
                auto action = boundAction.lock();
                if(action != nullptr)
                {
                    _triggerCallbackOnAction(*action, state);
                }
            }
        }
    }

    void InputManager::_processKeyUp(Key key)
    {
        _forEachInputListener([=](auto listener){ listener.keyEvent(key, true); });

        auto it = mBindings.find(key);
        if(it == mBindings.end())
        {
            return; // no bindings for this key
        }

        it->second.down = false;

        for(auto &boundAction : it->second.actions)
        {
            if(!boundAction.expired())
            {
                auto action = boundAction.lock();
                if(action != nullptr)
                {
                    _triggerCallbackOnAction(*action, ActionState::END);
                }
            }
        }
    }

    void InputManager::_triggerCallbackOnAction(IAction &action, ActionState state)
    {
        for(auto &l : mRawActionListeners)
        {
            if(!l.expired())
            {
                auto listener = l.lock();
                if(listener != nullptr && listener->callback != nullptr)
                {
                    listener->callback(action.getActionCode(), state);
                }
            }
        }

        if(state == ActionState::REPEAT && !action.isRepeatable())
        {
            return;

        }else if(state == ActionState::END && action.ignoresUpEvents())
        {
            return;
        }

        action.triggerCallback(state);
    }

}
