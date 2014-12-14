 /********************************************************************************************************************** 
 * THE OMICRON PROJECT
 *---------------------------------------------------------------------------------------------------------------------
 * Copyright 2010-2014								Electronic Visualization Laboratory, University of Illinois at Chicago
 * Authors:										
 *  Arthur Nishimoto								anishimoto42@gmail.com
 *---------------------------------------------------------------------------------------------------------------------
 * Copyright (c) 2010-2014, Electronic Visualization Laboratory, University of Illinois at Chicago
 * All rights reserved.
 * Redistribution and use in source and binary forms, with or without modification, are permitted provided that the 
 * following conditions are met:
 * 
 * Redistributions of source code must retain the above copyright notice, this list of conditions and the following 
 * disclaimer. Redistributions in binary form must reproduce the above copyright notice, this list of conditions 
 * and the following disclaimer in the documentation and/or other materials provided with the distribution. 
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, 
 * INCLUDING, BUT NOT LIMITED TO THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE 
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, 
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE  GOODS OR 
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, 
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE 
 * USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *---------------------------------------------------------------------------------------------------------------------
 * Provides a sound API to interface with a SuperCollider sound server.
*********************************************************************************************************************/
#include "omicron/SoundManager.h"
#include "omicron/AssetCacheManager.h"
#include "omicron/DataManager.h"
#include <sys/timeb.h>

using namespace omicron;
using namespace oscpkt;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
UdpSocket SoundManager::soundServerSocket;
UdpSocket SoundManager::soundMsgSocket;
bool SoundManager::showDebug = false;
bool SoundManager::startingSoundServer = false;
bool SoundManager::soundServerRunning = false;

int SoundManager::nUnitGenerators = -1;
int SoundManager::nSynths = -1;
int SoundManager::nGroups = -1;
int SoundManager::nLoadedSynths = -1;
float SoundManager::avgCPU = -1;
float SoundManager::peakCPU = -1;
double SoundManager::nominalSampleRate = -1;
double SoundManager::actualSampleRate = -1;
int SoundManager::soundServerVolume = -16;
int SoundManager::soundLoadWaitTime = 200;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
SoundManager::SoundManager():
	myAssetCacheEnabled(false)
{
	environment = new SoundEnvironment(this);
	myAssetCacheManager = new AssetCacheManager();

	listenerPosition = Vector3f::Zero();
	listenerOrientation = Quaternion::Identity();

	userPosition = Vector3f::Zero();
	userOrientation = Quaternion::Identity();

	assetDirectory = "";
	assetDirectorySet = false;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
SoundManager::~SoundManager()
{
	//stopSoundServer(); // Do not call this under penalty of catapult
	stopAllSounds();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
SoundManager::SoundManager(const String& serverIP, int serverPort):
	myAssetCacheEnabled(false)
{
	environment = new SoundEnvironment(this);
	myAssetCacheManager = new AssetCacheManager();
	connectToServer(serverIP, serverPort);

	listenerPosition = Vector3f::Zero();
	listenerOrientation = Quaternion::Identity();

	userPosition = Vector3f::Zero();
	userOrientation = Quaternion::Identity();

	assetDirectory = "";
	assetDirectorySet = false;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void SoundManager::setup(Setting& settings)
{
	Setting& dispCfg = settings["display"];
	if(dispCfg.exists("radius"))
	{
		radius = dispCfg["radius"];
	}
	else
	{
		radius = 10000; 
	}
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void SoundManager::connectToServer(const String& serverIP, int serverPort)
{
	soundMsgSocket.connectTo(serverIP, 57110);
	soundMsgSocket.bindTo(8001); // Port to receive notify messages

	Message msg5("/notify");
	msg5.pushInt32(1);

	PacketWriter pw;
	pw.startBundle().addMessage(msg5).endBundle();
	soundMsgSocket.sendPacket(pw.packetData(), pw.packetSize());

	// Create socket and connect to OSC server
	soundServerSocket.connectTo(serverIP, serverPort);
	if (!soundServerSocket.isOk()) {
		ofmsg( "SoundManager: Error connection to port %1%: %2%", %serverPort %soundServerSocket.errorMessage() );
	} else {
		ofmsg( "SoundManager: Connected to server, will send messages to %1% on port %2%", %serverIP %serverPort );
	}
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void SoundManager::startSoundServer()
{
	startingSoundServer = true;
	Message msg("/startServer");
	sendOSCMessage(msg);

	wait(200); // Give the server a second to startup

	Message msg2("/loadSynth");
	sendOSCMessage(msg2);

	wait(1000); // Give the server a second to startup

	Message msg3("/loadStereoSynth");
	sendOSCMessage(msg3);

	wait(1000); // Give the server a second to startup

	Message msg4("/startup");
	sendOSCMessage(msg4);

	startingSoundServer = false;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void SoundManager::stopSoundServer()
{
	Message msg("/killServer");
	sendOSCMessage(msg);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
bool SoundManager::isSoundServerRunning()
{
	// Query sound server
	Message msg5("/status");

	PacketWriter pw;
	pw.startBundle().addMessage(msg5).endBundle();
	soundMsgSocket.sendPacket(pw.packetData(), pw.packetSize());

	// Check for response to query
	if( soundMsgSocket.receiveNextPacket(1) ){// Paramater is timeout in milliseconds. -1 (default) will wait indefinatly 
		PacketReader pr(soundMsgSocket.packetData(), soundMsgSocket.packetSize());
        Message *incoming_msg;
		
		int MSG_STATUS = 0;
		int MSG_NODE_END = 1;
		int msgType = -1;

		int nodeID = -1;

        while (pr.isOk() && (incoming_msg = pr.popMessage()) != 0) {		

			//if( isDebugEnabled() )
			//	ofmsg( "SoundManager::isSoundServerRunning(): /status.reply received %1%", %incoming_msg->addressPattern() );

			if( incoming_msg->addressPattern() == "/status.reply" )
				msgType = MSG_STATUS;
			else if( incoming_msg->addressPattern() == "/n_end" )
				msgType = MSG_NODE_END;

			Message::ArgReader arg(incoming_msg->arg());
			while (arg.nbArgRemaining()) {
				if (arg.isBlob()) {
					std::vector<char> b; arg.popBlob(b); 
				} else if (arg.isBool()) {
					bool b; arg.popBool(b);
					//ofmsg( "  received %1%", %b );
				} else if (arg.isInt32()) {
					int i; arg.popInt32(i);
					//ofmsg( "  received %1%", %i );

					if( msgType == MSG_STATUS )
					{
						if( nUnitGenerators == -1 )
							nUnitGenerators = i;
						else if( nSynths == -1 )
							nSynths = i;
						else if( nGroups == -1 )
							nGroups = i;
						else if( nLoadedSynths == -1 )
							nLoadedSynths = i;
					}
					else if( msgType == MSG_NODE_END )
					{
						if( nodeID == -1 )
							nodeID = i;
					}
				} else if (arg.isInt64()) {
					int64_t h; arg.popInt64(h);
					//ofmsg( "  received %1%", %h );
				} else if (arg.isFloat()) {
					float f; arg.popFloat(f);
					//ofmsg( "  received %1%", %f );

					if( msgType == MSG_STATUS )
					{
						if( avgCPU == -1 )
							avgCPU = f;
						else if( peakCPU == -1 )
							peakCPU = f;
					}
				} else if (arg.isDouble()) {
					double d; arg.popDouble(d);
					//ofmsg( "  received %1%", %d );

					if( msgType = MSG_STATUS )
					{
						if( nominalSampleRate == -1 )
							nominalSampleRate = d;
						else if( actualSampleRate == -1 )
							actualSampleRate = d;
					}
				} else if (arg.isStr()) {
					std::string s; arg.popStr(s); 
					//ofmsg( "  received %1%", %s );
			  }
			}

			if( msgType == MSG_STATUS && nLoadedSynths > 0 )
			{
				/*
				if( isDebugEnabled() )
				{
					omsg( "  Sound Server Status: Running");
					ofmsg( "    Unit Generators: %1%", %nUnitGenerators );
					ofmsg( "    Synths: %1%", %nSynths );
					ofmsg( "    Groups: %1%", %nGroups );
					ofmsg( "    Loaded Synths: %1%", %nLoadedSynths );
					ofmsg( "    Avg. Signal Processing CPU: %1%", %avgCPU );
					ofmsg( "    Peak Signal Processing CPU: %1%", %peakCPU );
					ofmsg( "    Nominal Sample Rate: %1%", %nominalSampleRate );
					ofmsg( "    Actual Sample Rate: %1%", %actualSampleRate );
				}
				*/

				if( !soundServerRunning )
				{
					Message msg5("/notify");
					msg5.pushInt32(1);

					PacketWriter pw;
					pw.startBundle().addMessage(msg5).endBundle();
					soundMsgSocket.sendPacket(pw.packetData(), pw.packetSize());
					soundServerRunning = true;
				}
			}
			else if(  msgType == MSG_NODE_END )
			{
				if( isDebugEnabled() )
					ofmsg("SoundManager::isSoundServerRunning(): received /n_end Removed node %1%", %nodeID );
				removeInstanceNode(nodeID);
			}

        }
	}
	return soundServerRunning;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
SoundEnvironment* SoundManager::getSoundEnvironment()
{
	return environment;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void SoundManager::setEnvironment(SoundEnvironment* newEnv)
{
	environment = newEnv;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
bool SoundManager::sendOSCMessage(Message msg)
{
	if(soundServerRunning || startingSoundServer)
	{
		PacketWriter pw;
		pw.startBundle().addMessage(msg).endBundle();
		return soundServerSocket.sendPacket(pw.packetData(), pw.packetSize());
	}
	return false;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void SoundManager::showDebugInfo(bool value)
{
	showDebug = value;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
bool SoundManager::isDebugEnabled()
{
	return showDebug;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void SoundManager::wait(float millis)
{
	timeb tb;
	ftime( &tb );
	int curTime = tb.millitm + (tb.time & 0xfffff) * 1000;
	int startTime = curTime;

	while( true )
	{
		timeb tb;
		ftime( &tb );
		curTime = tb.millitm + (tb.time & 0xfffff) * 1000;
		int timeSinceLastCheck = curTime-startTime;

		//if( isDebugEnabled() )
		//	ofmsg("SoundManager: Waiting: %1%", %timeSinceLastCheck);
		if( timeSinceLastCheck > millis )
		{
			break;
		}
	}
		
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void SoundManager::poll()
{
	if( soundMsgSocket.receiveNextPacket(1) ){// Paramater is timeout in milliseconds. -1 (default) will wait indefinatly 
		PacketReader pr(soundMsgSocket.packetData(), soundMsgSocket.packetSize());
        Message *incoming_msg;
		
		bool nodeEndEvent = false;
		int nodeID = -1;

        while (pr.isOk() && (incoming_msg = pr.popMessage()) != 0) {
			//if( isDebugEnabled() )
			//	ofmsg( "SoundManager::poll() received %1%", %incoming_msg->addressPattern() );
			if( incoming_msg->addressPattern() == "/n_end" )
				nodeEndEvent = true;

			Message::ArgReader arg(incoming_msg->arg());
			while (arg.nbArgRemaining()) {
				if (arg.isBlob()) {
					std::vector<char> b; arg.popBlob(b); 
				} else if (arg.isBool()) {
					bool b; arg.popBool(b);
					//if( isDebugEnabled() )
					//	ofmsg( "  received %1%", %b );
				} else if (arg.isInt32()) {
					int i; arg.popInt32(i);
					//if( isDebugEnabled() ) 
					//	ofmsg( "  received %1%", %i );

					if( nodeID == -1 )
						nodeID = i;
				} else if (arg.isInt64()) {
					int64_t h; arg.popInt64(h);
					//if( isDebugEnabled() ) 
					//	ofmsg( "  received %1%", %h );
				} else if (arg.isFloat()) {
					float f; arg.popFloat(f);
					//if( isDebugEnabled() ) 
					//	ofmsg( "  received %1%", %f );
				} else if (arg.isDouble()) {
					double d; arg.popDouble(d);
					//if( isDebugEnabled() ) 
					//	ofmsg( "  received %1%", %d );
				} else if (arg.isStr()) {
					std::string s; arg.popStr(s); 
					//if( isDebugEnabled() ) 
					//	ofmsg( "  received %1%", %s );
			  }
			}
        }
		if( nodeEndEvent )
		{
			if( isDebugEnabled() )
					ofmsg("SoundManager::poll(): received /n_end Removed node %1%", %nodeID );
			removeInstanceNode(nodeID);
		}
	}
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void SoundManager::setForceCacheOverwrite(bool value)
{
	if( myAssetCacheManager )
		myAssetCacheManager->setForceOverwrite(value);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
bool SoundManager::isForceCacheOverwriteEnabled()
{
	if( myAssetCacheManager )
		return myAssetCacheManager->isForceOverwriteEnabled();
	else
		return false;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void SoundManager::setAssetDirectory(const String& directory)
{
	assetDirectory = directory;
	assetDirectorySet = true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
String& SoundManager::getAssetDirectory()
{
	return assetDirectory;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
bool SoundManager::isAssetDirectorySet()
{
	return assetDirectorySet;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void SoundManager::addInstance( Ref<SoundInstance> newInstance )
{
	soundInstanceList[newInstance->getID()] = newInstance;
	if( isDebugEnabled() )
		ofmsg("Added instance %1% from buffer %2%", %newInstance->getID() %newInstance->getBufferID() );
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void SoundManager::addBuffer( Ref<Sound> newSound )
{
	soundList[newSound->getBufferID()] = newSound;
	soundNameList[newSound->getName()] = newSound->getBufferID();

	if( isDebugEnabled() )
		ofmsg("Added buffer %1%", %newSound->getBufferID() );
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
Vector3f SoundManager::getListenerPosition()
{
	return listenerPosition;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
Quaternion SoundManager::getListenerOrientation()
{
	return listenerOrientation;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void SoundManager::setListenerPosition(Vector3f newPos)
{
	listenerPosition = newPos;
	updateInstancePositions();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void SoundManager::setListenerOrientation(Quaternion newOrientation)
{
	listenerOrientation = newOrientation;
	updateInstancePositions();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void SoundManager::setListener(Vector3f newPos, Quaternion newOrientation)
{
	listenerPosition = newPos;
	listenerOrientation = newOrientation;
	updateInstancePositions();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
Vector3f SoundManager::getUserPosition()
{
	return userPosition;
	//return Vector3f(6,6,6);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
Quaternion SoundManager::getUserOrientation()
{
	return userOrientation;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void SoundManager::setUserPosition(Vector3f newPos)
{

	userPosition = newPos;
	updateInstancePositions();

}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void SoundManager::setUserOrientation(Quaternion newOrientation)
{
	userOrientation = newOrientation;
	updateInstancePositions();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
Vector3f SoundManager::worldToLocalPosition( Vector3f position )
{
	Vector3f res = listenerOrientation.inverse() * (position - listenerPosition);
	return res;
};

///////////////////////////////////////////////////////////////////////////////////////////////////
Vector3f SoundManager::localToWorldPosition( Vector3f position )
{
	Vector3f res = listenerPosition + listenerOrientation * position;
    return res;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
Sound* SoundManager::getSound(const String& soundName)
{
	Sound* newSound = soundList[soundNameList[soundName]];
	if( newSound == NULL )
		ofmsg("SoundEnvironment:getSound() - '%1%' does not exist", %soundName);
	return newSound;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void SoundManager::setSound(const String& soundName, Ref<Sound> newSound)
{
	if( soundNameList.count(soundName) > 0 )
	{
		Sound* oldSound = getSound(soundName);

		ofmsg("SoundEnvironment:setSound() - Replacing bufferID %2% with bufferID %3% for sound '%1%' ", %soundName %oldSound->getBufferID() %newSound->getBufferID() );

		soundList[newSound->getBufferID()] = newSound;
		soundNameList[soundName] = newSound->getBufferID();
	}
	else
	{
		ofmsg("SoundEnvironment:setSound() - '%1%' does not exist", %soundName);
	}

}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
Vector<int> SoundManager::getInstanceIDList()
{
	return instanceNodeIDList;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void SoundManager::stopAllSounds()
{
	if( isDebugEnabled() )
		ofmsg("SoundManager: Freeing %1% nodes...", %soundInstanceList.size());

	map< int, Ref<SoundInstance> >::iterator it = soundInstanceList.begin();
	for ( it = soundInstanceList.begin(); it != soundInstanceList.end(); ++it )
	{
		SoundInstance* inst = it->second;
		int nodeID = inst->getID();
		Message msg("/freeNode");
		msg.pushInt32(nodeID);
		sendOSCMessage(msg);

		if( isDebugEnabled() )
			ofmsg("SoundManager: Node %1% freed", %nodeID);
	}
	soundInstanceList.clear();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void SoundManager::cleanupAllSounds()
{
	if( isDebugEnabled() )
		ofmsg("SoundManager: Freeing %1% buffers...", %soundList.size());

	map< int, Ref<Sound> >::iterator it = soundList.begin();
	for ( it = soundList.begin(); it != soundList.end(); ++it )
	{
		Sound* snd = it->second;
		int bufferID = snd->getBufferID();
		Message msg("/freeBuf");
		msg.pushInt32(bufferID);
		sendOSCMessage(msg);

		if( isDebugEnabled() )
			ofmsg("SoundManager: Buffer %1% freed", %bufferID);
	}
	soundList.clear();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void SoundManager::setServerVolume(int value)
{
	if( value > 8 )
		soundServerVolume = 8;
	else if( value < -30 )
		soundServerVolume = -30;
	else
		soundServerVolume = value;

	Message msg("/serverVol");
	msg.pushInt32(soundServerVolume);
	sendOSCMessage(msg);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
int SoundManager::getServerVolume()
{
	// TODO: Should query sound server to get current volume and update variable
	return soundServerVolume;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void SoundManager::updateInstancePositions()
{
	map< int, Ref<SoundInstance> >::iterator it = soundInstanceList.begin();
	for ( it = soundInstanceList.begin(); it != soundInstanceList.end(); ++it )
	{
		SoundInstance* inst = it->second;

		if( inst->isPlaying() )
		{
			Message msg("/setObjectLoc");
			msg.pushInt32(inst->getID());
		
			Vector3f soundLocalPosition = worldToLocalPosition( inst->getPosition() );

			msg.pushFloat( soundLocalPosition[0] );
			msg.pushFloat( soundLocalPosition[1] );
			msg.pushFloat( soundLocalPosition[2] );
			sendOSCMessage(msg);
			//ofmsg( "objectx %1% and objectz %2%", %soundLocalPosition[0] %soundLocalPosition[2] );

			// Calculate and send user location (pos and magnitude)
			Vector3f userPosition = environment->getUserPosition();
			float userPos = atan2((userPosition[2]), (userPosition[0]))/3.14159f;//pi
			userPos = userPos - 0.5;
			float userMag = Math::sqrt( Math::sqr(userPosition[0]) + Math::sqr(userPosition[2]));
			 
			Message msg3("/setUserLoc");
			msg3.pushInt32(inst->getID());
			msg3.pushFloat( userPos );
			msg3.pushFloat( userMag );
			sendOSCMessage(msg3);
			//ofmsg( "userPos %1% and userMag %2%", %userPos %userMag );

			// Calculate speaker angle relative to user
			updateAudioImage(soundLocalPosition, userPosition, inst->getID());

			// Calculate and send the volume rolloff
			float objToUser3D = Math::sqrt( Math::sqr(userPosition[0] - soundLocalPosition[0]) + Math::sqr(userPosition[1] - soundLocalPosition[1]) + Math::sqr(userPosition[2] - soundLocalPosition[2]) );
			float newVol = inst->getVolume();

			int rolloffType = 0;
			if( inst->isRolloffLinear() )
			{
				rolloffType = 1;
				newVol = ( 1 - ((objToUser3D - inst->getMinRolloffDistance() ) / inst->getMaxDistance())) * inst->getVolume(); //Linear rolloff
			}
			else if( inst->isRolloffLogarithmic() )
			{
				rolloffType = 2;
				if( objToUser3D > 0 )
					newVol = inst->getMinRolloffDistance() * (inst->getMaxDistance() / Math::sqr(objToUser3D) );  // Logarithmic
			}
			if( newVol > inst->getVolume() )
				newVol = inst->getVolume();
			else if( newVol < 0 )
				newVol = 0;

			if( newVol * inst->getVolumeScale() > 1 )
				newVol = 1.0;
			else
				newVol =  newVol * inst->getVolumeScale();

			Message msg2("/setVol");
			msg2.pushInt32(inst->getID());
			msg2.pushFloat(newVol);
			
			// Calculate Individual Sound Width (ISW) relative to user
			updateObjectWidth(inst->getWidth(), objToUser3D, inst->getID());
			environment->getSoundManager()->sendOSCMessage(msg2);

			//if( isDebugEnabled() )
			//ofmsg("%1%: instanceID %2% rolloff type %3% newVol %4%", %__FUNCTION__ %inst->getID() %rolloffType %newVol );
		}
	}
};
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void SoundManager::updateAudioImage(Vector3f soundLocalPosition, Vector3f userPosition, int instID)
{
	int flow;
	float m, intercept;
	float coeff1, coeff2, coeff3;
	float wallx1, wallx2, wallz1, wallz2; 
	float wallToObject, wallToUser;
	float wallx, wallz;
	float objToCenter = sqrt(soundLocalPosition[0]*soundLocalPosition[0] + soundLocalPosition[2]*soundLocalPosition[2]);
	float objToWall1, objToWall2;

	if (abs(userPosition[0] - soundLocalPosition[0]) < 0.1f){
		flow = 1;
		//ofmsg( "infinite slope case flow = %1%", %flow );

		wallx = soundLocalPosition[0];
		if (userPosition[2] > soundLocalPosition[2])
		{
			wallz = (-1.0f)* abs(sqrt(radius*radius-soundLocalPosition[0]*soundLocalPosition[0]));
		}
		else
		{
			wallz = abs(sqrt(radius*radius-soundLocalPosition[0]*soundLocalPosition[0]));
		}
	}
	else 
	{

		m = (userPosition[2]-soundLocalPosition[2])/(userPosition[0] - soundLocalPosition[0]);
		intercept = soundLocalPosition[2] - m* soundLocalPosition[0];
		//forumulas that obatin varables for quadratic
		coeff1 = 1 + m*m;
		coeff2 = 2 * m * intercept;
		coeff3 = intercept*intercept - radius*radius;
		//getting the two solutions...
		wallx1 = ((-1.0f*coeff2) + Math::sqrt(coeff2*coeff2-4*coeff1*coeff3))/(2*coeff1);
		wallx2 = ((-1.0f*coeff2) - Math::sqrt(coeff2*coeff2-4*coeff1*coeff3))/(2*coeff1);
		wallz1 = (m * wallx1) + intercept;
		wallz2 = (m * wallx2) + intercept;

		if(objToCenter < radius){
			flow = 2;
			//ofmsg( "finite inside case flow = %1%", %flow );

			wallToObject = Math::sqrt( ((wallx1-soundLocalPosition[0])*(wallx1-soundLocalPosition[0]))+((wallz1 - soundLocalPosition[2])*(wallz1 - soundLocalPosition[2])));
			wallToUser = Math::sqrt( ((wallx1-userPosition[0])*(wallx1-userPosition[0]))+((wallz1 - userPosition[2])*(wallz1 - userPosition[2])));
			if (wallToUser > wallToObject){
				wallx = wallx1;
				wallz = wallz1;
			}
			else{
				wallx = wallx2;
				wallz = wallz2;
			}					
		}
		// if object is farther away than radius
		else{
			flow = 3;
			//ofmsg( "finite outside case flow = %1%", %flow );

			objToWall1 = Math::sqrt(((soundLocalPosition[0]-wallx1)*(soundLocalPosition[0]-wallx1)) + ((soundLocalPosition[2]-wallz1)*(soundLocalPosition[2]-wallz1)));
			objToWall2 = Math::sqrt(((soundLocalPosition[0]-wallx2)*(soundLocalPosition[0]-wallx2)) + ((soundLocalPosition[2]-wallz2)*(soundLocalPosition[2]-wallz2)));
			if (objToWall1 < objToWall2){
				wallx = wallx1;
				wallz = wallz1;	
				}
			else{
				wallx = wallx2;
				wallz = wallz2;
			}
		}
	}
	//ofmsg( "wallx %1% and wallz %2%", %wallx %wallz );

	// Calculate and send the speaker angle to sound server
	float pos = atan2((wallz), (wallx))/3.14159f;//pi
	pos = pos - 0.5;

	Message msg("/setPos");
	msg.pushInt32( instID );
	msg.pushFloat( pos );
	sendOSCMessage(msg);
	//ofmsg("%1%: pos", %pos );
};
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void SoundManager::updateObjectWidth(float width, float objToUser3D, int instID)
{
	if (objToUser3D > 0.25)
	{
		width = width/objToUser3D;
	}

	if (width < 1)
	{
		width = 1;
	}

	if (width > 20)
	{
		width = 20;
	}

	Message msg("/setWidth");
	msg.pushInt32( instID );
	msg.pushFloat( width );
	sendOSCMessage(msg);
	//ofmsg("%1%: width", %width );
};
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void SoundManager::removeInstanceNode(int id)
{
	SoundInstance* si = soundInstanceList[id];
	if( si != NULL )
	{
		si->serverStopSignalReceived();
	}
	soundInstanceList.erase(id);
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
SoundEnvironment::SoundEnvironment(SoundManager* soundManager)
{
	this->soundManager = soundManager;

	environmentVolumeScale = 0.5;
	environmentRoomSize = 0.0;
	environmentWetness = 0.0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
SoundEnvironment::~SoundEnvironment()
{

}


///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void SoundEnvironment::showDebugInfo(bool value)
{
	soundManager->showDebugInfo(value);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
SoundManager* SoundEnvironment::getSoundManager()
{
	return soundManager;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
Sound* SoundEnvironment::createSound(const String& soundName)
{
	Sound* newSound = new Sound(soundName);
	newSound->setSoundEnvironment(this);

	soundManager->addBuffer( newSound );

	return newSound;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
Sound* SoundEnvironment::getSound(const String& soundName)
{
	return soundManager->getSound(soundName);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void SoundEnvironment::setSound(const String& soundName, Ref<Sound> newSound)
{
	soundManager->setSound( soundName, newSound );

}
/*
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
Sound* SoundEnvironment::loadSoundFromFile(const String& fileName)
{
	String soundFullPath = soundManager->getAssetDirectory() + "/" + fileName;
	if( !soundManager->isAssetDirectorySet() )
		soundFullPath = fileName;

	Sound* sound = createSound(soundFullPath);
	if(sound != NULL)
	{
		sound->loadFromFile(soundFullPath);
	}
	return sound;
}
*/
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
Sound* SoundEnvironment::loadSoundFromFile(const String& soundName, const String& filePath)
{
	String soundFullPath = soundManager->getAssetDirectory() + "/" + filePath;
	if( !soundManager->isAssetDirectorySet() )
		soundFullPath = filePath;

	// If the asset cache is enabled, copy local sound assets to the sound server
	 if(soundManager->isAssetCacheEnabled())
	 {
		AssetCacheManager* acm = soundManager->getAssetCacheManager();
		acm->setCacheName(soundManager->getAssetDirectory());
		acm->clearCacheFileList();
		acm->addFileToCacheList(filePath);
		acm->sync();
	}
     // If the asset cache is disabled we are playing sounds locally: convert the
     // relative sound path to a full sound path.
     else
     {
         if(!DataManager::findFile(filePath, soundFullPath))
         {
             ofwarn("SoundEnvironment:loadSoundFromFile could not find sound file %1%", %filePath);
         }
     }

	Sound* sound = createSound(soundName);
	if(sound != NULL)
	{
		sound->loadFromFile(soundFullPath);

		// Let the server load the sound before continuing (make sure buffer/sound is ready before a node/soundinstance is created)
		soundManager->wait(soundManager->getSoundLoadWaitTime()); 
	}
	return sound;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
SoundInstance* SoundEnvironment::createInstance(Sound* sound)
{
	SoundInstance* newInstance = new SoundInstance(sound);
	return newInstance;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
Vector3f SoundEnvironment::getListenerPosition()
{
	return soundManager->getListenerPosition();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
Quaternion SoundEnvironment::getListenerOrientation()
{
	return soundManager->getListenerOrientation();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void SoundEnvironment::setListenerPosition(Vector3f newPos)
{
	soundManager->setListenerPosition(newPos);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void SoundEnvironment::setListenerOrientation(Quaternion newOrientation)
{
	soundManager->setListenerOrientation(newOrientation);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void SoundEnvironment::setListener(Vector3f newPos, Quaternion newOrientation)
{
	soundManager->setListenerPosition(newPos);
	soundManager->setListenerOrientation(newOrientation);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
Vector3f SoundEnvironment::getUserPosition()
{
	return soundManager->getUserPosition(); // Disabled until local user position is correctly accounted for sound server side
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
Quaternion SoundEnvironment::getUserOrientation()
{
	return soundManager->getUserOrientation();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void SoundEnvironment::setUserPosition(Vector3f newPos)
{
	//newPos = Vector3f(5,5,5);
	soundManager->setUserPosition(newPos);
	/*
	Message msg("/setUserLoc");
					
			msg.pushFloat( newPos[0] );
			msg.pushFloat( newPos[1] );
			msg.pushFloat( newPos[2] );
			soundManager->sendOSCMessage(msg);
	*/
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void SoundEnvironment::setUserOrientation(Quaternion newOrientation)
{
	soundManager->setUserOrientation(newOrientation);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void SoundEnvironment::setAssetDirectory(const String& directory)
{
	soundManager->setAssetDirectory(directory);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
String& SoundEnvironment::getAssetDirectory()
{
	return soundManager->getAssetDirectory();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void SoundEnvironment::setServerVolume(int value)
{
	soundManager->setServerVolume(value);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
int SoundEnvironment::getServerVolume()
{
	return soundManager->getServerVolume();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void SoundEnvironment::setForceCacheOverwrite(bool value)
{
	soundManager->setForceCacheOverwrite(value);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
bool SoundEnvironment::isForceCacheOverwriteEnabled()
{
	return soundManager->isForceCacheOverwriteEnabled();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void SoundEnvironment::setVolumeScale(float value)
{
	environmentVolumeScale = value;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void SoundEnvironment::setSoundLoadWaitTime(int time)
{ 
	soundManager->setSoundLoadWaitTime(time);
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
int SoundEnvironment::getSoundLoadWaitTime()
{
	return soundManager->getSoundLoadWaitTime();
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
float SoundEnvironment::getVolumeScale()
{
	return environmentVolumeScale;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void SoundEnvironment::setRoomSize(float value)
{
	environmentRoomSize = value;

	if( soundManager->isSoundServerRunning() )
	{
		Vector<int> instanceNodeIDList = soundManager->getInstanceIDList();

		for( int i = 0; i < instanceNodeIDList.size(); i++ )
		{
			Message msg("/setReverb");
			msg.pushInt32(instanceNodeIDList[i]);
			msg.pushFloat(environmentWetness);
			msg.pushFloat(environmentRoomSize);
			soundManager->sendOSCMessage(msg);
		}
	}

}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
float SoundEnvironment::getRoomSize()
{
	return environmentRoomSize;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void SoundEnvironment::setWetness(float value)
{
	environmentWetness = value;

	if( soundManager->isSoundServerRunning() )
	{
		Vector<int> instanceNodeIDList = soundManager->getInstanceIDList();
		for( int i = 0; i < instanceNodeIDList.size(); i++ )
		{
			Message msg("/setReverb");
			msg.pushInt32(instanceNodeIDList[i]);
			msg.pushFloat(environmentWetness);
			msg.pushFloat(environmentRoomSize);
			soundManager->sendOSCMessage(msg);
		}
	}
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
float SoundEnvironment::getWetness()
{
	return environmentWetness;
}
