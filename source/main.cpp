#include "Globals.h"

#include <vector>
#include <deque>
#include <semaphore.h>
#include <iostream>

#include "Functions.h"
#include "ODApplication.h"
#include "GameMap.h"
#include "Player.h"
#include "RenderRequest.h"
#include "Socket.h"
#include "ProtectedObject.h"
#include "Random.h"

GameMap gameMap;

sem_t lightNumberLockSemaphore;
sem_t missileObjectUniqueNumberLockSemaphore;

ProtectedObject<unsigned int> numThreadsWaitingOnRenderQueueEmpty(0);

std::deque<ServerNotification*> serverNotificationQueue;
std::deque<ClientNotification*> clientNotificationQueue;
sem_t serverNotificationQueueSemaphore;
sem_t clientNotificationQueueSemaphore;
sem_t serverNotificationQueueLockSemaphore;
sem_t clientNotificationQueueLockSemaphore;

sem_t creatureAISemaphore;

Socket* serverSocket = NULL;
Socket* clientSocket = NULL;

ProtectedObject<long int> turnNumber(1);

#if OGRE_PLATFORM == OGRE_PLATFORM_WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

INT WINAPI WinMain( HINSTANCE hInst, HINSTANCE, LPSTR strCmdLine, INT )
#else
int main(int argc, char **argv)
#endif
{
#ifdef WIN32
    // Set up windows sockets
    WSADATA wsaData;

    if(WSAStartup(MAKEWORD(2, 0), &wsaData) != 0)
    {
        cerr << "Couldn't not find a usable WinSock DLL.n";
        exit(1);
    }
#endif

    sem_init(&lightNumberLockSemaphore, 0, 1);
    sem_init(&missileObjectUniqueNumberLockSemaphore, 0, 1);
    sem_init(&serverNotificationQueueSemaphore, 0, 0);
    sem_init(&clientNotificationQueueSemaphore, 0, 0);
    sem_init(&serverNotificationQueueLockSemaphore, 0, 1);
    sem_init(&clientNotificationQueueLockSemaphore, 0, 1);
    sem_init(&creatureAISemaphore, 0, 1);

    Random::initialize();
    try
    {
        new ODApplication;
    }
    catch (Ogre::Exception& e)
    {
#if OGRE_PLATFORM == OGRE_PLATFORM_WIN32 
        MessageBox( NULL, e.what(), "An exception has occurred!", MB_OK | MB_ICONERROR | MB_TASKMODAL);
#else
        fprintf(stderr, "An exception has occurred: %s\n", e.what());
#endif
    }

#ifdef WIN32
    WSACleanup();
#endif

    return 0;
}
