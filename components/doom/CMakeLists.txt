# DIABLITO Doom engine: kilograham/rp2040-doom (Chocolate Doom derivative) on ESP-IDF.
# src/ is a copy of upstream src/ with the patches listed in NOTES/phase-3-first-light.md;
# esp/ holds the platform layer; shim/ maps the pico-sdk headers the engine includes.
set(S src)
file(GLOB DOOM_GAME ${S}/doom/*.c)
idf_component_register(
    SRCS
        ${DOOM_GAME}
        ${S}/m_argv.c ${S}/m_misc.c
        ${S}/aes_prng.c ${S}/d_event.c ${S}/d_iwad.c ${S}/d_loop.c ${S}/d_mode.c ${S}/deh_str.c
        ${S}/gusconf.c ${S}/midifile.c ${S}/mus2mid.c ${S}/m_bbox.c ${S}/m_cheat.c ${S}/m_config.c
        ${S}/m_controls.c ${S}/m_fixed.c ${S}/net_client.c ${S}/sha1.c ${S}/memio.c ${S}/tables.c
        ${S}/v_diskicon.c ${S}/v_video.c ${S}/w_checksum.c ${S}/w_main.c ${S}/w_wad.c ${S}/w_file.c
        ${S}/w_file_posix.c ${S}/w_file_memory.c ${S}/w_merge.c ${S}/z_zone.c ${S}/i_sound.c
        ${S}/deh_io.c ${S}/deh_main.c ${S}/deh_mapping.c ${S}/deh_text.c
        ${S}/tiny_huff.c ${S}/musx_decoder.c ${S}/image_decoder.c ${S}/pd_render.cpp
        ${S}/adpcm-xq/adpcm-lib.c
        ${S}/pico/i_glob.c ${S}/pico/i_picosound.c
        # OPL2 music: emu8950 (portable C slot renderer) driven by i_oplmusic through opl_pico
        ${S}/i_oplmusic.c opl/opl_api.c opl/emu8950.c opl/slot_render.cpp opl/opl_pico.c
        esp/i_video.c esp/i_system.c esp/i_timer.c esp/i_input.c esp/i_sound_glue.c esp/doom_main.c esp/prof.c
    INCLUDE_DIRS include
    PRIV_INCLUDE_DIRS shim ${S} ${S}/doom ${S}/pico ${S}/adpcm-xq opl
    REQUIRES display
    PRIV_REQUIRES esp_timer driver ble_pad vfs nvs_flash
)

target_compile_options(${COMPONENT_LIB} PRIVATE
    -fms-extensions -w -Wno-error -Wno-unused-function -Wno-unused-variable -Wno-unused-but-set-variable
    -Wno-unused-parameter -Wno-sign-compare -Wno-missing-field-initializers -Wno-format-truncation
    -Wno-implicit-fallthrough -Wno-char-subscripts -Wno-parentheses -Wno-misleading-indentation
    -Wno-format -Wno-strict-aliasing -Wno-unused-but-set-parameter -Wno-error=unused-but-set-parameter -Wno-error=all)
set_source_files_properties(${S}/pd_render.cpp PROPERTIES COMPILE_OPTIONS "-Wno-narrowing;-Wno-register;-Wno-class-memaccess")

target_compile_definitions(${COMPONENT_LIB} PRIVATE
    # platform. NDEBUG as in the RP2040 MinSizeRel build: the decoders assert per pixel otherwise
    NDEBUG=1 PICO_ON_DEVICE=1 PICO_BUILD=1 PICO_RP2040=0 PICO_RP2350=0 LIB_PICO_STDLIB=0
    USE_INTERP=0 USE_CORE1_FOR_FLATS=0 USE_CORE1_FOR_REGULAR=0 USE_ZONE_FOR_MALLOC=0
    NO_USE_MOUSE=1 NO_USE_ENDDOOM=1 SUPPORT_OVERLAYS=1 SUPPORT_TEXT=0 TINY_WAD_ADDR=0
    PICO_AUDIO_I2S_DATA_PIN=23 PICO_AUDIO_I2S_CLOCK_PIN_BASE=20
    # small_doom_common (upstream src/CMakeLists.txt)
    SHRINK_MOBJ=1 DOOM_ONLY=1 DOOM_SMALL=1 DOOM_CONST=1 SOUND_LOW_PASS=1 NUM_SOUND_CHANNELS=8
    NO_USE_CHECKSUM=1 NO_USE_RELOAD=1 USE_SINGLE_IWAD=1 NO_USE_WIPE=1 NO_USE_JOYSTICK=1 NO_USE_DEH=1
    NO_USE_MUSIC_PACKS=1 USE_FLAT_MAX_256=1 USE_MEMMAP_ONLY=1 USE_LIGHTMAP_INDEXES=1 USE_ERASE_FRAME=1
    NO_DRAW_MID=1 NO_DRAW_TOP=1 NO_DRAW_BOTTOM=1 NO_DRAW_MASKED=1 NO_DRAW_SKY=1 NO_DRAW_SPRITES=1
    NO_DRAW_PSPRITES=1 NO_VISPLANE_GUTS=1 NO_VISPLANE_CACHES=1 NO_DRAWSEGS=1 NO_VISSPRITES=1
    NO_MASKED_FLOOR_CLIP=1 PD_DRAW_COLUMNS=1 PD_DRAW_MARKERS=1 PD_DRAW_PLANES=1 PD_SCALE_SORT=1
    PD_CLIP_WALLS=1 PD_QUANTIZE=1 PD_SANITY=1 PD_COLUMNS=1 PICO_DOOM=1 NO_USE_DS_COLORMAP=1
    NO_USE_DC_COLORMAP=1 USE_READONLY_MMAP=1 NO_USE_TIMIDITY=1 NO_USE_GUS=1 NO_USE_LIBSAMPLERATE=1
    NO_USE_STATE_MISC USE_RAW_MAPNODE=1 USE_RAW_MAPVERTEX=1 USE_RAW_MAPSEG=1 USE_RAW_MAPLINEDEF=1
    USE_RAW_MAPTHING=1 USE_INDEX_LINEBUFFER=1 NO_USE_ZLIGHT=1 NO_Z_ZONE_ID=1 Z_MALOOC_EXTRA_DATA=1
    USE_THINKER_POOL=1 NO_INTERCEPTS_OVERRUN=1 TEMP_IMMUTABLE_DISABLED=1 USE_CONST_SFX=1
    USE_CONST_MUSIC=1 NO_DEMO_RECORDING=1 PICO_NO_TIMING_DEMO=1 NO_USE_EXIT=1
    # doom_tiny (non super tiny: WHD, all demos, finale)
    DOOM_TINY=1 EMU8950_SLOT_RENDER=1 NO_RDRAW=1 USE_EMU8950_OPL=1 USE_DIRECT_MIDI_LUMP=1
    # emu8950 diet flags from upstream, with the ARM asm slot renderer off
    EMU8950_ASM=0 EMU8950_NO_WAVE_TABLE_MAP=1 EMU8950_NO_TLL=1 EMU8950_NO_FLOAT=1 EMU8950_NO_TIMER=1
    EMU8950_NO_TEST_FLAG=1 EMU8950_SIMPLER_NOISE=1 EMU8950_SHORT_NOISE_UPDATE_CHECK=1 EMU8950_LINEAR_SKIP=1
    EMU8950_LINEAR_END_OF_NOTE_OPTIMIZATION EMU8950_NO_PERCUSSION_MODE=1 EMU8950_LINEAR=1
    # half-rate synth (see opl/opl_pico.c): chip, mixer and I2S all run at 49716/2 Hz
    OPL_HALF_RATE=1 PICO_SOUND_SAMPLE_FREQ=24858
    NO_USE_NET=1 USE_PICO_NET=0 NO_FILE_ACCESS=1 SAVE_COMPRESSED=1 LOAD_COMPRESSED=1 NO_USE_ARGS=1
    NO_USE_SAVE_CONFIG=1 NO_USE_FLOAT=1 USE_VANILLA_KEYBOARD_MAPPING_ONLY=1 NO_USE_LOADING_DISK=1
    USE_WHD=1 NO_Z_MALLOC_USER_PTR=1 FIXED_SCREENWIDTH=1 FLOOR_CEILING_CLIP_8BIT=1 USE_MUSX=1
    MUSX_COMPRESSED=1 NO_SCREENSHOT=1 NO_USE_BOUND_CONFIG=1 USE_FPS=1 USE_MEMORY_WAD=1
    EMU8950_NO_RATECONV=1 NO_ZONE_DEBUG=1
    # render_newhope
    PICODOOM_RENDER_NEWHOPE=1 MERGE_DISTSCALE0_INTO_VIEWCOSSINANGLE=1
    # DOOM_PROFILE=1   # 2 kHz PC sampler, see esp/prof.c and tools/symbolize.sh
)
if(DEFINED IDLE_SLEEP_S)   # idf.py -DIDLE_SLEEP_S=0 build: display never sleeps (battery runtime test)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE DOOM_IDLE_SLEEP_S=${IDLE_SLEEP_S})
endif()
