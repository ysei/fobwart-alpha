/* 
 * network.c
 * Created: Wed Jul 18 01:29:32 2001 by tek@wiw.org
 * Revised: Fri Jul 20 04:02:08 2001 by tek@wiw.org
 * Copyright 2001 Julian E. C. Squires (tek@wiw.org)
 * This program comes with ABSOLUTELY NO WARRANTY.
 * $Id$
 * 
 */

#include <dentata/types.h>
#include <dentata/image.h>
#include <dentata/error.h>
#include <dentata/time.h>
#include <dentata/raster.h>
#include <dentata/event.h>
#include <dentata/font.h>
#include <dentata/sprite.h>
#include <dentata/tilemap.h>
#include <dentata/manager.h>
#include <dentata/sample.h>
#include <dentata/audio.h>
#include <dentata/s3m.h>
#include <dentata/set.h>
#include <dentata/color.h>
#include <dentata/memory.h>
#include <dentata/pcx.h>

#include <sys/types.h>
#include <limits.h>
#include <db.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <netinet/in.h>

#include <lua.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <netdb.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>

#include "fobwart.h"
#include "fobclient.h"


bool initnet(gamedata_t *gd, char *servname, int port);
void closenet(gamedata_t *gd);
bool login(gamedata_t *gd, char *uname, char *password);
void syncevents(gamedata_t *gd);
bool getobject(gamedata_t *gd, word handle);
bool getroom(gamedata_t *gd, word handle);


bool initnet(gamedata_t *gd, char *servname, int port)
{
    struct hostent *hostent;
    struct sockaddr_in sockaddr;
    packet_t p;
    bool status;

    gd->socket = socket(AF_INET, SOCK_STREAM, 0);
    if(gd->socket == -1) {
        d_error_debug(__FUNCTION__": %s\n", strerror(errno));
        return failure;
    }

    sockaddr.sin_family = AF_INET;
    hostent = gethostbyname(servname);
    if(hostent == NULL) {
        d_error_debug(__FUNCTION__": %s\n", strerror(errno));
        return failure;
    }

    memcpy(&sockaddr.sin_addr, hostent->h_addr_list[0], hostent->h_length);
    sockaddr.sin_port = htons(port);

    if(connect(gd->socket, (const struct sockaddr *)&sockaddr,
               sizeof(sockaddr)) == -1) {
        d_error_debug(__FUNCTION__": %s\n", strerror(errno));
        return failure;
    }

    /* Handshake */
    status = readpack(gd->socket, &p);
    if(status == failure || p.type != PACK_YEAWHAW)
        return failure;
    d_error_debug("PACK_YEAWHAW: %d\n", p.body.handshake);
    p.type = PACK_IRECKON;
    p.body.handshake = 42;
    status = writepack(gd->socket, p);
    if(status == failure)
        return failure;

    return success;
}


void closenet(gamedata_t *gd)
{
    close(gd->socket);
    gd->socket = -1;
    return;
}


bool login(gamedata_t *gd, char *uname, char *password)
{
    packet_t p;
    bool status;

    p.type = PACK_LOGIN;
    p.body.login.name = (byte *)uname;
    p.body.login.namelen = strlen(uname)+1;
    p.body.login.password = (byte *)password;
    p.body.login.pwlen = strlen(password)+1;
    status = writepack(gd->socket, p);
    status &= readpack(gd->socket, &p);
    if(status != success || p.type != PACK_AYUP)
        return failure;
    gd->localobj = p.body.handle;
    status = getobject(gd, p.body.handle);
    if(status != success) return failure;
    return success;
}


void syncevents(gamedata_t *gd)
{
    int i;
    packet_t p;

    for(i = 0; i < gd->evsk.top; i++) {
        p.type = PACK_EVENT;
        p.body.event = gd->evsk.events[i];
        writepack(gd->socket, p);
    }
    p.type = PACK_FRAME;
    i = writepack(gd->socket, p);
    if(i == -1)
        d_error_debug(__FUNCTION__": write frame failed.\n");
    while(i = readpack(gd->socket, &p), i != -1 && p.type != PACK_FRAME) {
        if(p.type != PACK_EVENT)
            d_error_debug(__FUNCTION__": read event failed.\n");
        evsk_push(&gd->evsk, p.body.event);
    }
    if(i == -1)
        d_error_debug(__FUNCTION__": read frame failed.\n");
    return;
}


bool getobject(gamedata_t *gd, word handle)
{
    bool status;
    object_t *o;
    packet_t p;
    char *s;

    d_error_debug("Added %d\n", handle);
    p.type = PACK_GETOBJECT;
    p.body.handle = handle;
    status = writepack(gd->socket, p);
    if(status == failure) return failure;
    status = readpack(gd->socket, &p);
    if(status == failure || p.type != PACK_OBJECT) return failure;

    o = d_memory_new(sizeof(object_t));
    if(o == NULL) return failure;
    status = d_set_add(gd->ws.objs, handle, (void *)o);
    if(status == failure) return failure;
    d_memory_copy(o, &p.body.object, sizeof(p.body.object));
    d_error_debug("Object %d uses sprite %s\n", handle, o->spname);
    s = d_memory_new(strlen(DATADIR)+strlen(o->spname)+6);
    sprintf(s, "%s/%s.spr", DATADIR, o->spname);
    o->sprite = loadsprite(s);
    d_memory_delete(s);
    if(o->sprite == NULL) return failure;
    status = d_manager_addsprite(o->sprite, &o->sphandle, 0);
    if(status == failure)
        return failure;

    return success;
}


bool getroom(gamedata_t *gd, word handle)
{
    bool status;
    room_t *room;
    packet_t p;
    d_image_t *im;
    char *s;
    dword key;
    object_t *o;

    d_error_debug("Added room %d\n", handle);
    p.type = PACK_GETROOM;
    p.body.handle = handle;
    status = writepack(gd->socket, p);
    if(status == failure) return failure;
    status = readpack(gd->socket, &p);
    if(status == failure || p.type != PACK_ROOM) return failure;

    room = d_memory_new(sizeof(room_t));
    if(room == NULL) return failure;
    status = d_set_add(gd->ws.rooms, handle, (void *)room);
    if(status == failure) return failure;
    d_memory_copy(room, &p.body.room, sizeof(p.body.room));

    d_error_debug("Room %d uses map %s\n", handle, room->mapname);
    s = d_memory_new(strlen(DATADIR)+strlen(room->mapname)+6);
    sprintf(s, "%s/%s.map", DATADIR, room->mapname);
    room->map = loadtmap(s);
    d_memory_delete(s);
    if(room->map == NULL) return failure;

    s = d_memory_new(strlen(DATADIR)+strlen(room->mapname)+6);
    sprintf(s, "%s/%s.pcx", DATADIR, room->bgname);
    room->bg = d_pcx_load(s);
    d_memory_delete(s);
    if(room->bg == NULL) return failure;

    d_manager_wipelayers();
    d_manager_wipesprites();
    d_set_resetiteration(gd->ws.objs);
    while(key = d_set_nextkey(gd->ws.objs), key != D_SET_INVALIDKEY) {
        d_set_fetch(gd->ws.objs, key, (void **)&o);
        if(o->location == room->handle)
            d_manager_addsprite(o->sprite, &o->sphandle, 0);
    }
    status = d_manager_addimagelayer(room->bg, &room->bghandle, -1);
    if(status == failure) return failure;

    status = d_manager_addtilemaplayer(room->map, &room->tmhandle, 0);
    if(status == failure) return failure;

    return success;
}

/* EOF network.c */
