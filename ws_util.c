/***********************************************************************
 ws-util.c - Some basic Winsock utility functions.

***********************************************************************/

//
// Standard include files
//
#include	<stdio.h>

//
// Project include files
//
#include	"ws_util.h"

//// Statics ///////////////////////////////////////////////////////////

// List of Winsock error constants mapped to an interpretation string.
// Note that this list must remain sorted by the error constants'
// values, because we do a binary search on the list when looking up
// items.
#ifdef WIN32
typedef struct
{
	int nID;
	const char * pcMessage;
} T_ERROR_ENTRY;

static T_ERROR_ENTRY gaErrorList[] =
{
	{WSAEINTR,           "Interrupted system call"},
	{WSAEBADF,           "Bad file number"},
	{WSAEACCES,          "Permission denied"},
	{WSAEFAULT,          "Bad address"},
	{WSAEINVAL,          "Invalid argument"},
	{WSAEMFILE,          "Too many open sockets"},
	{WSAEWOULDBLOCK,     "Operation would block"},
	{WSAEINPROGRESS,     "Operation now in progress"},
	{WSAEALREADY,        "Operation already in progress"},
	{WSAENOTSOCK,        "Socket operation on non-socket"},
	{WSAEDESTADDRREQ,    "Destination address required"},
	{WSAEMSGSIZE,        "Message too long"},
	{WSAEPROTOTYPE,      "Protocol wrong type for socket"},
	{WSAENOPROTOOPT,     "Bad protocol option"},
	{WSAEPROTONOSUPPORT, "Protocol not supported"},
	{WSAESOCKTNOSUPPORT, "Socket type not supported"},
	{WSAEOPNOTSUPP,      "Operation not supported on socket"},
	{WSAEPFNOSUPPORT,    "Protocol family not supported"},
	{WSAEAFNOSUPPORT,    "Address family not supported"},
	{WSAEADDRINUSE,      "Address already in use"},
	{WSAEADDRNOTAVAIL,   "Can't assign requested address"},
	{WSAENETDOWN,        "Network is down"},
	{WSAENETUNREACH,     "Network is unreachable"},
	{WSAENETRESET,       "Net connection reset"},
	{WSAECONNABORTED,    "Software caused connection abort"},
	{WSAECONNRESET,      "Connection reset by peer"},
	{WSAENOBUFS,         "No buffer space available"},
	{WSAEISCONN,         "Socket is already connected"},
	{WSAENOTCONN,        "Socket is not connected"},
	{WSAESHUTDOWN,       "Can't send after socket shutdown"},
	{WSAETOOMANYREFS,    "Too many references, can't splice"},
	{WSAETIMEDOUT,       "Connection timed out"},
	{WSAECONNREFUSED,    "Connection refused"},
	{WSAELOOP,           "Too many levels of symbolic links"},
	{WSAENAMETOOLONG,    "File name too long"},
	{WSAEHOSTDOWN,       "Host is down"},
	{WSAEHOSTUNREACH,    "No route to host"},
	{WSAENOTEMPTY,       "Directory not empty"},
	{WSAEPROCLIM,        "Too many processes"},
	{WSAEUSERS,          "Too many users"},
	{WSAEDQUOT,          "Disc quota exceeded"},
	{WSAESTALE,          "Stale NFS file handle"},
	{WSAEREMOTE,         "Too many levels of remote in path"},
	{WSASYSNOTREADY,     "Network system is unavailable"},
	{WSAVERNOTSUPPORTED, "Winsock version out of range"},
	{WSANOTINITIALISED,  "WSAStartup not yet called"},
	{WSAEDISCON,         "Graceful shutdown in progress"},
	{WSAHOST_NOT_FOUND,  "Host not found"},
	{WSANO_DATA,         "No host data of that type was found"},
	{0,                  "No error"},
};
#endif

//// WSAGetLastErrorMessage ////////////////////////////////////////////
// A function similar in spirit to Unix's perror() that tacks a canned 
// interpretation of the value of WSAGetLastError() onto the end of a
// passed string, separated by a ": ".  Generally, you should implement
// smarter error handling than this, but for default cases and simple
// programs, this function is sufficient.
//
// This function returns a pointer to an internal static buffer, so you
// must copy the data from this function before you call it again.  It
// follows that this function is also not thread-safe.

const char* WSAGetLastErrorMessage(const char* pcMessagePrefix)
{
#ifdef WIN32
    static char errorBuffer[256];
	T_ERROR_ENTRY * error;

    // Build basic error string
	sprintf(errorBuffer, "%s: ", pcMessagePrefix);

	for (error = gaErrorList; error->nID != 0; error++)
	{
		if (WSAGetLastError() == error->nID)
		{
			strcat(errorBuffer, error->pcMessage);
			break;
		}
	}

	// Handle the exceptions
	if (error->nID == 0)
		strcat(errorBuffer, WSAGetLastError()?"unknown error":"no error");

	// Add the error number
	sprintf(&errorBuffer[strlen(errorBuffer)], "%s (%d)", errorBuffer, WSAGetLastError());

	// Finish error message off and return it.
	return errorBuffer;
#else
	return pcMessagePrefix;
#endif
}


//// ShutdownConnection ////////////////////////////////////////////////
// Gracefully shuts the connection sd down.  Returns true if we're
// successful, false otherwise.

int ShutdownConnection(SOCKET sd, int status)
{
	unsigned char data[100];

	if (status == TRUE)
		while (recv(sd, data, 100, 0) > 0);

	// Disallow any further data sends.  This will tell the other side
	// that we want to go away now.  If we skip this step, we don't
	// shut the connection down nicely.
	if (shutdown(sd, SD_SEND) != SOCKET_ERROR && status == TRUE)
		while (recv(sd, data, 100, 0) > 0);

    // Close the socket.
	if (closesocket(sd) == SOCKET_ERROR)
		return FALSE;

	return TRUE;
}
