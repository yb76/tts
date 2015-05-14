/*
 * File: main.c
 * Description:	TCP/IP server for i-RIS
 */

//
// Standard include files
//
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/timeb.h>
#include <time.h>
#include <string.h>
#include <malloc.h>
#include <getopt.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <pthread.h>
#include <errno.h>
#include <signal.h>

//
// Project include files
//
#include "ws_util.h"
#include "macro.h"

#ifdef USE_MYSQL
	#include <mysql.h>
	#define	db_error(mysql, res)					mysql_error(mysql)
void * get_thread_dbh();
void * set_thread_dbh();
#endif

static pthread_mutex_t logMutex;
static pthread_mutex_t counterMutex;
static pthread_mutex_t hsmMutex;

//
// Local include files
//

typedef struct
{
	SOCKET sd;
	struct sockaddr_in sinRemote;
} T_THREAD_DATA;

static FILE * stream = NULL;
int debug = 1;
char * logFile = "./irisLog";
int running = 1;
int dispMessage = 0;
int counter = 0;

//// Constants /////////////////////////////////////////////////////////

/*
**-------------------------------------------------------------------------------------------
** FUNCTION   : UtilHexToString
**
** DESCRIPTION:	Transforms hex byte array to a string. Each byte is simply split in half.
**				The minimum size required is double that of the hex byte array
**
** PARAMETERS:	hex			<=	Array to store the converted hex data
**				string		=>	The number string.
**				length		<=	Length of hex byte array
**
** RETURNS:		The converted string
**-------------------------------------------------------------------------------------------
*/
char * UtilHexToString(unsigned char * hex, int length, char * string)
{
	int i;

	if (string)
	{
		string[0] = '\0';

		if (hex)
		{
			for (i = 0; i < length; i++)
				sprintf(&string[i*2], "%02X", hex[i]);
		}
	}

	return string;
}

static void counterIncrement(void)
{
	// Counter critical section
	pthread_mutex_lock(&counterMutex);
	counter++;
	pthread_mutex_unlock(&counterMutex);
}

static void counterDecrement(void)
{
	// Counter critical section
	pthread_mutex_lock(&counterMutex);
	counter--;
	pthread_mutex_unlock(&counterMutex);
}

static void logStart(void)
{
	// Initialise
	stream = stdout;

	// Open the appropriate stream
	if ((stream = fopen(logFile, "a+")) == NULL)
		stream = stdout;

	// Initialise the log mutex
	pthread_mutex_init(&logMutex, NULL);
}

void logEnd(void)
{
	fclose(stream);

	pthread_mutex_destroy(&logMutex);
}

static void dbStart(void)
{
	// Start of log critical section
	//pthread_mutex_lock(&dbMutex);
}

static void dbEnd(void)
{
	// free log mutex and unlock other threads
	//pthread_mutex_unlock(&dbMutex);
}

static char * timeString(char * string, int len)
{
	struct tm *newtime;
	struct tm temp;
	struct timeb tb;

	ftime(&tb);
	newtime = localtime_r(&tb.time, &temp);
	strftime(string, len, "%a, %d/%m/%Y %H:%M:%S", newtime);
	sprintf(&string[strlen(string)], ".%03ld", tb.millitm);

	return string;
}

int logArchive(FILE **stream, long maxSize)
{
	int result;

	if (stream && *stream != NULL && *stream != stderr && *stream != stdout && ftell(*stream) > maxSize)
	{
		char cmd[400];

		// Log file too large - gzip the current file and start over.
		// Archive name: <path>"<logfile>-DDMMYY.gz"
		if ((result = snprintf(cmd, sizeof(cmd), "gzip -f -S -`date +%%y%%m%%d%%H%%M`.gz %s", logFile)) < 0)
			return -1;

		fclose(*stream);

		system(cmd);

		/* Old log file discarded by gzip */
		if ((*stream = fopen(logFile, "a+")) == NULL)
			*stream = stdout;
	}

	return 1;
}

void logNow(const char * format, ...)
{
	va_list args;
	va_start( args, format );

	// Start of log critical section
	pthread_mutex_lock(&logMutex);

	// Print formatted message
	vfprintf(stream, format, args);
	fflush(stream);

	// check and archive
	logArchive(&stream, (10000*1024L));

	// free log mutex and unlock other threads
	pthread_mutex_unlock(&logMutex);

	va_end(args);
}

static void displayComms(char * header, char * data, int len)
{
	int i, j, k;
	char * line;

	if (dispMessage == 0) return;

	line = malloc(strlen(header) + (4 * len) + (len / 16 * 4) + 200);
	if (line == NULL) return;

	strcpy(line, header);
	strcat(line, "\n");

	for (i = 0, k = strlen(line); i < len;)
	{
		for (j = 0; j < 16; j++, i++)
		{
			if (i < len)
				sprintf(&line[k], "%02.2X ", (BYTE) data[i]);
			else
				strcat(line, "   ");
			k += 3;
		}

		strcat(line, "   ");
		k += 3;

		for (j = 0, i -= 16; j < 16; j++, i++)
		{
			if (i < len)
			{
				if (data[i] == '%')
				{
					line[k++] = '%';
					line[k++] = '%';
				}
				else if (data[i] >= ' ' && data[i] <= '~')
					line[k++] = data[i];
				else
					line[k++] = '.';
			}
			else line[k++] = ' ';
		}

		line[k++] = '\n';
		line[k] = '\0';
	}

	strcat(line, "\n");
	logNow(line);
	free(line);
}

static int databaseInsert(char * query, char * errorMsg)
{
	int result;

	//  Add the object
	dbStart();
	if (mysql_real_query(get_thread_dbh(), query, strlen(query)) == 0) // success
		result = TRUE;
	else
	{
		if (errorMsg) strcpy(errorMsg, db_error(get_thread_dbh(), res));
		result = FALSE;
	}

	dbEnd();

	return result;
}

static long databaseCount(char * query)
{
	long count = -1;
	MYSQL_RES * res;

	dbStart();

	if (mysql_real_query(get_thread_dbh(), query, strlen(query)) == 0) // success
	{
		MYSQL_ROW row;

		if (res = mysql_store_result(get_thread_dbh()))
		{
			if (row = mysql_fetch_row(res))
			{
				if(row[0] && strlen(row[0])) count = atol(row[0]);
			}
			mysql_free_result(res);
		}
	}

	dbEnd();

	return count;
}

int getFileData(char **pfileData,char *filename,int *len)
{
	FILE *fp = fopen(filename,"rb");
	int fileLen = 0;
	int nRead = 0;
	char *dataBuffer = NULL;

	*len = 0;
	*pfileData = NULL;
	
	if(fp!= NULL ) {
		fseek (fp , 0 , SEEK_END);
		fileLen = ftell (fp);
		rewind (fp);
		*pfileData = malloc( fileLen + 1 );
		nRead = fread(*pfileData,1,fileLen,fp);
		*len = nRead;
		fclose(fp);
	}
	return(*len);
}

int getObjectField(char * data, int count, char * field, char ** srcPtr, const char * tag)
{
	char * ptr;
	int i = 0;
	int j = 0;

	if (srcPtr)
	{
		if ((*srcPtr = strstr(data, tag)) == NULL)
			return 0;
		else
			return (strlen(*srcPtr));
	}

	// Extract the TYPE
	if ((ptr = strstr(data, tag)) != NULL)
	{
		for (--count; ptr[i+strlen(tag)] != ',' && ptr[i+strlen(tag)] != ']' && ptr[i+strlen(tag)] != '}' ; i++)
		{
			for(; count; count--, i++)
			{
				for(; ptr[i+strlen(tag)] != ','; i++);
			}
			field[j++] = ptr[i+strlen(tag)];
		}
	}

	field[j] = '\0';

	return strlen(field);
}

int getNextObject(unsigned char * request, unsigned int requestLength, unsigned int * offset,
				  char * type, char * name, char * version, char * event, char * value, char * object, char * json,
				  unsigned char * iv, unsigned char * MKr, unsigned int * currIVByte, char * serialnumber)
{
	unsigned int i;
	char data;
	unsigned int length = 0;
	int marker = 0;
	char temp[50];

	if (debug) logNow(	"\n%s:: Next OBJECT:: ", timeString(temp, sizeof(temp)));

	for (i = *offset; i < requestLength; i++)
	{
		data = request[i];

		// If the marker is detected, start storing the JSON object
		if (marker || data == '{')
			json[length++] = data;

		// If we detect the start of an object, increment the object marker
		if (data == '{')
			marker++;

		// If we detect graphic characters, do not process any further - corrupt message
		else if (((unsigned char) data) > 0x7F)
		{
			logNow("Invalid JSON object: %02X. No further processing....\n", data);
			return -1;
		}

		// IF we detect the end of an object, decrement the marker
		else if (data == '}')
		{
			marker--;
			if (marker < 0)
			{
				logNow("Incorrectly formated JSON object. Found end of object without finding beginning of object\n");
				return -1;
			}
			if (marker == 0)
			{
				i++;
				break;
			}
		}
	}

	// Set the start of the next object search
	*offset = i;

	// If the object exist...
	if (length)
	{
		// Terminate the "json" string
		json[length] = '\0';

		// Extract the type field
		getObjectField(json, 1, type, NULL, "TYPE:");

		if (strcmp(type, "IDENTITY") == 0)
		{
			// Extract the serial number field
			getObjectField(json, 1, name, NULL, "SERIALNUMBER:");

			// Extract the manufacturer field
			getObjectField(json, 1, version, NULL, "MANUFACTURER:");

			// Extract the model field
			getObjectField(json, 1, event, NULL, "MODEL:");

			// Output the data in the clear
			if (debug) logNow("%s\n", json);

			return 1;
		}
		else
		{
			// Extract the name field
			getObjectField(json, 1, name, NULL, "NAME:");

			// Output the data in the clear
			if (debug)
			{
				logNow(":+:+:%s:+:+:%s\n", serialnumber, json);
			}

			return 0;
		}
	}
	else logNow("No more...\n");

	return -99;
}

static void addObject(unsigned char ** response, char * data, int ofb, unsigned int offset, unsigned int maskLength)
{
	char temp[50];

	// If empty additional data, do not bother adding....
	if (data[0] == '\0')
		return;

	my_malloc_max( response, 10240, strlen(data)+1);

	// Output object to be sent
	if (debug)
	{
		if (maskLength)
			logNow("\n%s:: Sending Object:: %.*s **** OBJECT LOGGING TRUNCATED ****\n", timeString(temp, sizeof(temp)), maskLength, data);
		else
			logNow("\n%s:: Sending Object:: %s\n", timeString(temp, sizeof(temp)), data);
	}

	strcat(*response, data);

}

int my_malloc_max(unsigned char **buff, int maxlen, int currlen)
{
	int ilen = 0;

	if(*buff==NULL) {
		*buff = calloc( maxlen,1 );
	} else {
		ilen = strlen(*buff) + currlen + 1;
		if(ilen > maxlen)
			*buff = realloc(*buff, ilen);
	}

	return(0);
}

void get_mid_tid(char * serialnumber, char * __mid, char * __tid)
{
	FILE * fp;
	char tmp[1000];
	int i;

	// Get the current terminal TID and MID
	sprintf(tmp, "%s.iPAY_CFG", serialnumber);
	if ((fp = fopen(tmp, "r")) != NULL)
	{
		if (fgets(tmp, sizeof(tmp), fp))
		{
			char * tid = strstr(tmp, "TID:");
			char * mid = strstr(tmp, "MID:");
			if (__tid)
			{
				if (tid)
					sprintf(__tid, "%8.8s", &tid[4]);
				else strcpy(__tid, "");
			}
			if (__mid)
			{
				if (mid)
				{
					for (i = 4; mid[i] != ',' && mid[i] != '}' && mid[i]; i++)
						__mid[i-4] = mid[i];
					__mid[i-4] = '\0';
				}
				else strcpy(__mid, "");
			}
		}
		fclose(fp);
	}
}

int processRequest(SOCKET sd, unsigned char * request, unsigned int requestLength, char * serialnumber, int * unauthorised)
{
	char type[100];
	union
	{
		char name[100];
		char serialnumber[100];
	}u;
	union
	{
		char version[100];
		char manufacturer[100];
	}u2;
	union
	{
		char event[100];
		char model[100];
	}u3;
	char value[100];
	char object[100];
	char json[4000];
	char query[5000];
	unsigned int requestOffset = 0;
	unsigned char * response = NULL;
	unsigned int offset = 0;
	unsigned long length = 0;
	int objectType;
	char model[20];
	unsigned char MKr[16];
	unsigned int currIVByte = 0;
	char identity[500];
	int update = 0;
	//MessageMCA mcamsg;
	MYSQL *dbh = (MYSQL *)get_thread_dbh();
	int nextmsg = 0;
	char tid[30]="";
	char temp[50]="";
	int dldexist = 0;

	// Increment the unauthorised flag
	(*unauthorised)++;

	// Examine request an object at a time for trigger to download objects
	while((objectType = getNextObject(request, requestLength, &requestOffset, type, u.name, u2.version, u3.event, value, object, json, NULL, MKr, &currIVByte, serialnumber)) >= 0)
	{
		int id = 0;

		// Process the device identity
		if (objectType == 1)
		{
			// Add it in. If a duplicate, it does not matter but just get the ID back later
			strcpy(serialnumber, u.serialnumber);
			strcpy(model, u3.model);

			// Display merchant details TID, MID and ADDRESS for debugging purposes if available
			{
				FILE * fp;
				char tmp[1000];

				sprintf(tmp, "%s.iPAY_CFG", serialnumber);
				if ((fp = fopen(tmp, "r")) != NULL)
				{
					if (fgets(tmp, sizeof(tmp), fp))
					{
						char * tid = strstr(tmp, "TID:");
						char * addr2 = strstr(tmp, "ADDR2:");
						char * addr3 = strstr(tmp, "ADDR3:");

						logNow("*+*+*+*+*+*+*+*+*+*+*+*+* %12.12s,%26.26s,%26.26s +*+*+*+*+*+*+*+*+*+*+*+*\n", tid?tid:"oOoOoOoO", addr2?addr2:"", addr3?addr3:"");
					}
					fclose(fp);
				}
			}

			strcpy(identity, json);

			continue;
		}

		// Do not allow the object download to continue if the device has not identified itself
		//if (serialnumber[0] == '\0') continue;

		// If this is an authorisation, then examine the proof
		if (strcmp(type, "AUTH") == 0 )
		{
			FILE * fp;
			char temp[300];
			int position = 0;

			// If there is a specific message to the terminal, add it now
			sprintf(temp, "%s.dld", serialnumber);
			if ((fp = fopen(temp, "rb")) != NULL)
			{
				char line[300];

				while (fgets(line, 300, fp) != NULL)
					addObject(&response, line, 1, offset, 0);

				fclose(fp);
				remove(temp);
				dldexist = 1;
			}

			continue;
		}

		(*unauthorised) = 0;

		// If the type == DATA, then just store it in the object list for further processing at a later time
		if (strcmp(type, "DATA") == 0)
		{
			char extra[100];

			// Process transactions
#ifdef __GPS
			if (strcmp(u.name, "GPS_REQ") == 0)
			{
				char tid[64]="";
				char gomo_driverid[64]="TA0002";
				char gomo_terminalid[64]="00000001";
				char step[64]="";
				char cli_string[10240]="";
				char ser_string[10240]="";
				char sqlquery[1024]="";
				int iret = 0;

				getObjectField(json, 1, tid, NULL, "TID:");
				getObjectField(json, 1, step, NULL, "STEP:");
				strcpy(gomo_terminalid,tid);

				logNow( "gomo request .1\n");
				//sprintf(sqlquery," select driverid, terminalid from gomo_driverid where tid = '%s' ", tid);
				sprintf(sqlquery," select driversnumber,'%s' from terminal_account ta left join terminal_account_driver tad on  tad.terminalaccountid = ta.id and tad.current = 1 left join driver d on tad.driverid = d.id where ta.tid = '%s' ", tid,tid);

				dbStart();
				#ifdef USE_MYSQL
				if (mysql_real_query(dbh, sqlquery, strlen(sqlquery)) == 0) // success
				{
					MYSQL_RES * res;
					MYSQL_ROW row;
					if (res = mysql_store_result(dbh))
					{
						if (row = mysql_fetch_row(res))
						{
							if (row[0]) strcpy(gomo_driverid, row[0]);
							if (row[1]) strcpy(gomo_terminalid,row[1]);
						}
					}
					mysql_free_result(res);
				}
				#endif
				dbEnd();

				if(strcmp(step,"HB")==0) { /* heart beat*/
					char availability[64]="";
					char lat[64]="";
					char lon[64]="";
					char auth[64]="";
					char query[300]="";
					int found = 1;

					getObjectField(json, 1, auth, NULL, "AUTH_NO:");
					if(0 && strlen(auth) && strcmp(auth,gomo_driverid)) { //DELETED NOW
						char DBError[200];
						found = 0;
						sprintf(sqlquery," select ID from driver where driversnumber = '%s' ", auth);
						dbStart();
						#ifdef USE_MYSQL
						if (mysql_real_query(dbh, sqlquery, strlen(sqlquery)) == 0) // success
						{
							MYSQL_RES * res;
							res = mysql_store_result(dbh);
							mysql_free_result(res);
							found =1;
						}
						#endif
						dbEnd();

						if(found) {
							if(strcmp(gomo_driverid,"TA0001")!=0) 
								sprintf(query, "UPDATE gomo_driverid set driverid='%s' where tid='%s'", auth,tid);
							else
								sprintf(query, "insert into gomo_driverid values('%s','%s','%s')", tid,auth,tid);
							if (databaseInsert(query, DBError))
								logNow( "GOMO_DRIVERID[%s] **RECORDED**\n", query);
							else
							{
								logNow( "Failed to update 'gomo_driverid' table[%s].  Error: %s\n", query,DBError);
							}
							strcpy(gomo_driverid,auth);
						}
					}
					if(found) {
						char plate_no[30]="";
						char stmp[50]="";
						getObjectField(json, 1, lat, NULL, "LAT:");
						getObjectField(json, 1, lon, NULL, "LON:");
						getObjectField(json, 1, availability, NULL, "AV:");
						getObjectField(json, 1, plate_no, NULL, "TAXI_NO:");

						if(strlen(plate_no)) sprintf(stmp,"&plate=%s",plate_no); else strcpy(stmp,"");
						sprintf(cli_string,"driver_id=%s&terminal_id=%s%s&latitude=%s&longitude=%s&availability=%s",
							gomo_driverid,gomo_terminalid,stmp,lat,lon,availability);
						iret = irisGomo_heartbeat(cli_string,ser_string);
						if(iret == 200 && ser_string[0] == '{') {
							//sprintf(cli_string,"{TYPE:DATA,NAME:GPS_RESP,VERSION:1,%s",&ser_string[1]);
							//strcpy(ser_string,cli_string);
						}
						else if(iret>0) {
							sprintf(cli_string,"{TYPE:DATA,NAME:GPS_RESP,VERSION:1,ERRORCODE:%d,ERRORSTR:%.100s}",iret,ser_string);
							strcpy(ser_string,cli_string);
						}
					} else {
						sprintf(ser_string,"{TYPE:DATA,NAME:GPS_RESP,VERSION:1,ERRORCODE:901,ERRORSTR:%s}","NO AUTH");
					}
				}
				else if(strcmp(step,"BL")==0) { /* booking list*/
					sprintf(cli_string,"driver_id=%s", gomo_driverid);
					iret = irisGomo_bookinglist(cli_string,ser_string);
					if(iret == 200 ) {
					}
					else if(iret>0) {
						sprintf(cli_string,"{TYPE:DATA,NAME:GPS_RESP,VERSION:1,ERRORCODE:%d,ERRORSTR:%.100s}",iret,ser_string);
						strcpy(ser_string,cli_string);
					}
					
				}
				else if(strcmp(step,"NB")==0) { /* new booking list*/
					sprintf(cli_string,"driver_id=%s", gomo_driverid);
					iret = irisGomo_newbookinglist(cli_string,ser_string);
					if(iret == 200 ) {
					}
					else if(iret>0) {
						sprintf(cli_string,"{TYPE:DATA,NAME:GPS_RESP,VERSION:1,ERRORCODE:%d,ERRORSTR:%.100s}",iret,ser_string);
						strcpy(ser_string,cli_string);
					}
					
				}
				else if(strcmp(step,"BA")==0) { /* booking accept*/
					char booking_id[64]="";
					getObjectField(json, 1, booking_id, NULL, ",ID:");
					sprintf(cli_string,"driver_id=%s&booking_id=%s", gomo_driverid,booking_id);
					iret = irisGomo_bookingaccept(booking_id,cli_string,ser_string);
					if(iret == 200 && ser_string[0] == '{') {
						sprintf(cli_string,"{TYPE:DATA,NAME:GPS_RESP,VERSION:1,%s",&ser_string[1]);
						strcpy(ser_string,cli_string);
					}
					else if(iret>0) {
						sprintf(cli_string,"{TYPE:DATA,NAME:GPS_RESP,VERSION:1,ERRORCODE:%d,ERRORSTR:%.100s}",iret,ser_string);
						strcpy(ser_string,cli_string);
					}
				}
				else if(strcmp(step,"BR")==0) { /* booking release */
					char msg[64]="";
					getObjectField(json, 1, msg, NULL, "REASON:");
					sprintf(cli_string,"driver_id=%s&reason=%s", gomo_driverid,msg);
					iret = irisGomo_bookingrelease(cli_string,ser_string);
					if(iret>0) {
						sprintf(cli_string,"{TYPE:DATA,NAME:GPS_RESP,VERSION:1,ERRORCODE:%d,ERRORSTR:%.100s}",iret,ser_string);
						strcpy(ser_string,cli_string);
					}
				}
				else if(strcmp(step,"MG")==0) { /* message */
					char msgid[64]="";
					char msg[64]="";
					getObjectField(json, 1, msgid, NULL, "MSGID:");
					if(strcmp(msgid,"1")==0) strcpy(msg,"at-pickup");
					else if(strcmp(msgid,"2")==0) strcpy(msg,"delayed");
					else if(strcmp(msgid,"3")==0) strcpy(msg,"contact-me");
					else if(strcmp(msgid,"4")==0) strcpy(msg,"no-show");
					
					sprintf(cli_string,"driver_id=%s&message_type=%s", gomo_driverid,msg);
					iret = irisGomo_message(cli_string,ser_string);
					if(iret>0) {
						sprintf(cli_string,"{TYPE:DATA,NAME:GPS_RESP,VERSION:1,ERRORCODE:%d,ERRORSTR:%.100s}",iret,ser_string);
						strcpy(ser_string,cli_string);
					}
				}
				else if(strcmp(step,"TS")==0) { /* trip start */
					sprintf(cli_string,"driver_id=%s", gomo_driverid);
					iret = irisGomo_tripstart(cli_string,ser_string);
					if(iret>0) {
						sprintf(cli_string,"{TYPE:DATA,NAME:GPS_RESP,VERSION:1,ERRORCODE:%d,ERRORSTR:%.100s}",iret,ser_string);
						strcpy(ser_string,cli_string);
					}
				}
				else if(strcmp(step,"PQ")==0) { /* payment request */
					char fare[64]="";
					char extra[64]="";
					char pay[64]="";
					getObjectField(json, 1, fare, NULL, "FARE:");
					getObjectField(json, 1, extra, NULL, "EXTRA:");
					getObjectField(json, 1, pay, NULL, "PAY:");
					sprintf(cli_string,"driver_id=%s&payment_method=%s&fare=%s&extra=%s", gomo_driverid,pay,fare,extra);
					iret = irisGomo_paymentrequest(cli_string,ser_string);
					if(iret>0) {
						sprintf(cli_string,"{TYPE:DATA,NAME:GPS_RESP,VERSION:1,ERRORCODE:%d,ERRORSTR:%.100s}",iret,ser_string);
						strcpy(ser_string,cli_string);
					}
				}
				else if(strcmp(step,"PS")==0) { /* payment status*/
					sprintf(cli_string,"driver_id=%s", gomo_driverid);
					iret = irisGomo_paymentstatus(cli_string,ser_string);
					if(iret == 200 ) {
					}
					else if(iret>0) {
						sprintf(cli_string,"{TYPE:DATA,NAME:GPS_RESP,VERSION:1,ERRORCODE:%d,ERRORSTR:%.100s}",iret,ser_string);
						strcpy(ser_string,cli_string);
					}
				}

				else if(strcmp(step,"TE")==0) { /* trip end */
					char paid[64]="";
					getObjectField(json, 1, paid, NULL, "PAID:");
					if(strcmp(paid,"YES")==0) strcpy(paid,"true"); 
					else strcpy(paid,"false");

					sprintf(cli_string,"driver_id=%s&paid=%s", gomo_driverid,paid);
					iret = irisGomo_tripfinished(cli_string,ser_string);
					if(iret>0) {
						sprintf(cli_string,"{TYPE:DATA,NAME:GPS_RESP,VERSION:1,ERRORCODE:%d,ERRORSTR:%.100s}",iret,ser_string);
						strcpy(ser_string,cli_string);
					}
				}

				if(strlen(ser_string) && ser_string[0] == '{') {
					addObject(&response, ser_string, 1, offset, 0);
				}
			}
#endif

			else if (strcmp(u.name, "iRIS_POWERON") == 0)
			{
			}
			else if (strcmp(u.name, "IRIS_CFG") == 0)
			{
			}

			else if (strcmp(u.name, "iPAY_CFG") == 0)
			{
			}

			continue;
		}
	}

	if (response)
	{
		// If an empty response, add some dummy bytes to ensure the compressor does not complain
		if (strlen(&response[offset]) == 0)
			addObject(&response, "Empty!!", 1, offset, 0);

		length = strlen(&response[offset]);
	}

	if (length > 0)
	{
		char title[200];
		char temp[50];

		response = realloc(response, length+2);
		memmove(&response[2], response, length);
		response[0] = (unsigned char) (length / 256);
		response[1] = (unsigned char) (length % 256);

		sprintf(title,	"\n\n---------------------------"
						"\n%s"
						"\nSending updates to terminal:"
						"\n---------------------------\n", timeString(temp, sizeof(temp)));
		displayComms(title, response, length+2);

		if (send(sd, response, length+2, MSG_NOSIGNAL) == (int) (length+2))
		{
			logNow(	"\n%s:: ***SENT***", timeString(temp, sizeof(temp)));
		}
		else
			logNow(	"\n%s:: ***SEND FAILED***", timeString(temp, sizeof(temp)));

		free(response);
	}
	else
	{
		char title[200];
		char temp[50];
		unsigned char resp[2];

		resp[0] = 0;
		resp[1] = 0;

		sprintf(title,	"\n\n---------------------------"
						"\n%s"
						"\nSending updates to terminal:"
						"\n---------------------------\n", timeString(temp, sizeof(temp)));
		displayComms(title, resp, 2);

		send(sd, resp, 2, MSG_NOSIGNAL);
		logNow(	"\n%s:: ***SENT***", timeString(temp, sizeof(temp)));
	}

	return update;
}

int sendToTerminal( int sd,char **sendResponse, int offset, int length, int endflag)
{
	unsigned char *response = *sendResponse;

	if (response)
	{
		// If an empty response, add some dummy bytes to ensure the compressor does not complain
		if (strlen(&response[offset]) == 0)
			addObject(&response, "Empty!!", 1, offset, 0);

		length = strlen(&response[offset]);
	}

	if (length > 0)
	{
		char title[200];
		char temp[50];

		memmove(&response[2], response, length);
		response[0] = (unsigned char) (length / 256);
		response[1] = (unsigned char) (length % 256);

		sprintf(title,	"\n\n---------------------------"
						"\n%s"
						"\nSending updates to terminal:"
						"\n---------------------------\n", timeString(temp, sizeof(temp)));
		displayComms(title, response, length+2);

		if (send(sd, response, length+2, MSG_NOSIGNAL) == (int) (length+2))
		{
			logNow(	"\n%s:: ***SENT***", timeString(temp, sizeof(temp)));
		}
		else
			logNow(	"\n%s:: ***SEND FAILED***", timeString(temp, sizeof(temp)));

		return(1);
	}
	return(0);
}

//// EchoIncomingPackets ///////////////////////////////////////////////
// Bounces any incoming packets back to the client.  We return false
// on errors, or true if the client closed the socket normally.
int EchoIncomingPackets(SOCKET sd)
{
    // Read data from client
	unsigned char acReadBuffer[BUFFER_SIZE];
	int nReadBytes;
	int lengthBytes = 2;
	int length = 0;
	unsigned char * request = NULL;
	unsigned int requestLength = 0;
	char serialnumber[100];
	int update = 0;
	int unauthorised = 0;
	char temp[50];

	// Initialisation
	serialnumber[0] = '\0';

	logNow("\n%s:: Get thread DBHandler start\n", timeString(temp, sizeof(temp)));
	if( set_thread_dbh() == NULL) {
		logNow("\n%s:: Get DBHandler failed !!\n", timeString(temp, sizeof(temp)));
		return FALSE;
	}
	logNow("\n%s:: Get thread DBHandler ok!\n", timeString(temp, sizeof(temp)));

	while(1)
	{
		struct timeval timeout;

		// Get the length first
		do
		{
			timeout.tv_sec = 30;	// If the connection stays for more than 30 seconds, lose it.
			timeout.tv_usec = 100;
			setsockopt(sd, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(timeout));
	
			nReadBytes = recv(sd, acReadBuffer, lengthBytes, 0);
			if (nReadBytes == 0)
			{
				logNow("\n%s:: Connection closed by peer (1).\n", timeString(temp, sizeof(temp)));
				return TRUE;
			}
			else if (nReadBytes > 0)
			{
				length = length * 256 + acReadBuffer[0];
				if (--lengthBytes && --nReadBytes)
				{
					length = length * 256 + acReadBuffer[1];
					--lengthBytes;
				}
			}
			else if (nReadBytes == SOCKET_ERROR  || nReadBytes < 0)
			{
				logNow("\n%s:: Connection closed by peer (socket - %d).\n", timeString(temp, sizeof(temp)), errno);
				return FALSE;
			}	
		} while (lengthBytes);

		logNow(	"\n--------------------------------"
				"\n%s"
				"\nReceiving request from terminal:"
				"\n--------------------------------"
				"\nExpected request length = %d bytes from client.\n", timeString(temp, sizeof(temp)), length);

		do
		{
			if (length <= 0)
			{
				logNow(	"\n---------------------------------"
						"\n%s"
						"\nProcessing request from terminal:"
						"\n---------------------------------\n", timeString(temp, sizeof(temp)));
				displayComms("Message Received:\n", request, requestLength);

				update = processRequest(sd, request, requestLength, serialnumber, &unauthorised );

				// Reinitialise for the next request
				lengthBytes = 2;
				free(request);
				request = NULL;
				requestLength = 0;

				// Flush the buffer
				timeout.tv_sec = 0;	// If the connection stays for more than 30 minutes, lose it.
				timeout.tv_usec = 1;
				setsockopt(sd, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(timeout));
				while (recv(sd, acReadBuffer, 1, 0) > 0);
				break;
			}

			timeout.tv_sec = 5;
			timeout.tv_usec = 100;
			setsockopt(sd, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(timeout));
	
			nReadBytes = recv(sd, acReadBuffer, length, 0);
			if (nReadBytes > 0)
			{
				length -= nReadBytes;

				acReadBuffer[nReadBytes] = '\0';

				if (request == NULL)
					request = malloc(nReadBytes);
				else
					request = realloc(request, requestLength + nReadBytes);

				memcpy(&request[requestLength], acReadBuffer, nReadBytes);
				requestLength += nReadBytes;
			}
			else if (nReadBytes == SOCKET_ERROR || nReadBytes < 0)
			{
				if (request) free(request);
				logNow("\n%s:: Connection closed by peer (socket2 - %d).\n", timeString(temp, sizeof(temp)), errno);
				return FALSE;
			}
		} while (nReadBytes != 0);
	}

	logNow("Connection closed by peer.\n");
	if (request) free(request);
	return TRUE;
}

//// EchoHandler ///////////////////////////////////////////////////////
// Handles the incoming data by reflecting it back to the sender.

DWORD WINAPI EchoHandler(void * threadData_)
{
	char temp[50];
	int result;
	int nRetval = 0;
	T_THREAD_DATA threadData = *((T_THREAD_DATA *)threadData_);
	SOCKET sd = threadData.sd;

	// Clean up
	free(threadData_);

	if (!(result = EchoIncomingPackets(sd)))
	{
		logNow("\n%s\n", "Echo incoming packets failed");
		nRetval = 3;
    	}
	
	close_thread_dbh();

	logNow("Shutting connection down...");
    if (ShutdownConnection(sd, result))
		logNow("Connection is down.\n");
	else
	{
		logNow("\n%s\n", "Connection shutdown failed");
		nRetval = 3;
	}

	counterDecrement();
	logNow("\n%s:: Number of sessions left after closing (%ld.%ld.%ld.%ld:%d) = %d.\n", timeString(temp, sizeof(temp)), ntohl(threadData.sinRemote.sin_addr.s_addr) >> 24, (ntohl(threadData.sinRemote.sin_addr.s_addr) >> 16) & 0xff,
					(ntohl(threadData.sinRemote.sin_addr.s_addr) >> 8) & 0xff, ntohl(threadData.sinRemote.sin_addr.s_addr) & 0xff, ntohs(threadData.sinRemote.sin_port), counter);

	return nRetval;
}


//// AcceptConnections /////////////////////////////////////////////////
// Spins forever waiting for connections.  For each one that comes in, 
// we create a thread to handle it and go back to waiting for
// connections.  If an error occurs, we return.

void AcceptConnections(SOCKET ListeningSocket)
{
	T_THREAD_DATA threadData;
    int nAddrSize = sizeof(struct sockaddr_in);

    while (running)
	{
		threadData.sd = accept(ListeningSocket, (struct sockaddr *)&threadData.sinRemote, &nAddrSize);
        if (threadData.sd != INVALID_SOCKET)
		{
			char temp[50];
			T_THREAD_DATA * threadDataCopy;
			pthread_t thread;
			pthread_attr_t tattr;
			int status;

			counterIncrement();
			if( counter > 300 ) {
				logNow(	"\nToomany sessions blocking...%d,"
				"\n**************************"
				"\nExiting program now..."
				"\n**************************\n\n", counter);
				sleep(2);
				exit(1);
			}
			logNow("\n%s:: Received TCP packet from %ld.%ld.%ld.%ld:%d - Number of Sessions = %d\n", timeString(temp, sizeof(temp)), ntohl(threadData.sinRemote.sin_addr.s_addr) >> 24, (ntohl(threadData.sinRemote.sin_addr.s_addr) >> 16) & 0xff,
					(ntohl(threadData.sinRemote.sin_addr.s_addr) >> 8) & 0xff, ntohl(threadData.sinRemote.sin_addr.s_addr) & 0xff, ntohs(threadData.sinRemote.sin_port), counter);
			threadDataCopy = malloc(sizeof(T_THREAD_DATA));
			*threadDataCopy = threadData;

			pthread_attr_init(&tattr);
			pthread_attr_setdetachstate(&tattr, PTHREAD_CREATE_DETACHED);
			status = pthread_create(&thread, &tattr, (void *(*)(void*))EchoHandler, (void *) threadDataCopy);
			if (status)
			{
				logNow("pthread_create() failed with error number = %d", status);
				return;
			}
			pthread_attr_destroy(&tattr);

        }
        else
		{
            logNow("%s\n", "accept() failed");
            return;
        }
    }
}

//// SetUpListener /////////////////////////////////////////////////////
// Sets up a listener on the given interface and port, returning the
// listening socket if successful; if not, returns INVALID_SOCKET.
SOCKET SetUpListener(const char * pcAddress, int nPort)
{
	u_long nInterfaceAddr = inet_addr(pcAddress);

	if (nInterfaceAddr != INADDR_NONE)
	{
		SOCKET sd = socket(AF_INET, SOCK_STREAM, 0);
		if (sd != INVALID_SOCKET)
		{
			struct sockaddr_in sinInterface;
			int reuse;

			reuse = 1;
			setsockopt(sd, SOL_SOCKET, SO_REUSEADDR, (char*)&reuse, sizeof(reuse));

			sinInterface.sin_family = AF_INET;
			sinInterface.sin_addr.s_addr = nInterfaceAddr;
			sinInterface.sin_port = nPort;
			if (bind(sd, (struct sockaddr *)&sinInterface, sizeof(struct sockaddr_in)) != SOCKET_ERROR)
			{
				listen(sd, SOMAXCONN);
				return sd;
			}
			else
			{
				logNow("%s\n", "bind() failed");
			}
		}
	}

	return INVALID_SOCKET;
}

#ifndef WIN32
void signalHandler(int s)
{
	logNow("%s\n", 	"\n******************"
					"\ni-RIS stopping...."
					"\n******************\n");
	running = 0;
	sleep(5);

	//mysql_close(mysql);

	//pthread_mutex_destroy(&dbMutex);
	pthread_mutex_destroy(&counterMutex);
	pthread_mutex_destroy(&hsmMutex);

	logEnd();

	exit(0);
}
#endif

//// DoWinsock /////////////////////////////////////////////////////////
// The module's driver function -- we just call other functions and
// interpret their results.

int DoWinsock(const char * schema, const char * pcAddress, int nPort)
{
	char temp[50];
	SOCKET listeningSocket;

	logNow("\n\n%s:: Establishing the %s listener using port %d ...\n", timeString(temp, sizeof(temp)), schema, nPort);
	listeningSocket = SetUpListener(pcAddress, htons((u_short) nPort));
	if (listeningSocket == INVALID_SOCKET)
	{
		logNow("\n%s\n", "establish listener");
		return 3;
	}

	signal(SIGINT, signalHandler);
	signal(SIGUSR1, signalHandler);

	logNow("Waiting for connections...");
	AcceptConnections(listeningSocket);

	return 0;   // warning eater
}

//// main //////////////////////////////////////////////////////////////

int main(int argc, char* argv[])
{
	char * database = "i-ris";
	char * databaseIPAddress = "localhost";
	int databasePortNumber = 5432;
	char * serverIPAddress = "localhost";
	int serverPortNumber = 44555;
	char * user = "root";
	char * password = "password.01";
	int db_reconnect;
	time_t mytime;

	int arg;
	int nCode;
	int retval;
	char * unix_socket = NULL;

	// Initialisation
	pthread_mutex_init(&hsmMutex, NULL);
	pthread_mutex_init(&counterMutex, NULL);

	mytime = time(NULL);

	while((arg = getopt(argc, argv, "a:A:F:W:w:Q:q:z:Z:d:D:C:i:o:s:p:S:P:e:E:l:L:u:U:k:M:I:Y:x:X:G:BrRcmnNtHTh?")) != -1)
	{
		switch(arg)
		{
			case 'A':
				break;
			case 'B':
				break;
			case 'D':
				break;
			case 'F':
				break;
			case 'C':
				break;
			case 'c':
				break;
			case 'z':
				break;
			case 'Z':
				break;
			case 'd':
				database = optarg;
				break;
			case 'i':
				databaseIPAddress = optarg;
				break;
			case 'o':
				databasePortNumber = atoi(optarg);
				break;
			case 'M':
				break;
			case 'I':
				break;
			case 's':
				serverIPAddress = optarg;
				break;
			case 'p':
				serverPortNumber = atoi(optarg);
				break;
			case 'S':
				break;
			case 'P':
				break;
			case 'e':
				break;
			case 'E':
				break;
			case 'l':
				break;
			case 'L':
				logFile = optarg;
				break;
			case 'r':
				break;
			case 'm':
				dispMessage = 1;
				break;
			case 'n':
				break;
			case 't':
				break;
			case 'H':
				break;
			case 'a':
				break;
			case 'T':
				break;
			case 'u':
				user = optarg;
				break;
			case 'U':
				password = optarg;
				break;
			case 'k':
				unix_socket = optarg;
				break;
			case 'q':
				break;
			case 'Q':
				break;
			case 'w':
				break;
			case 'W':
				break;
			case 'R':
				break;
			case 'N':
				break;
			case 'x':
				break;
			case 'X':
				break;
			case 'G':
                                SetGomoUrl(optarg);
                                break;
			case 'h':
			case '?':
			default:
				printf(	"Usage: %s [-h=help] [-?=help] [-d database=i-ris] [-L logFileName] [-r=relaxSerialNumber]\n"
						"            [-n=no trace] [-i databaseIPAddress=localhost] [-o databasePortNumber=5432]\n"
						"            [-s serverIPAddress=localhost] [-p serverPortNumber=44555]\n"
						"            [-u databaseUserID=root] [-U databaseuserPassword=password.01]\n"
						"            [-q revIPAddress=localhost] [-Q revPortNumber=32001]\n"
						"            [-w rewardIPAddress=localhost] [-W rewardPortNumber=32002]\n"
						"            [-S deviceGatewayIPAddress=localhost] [-P deviceGatewayPortNumber]\n", argv[0]);
				exit(-1);
		}
	}

	{
			MYSQL *dbh = NULL;
			// mysql initialisation
			if ((dbh = mysql_init(NULL)) == NULL)
			{
				printf("MySql Initialisation error. Exiting...\n");
				exit(-1);
			}

			// mysql database connection options
			db_reconnect = 1;
			mysql_options(dbh, MYSQL_OPT_RECONNECT, &db_reconnect);

			// mysql database connection
			if (!mysql_real_connect(dbh, databaseIPAddress, user, password, database, 0, unix_socket, 0))
			{
				fprintf(stderr, "%s\n", db_error(dbh, res));
				exit(-2);
			}
			set_db_connect_param( databaseIPAddress, user, password, database, unix_socket);


			mysql_close(dbh);
	}

	// Start the log & counter
	logStart();

    // Call the main example routine.
	retval = DoWinsock(database, serverIPAddress, serverPortNumber);

	// Shut Winsock back down and take off.

	logNow(	"\n**************************"
		"\nExiting program now..."
		"\n**************************\n\n");

	pthread_mutex_destroy(&counterMutex);
	pthread_mutex_destroy(&hsmMutex);

	logEnd();

	return retval;
}
