#ifndef __MACRO_H_
#define __MACRO_H_

#define	FALSE				0
#define	TRUE				1

typedef	unsigned char		BYTE;
typedef	unsigned short		WORD;
typedef	unsigned long		DWORD;
typedef	char				TEXT;
typedef	signed char			INT8;
typedef unsigned char		FLAG;

typedef	unsigned char *		BPTR;
typedef	unsigned short *	WPTR;
typedef	unsigned long *		DWPTR;
typedef	char *				TPTR;

typedef struct
{
	char tid[20];
	char nextmsg;
	char serialNumber[20];
	char manufacturer[20];
	char model[20];
	char appname[30];
	char appversion[30];
	char objectname[30];
	char amount[15];
	char last4Digits[5];
	char reponseCode[5];
	char authID[10];
	char cardType[30];
	char transTime[30];
	char result[5];

	char *jsontext;
} T_WEBREQUEST;

typedef struct
{
	char tid[20];
	char nextmsg;
	char *jsontext;
} T_WEBRESP;

#define WEBREQUEST_MSGTYPE_HEARTBEAT 1
#define WEBREQUEST_MSGTYPE_GETCONFIG 2
#define WEBREQUEST_MSGTYPE_TRANSLOG 3
#define WEBREQUEST_MSGTYPE_TRADEOFFER 4
#define WEBREQUEST_MSGTYPE_IPAYTRANS 5

#endif /* __MACRO_H_ */

