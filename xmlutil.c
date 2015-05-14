
#include <stdio.h>
#include <string.h>
#include "libxml/xmlreader.h"
#include "libxml/xmlwriter.h"

int getNextElement( xmlTextReaderPtr reader, char* name, int*dep)
{
	const char *tmpname=NULL;
	int ret = 0;
	strcpy( name, "");
	*dep = -1;

	while( (ret = xmlTextReaderRead(reader)) == 1)
	{
		int nodetype = xmlTextReaderNodeType(reader);
		if( nodetype == XML_READER_TYPE_ELEMENT )
		{
			tmpname = (const char *)xmlTextReaderConstName(reader);
			*dep = xmlTextReaderDepth(reader);
			strcpy( name, tmpname);
			return(1);
		}
	}

	return(0);
}

int getAttribute( xmlTextReaderPtr reader, const char* name, char* value)
{
	char *tmpvalue = NULL;
	tmpvalue = (char *)xmlTextReaderGetAttribute(reader, (const xmlChar *)name);
	strcpy( value , "");

	if(tmpvalue == NULL) return(0);
	strcpy( value, tmpvalue );
	xmlFree(tmpvalue);
	return(1);
}

int getElementTextPtr( xmlTextReaderPtr reader, char* value)
{

	int ret = 0;
	const char *tmpvalue ;

	while( (ret = xmlTextReaderRead(reader)) == 1)
	{
		int nodetype = xmlTextReaderNodeType(reader);
		if( nodetype == XML_READER_TYPE_END_ELEMENT ) break;
		if( nodetype == XML_READER_TYPE_TEXT )
		{
			tmpvalue = (const char *)xmlTextReaderConstValue(reader);
			value =  (char*)tmpvalue;
			return(1);
		}
	}
	return(0);
}

int getElementTextValue( xmlTextReaderPtr reader, char* value)
{

	int ret = 0;
	const char *tmpvalue ;

	while( (ret = xmlTextReaderRead(reader)) == 1)
	{
		int nodetype = xmlTextReaderNodeType(reader);
		if( nodetype == XML_READER_TYPE_END_ELEMENT ) break;
		if( nodetype == XML_READER_TYPE_TEXT )
		{
			tmpvalue = (const char *)xmlTextReaderConstValue(reader);
			strcpy( value, tmpvalue );
			return(1);
		}
	}
	return(0);
}

int StartElement( xmlTextWriterPtr writer, char *name)
{
	int rc = xmlTextWriterStartElement(writer, BAD_CAST name);
	if (rc < 0) 
		{
		return(0);
		}
	return(1);
}

int WriteElement( xmlTextWriterPtr writer,char *name, char* value)
{
	int rc = 0;

	if(value == NULL || strlen(value) == 0) 
		return(1);
	rc = xmlTextWriterWriteElement(writer, BAD_CAST name, BAD_CAST value);

	if (rc < 0) 
	{
		return(0);
	}
	return(1);
}

int EndElement( xmlTextWriterPtr writer)
{
	int rc = xmlTextWriterEndElement(writer);
	if (rc < 0) 
	{
		return(0);
	}
	return(1);
}

int WriteAttribute( xmlTextWriterPtr writer, char *name, char* value)
{
	int rc =xmlTextWriterWriteAttribute(writer, BAD_CAST name, BAD_CAST value);
	if (rc < 0) 
	{
    	return(0);
	}
	return(1);
}


