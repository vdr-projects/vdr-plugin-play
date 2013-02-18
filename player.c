///
///	@file player.c	@brief A play plugin for VDR.
///
///	Copyright (c) 2012, 2013 by Johns.  All Rights Reserved.
///
///	Contributor(s):
///
///	License: AGPLv3
///
///	This program is free software: you can redistribute it and/or modify
///	it under the terms of the GNU Affero General Public License as
///	published by the Free Software Foundation, either version 3 of the
///	License.
///
///	This program is distributed in the hope that it will be useful,
///	but WITHOUT ANY WARRANTY; without even the implied warranty of
///	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
///	GNU Affero General Public License for more details.
///
///	$Id$
//////////////////////////////////////////////////////////////////////////////

#include <sys/types.h>
#include <sys/wait.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include <poll.h>
#include <pthread.h>

#include <libintl.h>
#define _(str) gettext(str)		///< gettext shortcut
#define _N(str) str			///< gettext_noop shortcut

#include "player.h"
#include "video.h"
#include "misc.h"

//////////////////////////////////////////////////////////////////////////////

const char *ConfigBrowserRoot = "/";	///< browser starting point

static char ConfigOsdOverlay;		///< show osd overlay
static char ConfigUseSlave;		///< external player use slave mode
static char ConfigFullscreen;		///< external player uses fullscreen
static char *ConfigVideoOut;		///< video out device
static char *ConfigAudioOut;		///< audio out device
static char *ConfigAudioMixer;		///< audio mixer device
static char *ConfigMixerChannel;	///< audio mixer channel
static const char *ConfigMplayer = "/usr/bin/mplayer";	///< mplayer executable
static const char *ConfigMplayerArguments;	///< extra mplayer arguments
static const char *ConfigX11Display = ":0.0";	///< x11 display

    /// DVD-Drive for mplayer
static const char *ConfigMplayerDevice = "/dev/dvd";
static uint32_t ConfigColorKey = 0x00020507;	///< color key

//////////////////////////////////////////////////////////////////////////////
//	Osd
//////////////////////////////////////////////////////////////////////////////

/**
**	Open OSD.
*/
void OsdOpen(void)
{
    Debug(3, "play: %s\n", __FUNCTION__);

    VideoWindowShow();
}

/**
**	Close OSD.
*/
void OsdClose(void)
{
    Debug(3, "play: %s\n", __FUNCTION__);

    VideoWindowHide();
    VideoWindowClear();
}

/**
**	Clear OSD.
*/
void OsdClear(void)
{
    Debug(3, "play: %s\n", __FUNCTION__);

    VideoWindowClear();
}

/**
**	Get OSD size and aspect.
**
**	@param width[OUT]	width of OSD
**	@param height[OUT]	height of OSD
**	@param aspect[OUT]	aspect ratio (4/3, 16/9, ...) of OSD
*/
void GetOsdSize(int *width, int *height, double *aspect)
{
    VideoGetOsdSize(width, height);
    *aspect = 16.0 / 9.0 / (double)*width * (double)*height;
}

/**
**	Draw osd pixmap
*/
void OsdDrawARGB(int x, int y, int w, int h, const uint8_t * argb)
{
    Debug(3, "play: %s %d,%d %d,%d\n", __FUNCTION__, x, y, w, h);

    VideoDrawARGB(x, y, w, h, argb);
}

//////////////////////////////////////////////////////////////////////////////
//	External player
//////////////////////////////////////////////////////////////////////////////

static pid_t PlayerPid;			///< player pid
static pthread_t PlayerThread;		///< player decode thread

static char PlayerPipeBuf[4096];	///< pipe buffer
static int PlayerPipeCnt;		///< pipe buffer count
static int PlayerPipeIdx;		///< pipe buffer index
static int PlayerPipeOut[2];		///< player write pipe
static int PlayerPipeIn[2];		///< player read pipe

static int PlayerVolume = -1;		///< volume 0 - 100

char PlayerDvdNav;			///< dvdnav active
char PlayerPaused;			///< player paused
char PlayerSpeed;			///< player playback speed

//////////////////////////////////////////////////////////////////////////////
//	Slave
//////////////////////////////////////////////////////////////////////////////

/**
**	Parse player output.
**
**	@param data	line pointer (\0 terminated)
**	@param size	line length
*/
static void PlayerParseLine(const char *data, int size)
{
    Debug(4, "play/parse: |%.*s|\n", size, data);

    // data is \0 terminated
    if (!strncasecmp(data, "DVDNAV_TITLE_IS_MENU", 20)) {
	PlayerDvdNav = 1;
    } else if (!strncasecmp(data, "DVDNAV_TITLE_IS_MOVIE", 21)) {
	PlayerDvdNav = 2;
    } else if (!strncasecmp(data, "ID_DVD_VOLUME_ID=", 17)) {
	Debug(3, "DVD_VOLUME = %s\n", data + 17);
    } else if (!strncasecmp(data, "ID_AID_", 7)) {
	char lang[64];
	int aid;

	if (sscanf(data, "ID_AID_%d_LANG=%s", &aid, lang) == 2) {
	    Debug(3, "AID(%d) = %s\n", aid, lang);
	}
    } else if (!strncasecmp(data, "ID_SID_", 7)) {
	char lang[64];
	int sid;

	if (sscanf(data, "ID_SID_%d_LANG=%s", &sid, lang) == 2) {
	    Debug(3, "SID(%d) = %s\n", sid, lang);
	}
    }
}

/**
**	Poll input pipe.
*/
static void PlayerPollPipe(void)
{
    struct pollfd poll_fds[1];
    int n;
    int i;
    int l;

    // check if something to read
    poll_fds[0].fd = PlayerPipeOut[0];
    poll_fds[0].events = POLLIN;

    switch (poll(poll_fds, 1, 0)) {
	case 0:			// timeout
	    return;
	case -1:			// error
	    Error(_("play/player: poll failed: %s\n"), strerror(errno));
	    return;
	default:			// ready
	    break;
    }

    // fill buffer
    if ((n = read(PlayerPipeOut[0], PlayerPipeBuf + PlayerPipeCnt,
		sizeof(PlayerPipeBuf) - PlayerPipeCnt)) < 0) {
	Error(_("play/player: read failed: %s\n"), strerror(errno));
	return;
    }

    PlayerPipeCnt += n;
    l = 0;
    for (i = PlayerPipeIdx; i < PlayerPipeCnt; ++i) {
	if (PlayerPipeBuf[i] == '\n') {
	    PlayerPipeBuf[i] = '\0';
	    PlayerParseLine(PlayerPipeBuf + PlayerPipeIdx, i - PlayerPipeIdx);
	    PlayerPipeIdx = i + 1;
	    l = PlayerPipeIdx;
	}
    }

    if (l) {				// remove consumed bytes
	if (PlayerPipeCnt - l) {
	    memmove(PlayerPipeBuf, PlayerPipeBuf + l, PlayerPipeCnt - l);
	}
	PlayerPipeCnt -= l;
	PlayerPipeIdx -= l;
    } else if (PlayerPipeCnt == sizeof(PlayerPipeBuf)) {
	// no '\n' in buffer use it as single line
	PlayerPipeBuf[sizeof(PlayerPipeBuf) - 1] = '\0';
	PlayerParseLine(PlayerPipeBuf + PlayerPipeIdx, PlayerPipeCnt);
	PlayerPipeIdx = 0;
	PlayerPipeCnt = 0;
    }
}

#define MPLAYER_MAX_ARGS 64		///< number of arguments supported

/**
**	Execute external player.
**
**	@param filename	path and name of file to play
*/
static void PlayerExec(const char *filename)
{
    const char *args[MPLAYER_MAX_ARGS];
    int argn;
    char wid_buf[32];
    char volume_buf[32];
    int i;

    if (ConfigUseSlave) {		// connect pipe to stdin/stdout
	dup2(PlayerPipeIn[0], STDIN_FILENO);
	close(PlayerPipeIn[0]);
	close(PlayerPipeIn[1]);
	close(PlayerPipeOut[0]);
	dup2(PlayerPipeOut[1], STDOUT_FILENO);
	dup2(PlayerPipeOut[1], STDERR_FILENO);
	close(PlayerPipeOut[1]);
    }
    // close all file handles
    for (i = getdtablesize() - 1; i > STDERR_FILENO; i--) {
	close(i);
    }

    // export DISPLAY=

    args[0] = ConfigMplayer;
    args[1] = "-quiet";
    args[2] = "-msglevel";
    // FIXME: play with the options
#ifdef DEBUG
    args[3] = "all=6:global=4:cplayer=4:identify=4";
#else
    args[3] = "all=2:global=2:cplayer=2:identify=4";
#endif
    if (ConfigOsdOverlay) {
	args[4] = "-noontop";
    } else {
	args[4] = "-ontop";
    }
    args[5] = "-noborder";
    if (ConfigDisableRemote) {
	args[6] = "-lirc";
    } else {
	args[6] = "-nolirc";
    }
    args[7] = "-nojoystick";		// disable all unwanted inputs
    args[8] = "-noar";
    args[9] = "-nomouseinput";
    args[10] = "-nograbpointer";
    args[11] = "-noconsolecontrols";
    args[12] = "-fixed-vo";
    argn = 13;
    if (ConfigMplayerDevice) {		// dvd-device
	args[argn++] = "-dvd-device";
	args[argn++] = ConfigMplayerDevice;
    }
    if (!strncasecmp(filename, "cdda://", 7)) {
	args[argn++] = "-cache";	// cdrom needs cache
	args[argn++] = "1000";
    } else {
	args[argn++] = "-nocache";	// dvdnav needs nocache
    }
    if (ConfigUseSlave) {
	args[argn++] = "-slave";
	//args[argn++] = "-idle";
    }
    if (ConfigOsdOverlay) {		// no mplayer osd with overlay
	args[argn++] = "-osdlevel";
	args[argn++] = "0";
    }
    if (ConfigFullscreen) {
	args[argn++] = "-fs";
	args[argn++] = "-zoom";
    } else {
	args[argn++] = "-nofs";
    }
    if (VideoGetPlayWindow()) {
	snprintf(wid_buf, sizeof(wid_buf), "%d", VideoGetPlayWindow());
	args[argn++] = "-wid";
	args[argn++] = wid_buf;
    }
    if (ConfigVideoOut) {
	args[argn++] = "-vo";
	args[argn++] = ConfigVideoOut;
	// add options based on selected video out
	if (!strncmp(ConfigVideoOut, "vdpau", 5)) {
	    args[argn++] = "-vc";
	    args[argn++] =
		"ffmpeg12vdpau,ffwmv3vdpau,ffvc1vdpau,ffh264vdpau,ffodivxvdpau,";
	} else if (!strncmp(ConfigVideoOut, "vaapi", 5)) {
	    args[argn++] = "-va";
	    args[argn++] = "vaapi";
	}
    }
    if (ConfigAudioOut) {
	args[argn++] = "-ao";
	args[argn++] = ConfigAudioOut;
	// FIXME: -ac hwac3,hwdts,hwmpa,
    }
    if (ConfigAudioMixer) {
	args[argn++] = "-mixer";
	args[argn++] = ConfigAudioMixer;
    }
    if (ConfigMixerChannel) {
	args[argn++] = "-mixer-channel";
	args[argn++] = ConfigMixerChannel;
    }
    if (ConfigX11Display) {
	args[argn++] = "-display";
	args[argn++] = ConfigX11Display;
    }
    if (PlayerVolume != -1) {
	// FIXME: here could be a problem with LANG
	snprintf(volume_buf, sizeof(volume_buf), "%.2f",
	    (PlayerVolume * 100.0) / 255);
	args[argn++] = "-volume";
	args[argn++] = volume_buf;
    }
    //	split X server arguments string into words
    if (ConfigMplayerArguments) {
	const char *sval;
	char *buf;
	char *s;

	sval = ConfigMplayerArguments;
#ifndef __FreeBSD__
	s = buf = strdupa(sval);
#else
	s = buf = alloca(strlen(sval) + 1);
	strcpy(buf, sval);
#endif
	while ((sval = strsep(&s, " \t"))) {
	    args[argn++] = sval;

	    if (argn == MPLAYER_MAX_ARGS - 2) {	// argument overflow
		Error(_("play: too many arguments for mplayer\n"));
		// argn = 1;
		break;
	    }
	}
    }
    args[argn++] = filename;
    args[argn] = NULL;
#ifdef DEBUG
    if (argn + 1 >= (int)(sizeof(args) / sizeof(*args))) {
	Debug(3, "play: too many arguments %d\n", argn);
    }
    for (i = 0; i < argn; ++i) {
	Debug(3, "%s", args[i]);
    }
#endif

    execvp(args[0], (char *const *)args);

    // shouldn't be reached
    Error(_("play: execvp of '%s' failed: %s\n"), args[0], strerror(errno));
    exit(-1);
}

/**
**	Execute external player.
**
**	@param filename	path and name of file to play
*/
static void PlayerForkAndExec(const char *filename)
{
    pid_t pid;

    if (ConfigUseSlave) {
	if (pipe(PlayerPipeIn)) {
	    Error(_("play: pipe failed: %s\n"), strerror(errno));
	    return;
	}
	if (pipe(PlayerPipeOut)) {
	    Error(_("play: pipe failed: %s\n"), strerror(errno));
	    return;
	}
    }

    if ((pid = fork()) == -1) {
	Error(_("play: fork failed: %s\n"), strerror(errno));
	return;
    }
    if (!pid) {				// child
	PlayerExec(filename);
	return;
    }
    PlayerPid = pid;			// parent
    setpgid(pid, 0);

    if (ConfigUseSlave) {
	close(PlayerPipeIn[0]);
	close(PlayerPipeOut[1]);
    }

    Debug(3, "play: child pid=%d\n", pid);
}

/**
**	Close pipes.
*/
static void PlayerClosePipes(void)
{
    if (ConfigUseSlave) {
	if (PlayerPipeIn[0] != -1) {
	    close(PlayerPipeIn[0]);
	}
	if (PlayerPipeOut[1] != -1) {
	    close(PlayerPipeOut[1]);
	}
    }
}

//////////////////////////////////////////////////////////////////////////////
//	Thread
//////////////////////////////////////////////////////////////////////////////

/**
**	External player handler thread.
**
**	@param dummy	dummy pointer
*/
static void *PlayerHandlerThread(void *dummy)
{
    Debug(3, "play: player thread started\n");

    while (PlayerIsRunning()) {
	if (ConfigUseSlave) {
	    PlayerPollPipe();
	    // FIXME: wait only if pipe not ready
	}
	if (ConfigOsdOverlay) {
	    VideoPollEvents(10);
	} else {
	    usleep(10 * 1000);
	}
    }

    Debug(3, "play: player thread stopped\n");
    PlayerThread = 0;

    return dummy;
}

/**
**	Initialize player threads.
*/
static void PlayerThreadInit(void)
{
    pthread_create(&PlayerThread, NULL, PlayerHandlerThread, NULL);
    pthread_setname_np(PlayerThread, "play player");
}

/**
**	Exit and cleanup player threads.
*/
static void PlayerThreadExit(void)
{
    if (PlayerThread) {
	void *retval;

	Debug(3, "play: player thread canceled\n");
	if (pthread_cancel(PlayerThread)) {
	    Error(_("play: can't queue cancel player thread\n"));
	}
	if (pthread_join(PlayerThread, &retval) || retval != PTHREAD_CANCELED) {
	    Error(_("play: can't cancel player thread\n"));
	}
	PlayerThread = 0;
    }
}

/**
**	Send command to player.
**
**	@param formst	printf format string
*/
static void SendCommand(const char *format, ...)
{
    va_list va;
    char buf[256];
    int n;

    if (!PlayerPid) {
	return;
    }
    if (PlayerPipeIn[1] == -1) {
	Error(_("play: no pipe to send command available\n"));
	return;
    }
    va_start(va, format);
    n = vsnprintf(buf, sizeof(buf), format, va);
    va_end(va);

    Debug(3, "play: send '%.*s'\n", n - 1, buf);

    // FIXME: poll pipe if ready
    if (write(PlayerPipeIn[1], buf, n) != n) {
	fprintf(stderr, "play: write failed\n");
    }
}

/**
**	Send player quit.
*/
void PlayerSendQuit(void)
{
    if (ConfigUseSlave) {
	SendCommand("quit\n");
    }
}

/**
**	Send player pause.
*/
void PlayerSendPause(void)
{
    if (ConfigUseSlave) {
	SendCommand("pause\n");
    }
}

/**
**	Send player speed_set.
**
**	@param speed	mplayer speed
*/
void PlayerSendSetSpeed(int speed)
{
    if (ConfigUseSlave) {
	SendCommand("pausing_keep speed_set %d\n", speed);
    }
}

/**
**	Send player seek.
**
**	@param seconds	seek in seconds relative to current position
*/
void PlayerSendSeek(int seconds)
{
    if (ConfigUseSlave) {
	SendCommand("pausing_keep seek %+d 0\n", seconds);
    }
}

/**
**	Send player volume.
*/
void PlayerSendVolume(void)
{
    if (ConfigUseSlave) {
	// FIXME: %.2f could have a problem with LANG
	SendCommand("pausing_keep volume %.2f 1\n",
	    (PlayerVolume * 100.0) / 255);
    }
}

/**
**	Send switch audio track.
*/
void PlayerSendSwitchAudio(void)
{
    if (ConfigUseSlave) {
	SendCommand("pausing_keep switch_audio\n");
    }
}

/**
**	Send select subtitle.
*/
void PlayerSendSubSelect(void)
{
    if (ConfigUseSlave) {
	SendCommand("pausing_keep sub_select\n");
    }
}

/**
**	Send up for dvdnav.
*/
void PlayerSendDvdNavUp(void)
{
    if (ConfigUseSlave) {
	SendCommand("pausing_keep dvdnav up\n");
    }
}

/**
**	Send down for dvdnav.
*/
void PlayerSendDvdNavDown(void)
{
    if (ConfigUseSlave) {
	SendCommand("pausing_keep dvdnav down\n");
    }
}

/**
**	Send left for dvdnav.
*/
void PlayerSendDvdNavLeft(void)
{
    if (ConfigUseSlave) {
	SendCommand("pausing_keep dvdnav left\n");
    }
}

/**
**	Send right for dvdnav.
*/
void PlayerSendDvdNavRight(void)
{
    if (ConfigUseSlave) {
	SendCommand("pausing_keep dvdnav right\n");
    }
}

/**
**	Send select for dvdnav.
*/
void PlayerSendDvdNavSelect(void)
{
    if (ConfigUseSlave) {
	SendCommand("pausing_keep dvdnav select\n");
    }
}

/**
**	Send prev for dvdnav.
*/
void PlayerSendDvdNavPrev(void)
{
    if (ConfigUseSlave) {
	SendCommand("pausing_keep dvdnav prev\n");
    }
}

/**
**	Send menu for dvdnav.
*/
void PlayerSendDvdNavMenu(void)
{
    if (ConfigUseSlave) {
	SendCommand("pausing_keep dvdnav menu\n");
    }
}

/**
**	Start external player.
**
**	@param filename	path and name of file to play
*/
void PlayerStart(const char *filename)
{
    PlayerPipeCnt = 0;			// reset to defaults
    PlayerPipeIdx = 0;

    PlayerPipeIn[0] = -1;
    PlayerPipeIn[1] = -1;
    PlayerPipeOut[0] = -1;
    PlayerPipeOut[1] = -1;
    PlayerPid = 0;

    PlayerPaused = 0;
    PlayerSpeed = 1;

    PlayerDvdNav = 0;

    if (ConfigOsdOverlay) {		// overlay wanted
	VideoSetColorKey(ConfigColorKey);
	VideoInit(ConfigX11Display);
	EnableDummyDevice();
    }
    PlayerForkAndExec(filename);

    if (ConfigOsdOverlay || ConfigUseSlave) {
	PlayerThreadInit();
    }
}

/**
**	Stop external player.
*/
void PlayerStop(void)
{
    PlayerThreadExit();

    //
    //	stop mplayer, if it is still running.
    //
    if (PlayerIsRunning()) {
	int waittime;
	int timeout;

	waittime = 0;
	timeout = 500;			// 0.5s

	kill(PlayerPid, SIGTERM);
	// wait for player finishing, with timeout
	while (PlayerIsRunning() && waittime++ < timeout) {
	    usleep(1 * 1000);
	}
	if (PlayerIsRunning()) {	// still running
	    waittime = 0;
	    timeout = 500;		// 0.5s

	    kill(PlayerPid, SIGKILL);
	    // wait for player finishing, with timeout
	    while (PlayerIsRunning() && waittime++ < timeout) {
		usleep(1 * 1000);
	    }
	    if (PlayerIsRunning()) {
		Error(_("play: can't stop player\n"));
	    }
	}
    }
    PlayerPid = 0;
    PlayerClosePipes();

    if (ConfigOsdOverlay) {
	DisableDummyDevice();
	VideoExit();
    }
}

/**
**	Is external player still running?
*/
int PlayerIsRunning(void)
{
    pid_t wpid;
    int status;

    if (!PlayerPid) {			// no player
	return 0;
    }

    wpid = waitpid(PlayerPid, &status, WNOHANG);
    if (wpid <= 0) {
	return 1;
    }
    if (WIFEXITED(status)) {
	Debug(3, "play: player exited (%d)\n", WEXITSTATUS(status));
    }
    if (WIFSIGNALED(status)) {
	Debug(3, "play: player killed (%d)\n", WTERMSIG(status));
    }
    PlayerPid = 0;
    return 0;
}

/**
**	Set player volume.
**
**	@param volume	new volume (0..255)
*/
void PlayerSetVolume(int volume)
{
    Debug(3, "player: set volume=%d\n", volume);

    if (PlayerVolume != volume) {
	PlayerVolume = volume;
	if (PlayerPid) {
	    PlayerSendVolume();
	}
    }
}

//////////////////////////////////////////////////////////////////////////////
//	Device/Plugin C part
//////////////////////////////////////////////////////////////////////////////

/**
**	Return command line help string.
*/
const char *CommandLineHelp(void)
{
    return "  -% device\tmplayer dvd device\n"
	"  -/\t/dir\tbrowser root directory\n"
	"  -a audio\tmplayer -ao (alsa:device=hw=0.0) overwrites mplayer.conf\n"
	"  -d display\tX11 display (default :0.0) overwrites $DISPLAY\n"
	"  -f\t\tmplayer fullscreen playback\n"
	"  -g geometry\tx11 window geometry wxh+x+y\n"
	"  -k colorkey\tvideo color key (default=0x020507, mplayer2=0x76B901)\n"
	"  -m mplayer\tfilename of mplayer executable\n"
	"  -M args\targuments for mplayer\n"
	"  -o\t\tosd overlay experiments\n" "  -s\t\tmplayer slave mode\n"
	"  -v video\tmplayer -vo (vdpau:deint=4:hqscaling=1) overwrites mplayer.conf\n";
}

/**
**	Process the command line arguments.
**
**	@param argc	number of arguments
**	@param argv	arguments vector
*/
int ProcessArgs(int argc, char *const argv[])
{
    const char *s;

    //
    //	Parse arguments.
    //
#ifdef __FreeBSD__
    if (!strcmp(*argv, "play")) {
	++argv;
	--argc;
    }
#endif
    if ((s = getenv("DISPLAY"))) {
	ConfigX11Display = s;
    }

    for (;;) {
	switch (getopt(argc, argv, "-%:/:b:d:fg:k:m:M:osv:")) {
	    case '%':			// dvd-device
		ConfigMplayerDevice = optarg;
		continue;
	    case '/':			// browser root
		ConfigBrowserRoot = optarg;
		continue;
	    case 'a':			// audio out
		ConfigAudioOut = optarg;
		continue;
	    case 'd':			// display x11
		ConfigX11Display = optarg;
		continue;
	    case 'f':			// fullscreen mode
		ConfigFullscreen = 1;
		continue;
	    case 'g':			// geometry
		VideoSetGeometry(optarg);
		continue;
	    case 'k':			// color key
		ConfigColorKey = strtol(optarg, NULL, 0);
		continue;
	    case 'm':			// mplayer executable
		ConfigMplayer = optarg;
		continue;
	    case 'M':			// mplayer extra arguments
		ConfigMplayerArguments = optarg;
		continue;
	    case 'o':			// osd / overlay
		ConfigOsdOverlay = 1;
		continue;
	    case 's':			// slave mode
		ConfigUseSlave = 1;
		continue;
	    case 'v':			// video out
		ConfigVideoOut = optarg;
		continue;
	    case EOF:
		break;
	    case '-':
		fprintf(stderr, _("We need no long options\n"));
		return 0;
	    case ':':
		fprintf(stderr, _("Missing argument for option '%c'\n"),
		    optopt);
		return 0;
	    default:
		fprintf(stderr, _("Unkown option '%c'\n"), optopt);
		return 0;
	}
	break;
    }
    while (optind < argc) {
	fprintf(stderr, _("Unhandled argument '%s'\n"), argv[optind++]);
    }

    return 1;
}
