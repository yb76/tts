#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <curl/curl.h>
#include <pthread.h>
#include "jsmn.h"


static char sUrl_gomo[256]="";
static char sUrl_heartbeat[256]="";
static char sUrl_bookinglist[256]="";
static char sUrl_newbookinglist[256]="";
static char sUrl_bookingaccept[256]="";
static char sUrl_bookingrelease[256]="";
static char sUrl_message[256]="";
static char sUrl_tripstart[256]="";
static char sUrl_paymentrequest[256]="";
static char sUrl_paymentstatus[256]="";
static char sUrl_tripfinished[256]="";

struct MemoryStruct {
    char *memory;
    size_t size;
  };

static int jsoneq(const char *json, jsmntok_t *tok, const char *s) {
        if (tok->type == JSMN_STRING && (int) strlen(s) == tok->end - tok->start &&
                        strncmp(json + tok->start, s, tok->end - tok->start) == 0) {
                return 0;
        }
        return -1;
}


static void
stripQuotes (char *src, char *dest)
{
    int withinQuotes = 0;
    unsigned int i, j;

    for (i = 0, j = 0; i < strlen (src); i++)
    {
        // Detect any quotes
        if (src[i] == '"')
        {
            withinQuotes = !withinQuotes;
            continue;
        }

        if (!withinQuotes
                && (src[i] == ' ' || src[i] == '\t' || src[i] == '\n'
                    || src[i] == '\r'))
            continue;

        // Add the character
        dest[j++] = src[i];
    }

    dest[j] = '\0';
}

void *myrealloc(void *ptr, size_t size)
 {
   /* There might be a realloc() out there that doesn't like reallocing
      NULL pointers, so we take care of it here */
   if(ptr){
     return realloc(ptr, size);
   }
   else
     return calloc(size,1);
 }
 
size_t
WriteMemoryCallback(void *ptr, size_t size, size_t nmemb, void *data)
 {
   size_t realsize = size * nmemb;
   struct MemoryStruct *mem = (struct MemoryStruct *)data;
 
   mem->memory = (char *)myrealloc(mem->memory, mem->size + realsize + 1);
   if (mem->memory) {
     memcpy(&(mem->memory[mem->size]), ptr, realsize);
     mem->size += realsize;
     mem->memory[mem->size] = 0;
   }
   return realsize;
 }

int irisGomo_call(char *url,char* calltype,char *cli_string,char **resp)
{
  CURL *curl;
  CURLcode res;
  struct MemoryStruct chunk;
  chunk.memory = malloc(1);  /* will be grown as needed by the realloc above */ 
  strcpy(chunk.memory,"");
  chunk.size = 0;    /* no data at this point */ 

  if(cli_string==NULL || strlen(cli_string)==0) {
    	if(chunk.memory) free(chunk.memory);
	return(-1);
  }

  if(calltype==NULL || strlen(calltype)==0 || ( strcmp(calltype,"POST") && strcmp(calltype,"PUT") && strcmp(calltype,"GET"))) {
    	if(chunk.memory) free(chunk.memory);
	chunk.memory = NULL;
	return(-1);
  }

  long http_code = 0;
  curl = curl_easy_init();
  if(curl) {
 /* we want to use our own read function */ 
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);//setup curl connection with preferences
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, calltype);
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, cli_string);
curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);

    /* Now run off and do what you've been told! */ 
    res = curl_easy_perform(curl);
    /* Check for errors */ 
    if(res != CURLE_OK) {
      logNow( "GOMO:curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
      http_code = -1;
   }
   else {
    /*
     * Now, our chunk.memory points to a memory block that is chunk.size
     * bytes big and contains the remote file.
     *
     * Do something nice with it!
     */ 
 
      curl_easy_getinfo (curl, CURLINFO_RESPONSE_CODE, &http_code);
      logNow("GOMO:%lu bytes retrieved[%d,%.200s]\n",(long)chunk.size, http_code,chunk.memory);
    }
    if(chunk.memory) *resp = chunk.memory;
    else *resp = NULL;
 
    /* always cleanup */ 
    curl_easy_cleanup(curl);
  }

  return(http_code);
}

/*
int irisGomo_get_id(char *tid ,char* gomo_driverid, char *gomo_terminalid)
{
	strcpy(gomo_driverid, "TA0001");
	strcpy(gomo_terminalid, "00000001");
	return(0);
}
*/


int irisGomo_init()
{
  static int inited = 0;

  if(inited) return(0);
  else inited = 1;

	//char prefix[256] = "http://dev.terminal.gm-mobile.com:80/v2";
	char prefix[256] = "";

	if(strlen(sUrl_gomo))
		strcpy(prefix,sUrl_gomo);
	else {
		strcpy(prefix,"https://terminal.gm-mobile.com/v2"); //prod
		//strcpy(prefix,"http://dev.terminal.gm-mobile.com:80/v2"); //test
	}

	sprintf(sUrl_heartbeat,"%s/heartbeat/heartbeat",prefix);
	sprintf(sUrl_newbookinglist,"%s/bookings/new-bookings",prefix);
	sprintf(sUrl_bookinglist,"%s/bookings/bookings",prefix);
	sprintf(sUrl_bookingaccept,"%s/bookings/accept",prefix);
	sprintf(sUrl_bookingrelease,"%s/bookings/release",prefix);
	sprintf(sUrl_message,"%s/messaging/message-passenger",prefix);
	sprintf(sUrl_tripstart,"%s/trips/start",prefix);
	sprintf(sUrl_paymentrequest,"%s/payments/request",prefix);
	sprintf(sUrl_paymentstatus,"%s/payments/status",prefix);
	sprintf(sUrl_tripfinished,"%s/trips/finished",prefix);

	curl_global_init(CURL_GLOBAL_DEFAULT);
	
	return(0);
}

int irisGomo_convert_tag(char *tag)
{
	if(tag==NULL) return(0);
	if(strcmp(tag,"nextAvailable")==0) {
		strcpy(tag,"");
	}
	else if(strcmp(tag,"schedule")==0) {
		strcpy(tag,"");
	}
	else if(strcmp(tag,"passengerCount")==0) {
		strcpy(tag,"pc");
	}
	else if(strcmp(tag,"fromSuburb")==0) {
		strcpy(tag,"fs");
	}
	else if(strcmp(tag,"toSuburb")==0) {
		strcpy(tag,"ts");
	}
	else if(strcmp(tag,"passengerName")==0) {
		strcpy(tag,"nm");
	}

}

int irisGomo_convertJson_bookinglist(char *ser_string)
{
	char json[10240] = "";
	char iris_json[10240] = "";
	jsmn_parser p;
	jsmntok_t t[1024]; /* We expect no more than 1024 tokens */
	int i = 0,j=1;
	int icnt = 0;
	int booking_cnt = 0;

	strcpy( json,ser_string);
	jsmn_init(&p);
	icnt = jsmn_parse(&p, json, strlen(json), t, sizeof(t)/sizeof(t[0]));
	if (icnt < 0) {
		logNow("GOMO:Failed to parse JSON: %d\n", icnt);
		return 1;
	}

	/* Assume the top-level element is an object or array */
	if (icnt < 1 ) {
		return -1;
	}

	if(t[0].type == JSMN_OBJECT) {
		j = 1;
		booking_cnt = 1;
	}
	else if(t[0].type == JSMN_ARRAY) {
		j = 0;
		booking_cnt = t[0].size;
	} else {
		return(-1);
	}

	sprintf(iris_json,"{TYPE:DATA,NAME:GPS_RESP,STEP:BL,COUNT:%d",booking_cnt); 

	for(i=1;i<icnt;i++) {
		char irisjson_tag[128]="";
		char irisjson_value[128]="";
		char stmp[128];

		if( t[i].type == JSMN_PRIMITIVE) {
			sprintf(stmp,"%.*s",t[i].end-t[i].start,json+t[i].start);
			sprintf(irisjson_value,"%s",stmp);
		} else if ( t[i].type == JSMN_OBJECT) {
			j++;
		} else if ( t[i].type == JSMN_ARRAY) {
		} else if ( t[i].type == JSMN_STRING) {
			sprintf(stmp,"%.*s",t[i].end-t[i].start,json+t[i].start);

			if( t[i].size == 1) {
				sprintf(irisjson_tag,"%s",stmp);
			} else {
				sprintf(irisjson_value,"%s",stmp);
			}
		}
		if(strlen(irisjson_tag)) {
			irisGomo_convert_tag(irisjson_tag);
			if(strlen(irisjson_tag)) {
				sprintf(stmp,",%s_%d:", irisjson_tag, j);
				strcat(iris_json,stmp);
			} else i++; // tag is not needed
		}

		if(strlen(irisjson_value)) {
			strcat(iris_json,irisjson_value);
		}
		
	}
	strcat(iris_json,"}");

	//logNow("newjson = [%s]", iris_json);
	strcpy(ser_string,iris_json);
	return(0);
}

int irisGomo_convertJson_newbookinglist(char *ser_string)
{
	char json[10240] = "";
	char iris_json[10240] = "";
	jsmn_parser p;
	jsmntok_t t[1024]; /* We expect no more than 1024 tokens */
	int i = 0,j=0,k=1;
	int icnt = 0;
	int booking_cnt = 0;
	int inArrayCurrent=0;
	int inArrayNew=0;

	strcpy( json,ser_string);
	jsmn_init(&p);
	icnt = jsmn_parse(&p, json, strlen(json), t, sizeof(t)/sizeof(t[0]));
	if (icnt < 0) {
		logNow("GOMO:Failed to parse JSON: %d\n", icnt);
		return 1;
	}

	/* Assume the top-level element is an object or array */
	if (icnt < 1 ) {
		return -1;
	}

	if(t[0].type == JSMN_OBJECT && t[1].type == JSMN_STRING && t[2].type == JSMN_ARRAY) {
	        //t[1] : "currentBookings"
		booking_cnt = t[2].size;
	} else {
		return(-1);
	}

	sprintf(iris_json,"{TYPE:DATA,NAME:GPS_RESP,STEP:NB,COUNT:%d",booking_cnt); 

	// get current booking
	for(i=1;i<icnt;i++) {
		char irisjson_tag[128]="";
		char irisjson_value[128]="";
		char stmp[128];

		if( t[i].type == JSMN_PRIMITIVE) {
		} else if ( t[i].type == JSMN_OBJECT) {
		} else if ( t[i].type == JSMN_ARRAY) {

		} else if ( t[i].type == JSMN_STRING) {
			char sType[64];
			sprintf(sType,"%.*s",t[i].end-t[i].start,json+t[i].start);
			if(strcmp(sType,"currentBookings")==0) {
				i++;
				booking_cnt == t[i].size;	//array
				i++;
				for(k=1;k<=booking_cnt;k++,i++) {
					if( t[i].type == JSMN_PRIMITIVE) {
						sprintf(stmp,"%.*s",t[i].end-t[i].start,json+t[i].start);
						sprintf(irisjson_value,",oid_%d:%s",k,stmp);
						strcat(iris_json,irisjson_value);
					}
				}
				if(booking_cnt>=1) i--;

			}
			else if(strcmp(sType,"newBookings")==0) {
				i++;
				for(k=1;i<icnt;k++,i++) { // to the end
					strcpy(irisjson_value,"");
					strcpy(irisjson_tag,"");

					if( t[i].type == JSMN_PRIMITIVE) {
						sprintf(stmp,"%.*s",t[i].end-t[i].start,json+t[i].start);
						sprintf(irisjson_value,"%s",stmp);
					} else if ( t[i].type == JSMN_OBJECT) {
						j++;
					} else if ( t[i].type == JSMN_ARRAY) {
					} else if ( t[i].type == JSMN_STRING) {
						sprintf(stmp,"%.*s",t[i].end-t[i].start,json+t[i].start);
						if( t[i].size == 1) {
							sprintf(irisjson_tag,"%s",stmp);
						} else {
							char *p = stmp;
							while(*p!=0) {
								if(*p == ',') *p = '%'; //remove comma
								p++;
							}
							sprintf(irisjson_value,"%s",stmp);
						}
					}
					if(strlen(irisjson_tag)) {
						irisGomo_convert_tag(irisjson_tag);
						if(strlen(irisjson_tag)) {
							sprintf(stmp,",%s_%d:", irisjson_tag,j);
							strcat(iris_json,stmp);
						} else i++;
					}

					if(strlen(irisjson_value)) {
						strcat(iris_json,irisjson_value);
					}

				}

			}

		}

	}

	strcat(iris_json,"}");

	//logNow("newjson = [%s]", iris_json);
	strcpy(ser_string,iris_json);
	return(0);
}

int irisGomo_convertJson_bookingaccept(char *ser_string)
{
	char json[10240] = "";
	char iris_json[10240] = "";
	jsmn_parser p;
	jsmntok_t t[1024]; /* We expect no more than 1024 tokens */
	int i = 0,j=1;
	int icnt = 0;

	strcpy( json,ser_string);
	jsmn_init(&p);
	icnt = jsmn_parse(&p, json, strlen(json), t, sizeof(t)/sizeof(t[0]));
	if (icnt < 0) {
		logNow("GOMO:Failed to parse JSON: %d\n", icnt);
		return 1;
	}

	/* Assume the top-level element is an object or array */
	if (icnt < 1 ) {
		return -1;
	}

	if(t[0].type == JSMN_OBJECT) {
	}
	else if(t[0].type == JSMN_ARRAY) {
	} else {
		return(-1);
	}

	sprintf(iris_json,"{TYPE:DATA,NAME:GPS_RESP,STEP:BA");

	for(i=1;i<icnt;i++) {
		char irisjson_tag[128]="";
		char irisjson_value[128]="";
		char stmp[128];

		if( t[i].type == JSMN_PRIMITIVE) {
			sprintf(stmp,"%.*s",t[i].end-t[i].start,json+t[i].start);
			sprintf(irisjson_value,"%s",stmp);
		} else if ( t[i].type == JSMN_OBJECT) {
		} else if ( t[i].type == JSMN_ARRAY) {
		} else if ( t[i].type == JSMN_STRING) {
			sprintf(stmp,"%.*s",t[i].end-t[i].start,json+t[i].start);

			if( t[i].size == 1) {
				sprintf(irisjson_tag,"%s",stmp);
			} else {
				char *p = stmp;
				while(*p!=0) {
					if(*p == ',') *p = '%'; //remove comma
					p++;
				}
				sprintf(irisjson_value,"%s",stmp);
			}
		}
		if(strlen(irisjson_tag)) {
			irisGomo_convert_tag(irisjson_tag);
			if(strlen(irisjson_tag)) {
				sprintf(stmp,",%s:", irisjson_tag);
				strcat(iris_json,stmp);
			} else i++;
		}
		if(strlen(irisjson_value)) {
			strcat(iris_json,irisjson_value);
		}
		
	}
	strcat(iris_json,"}");

	//logNow("newjson = [%s]", iris_json);
	strcpy(ser_string,iris_json);
	return(0);
}

int irisGomo_convertJson(char *step,char *ser_string)
{
	char json[10240] = "";
	char iris_json[10240] = "";
	jsmn_parser p;
	jsmntok_t t[1024]; /* We expect no more than 1024 tokens */
	int i = 0,j=1;
	int icnt = 0;

	strcpy( json,ser_string);
	jsmn_init(&p);
	icnt = jsmn_parse(&p, json, strlen(json), t, sizeof(t)/sizeof(t[0]));
	if (icnt < 0) {
		logNow("GOMO:Failed to parse JSON: %d\n", icnt);
		return 1;
	}

	/* Assume the top-level element is an object or array */
	if (icnt < 1 ) {
		return -1;
	}

	if(t[0].type == JSMN_OBJECT) {
	} else {
		return(-1);
	}

	sprintf(iris_json,"{TYPE:DATA,NAME:GPS_RESP,STEP:%s,ERRORCODE:200",step);

	for(i=1;i<icnt;i++) {
		char irisjson_tag[128]="";
		char irisjson_value[128]="";
		char stmp[128];

		if( t[i].type == JSMN_PRIMITIVE) {
			sprintf(stmp,"%.*s",t[i].end-t[i].start,json+t[i].start);
			sprintf(irisjson_value,"%s",stmp);
		} else if ( t[i].type == JSMN_OBJECT) {
		} else if ( t[i].type == JSMN_ARRAY) {
		} else if ( t[i].type == JSMN_STRING) {
			sprintf(stmp,"%.*s",t[i].end-t[i].start,json+t[i].start);

			if( t[i].size == 1) {
				sprintf(irisjson_tag,"%s",stmp);
			} else {
				char *p = stmp;
				while(*p!=0) {
					if(*p == ',') *p = '%'; //remove comma
					p++;
				}
				sprintf(irisjson_value,"%s",stmp);
			}
		}
		if(strlen(irisjson_tag)) {
			irisGomo_convert_tag(irisjson_tag);
			if(strlen(irisjson_tag)) {
				sprintf(stmp,",%s:", irisjson_tag);
				strcat(iris_json,stmp);
			} else i++;
		}
		if(strlen(irisjson_value)) {
			strcat(iris_json,irisjson_value);
		}
		
	}
	strcat(iris_json,"}");

	//logNow("newjson = [%s]", iris_json);
	strcpy(ser_string,iris_json);
	return(0);
}

int irisGomo_heartbeat(char *cli_string,char *ser_string)
{
	int iret = 0;
	char *resp = NULL;

	strcpy(ser_string,"");
	logNow("GOMO: heartbeat sending[%s]\n", cli_string);
	if(strlen(sUrl_heartbeat)==0) irisGomo_init();

	iret =  irisGomo_call(sUrl_heartbeat,"PUT",cli_string,&resp);
	if(iret==200) {
		if(resp) {
			strcpy(ser_string,resp);
			if(strlen(ser_string)) {
				irisGomo_convertJson("HB",ser_string);
			}
			logNow("GOMO: heartbeat recv[%s]\n", ser_string);
		}
		
	}
	else if(iret>0) {
		if(resp) {
			sprintf(ser_string,"%.100s",resp);
			char *p = ser_string;
			while(*p!=0) {
				if(*p == ',') *p = ' '; //remove comma
				p++;
			}
		}
	}
	if(resp) {
		free(resp);
	}
	return(iret);

}

int irisGomo_bookinglist(char *cli_string,char *ser_string)
{
	int iret = 0;
	char *resp = NULL;

	strcpy(ser_string,"");
	logNow("GOMO: bookinglist sending[%s]\n", cli_string);
	if(strlen(sUrl_bookinglist)==0) irisGomo_init();

	iret =  irisGomo_call(sUrl_bookinglist,"GET",cli_string,&resp);
	if(iret==200) {
		if(resp) {
			//stripQuotes(resp, ser_string);
			strcpy(ser_string,resp);
			logNow("GOMO: bookinglist recv[%s]\n", ser_string);
			if(strlen(ser_string)) {
				irisGomo_convertJson_bookinglist(ser_string);
			}
		}
		
	}
	else if(iret>0) {
		if(resp) {
			sprintf(ser_string,"%.100s",resp);
			char *p = ser_string;
			while(*p!=0) {
				if(*p == ',') *p = ' '; //remove comma
				p++;
			}
		}
	}
	if(resp) {
		free(resp);
		resp = NULL;
	}
	return(iret);

}

int irisGomo_newbookinglist(char *cli_string,char *ser_string)
{
	int iret = 0;
	char *resp = NULL;

	strcpy(ser_string,"");
	logNow("GOMO: new bookinglist sending[%s]\n", cli_string);
	if(strlen(sUrl_newbookinglist)==0) irisGomo_init();

	iret =  irisGomo_call(sUrl_newbookinglist,"GET",cli_string,&resp);
	if(iret==200) {
		if(resp) {
			strcpy(ser_string,resp);
			logNow("GOMO: new bookinglist recv[%s]\n", ser_string);
			if(strlen(ser_string)) {
				irisGomo_convertJson_newbookinglist(ser_string);
			}
		}
		
	}
	else if(iret>0) {
		if(resp) {
			sprintf(ser_string,"%.100s",resp);
			char *p = ser_string;
			while(*p!=0) {
				if(*p == ',') *p = ' '; //remove comma
				p++;
			}
		}
	}
	if(resp) {
		free(resp);
		resp = NULL;
	}
	return(iret);

}

int irisGomo_bookingaccept(char *booking_id,char *cli_string,char *ser_string)
{
	int iret = 0;
	char url[256] = "";
	char *resp = NULL;

	strcpy(ser_string,"");
	logNow("GOMO: bookingaccept sending[%s]\n", cli_string);
	if(strlen(sUrl_bookingaccept)==0) irisGomo_init();
	sprintf(url,"%s/%s",sUrl_bookingaccept, booking_id);

	iret =  irisGomo_call(url,"PUT",cli_string,&resp);
	if(iret==200) {
		if(resp) {
			//stripQuotes(resp, ser_string);
			strcpy(ser_string,resp);
			if(strlen(ser_string)) {
				irisGomo_convertJson_bookingaccept(ser_string);
			}
			logNow("GOMO: bookingaccept recv[%s]\n", ser_string);
		}
		
	}
	else if(iret>0) {
		if(resp) {
			sprintf(ser_string,"%.100s",resp);
			char *p = ser_string;
			while(*p!=0) {
				if(*p == ',') *p = ' '; //remove comma
				p++;
			}
		}
	}
	if(resp) {
		free(resp);
		resp = NULL;
	}
	return(iret);
}

int irisGomo_bookingrelease(char *cli_string,char *ser_string)
{
	int iret = 0;
	char url[256] = "";
	char *resp = NULL;

	strcpy(ser_string,"");
	logNow("GOMO: bookingrelease sending[%s]\n", cli_string);

	if(strlen(sUrl_bookingrelease)==0) irisGomo_init();

	iret =  irisGomo_call(sUrl_bookingrelease,"PUT",cli_string,&resp);
	if(iret==200) {
		if(resp) {
			strcpy(ser_string,resp);
			char *p = ser_string;
			while(*p!=0) {
				if(*p == ',') *p = ' '; //remove comma
				p++;
			}
		}
	}

	if(resp) {
		free(resp);
		resp = NULL;
	}
	return(iret);
}

int irisGomo_message(char *cli_string,char *ser_string)
{
	int iret = 0;
	char url[256] = "";
	char *resp = NULL;

	strcpy(ser_string,"");
	logNow("GOMO: message-passenger sending[%s]\n", cli_string);

	if(strlen(sUrl_message)==0) irisGomo_init();

	iret =  irisGomo_call(sUrl_message,"POST",cli_string,&resp);
	if(iret>0) {
		if(resp) {
			strcpy(ser_string,resp);
			char *p = ser_string;
			while(*p!=0) {
				if(*p == ',') *p = ' '; //remove comma
				p++;
			}
		}
	}

	if(resp) {
		free(resp);
		resp = NULL;
	}
	return(iret);
}

int irisGomo_tripstart(char *cli_string,char *ser_string)
{
	int iret = 0;
	char url[256] = "";
	char *resp = NULL;

	strcpy(ser_string,"");
	logNow("GOMO: tripstart sending[%s]\n", cli_string);

	if(strlen(sUrl_tripstart)==0) irisGomo_init();

	iret =  irisGomo_call(sUrl_tripstart,"PUT",cli_string,&resp);
	if(iret>0) {
		if(resp) {
			strcpy(ser_string,resp);
			char *p = ser_string;
			while(*p!=0) {
				if(*p == ',') *p = ' '; //remove comma
				p++;
			}
		}
	}

	if(resp) {
		free(resp);
		resp = NULL;
	}
	return(iret);
}

int irisGomo_paymentrequest(char *cli_string,char *ser_string)
{
	int iret = 0;
	char *resp = NULL;

	strcpy(ser_string,"");
	logNow("GOMO: payment request sending[%s]\n", cli_string);

	if(strlen(sUrl_paymentrequest)==0) irisGomo_init();

	iret =  irisGomo_call(sUrl_paymentrequest,"POST",cli_string,&resp);
	if(iret>0) {
		if(resp) {
			strcpy(ser_string,resp);
			char *p = ser_string;
			while(*p!=0) {
				if(*p == ',') *p = ' '; //remove comma
				p++;
			}
		}
	}

	if(resp) {
		free(resp);
		resp = NULL;
	}
	return(iret);
}

int irisGomo_paymentstatus(char *cli_string,char *ser_string)
{
	int iret = 0;
	char *resp = NULL;

	strcpy(ser_string,"");
	logNow("GOMO: payment status sending[%s]\n", cli_string);

	if(strlen(sUrl_paymentstatus)==0) irisGomo_init();

	iret =  irisGomo_call(sUrl_paymentstatus,"GET",cli_string,&resp);
	if(iret==200) {
		if(resp) {
			strcpy(ser_string,resp);
			if(strlen(ser_string)) {
				irisGomo_convertJson("PS",ser_string);
			}
			logNow("GOMO:payment status recv[%s]\n", ser_string);
		}
	} else if (iret>0) {
		if(resp) {
			sprintf(ser_string,"%.100s",resp);
			char *p = ser_string;
			while(*p!=0) {
				if(*p == ',') *p = ' '; //remove comma
				p++;
			}
		}
	}

	if(resp) {
		free(resp);
		resp = NULL;
	}
	return(iret);
}

int irisGomo_tripfinished(char *cli_string,char *ser_string)
{
	int iret = 0;
	char *resp = NULL;

	strcpy(ser_string,"");
	logNow("GOMO: trip finished sending[%s]\n", cli_string);

	if(strlen(sUrl_tripfinished)==0) irisGomo_init();

	iret =  irisGomo_call(sUrl_tripfinished,"PUT",cli_string,&resp);
	if(iret>0) {
		if(resp) {
			strcpy(ser_string,resp);
			char *p = ser_string;
			while(*p!=0) {
				if(*p == ',') *p = ' '; //remove comma
				p++;
			}
		}
	}

	if(resp) {
		free(resp);
		resp = NULL;
	}
	return(iret);
}

char * SetGomoUrl(char* url)
{
	if(url&&strlen(url))
		strcpy(sUrl_gomo,url);
	return(sUrl_gomo);
}
