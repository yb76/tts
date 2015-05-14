/***********************************************************************
 wsa-util.h - Declarations for the Winsock utility functions module.
***********************************************************************/

#if !defined(WS_UTIL_H)
#define WS_UTIL_H

// Uncomment one.
#ifdef WIN32
	#include <winsock2.h>
//	#include <winsock2.h>
#else
	typedef	unsigned int	SOCKET;
	#define INVALID_SOCKET	(SOCKET)(~0)
	#define SOCKET_ERROR	(-1)
	#define	WINAPI
	#define	SD_RECEIVE		0
	#define	SD_SEND			1
	#define	SD_BOTH			2
	#define	closesocket(sd)	close(sd)
#endif

extern const char * WSAGetLastErrorMessage(const char* pcMessagePrefix);
extern int ShutdownConnection(SOCKET sd, int status);

#define	BUFFER_SIZE				4096
#define	DEFAULT_SERVER_PORT		1234

#define	FALSE					0
#define	TRUE					1

#endif // !defined (WS_UTIL_H)
