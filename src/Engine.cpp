/*
 * Engine.cpp
 *
 *  Created on: 14 Feb 2018
 *      Author: zal
 */


#include "Engine.h"

#include <osgViewer/ViewerEventHandlers>
#include <osgGA/TrackballManipulator>

#include "Exception.h"
#include "Logger.h"
#include "Level.h"
#include "LevelObject.h"
#include "Player.h"
#include "rfl/Rfl.h"

namespace od
{

	Engine::Engine()
	: mShaderManager(*this, FilePath("shader_src"))
	, mInitialLevelFile("Mountain World/Intro Level/Intro.lvl") // is this defined anywhere?
	, mMaxFrameRate(60)
	{
	}

	void Engine::run()
	{
		Logger::info() << "Starting OpenDrakan...";

		Rfl &rfl = od::Rfl::getSingleton();
		Logger::info() << "OpenDrakan linked against RFL " << rfl.getName() << " with " << rfl.getClassTypeCount() << " registered classes";

		osg::ref_ptr<osg::Group> rootNode(new osg::Group);

		mViewer = new osgViewer::Viewer;
		mViewer->realize();
		mViewer->setName("OpenDrakan");
		mViewer->getCamera()->setClearColor(osg::Vec4(0.2,0.2,0.2,1));
		mViewer->setSceneData(rootNode);

		// since we want to use the programmable pipeline, set things up so we can use modern GLSL
		mViewer->getCamera()->getGraphicsContext()->getState()->setUseModelViewAndProjectionUniforms(true);
		mViewer->getCamera()->getGraphicsContext()->getState()->setUseVertexAttributeAliasing(true);

		// attach our default shaders to root node so we don't use the fixed function pipeline anymore
		osg::ref_ptr<osg::Program> defaultProgram = mShaderManager.makeProgram(nullptr, nullptr); // nullptr will cause SM to load default shader
		rootNode->getOrCreateStateSet()->setAttribute(defaultProgram);

		mCamera.reset(new Camera(*this, mViewer->getCamera()));

		osg::ref_ptr<osgViewer::StatsHandler> statsHandler(new osgViewer::StatsHandler);
		statsHandler->setKeyEventPrintsOutStats(osgGA::GUIEventAdapter::KEY_F2);
		statsHandler->setKeyEventTogglesOnScreenStats(osgGA::GUIEventAdapter::KEY_F1);
		mViewer->addEventHandler(statsHandler);

		mLevel.reset(new od::Level(mInitialLevelFile, *this, rootNode));

		if(mLevel->getPlayer().getLevelObject() == nullptr)
    	{
			Logger::error() << "Can't start engine. Level does not contain a Human Control object";
    		throw Exception("No HumanControl object present in level");
    	}

		mInputManager = new InputManager(*this, mLevel->getPlayer(), mViewer);

		// need to provide our own loop as mViewer->run() installs camera manipulator we don't need
		double simTime = 0;
		double frameTime = 0;
		while(!mViewer->done())
		{
			double minFrameTime = (mMaxFrameRate > 0.0) ? (1.0/mMaxFrameRate) : 0.0;
			osg::Timer_t startFrameTick = osg::Timer::instance()->tick();

			mViewer->advance(simTime);
			mViewer->eventTraversal();

			mLevel->getPlayer().update(frameTime);

			mViewer->updateTraversal();
			mViewer->renderingTraversals();

			osg::Timer_t endFrameTick = osg::Timer::instance()->tick();
			frameTime = osg::Timer::instance()->delta_s(startFrameTick, endFrameTick);
			simTime += frameTime;
			if(frameTime < minFrameTime)
			{
				OpenThreads::Thread::microSleep(static_cast<unsigned int>(1000000.0*(minFrameTime-frameTime)));
			}
		}

		Logger::info() << "Shutting down gracefully";
	}

}
