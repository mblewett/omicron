/**************************************************************************************************
* THE OMICRON PROJECT
 *-------------------------------------------------------------------------------------------------
 * Copyright 2010-2014		Electronic Visualization Laboratory, University of Illinois at Chicago
 * Authors:										
 *  Arthur Nishimoto		anishimoto42@gmail.com
 *-------------------------------------------------------------------------------------------------
 * Copyright (c) 2010-2014, Electronic Visualization Laboratory, University of Illinois at Chicago
 * All rights reserved.
 * Redistribution and use in source and binary forms, with or without modification, are permitted 
 * provided that the following conditions are met:
 * 
 * Redistributions of source code must retain the above copyright notice, this list of conditions 
 * and the following disclaimer. Redistributions in binary form must reproduce the above copyright 
 * notice, this list of conditions and the following disclaimer in the documentation and/or other 
 * materials provided with the distribution. 
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR 
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO THE IMPLIED WARRANTIES OF MERCHANTABILITY AND 
 * FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR 
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL 
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE  GOODS OR SERVICES; LOSS OF 
 * USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, 
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN 
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *************************************************************************************************/
#include <omicron.h>
#include <vector>

#include <time.h>
using namespace omicron;

#include "omicron/SoundManager.h"

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class SoundTest
{
private:
	Vector3f audioListener;

	SoundManager* soundManager;
	SoundEnvironment* env;

	Sound* stereoTest;
	Sound* monoTest;

	Ref<SoundInstance> si_stereoTest;
	Ref<SoundInstance> si_monoTest;

	String soundServerIP;
	int soundServerPort;
	int soundServerCheckDelay;

	String stereoTestSoundPath;
	String monoTestSoundPath;

public:
	///////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	SoundTest(Config* cfg)
	{
		if(!cfg->isLoaded()) cfg->load();

		Setting& stRoot = cfg->getRootSetting()["config"];
		if( stRoot.exists("sound") )
			setup( stRoot["sound"] );
		else
		{
			soundServerIP = "127.0.0.1";
			soundServerPort = 57120;
		}

		//soundManager = new SoundManager();
		//soundManager->connectToServer(soundServerIP,soundServerPort);
		
		// More concise method of above two lines
		soundManager = new SoundManager(soundServerIP,soundServerPort);

		// Delay for loadSoundFromFile()
		// Necessary if creating a new SoundInstance immediatly after calling loadSoundFromFile()
		// Default is 500, although may need to be adjusted depending on sound server
		soundManager->setSoundLoadWaitTime(500);

		//soundManager->showDebugInfo(true);

		// Start the sound server (if not already started)
		soundManager->startSoundServer();
		
		// Get default sound environment
		env = soundManager->getSoundEnvironment();

		
		ofmsg("SoundTest: Checking if sound server is ready at %1% on port %2%... (Waiting for %3% seconds)", %soundServerIP %soundServerPort %(soundServerCheckDelay/1000));

		bool serverReady = true;
		timeb tb;
		ftime( &tb );
		int curTime = tb.millitm + (tb.time & 0xfffff) * 1000;
		int lastSoundServerCheck = curTime;

		while( !soundManager->isSoundServerRunning() )
		{
			timeb tb;
			ftime( &tb );
			curTime = tb.millitm + (tb.time & 0xfffff) * 1000;
			int timeSinceLastCheck = curTime-lastSoundServerCheck;

			if( timeSinceLastCheck > soundServerCheckDelay )
			{
				omsg("SoundTest: Failed to start sound server. Sound disabled.");
				serverReady = false;
				break;
			}
		}
		omsg("SoundTest: SoundServer reports ready.");
		
		// Load sound assets
		//env->setAssetDirectory("menu_sounds");

		stereoTest = env->loadSoundFromFile("mus",stereoTestSoundPath);

		si_stereoTest = new SoundInstance(stereoTest);
		si_stereoTest->setPitch(3);
		si_stereoTest->setLoop(true);

		si_stereoTest->playStereo();
		
	}

	///////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	void setup(Setting& syscfg)
	{
		soundServerIP = Config::getStringValue("soundServerIP", syscfg, "localhost");
        soundServerPort = Config::getIntValue("soundServerPort", syscfg, 57120);

        // Config in seconds, function below in milliseconds
        soundServerCheckDelay = Config::getFloatValue("soundServerReconnectDelay", syscfg, 5) * 1000;

		stereoTestSoundPath = Config::getStringValue("stereoTestSound", syscfg, "stereoTestSound.wav");
		monoTestSoundPath = Config::getStringValue("monoTestSound", syscfg, "monoTestSound.wav");
	}

	///////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// Checks the type of event. If a valid event, creates an event packet and returns true. Else return false.
	virtual bool handleEvent(const Event& evt)
	{
		
		float leftRightAnalog;
		float upDownAnalog;
		float zeroTolerence = 0.008f;
		float xPos;
		float yPos;
		float zPos;

		switch(evt.getServiceType())
		{
			case Service::Wand:
				leftRightAnalog = evt.getExtraDataFloat(0);
				upDownAnalog = evt.getExtraDataFloat(1);

				volume = evt.getExtraDataFloat(4);


				if( evt.getType() == Event::Down ){

					if( evt.getFlags() & Event::Button3){ // Cross
	
					}
					if( evt.getFlags() & Event::Button2){ // Circle
						si_stereoTest->stop();
					}
					
					if( evt.getFlags() & Event::Button5){ // L1
				
					}

					if( evt.getFlags() & Event::ButtonRight){

					}
					if( evt.getFlags() & Event::ButtonLeft){

					}
					if( evt.getFlags() & Event::ButtonUp){

					}
					if( evt.getFlags() & Event::ButtonDown){

					}
					//printf("%d \n", evt.getFlags() );
				}
				
				if( (leftRightAnalog > zeroTolerence || leftRightAnalog < -zeroTolerence) &&
					(upDownAnalog > zeroTolerence || upDownAnalog < -zeroTolerence)
					){
					position[0] = leftRightAnalog;
					position[1] = upDownAnalog;
					printf("Analog Stick: %f %f\n", position[0], position[1]);
				}
				if( volume > 0 )
					printf("Analog Trigger (L2): %f\n", volume);
				return true;
				break;

			case Service::Mocap:
				//if( evt.getSourceId() == 0 )
				//	soundManager->setListenerPosition( evt.getPosition() );
				//env->setListenerPosition( Vector3f(0,0,0) );
				//else if( instanceCreated && evt.getSourceId() == 1 )
				//	soundInstance->setPosition( evt.getPosition() );
				//printf("ID: %d Pos: %f %f %f\n", evt.getSourceId(), evt.getPosition(0), evt.getPosition(1), evt.getPosition(2) );
				break;
		}
		
		return false;
	}

	void update()
	{
		soundManager->poll();

		env->setListenerPosition( Vector3f(0,0,0) );
	}
private:
	Vector3f position;
	float volume;
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
int main(int argc, char** argv)
{
	// Read config file name from command line or use default one.
	const char* cfgName = "soundTest.cfg";
	if(argc == 2) cfgName = argv[1];

	// Load the config file
	Config* cfg = new Config(cfgName);

	DataManager* dm = DataManager::getInstance();
	// Add a default filesystem data source using current work dir.
	dm->addSource(new FilesystemDataSource("./"));
	dm->addSource(new FilesystemDataSource(OMICRON_DATA_PATH));

	ServiceManager* sm = new ServiceManager();
	sm->setupAndStart(cfg);

	// Configure app
	if( !cfg->exists("config/sound") )
	{
		printf("Config/Sound section missing from config file: Aborting.\n");
		return 0;
	}

	SoundTest app = SoundTest(cfg);

	float delay = -0.01f; // Seconds to delay sending events (<= 0 disables delay)
#ifdef _DEBUG
	bool printOutput = true;
#else
	bool printOutput = false;
#endif

	while(true)
	{
		sm->poll();

		// Get events
		int av = sm->getAvailableEvents();
		if(av != 0)
		{
			// TODO: Instead of copying the event list, we can lock the main one.
			Event evts[OMICRON_MAX_EVENTS];
			sm->getEvents(evts, OMICRON_MAX_EVENTS);
			for( int evtNum = 0; evtNum < av; evtNum++)
			{
				app.handleEvent(evts[evtNum]);
			}
			
			//if( printOutput )
			//	printf("------------------------------------------------------------------------------\n");
		}
		app.update();
	}

	sm->stop();
	delete sm;
	delete cfg;
	delete dm;
	
	return 0;
}
