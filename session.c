/*
 * SALLOC - simple thread-specific data allocator.
 * salloc.c,v 1.1 2013/02/11 10:32:28 boyang $	
 */
#include <stdlib.h>
#include <pthread.h>

#ifdef USE_MYSQL
#include <mysql.h>
#endif

static int initialised = 0;
static pthread_key_t threadSpecificKey;

typedef struct
{
#ifdef USE_MYSQL
        MYSQL * dbh;
#endif
        int thread_id; //not used
} sSession, *psSession;

static char *databaseIPAddress=NULL; 
static char *user=NULL; 
static char *password=NULL; 
static char *database=NULL; 
static char *unix_socket=NULL;


/*
 * Free the thread-specific session data.
 * This is called by the OS when the thread is about to die.
 */
static void cleanup(void *data)
{
	if (data)
	{
		psSession sessData = pthread_getspecific(threadSpecificKey);
		if(sessData && sessData->dbh) {
			mysql_close(sessData->dbh);
		}
		logNow("pthread cleanup\n");
		free(data);
	}
	pthread_setspecific(threadSpecificKey, NULL);
}
/*
 * Initialise the thread-specific storage
 */
static int sallocInit(void)
{
	int r = 0;
	if (!initialised)
	{
		r = pthread_key_create(&threadSpecificKey, cleanup);
		if (0 == r)
		{
			initialised = 1;
		}
	}
	return r;
}

/*
 * Allocate memory and associate the new block with the 
 * current thread.
 */
void * salloc(size_t size)
{
	void * p;

	if (0 == sallocInit())
	{
		p = calloc(size, 1);
		if (p)
		{
			pthread_setspecific(threadSpecificKey, p);
			return p;
		}
	}
	return NULL;
}

void * sgetalloc(void)
{
	if (0 == sallocInit())
	{
		return pthread_getspecific(threadSpecificKey);
	}
	return NULL;
}

/*
 * reallocate the memory already associated
 */
static void * srealloc(size_t newsize)
{
	void *p;

	if (0 == sallocInit())
	{
		p = pthread_getspecific(threadSpecificKey);
		if (p)
		{
			p = realloc(p, newsize);
			if (p)
			{
				pthread_setspecific(threadSpecificKey, p);
				return p;
			}
		}
	}
	return NULL;
}

void sfree(void ** p)
{
	if (p && *p)
	{
		cleanup(*p);
		*p = 0;
	}	
}

MYSQL * get_new_mysql_dbh()
{

	MYSQL *dbh = NULL;

        // mysql initialisation
        if ((dbh = mysql_init(NULL)) == NULL)
        {
		logNow("mysql init error: [%s]\n", mysql_error(dbh));
                return(NULL);
        }

        // mysql database connection
        if (!mysql_real_connect(dbh, databaseIPAddress, user, password, database, 0, unix_socket, 0))
        {
		logNow("mysql real connect error: [%s]\n", mysql_error(dbh));
                return(NULL);
        }

	return(dbh);
}

void * set_thread_dbh()
{
	psSession sessData = NULL;
	sessData = salloc( sizeof (sSession ));

	if( !sessData ) return( NULL);

	sessData->dbh = get_new_mysql_dbh();
	return(sessData->dbh );
}

void * get_thread_dbh()
{
	psSession sessData = pthread_getspecific(threadSpecificKey);

	if( sessData && sessData->dbh )
		return(sessData->dbh);
	else return(NULL);

}

int close_thread_dbh()
{
	psSession sessData = pthread_getspecific(threadSpecificKey);

	if( sessData && sessData->dbh ) {
		mysql_close(sessData->dbh);
		sessData->dbh = NULL;
		logNow("pthread close thread dbh\n");
	}

	return(0);

}

int set_db_connect_param( char *conn_databaseIPAddress, char *conn_user, char *conn_password, char *conn_database, char *conn_unix_socket)
{
	databaseIPAddress =  conn_databaseIPAddress;
	user = conn_user;
	password = conn_password; 
	database= conn_database; 
	unix_socket= conn_unix_socket;
	sallocInit();

	return(0);
}
