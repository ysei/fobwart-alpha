/* 
 * event.c
 * Created: Sun Jul 15 03:40:42 2001 by tek@wiw.org
 * Revised: Wed Jul 18 00:30:05 2001 by tek@wiw.org
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
#include <dentata/memory.h>
#include <dentata/s3m.h>
#include <dentata/set.h>
#include <dentata/random.h>
#include <dentata/util.h>

#include <lua.h>

#include "fobwart.h"

void processevents(gamedata_t *gd);
int localsprintf(byte **s, byte *msg, ...);

void processevents(gamedata_t *gd)
{
    event_t ev;
    object_t *o;
    room_t *room;
    byte *s;
    int slen;

    d_set_fetch(gd->rooms, gd->curroom, (void **)&room);

    while(evsk_top(&gd->evsk, &ev)) {
        d_set_fetch(gd->objs, ev.subject, (void **)&o);

        switch(ev.verb) {
        case VERB_RIGHT:
            if(o->ax < XACCELMAX)
                o->ax++;
            d_sprite_setcuranim(o->sprite,
                                (o->onground)?ANIMRUNRIGHT:ANIMJUMPRIGHT);
            o->facing = right;
            break;

        case VERB_LEFT:
            if(o->ax > -XACCELMAX)
                o->ax--;
            d_sprite_setcuranim(o->sprite,
                                (o->onground)?ANIMRUNLEFT:ANIMJUMPLEFT);
            o->facing = left;
            break;

        case VERB_AUTO:
            o->ax = 0;
            d_sprite_setcuranim(o->sprite, (o->facing==left)?
                                ((o->onground)?ANIMSTANDLEFT:ANIMJUMPLEFT):
                                ((o->onground)?ANIMSTANDRIGHT:ANIMJUMPRIGHT));
            break;

        case VERB_ACT:
            gd->slowmo ^= 1;
            gd->slowcount = 0;
            break;

        case VERB_JUMP:
            if(o->onground) {
                o->ay = room->gravity;
                o->y--;
                o->vy = (-room->gravity)*JUMPVELOCITY;
                o->onground = false;
                d_sprite_setcuranim(o->sprite, (o->facing == left)?
                                    ANIMJUMPLEFT:ANIMJUMPRIGHT);
            }
            break;

        case VERB_TALK:
            slen = localsprintf(&s, (byte *)"<%s> %s", o->name, ev.auxdata);
            messagebuf_add(&gd->mbuf, s, slen);
            d_memory_delete(s);
            d_memory_delete(ev.auxdata);
            ev.auxdata = NULL;
            break;
        }
        evsk_pop(&gd->evsk);
    }
    return;
}

int localsprintf(byte **s, byte *msg, ...)
{
    int slen;
    void *args;

    args = &msg+1;
    slen = d_util_printflen(msg, args)+1;
    *s = d_memory_new(slen);
    d_util_sprintf(*s, msg, args);
    return slen;
}

/* EOF event.c */