/* What the DSi's DSP program and everything that talks to it agree on: the
 * commands, the sizes of the rings, the marks. Nothing but numbers: the
 * program's assembly includes this as well as the C that models it
 * (hl_dsp.h) and the ARM9 that feeds it. */
#ifndef HL_DSP_PROTO_H
#define HL_DSP_PROTO_H

#define HL_DSP_MAGIC 0x484C /* "HL": the answer to a ping */

#define HL_DSP_MUSIC_WORDS 4096 /* a third of a second of music */
#define HL_DSP_SEND_WORDS 2048  /* a sixth of a second of what is to echo */

/* Commands: the number in register 0, its arguments in registers 1 and 2
 * (written first). Every command is answered in register 0, with the number
 * of the tick it took effect before in register 1 (which is how the tests
 * line the model up with the program). */
#define HL_DSP_CMD_PING 1        /* -> HL_DSP_MAGIC */
#define HL_DSP_CMD_INFO 2        /* arg1: HL_DSP_INFO_* -> a DSP data address */
#define HL_DSP_CMD_MUSIC_START 3 /* arg1: the ring's write position (what is in it already) */
#define HL_DSP_CMD_MUSIC_STOP 4
#define HL_DSP_CMD_MUSIC_FED 5   /* arg1: the ring's new write position -> its read position */
#define HL_DSP_CMD_SEND_FED 6    /* the same for the echo's ring */
#define HL_DSP_CMD_ROOM 7        /* the room's numbers, written first at HL_DSP_INFO_ROOM, taken (and the
                                  * lines emptied) */
#define HL_DSP_CMD_STATUS 8      /* -> times the music ran dry since last asked (low 15 bits),
                                  * and HL_DSP_FAULT if the music has been stopped as wrong */
#define HL_DSP_CMD_HEARD 9       /* -> the loudest sample sent to echo since last asked */

#define HL_DSP_INFO_MUSIC 0
#define HL_DSP_INFO_SEND 1
#define HL_DSP_INFO_ROOM 2

/* The room's numbers (HlDspRoom.hpp works them out from the game's preset),
 * HL_DSP_ROOM_WORDS words at HL_DSP_INFO_ROOM, in this order: */
#define HL_DSP_ROOM_ER_LEN 0     /* the early line's length (samples) */
#define HL_DSP_ROOM_ER_TAP 1     /* ... four taps back along it: left, right, left, right */
#define HL_DSP_ROOM_ER_MIX 5     /* ... how loud they are (Q15) */
#define HL_DSP_ROOM_FDN_LEN 6    /* the four lines of the reverb's lengths */
#define HL_DSP_ROOM_FDN_GAIN 10  /* ... how much goes round (Q15) */
#define HL_DSP_ROOM_FDN_DAMP 11  /* ... how dulled each time (Q14: of what it was) */
#define HL_DSP_ROOM_LATE_MIX 12  /* ... how loud it is (Q15) */
#define HL_DSP_ROOM_DLY_LEN 13   /* the echo's distance (samples; 0: none) */
#define HL_DSP_ROOM_DLY_GAIN 14  /* ... how much comes back (Q15) */
#define HL_DSP_ROOM_DLY_LOW 15   /* ... dulled each time (0, 1) */
#define HL_DSP_ROOM_ECHO_MIX 16  /* ... how loud (Q15) */
#define HL_DSP_ROOM_LEVEL 17     /* all of it, at the end (Q6: 64 is as it is; 0: no room at all) */
#define HL_DSP_ROOM_WORDS 18

/* The lines' room. */
#define HL_DSP_ER_WORDS 1200     /* 100 ms at 11,901 Hz */
#define HL_DSP_FDN_LINE 1700     /* each of the four */
#define HL_DSP_DLY_WORDS 4800    /* 400 ms */

#define HL_DSP_FAULT 0x8000

/* The music arrives in blocks: a mark, the four words of both decoders'
 * states at the block's start (left sample, left step, right sample, right
 * step), then HL_DSP_BLOCK_WORDS words of samples. The decoders take their
 * states afresh from every block, so nothing that goes wrong outlasts one;
 * and a block that does not begin with the mark is the stream out of step,
 * at which the music is stopped (HL_DSP_FAULT) rather than played as noise. */
#define HL_DSP_BLOCK_MARK 0xB10C
#define HL_DSP_BLOCK_WORDS 1024
#define HL_DSP_BLOCK_WHOLE (HL_DSP_BLOCK_WORDS + 5)

/* What has been worked out and not yet sent to the speakers: pairs, in a
 * ring between the program's main loop (which fills it) and the interrupt
 * that asks for a pair 47,605 times a second (which only empties it). */
#define HL_DSP_OUT_WORDS 128



#endif
