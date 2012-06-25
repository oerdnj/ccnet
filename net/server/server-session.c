/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#include "common.h"

#include <signal.h>
#include <dirent.h>
#include <sys/stat.h>
#include <time.h>

#include "getgateway.h"
#include "utils.h"
#include "net.h"
#include "rsa.h"

#include "server-session.h"
#include "peer.h"
#include "peer-mgr.h"
#include "perm-mgr.h"
#include "packet-io.h"
#include "connect-mgr.h"
#include "message.h"
#include "message-manager.h"
#include "proc-factory.h"
#include "algorithms.h"
#include "ccnet-config.h"
#include "user-mgr.h"
#include "group-mgr.h"
#include "org-mgr.h"

#define DEBUG_FLAG CCNET_DEBUG_OTHER
#include "log.h"
#define CCNET_DB "ccnet.db"


G_DEFINE_TYPE (CcnetServerSession, ccnet_server_session, CCNET_TYPE_SESSION);

static int load_database_config (CcnetSession *session);
static int server_session_prepare (CcnetSession *session);
static void server_session_start (CcnetSession *session);
static void on_peer_auth_done (CcnetSession *session, CcnetPeer *peer);


static void
ccnet_server_session_class_init (CcnetServerSessionClass *klass)
{
    CcnetSessionClass *session_class = CCNET_SESSION_CLASS (klass);

    session_class->prepare = server_session_prepare;
    session_class->start = server_session_start;
    session_class->on_peer_auth_done = on_peer_auth_done;
}

static void
ccnet_server_session_init (CcnetServerSession *server_session)
{
    CcnetSession *session = (CcnetSession *)server_session;
    server_session->user_mgr = ccnet_user_manager_new (session);
    server_session->group_mgr = ccnet_group_manager_new (session);
    server_session->org_mgr = ccnet_org_manager_new (session);
}

CcnetServerSession *
ccnet_server_session_new (const char *config_dir_r)
{
    return g_object_new (CCNET_TYPE_SERVER_SESSION, NULL);
}

int
server_session_prepare (CcnetSession *session)
{
    CcnetServerSession *server_session = (CcnetServerSession *)session;
    char *service_url = NULL;

    service_url = ccnet_key_file_get_string (session->keyf, "General", "SERVICE_URL");
    session->base.service_url = service_url;

    if (load_database_config (session) < 0) {
        ccnet_warning ("Failed to load database config.\n");
        return -1;
    }

    ccnet_user_manager_prepare (server_session->user_mgr);
    ccnet_group_manager_prepare (server_session->group_mgr);
    ccnet_org_manager_prepare (server_session->org_mgr);
    return 0;
}

void
server_session_start (CcnetSession *session)
{
    g_signal_connect (session->peer_mgr, "peer-auth-done",
                      G_CALLBACK(on_peer_auth_done), NULL);    
}


static int init_sqlite_database (CcnetSession *session)
{
    char *db_path;

    db_path = g_build_filename (session->config_dir, CCNET_DB, NULL);
    session->db = ccnet_db_new_sqlite (db_path);
    if (!session->db) {
        g_warning ("Failed to open database.\n");
        return -1;
    }
    return 0;
}

static int init_mysql_database (CcnetSession *session)
{
    char *host, *user, *passwd, *db, *unix_socket;

    host = ccnet_key_file_get_string (session->keyf, "Database", "HOST");
    user = ccnet_key_file_get_string (session->keyf, "Database", "USER");
    passwd = ccnet_key_file_get_string (session->keyf, "Database", "PASSWD");
    db = ccnet_key_file_get_string (session->keyf, "Database", "DB");

    if (!host) {
        g_warning ("DB host not set in config.\n");
        return -1;
    }
    if (!user) {
        g_warning ("DB user not set in config.\n");
        return -1;
    }
    if (!passwd) {
        g_warning ("DB passwd not set in config.\n");
        return -1;
    }
    if (!db) {
        g_warning ("DB name not set in config.\n");
        return -1;
    }
    unix_socket = ccnet_key_file_get_string (session->keyf,
                                             "Database", "UNIX_SOCKET");
    if (!unix_socket) {
        g_warning ("Unix socket path not set in config.\n");
    }

    session->db = ccnet_db_new_mysql (host, user, passwd, db, unix_socket);
    if (!session->db) {
        g_warning ("Failed to open database.\n");
        return -1;
    }

   return 0;
}

static int
load_database_config (CcnetSession *session)
{
    int ret;
    char *engine;

    engine = ccnet_key_file_get_string (session->keyf, "Database", "ENGINE");
    if (!engine || strncasecmp (engine, DB_SQLITE, sizeof(DB_SQLITE)) == 0) {
        ccnet_debug ("Use database sqlite\n");
        ret = init_sqlite_database (session);
    } else if (strncasecmp (engine, DB_MYSQL, sizeof(DB_MYSQL)) == 0) {
        ccnet_debug ("Use database Mysql\n");
        ret = init_mysql_database (session);
    }
    return ret;
}


static void
on_peer_auth_done (CcnetSession *session, CcnetPeer *peer)
{
    ccnet_peer_manager_send_ready_message (session->peer_mgr, peer);
}