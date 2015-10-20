#include "stdafx.h"
#include "IocpManager.h"
#include "EduServer_IOCP.h"
#include "ClientSession.h"
#include "SessionManager.h"

#define GQCS_TIMEOUT	20

__declspec(thread) int LIoThreadId = 0;
IocpManager* GIocpManager = nullptr;

IocpManager::IocpManager() : mCompletionPort(NULL), mIoThreadCount(2), mListenSocket(NULL)
{
}


IocpManager::~IocpManager()
{
}

bool IocpManager::Initialize()
{
	SYSTEM_INFO sysInfo;
	GetSystemInfo(&sysInfo);
	mIoThreadCount = sysInfo.dwNumberOfProcessors;

	/// winsock initializing
	WSADATA wsa;
	if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
		return false;

	/// Create I/O Completion Port
	//TODO: mCompletionPort = CreateIoCompletionPort(...)
	/// create TCP socket
	//TODO: mListenSocket = ...
	mCompletionPort = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
	mListenSocket = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);

	int opt = 1;
	setsockopt(mListenSocket, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(int));

	//TODO:  bind
	//if (SOCKET_ERROR == bind(mListenSocket, (SOCKADDR*)&serveraddr, sizeof(serveraddr)))
	//	return false;

	SOCKADDR_IN serveraddr;
	memset(&serveraddr, 0, sizeof(serveraddr));
	serveraddr.sin_family = AF_INET;
	serveraddr.sin_addr.s_addr = htonl(INADDR_ANY);
	serveraddr.sin_port = htons(LISTEN_PORT);

	if (SOCKET_ERROR == bind(mListenSocket, (SOCKADDR*)&serveraddr, sizeof(serveraddr)))
		return false;
	printf_s("[DEBUG] bind Complete IP=%s, PORT=%d \n", inet_ntoa(serveraddr.sin_addr), htons(serveraddr.sin_port));

	return true;
}


bool IocpManager::StartIoThreads()
{
	/// I/O Thread
	for (int i = 0; i < mIoThreadCount; ++i)
	{
		//TODO: HANDLE hThread = (HANDLE)_beginthreadex...);
		DWORD dwThreadId;
		HANDLE hThread = (HANDLE)_beginthreadex(NULL, 0, IoWorkerThread, (LPVOID)i, 0, (unsigned int*)&dwThreadId);
	}

	return true;
}


bool IocpManager::StartAcceptLoop()
{
	/// listen
	if (SOCKET_ERROR == listen(mListenSocket, SOMAXCONN))
		return false;

	
	/// accept loop
	while (true)
	{
		SOCKET acceptedSock = accept(mListenSocket, NULL, NULL);
		if (acceptedSock == INVALID_SOCKET)
		{
			printf_s("accept: invalid socket\n");
			continue;
		}

		SOCKADDR_IN clientaddr;
		int addrlen = sizeof(clientaddr);
		getpeername(acceptedSock, (SOCKADDR*)&clientaddr, &addrlen);

		/// 소켓 정보 구조체 할당과 초기화
		ClientSession* client = GSessionManager->CreateClientSession(acceptedSock);

		/// 클라 접속 처리
		if (false == client->OnConnect(&clientaddr))
		{
			client->Disconnect(DR_ONCONNECT_ERROR);
			GSessionManager->DeleteClientSession(client);
		}
	}

	return true;
}

void IocpManager::Finalize()
{
	CloseHandle(mCompletionPort);

	/// winsock finalizing
	WSACleanup();

}


unsigned int WINAPI IocpManager::IoWorkerThread(LPVOID lpParam)
{
	LThreadType = THREAD_IO_WORKER;

	LIoThreadId = reinterpret_cast<int>(lpParam);
	HANDLE hComletionPort = GIocpManager->GetComletionPort();

	while (true)
	{
		DWORD dwTransferred = 0;
		OverlappedIOContext* context = nullptr;
		ClientSession* asCompletionKey = nullptr;

		int ret = GetQueuedCompletionStatus(GIocpManager->GetComletionPort(), &dwTransferred,
			(PULONG_PTR)&asCompletionKey, (LPOVERLAPPED*)&context, GQCS_TIMEOUT);

		/// check time out first 
		if (ret == 0 && GetLastError()==WAIT_TIMEOUT)
			continue;

		if (ret == 0 || dwTransferred == 0)
		{
			/// connection closing
			asCompletionKey->Disconnect(DR_RECV_ZERO);
			GSessionManager->DeleteClientSession(asCompletionKey);
			continue;
		}

		if (nullptr == context)
		{
			printf_s("context has nullptr\n");
			continue;
		}

		bool completionOk = true;
		switch (context->mIoType)
		{
		case IO_SEND:
			completionOk = SendCompletion(asCompletionKey, context, dwTransferred);
			break;

		case IO_RECV:
			completionOk = ReceiveCompletion(asCompletionKey, context, dwTransferred);
			break;

		default:
			printf_s("Unknown I/O Type: %d\n", context->mIoType);
			break;
		}

		if ( !completionOk )
		{
			/// connection closing
			asCompletionKey->Disconnect(DR_COMPLETION_ERROR);
			GSessionManager->DeleteClientSession(asCompletionKey);
		}

	}

	return 0;
}

bool IocpManager::ReceiveCompletion(const ClientSession* client, OverlappedIOContext* context, DWORD dwTransferred)
{

	/// echo back 처리 client->PostSend()사용.
	if (!client->PostSend(context->mBuffer, dwTransferred))
	{
		printf_s("Post Send error: %d\n", GetLastError());
		delete context;
		return false;
	}
	delete context;
	return client->PostRecv();
}

bool IocpManager::SendCompletion(const ClientSession* client, OverlappedIOContext* context, DWORD dwTransferred)
{
	/// 전송 다 되었는지 확인하는 것 처리..
	if (context->mWsaBuf.len != dwTransferred)
	{
		printf_s("Send Not Completed\n");
		delete context;
		return false;
	}

	delete context;
	return true;
}
