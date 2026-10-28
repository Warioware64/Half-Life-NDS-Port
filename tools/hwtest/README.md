# Hardware probes

Small ROMs that answer questions only real hardware can answer (melonDS
emulates neither sound capture faithfully, nor the DSP's DMA, nor the bus
jams). Each is its own BlocksDS project:

    cd tools/hwtest/<probe>
    BLOCKSDS=/opt/wonderful/thirdparty/blocksds/core \
    BLOCKSDSEXT=/opt/wonderful/thirdparty/blocksds/external \
    WONDERFUL_TOOLCHAIN=/opt/wonderful make -j4

They were made for the custom ARM7 audio core (phase P0 of the audio plan).

## routing/ — the DS sound routing the room effect depends on

The ARM7 drives the channels, capture units and SOUNDCNT itself; the
automatic tests run at start, the listening ones on buttons (the screen says
which). Results on a DSi and a DS, 2026-10-28 (the same on both):

| Test | Result |
|---|---|
| 1. Capture of the left mixer with SOUNDCNT bit 12 set (Ch1 out of the mixer) | Ch1 in: mean 4096; Ch1 out: mean 0 -- **the capture is dry only** |
| 2. Capture into ARM7 WRAM | 512/512 words written, mean 4096 -- **works** |
| 3. Capture against Ch1's timer | always **8 samples ahead** (100/96 … 800/808): a fixed offset |
| 4. Pan with the speakers taking Ch1/Ch3 directly | by ear: fine (the core pans Ch1 hard left and Ch3 hard right either way) |
| 5. A tone through capture and Ch1/Ch3 against the plain mixer | by ear: the same |
| 6. Block timer (64 samples) and capture lock over minutes | DSi 232 s, DS 201 s: **0 late blocks**, the click never moved; gaps 57959..73113 ticks on a DSi, 62858..68214 on a DS (65536 ideal): at most ~0.23 ms (7 samples) of wobble |

A ring two or three 64-sample blocks deep has room to spare for that wobble.
So on a DS the room effect plays its wet signal on Ch1/Ch3, kept out of the
mixer, and takes the dry mix from the capture units: it never hears itself.

## dsp_bus/ — does feeding the DSP jam the bus? (DSi only)

The game feeds the DSP by writing words into the PDATA FIFO (the DSP's own
DMA moves them into its memory); NEA measured a jam with transfers the DSP
pulls itself through AHBM while a DMA feeds the geometry FIFO
(nitro-engine NEADisplayList.c). This probe runs the game's DSP program and
does 4 x 512 words written and read back per frame, alone, after, and during
a geometry-FIFO DMA (legacy and NDMA) of ~4000 4x4 matrix multiplies (a list
that makes no polygons: the first version drew more triangles than polygon
memory holds, and that choked the geometry engine on its own).

Results on a DSi, 2026-10-28, about 500 frames per condition:

| Condition | Fails |
|---|---|
| PDATA alone | 0 |
| DMA display list alone (DSP running) | 0 |
| NDMA display list alone | 0 |
| DMA list, then PDATA | 0 |
| NDMA list, then PDATA | 0 |
| PDATA **during** an NDMA list | 0 |
| PDATA **during** a legacy DMA list | 0 |

No stalls, no wrong data, no restarts. **The PDATA path does not jam**, even
fully overlapping. The rule that follows: the DSP is only ever given data by
the ARM9 through PDATA, never through AHBM -- and then the world's NDMA
display list can stay on while the DSP runs.

Note for writing such code: on hardware the PDATA registers must be driven
exactly as libnds does (PCFG before PADR for writes, PADR before PCFG for
reads, a short wait before each access, no waiting on the write-busy bit);
the probe's first version did not, and every transfer stalled on a DSi while
melonDS accepted it.
