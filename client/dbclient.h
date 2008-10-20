// dbclient.h - connect to a Mongo database as a client, from C++

/**
*    Copyright (C) 2008 10gen Inc.
*  
*    This program is free software: you can redistribute it and/or  modify
*    it under the terms of the GNU Affero General Public License, version 3,
*    as published by the Free Software Foundation.
*  
*    This program is distributed in the hope that it will be useful,
*    but WITHOUT ANY WARRANTY; without even the implied warranty of
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*    GNU Affero General Public License for more details.
*  
*    You should have received a copy of the GNU Affero General Public License
*    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#pragma once

#include "../grid/message.h"
#include "../db/jsobj.h"

/* the query field 'options' can have these bits set: */
enum { 
    /* Tailable means cursor is not closed when the last data is retrieved.  rather, the cursor marks
       the final object's position.  you can resume using the cursor later, from where it was located, 
       if more data were received.  Set on dbQuery and dbGetMore.

       like any "latent cursor", the cursor may become invalid at some point -- for example if that 
       final object it references were deleted.  Thus, you should be prepared to requery if you get back 
       ResultFlag_CursorNotFound.
    */
    Option_CursorTailable = 2,

    /* allow query of replica slave.  normally these return an error except for namespace "local".
    */
    Option_SlaveOk = 4,

    Option_ALLMASK = 6
};

class JSObj;

#pragma pack(push,1)
struct QueryResult : public MsgData {
	long long cursorId;
	int startingFrom;
	int nReturned;
	const char *data() { return (char *) (((int *)&nReturned)+1); }
	int& resultFlags() { return dataAsInt(); }
};
#pragma pack(pop)

class DBClientConnection;
class DBClientCursor : boost::noncopyable { 
	friend class DBClientConnection;
	DBClientConnection *conn;
	MessagingPort& p;
	long long cursorId;
	int nReturned;
	int pos;
	const char *data;
	auto_ptr<Message> m;
    int opts;
	string ns;
	int nToReturn;
	void dataReceived();
	void requestMore();

	DBClientCursor(DBClientConnection *_conn, MessagingPort& _p, auto_ptr<Message> _m, int _opts) : 
      conn(_conn), p(_p), m(_m), opts(_opts) { 
          cursorId = 0;
          dataReceived(); 
      }

public:

	bool more(); // if true, safe to call next()

    /* returns next object in the result cursor.
       on an error at the remote server, you will get back:
         { $err: <string> }
       if you do not want to handle that yourself, call nextSafe().
    */
	JSObj next(); 

    JSObj nextSafe() { 
        JSObj o = next();
        Element e = o.firstElement();
        assert( strcmp(e.fieldName(), "$err") != 0 );
    }

	// cursor no longer valid -- use with tailable cursors.
	// note you should only rely on this once more() returns false; 
	// 'dead' may be preset yet some data still queued and locally
	//  available from the dbclientcursor.
	bool isDead() const { return cursorId == 0; }

	bool tailable() const { return (opts & Option_CursorTailable) != 0; }
};

class DBClientInterface : boost::noncopyable { 
public:
    virtual 
	auto_ptr<DBClientCursor> query(const char *ns, JSObj query, int nToReturn = 0, int nToSkip = 0, 
		JSObj *fieldsToReturn = 0, int queryOptions = 0) = 0;

    virtual
	JSObj findOne(const char *ns, JSObj query, JSObj *fieldsToReturn = 0, int queryOptions = 0) = 0;
};

/* db "commands" 
     basically just invocations of connection.$cmd.findOne({...});
*/
class DBClientCommands : public DBClientInterface { 
public:
    /* returns true in isMaster parm if this db is the master instance.  
       JSObj contains more details e.g.: 
         { "ismaster" : 1.0 , "msg" : "not paired" , "ok" : 1.0  }
         */
    JSObj cmdIsMaster(bool& isMaster);
};

class DBClientPaired;

class DBClientConnection : public DBClientCommands { 
    friend class DBClientCursor; 
    DBClientPaired *clientPaired;
	auto_ptr<MessagingPort> p;
	auto_ptr<SockAddr> server;
    bool failed; // true if some sort of fatal error has ever happened
    bool autoReconnect;
    time_t lastReconnectTry;
    string serverAddress; // remember for reconnects
    void checkConnection();
public:
    string toString() const { return serverAddress; }
    MessagingPort& port() { return *p.get(); }
    bool isFailed() const { return failed; }
    DBClientConnection(bool _autoReconnect=false,DBClientPaired* cp=0) : 
      clientPaired(cp), failed(false), autoReconnect(_autoReconnect), lastReconnectTry(0) { }

    /* Returns false if fails to connect.
       If autoReconnect is true, you can try to use the DBClientConnection even when
       false was returned -- it will try to connect again.
    */
    bool connect(const char *serverHostname, string& errmsg);

	/* send a query to the database.
       ns:            namespace to query, format is <dbname>.<collectname>[.<collectname>]*
       query:         query to perform on the collection.  this is a JSObj (binary JSON)
                      You may format as 
                        { query: { ... }, order: { ... } } 
                      to specify a sort order.
	   nToReturn:     n to return.  0 = unlimited
       nToSkip:       start with the nth item
	   fieldsToReturn: 
                      optional template of which fields to select. if unspecified, returns all fields
       queryOptions:  see options enum at top of this file

	   returns:       cursor.
                      0 if error (connection failure)
	*/
    /*throws AssertionException*/
	auto_ptr<DBClientCursor> query(const char *ns, JSObj query, int nToReturn = 0, int nToSkip = 0, 
		JSObj *fieldsToReturn = 0, int queryOptions = 0);

    /*throws AssertionException*/
	JSObj findOne(const char *ns, JSObj query, JSObj *fieldsToReturn = 0, int queryOptions = 0);
};

/* Use this class to connect to a replica pair of servers.  The class will manage 
   checking for which is master, and do failover automatically.
*/
class DBClientPaired : public DBClientCommands { 
    friend class DBClientCursor;
    DBClientConnection left,right;
    enum State { 
        NotSetL=0, 
        NotSetR=1,
        Left, Right
    } master;

    void _checkMaster();
    DBClientConnection& checkMaster();

    /* notification that we got a "not master" error. 
    */
    void isntMaster() { 
        master == Left ? NotSetR : NotSetL; 
    }
public:
    DBClientPaired();

    /* Returns false is neither member of the pair were reachable, or neither is 
       master, although, 
       when false returned, you can still try to use this connection object, it will 
       try reconnects.
       */
	bool connect(const char *serverHostname1, const char *serverHostname2);

    /* throws userassertion "no master found" */
    virtual 
	auto_ptr<DBClientCursor> query(const char *ns, JSObj query, int nToReturn = 0, int nToSkip = 0, 
		JSObj *fieldsToReturn = 0, int queryOptions = 0);

    /* throws userassertion "no master found" */
    virtual
	JSObj findOne(const char *ns, JSObj query, JSObj *fieldsToReturn = 0, int queryOptions = 0);
};

