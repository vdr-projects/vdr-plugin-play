///
///	@file play.cpp		@brief A play plugin for VDR.
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

#include <vdr/interface.h>
#include <vdr/plugin.h>
#include <vdr/player.h>
#include <vdr/osd.h>
#include <vdr/shutdown.h>
#include <vdr/status.h>
#include <vdr/videodir.h>

#ifdef HAVE_CONFIG
#include "config.h"
#endif

#include "play_service.h"
extern "C"
{
#include "readdir.h"
#include "video.h"
#include "player.h"
}

//////////////////////////////////////////////////////////////////////////////

    /// vdr-plugin version number.
    /// Makefile extracts the version number for generating the file name
    /// for the distribution archive.
static const char *const VERSION = "0.0.14"
#ifdef GIT_REV
    "-GIT" GIT_REV
#endif
    ;

    /// vdr-plugin description.
static const char *const DESCRIPTION = trNOOP("A play plugin");

    /// vdr-plugin text of main menu entry
static const char *MAINMENUENTRY = trNOOP("Play");

//////////////////////////////////////////////////////////////////////////////

static char ConfigHideMainMenuEntry;	///< hide main menu entry
static char ConfigDisableRemote;	///< disable remote during external play

static volatile int DoMakePrimary;	///< switch primary device to this

//////////////////////////////////////////////////////////////////////////////
//	C Callbacks
//////////////////////////////////////////////////////////////////////////////

/**
**	Device plugin remote class.
*/
class cMyRemote:public cRemote
{
  public:

    /**
    **	Soft device remote class constructor.
    **
    **	@param name	remote name
    */
    cMyRemote(const char *name):cRemote(name)
    {
    }

    /**
    **	Put keycode into vdr event queue.
    **
    **	@param code	key code
    **	@param repeat	flag key repeated
    **	@param release	flag key released
    */
    bool Put(const char *code, bool repeat = false, bool release = false) {
	return cRemote::Put(code, repeat, release);
    }
};

/**
**	Feed key press as remote input (called from C part).
**
**	@param keymap	target keymap "XKeymap" name
**	@param key	pressed/released key name
**	@param repeat	repeated key flag
**	@param release	released key flag
*/
extern "C" void FeedKeyPress(const char *keymap, const char *key, int repeat,
    int release)
{
    cRemote *remote;
    cMyRemote *csoft;

    if (!keymap || !key) {
	return;
    }
    // find remote
    for (remote = Remotes.First(); remote; remote = Remotes.Next(remote)) {
	if (!strcmp(remote->Name(), keymap)) {
	    break;
	}
    }
    // if remote not already exists, create it
    if (remote) {
	csoft = (cMyRemote *) remote;
    } else {
	dsyslog("[play]%s: remote '%s' not found\n", __FUNCTION__, keymap);
	csoft = new cMyRemote(keymap);
    }

    //dsyslog("[play]%s %s, %s\n", __FUNCTION__, keymap, key);
    if (key[1]) {			// no single character
	csoft->Put(key, repeat, release);
    } else if (!csoft->Put(key, repeat, release)) {
	cRemote::Put(KBDKEY(key[0]));	// feed it for edit mode
    }
}

/**
**	Disable remotes.
*/
void RemoteDisable(void)
{
    dsyslog("[play]: remote disabled\n");
    cRemote::SetEnabled(false);
}

/**
**	Enable remotes.
*/
void RemoteEnable(void)
{
    dsyslog("[play]: remote enabled\n");
    cRemote::SetEnabled(false);
    cRemote::SetEnabled(true);
}

//////////////////////////////////////////////////////////////////////////////
//	C Callbacks for diashow
//////////////////////////////////////////////////////////////////////////////

#if 0

/**
**	Draw rectangle.
*/
extern "C" void DrawRectangle(int x1, int y1, int x2, int y2, uint32_t argb)
{
    //GlobalDiashow->Osd->DrawRectangle(x1, y1, x2, y2, argb);
}

/**
**	Draw text.
**
**	@param FIXME:
*/
extern "C" void DrawText(int x, int y, const char *s, uint32_t fg, uint32_t bg,
    int w, int h, int align)
{
    const cFont *font;

    font = cFont::GetFont(fontOsd);
    //GlobalDiashow->Osd->DrawText(x, y, s, fg, bg, font, w, h, align);
}

#endif

//////////////////////////////////////////////////////////////////////////////
//	cPlayer
//////////////////////////////////////////////////////////////////////////////

/**
**	Player class.
*/
class cMyPlayer:public cPlayer
{
  private:
    char *FileName;			///< file to play

  public:
     cMyPlayer(const char *);		///< player constructor
     virtual ~ cMyPlayer();		///< player destructor
    void Activate(bool);		///< player attached/detached
    /// get current replay mode
    virtual bool GetReplayMode(bool &, bool &, int &);
};

/**
**	Player constructor.
**
**	@param filename	path and name of file to play
*/
cMyPlayer::cMyPlayer(const char *filename)
:cPlayer(pmExtern_THIS_SHOULD_BE_AVOIDED)
{
    dsyslog("[play]%s: '%s'\n", __FUNCTION__, filename);

    PlayerSetVolume(cDevice::CurrentVolume());
    dsyslog("[play]: initial volume %d\n", cDevice::CurrentVolume());

    FileName = strdup(filename);
    if (ConfigDisableRemote) {
	RemoteDisable();
    }
}

/**
**	Player destructor.
*/
cMyPlayer::~cMyPlayer()
{
    dsyslog("[play]%s: end\n", __FUNCTION__);

    PlayerStop();
    free(FileName);
    if (ConfigDisableRemote) {
	RemoteEnable();
    }
    // FIXME: wait until primary device is switched?
    dsyslog("[play]: device %d->%d\n",
	cDevice::PrimaryDevice()->DeviceNumber(), DoMakePrimary);
}

/**
**	Player attached or detached.
*/
void cMyPlayer::Activate(bool on)
{
    dsyslog("[play]%s: '%s' %d\n", __FUNCTION__, FileName, on);

    if (on) {
	PlayerStart(FileName);
    } else {
	PlayerStop();
    }
}

/**
**	Get current replay mode.
*/
bool cMyPlayer::GetReplayMode(bool & play, bool & forward, int &speed)
{
    play = !PlayerPaused;
    forward = true;
    speed = play ? PlayerSpeed : -1;
    return true;
}

//////////////////////////////////////////////////////////////////////////////
//	cStatus
//////////////////////////////////////////////////////////////////////////////

/**
**	Status class.
**
**	To get volume changes.
*/
class cMyStatus:public cStatus
{
  private:
    int Volume;				///< current volume

  public:
    cMyStatus(void);			///< my status constructor

  protected:
    virtual void SetVolume(int, bool);	///< volume changed
};

cMyStatus *Status;			///< status monitor for volume

/**
**	Status constructor.
*/
cMyStatus::cMyStatus(void)
{
    Volume = 0;
}

/**
**	Called if volume is set.
*/
void cMyStatus::SetVolume(int volume, bool absolute)
{
    dsyslog("[play]: volume %d %s\n", volume, absolute ? "abs" : "rel");

    if (absolute) {
	Volume = volume;
    } else {
	Volume += volume;
    }

    PlayerSetVolume(Volume);
}

//////////////////////////////////////////////////////////////////////////////
//	cControl
//////////////////////////////////////////////////////////////////////////////

/**
**	Our player control class.
*/
class cMyControl:public cControl
{
  private:
    cMyPlayer * Player;			///< our player
    cSkinDisplayReplay *Display;	///< our osd display
    void ShowReplayMode(void);		///< display replay mode
    void ShowProgress(void);		///< display progress bar
    virtual void Show(void);		///< show replay control
    virtual void Hide(void);		///< hide replay control

  public:
    cMyControl(const char *);
    virtual ~ cMyControl();

    virtual eOSState ProcessKey(eKeys);	///< handle keyboard input

};

/**
**	Show replay mode.
*/
void cMyControl::ShowReplayMode(void)
{
    dsyslog("[play]%s: %d - %d\n", __FUNCTION__, Setup.ShowReplayMode,
	cOsd::IsOpen());

    // use vdr setup
    if (Display || (Setup.ShowReplayMode && !cOsd::IsOpen())) {
	bool play;
	bool forward;
	int speed;

	if (GetReplayMode(play, forward, speed)) {
	    if (!Display) {
		// no need to show normal play
		if (play && forward && speed == 1) {
		    return;
		}
		Display = Skins.Current()->DisplayReplay(true);
	    }
	    Display->SetMode(play, forward, speed);
	}
    }
}

/**
**	Show progress.
*/
void cMyControl::ShowProgress(void)
{
    // FIXME:
}

/**
**	Show control.
*/
void cMyControl::Show(void)
{
    dsyslog("[play]%s:\n", __FUNCTION__);
    if (!Display) {
	ShowProgress();
    }
}

/**
**	Control constructor.
**
**	@param filename	pathname of file to play.
*/
cMyControl::cMyControl(const char *filename)
:cControl(Player = new cMyPlayer(filename))
{
    Display = NULL;
    Status = new cMyStatus;		// start monitoring volume

    //LastSkipKey = kNone;
    //LastSkipSeconds = REPLAYCONTROLSKIPSECONDS;
    //LastSkipTimeout.Set(0);
    cStatus::MsgReplaying(this, filename, filename, true);

    cDevice::PrimaryDevice()->ClrAvailableTracks(true);
}

/**
**	Control destructor.
*/
cMyControl::~cMyControl()
{
    dsyslog("[play]%s\n", __FUNCTION__);

    delete Player;

    //delete Display;
    delete Status;

    Hide();
    cStatus::MsgReplaying(this, NULL, NULL, false);
    //Stop();
}

/**
**	Hide control.
*/
void cMyControl::Hide(void)
{
    dsyslog("[play]%s:\n", __FUNCTION__);

    if (Display) {
	delete Display;

	Display = NULL;
	SetNeedsFastResponse(false);
    }
}

/**
**	Process keyboard input.
**
**	@param key	pressed or releaded key
*/
eOSState cMyControl::ProcessKey(eKeys key)
{
    eOSState state;

    if (key != kNone) {
	dsyslog("[play]%s: key=%d\n", __FUNCTION__, key);
    }

    if (!PlayerIsRunning()) {		// check if player is still alive
	dsyslog("[play]: player died\n");
	Hide();
	//FIXME: Stop();
	return osEnd;
    }
    //state=cOsdMenu::ProcessKey(key);
    state = osContinue;
    switch ((int)key) {			// cast to shutup g++ warnings
	case kUp:
	    if (PlayerDvdNav) {
		PlayerSendDvdNavUp();
		break;
	    }
	case kPlay:
	    Hide();
	    if (PlayerSpeed != 1) {
		PlayerSendSetSpeed(PlayerSpeed = 1);
	    }
	    if (PlayerPaused) {
		PlayerSendPause();
		PlayerPaused ^= 1;
	    }
	    ShowReplayMode();
	    break;

	case kDown:
	    if (PlayerDvdNav) {
		PlayerSendDvdNavDown();
		break;
	    }
	case kPause:
	    PlayerSendPause();
	    PlayerPaused ^= 1;
	    ShowReplayMode();
	    break;

	case kFastRew | k_Release:
	case kLeft | k_Release:
	    if (Setup.MultiSpeedMode) {
		break;
	    }
	    // FIXME:
	    break;
	case kLeft:
	    if (PlayerDvdNav) {
		PlayerSendDvdNavLeft();
		break;
	    }
	case kFastRew:
	    if (PlayerSpeed > 1) {
		PlayerSendSetSpeed(PlayerSpeed /= 2);
	    } else {
		PlayerSendSeek(-10);
	    }
	    ShowReplayMode();
	    break;
	case kRight:
	    if (PlayerDvdNav) {
		PlayerSendDvdNavRight();
		break;
	    }
	case kFastFwd:
	    if (PlayerSpeed < 32) {
		PlayerSendSetSpeed(PlayerSpeed *= 2);
	    }
	    ShowReplayMode();
	    break;

	case kRed:
	    // FIXME: TimeSearch();
	    break;

#ifdef USE_JUMPINGSECONDS
	case kGreen | k_Repeat:
	    PlayerSendSeek(-Setup.JumpSecondsRepeat);
	    break;
	case kGreen:
	    PlayerSendSeek(-Setup.JumpSeconds);
	    break;
	case k1 | k_Repeat:
	case k1:
	    PlayerSendSeek(-Setup.JumpSecondsSlow);
	    break;
	case k3 | k_Repeat:
	case k3:
	    PlayerSendSeek(Setup.JumpSecondsSlow);
	    break;
	case kYellow | k_Repeat:
	    PlayerSendSeek(Setup.JumpSecondsRepeat);
	    break;
	case kYellow:
	    PlayerSendSeek(Setup.JumpSeconds);
	    break;
#else
	case kGreen | k_Repeat:
	case kGreen:
	    PlayerSendSeek(-60);
	    break;
	case kYellow | k_Repeat:
	case kYellow:
	    PlayerSendSeek(+60);
	    break;
#endif /* JUMPINGSECONDS */
#ifdef USE_LIEMIKUUTIO
#ifndef USE_JUMPINGSECONDS
	case k1 | k_Repeat:
	case k1:
	    PlayerSendSeek(-20);
	    break;
	case k3 | k_Repeat:
	case k3:
	    PlayerSendSeek(+20);
	    break;
#endif /* JUMPINGSECONDS */
#endif

	case kStop:
	case kBlue:
	    Hide();
	    // FIXME: Stop();
	    return osEnd;

	case kOk:
	    if (PlayerDvdNav) {
		PlayerSendDvdNavSelect();
		// FIXME: PlayerDvdNav = 0;
		break;
	    }
	    // FIXME: full mode
	    ShowReplayMode();
	    break;

	case kBack:
	    if (PlayerDvdNav > 1) {
		PlayerSendDvdNavPrev();
		break;
	    }
	    PlayerSendQuit();
	    // FIXME: need to select old directory and index
	    cRemote::CallPlugin("play");
	    return osBack;

	case kMenu:
	    if (PlayerDvdNav) {
		PlayerSendDvdNavMenu();
		break;
	    }
	    break;

	case kAudio:			// VDR: eats the keys
	case k7:
	    // FIXME: audio menu
	    PlayerSendSwitchAudio();
	    break;
	case kSubtitles:		// VDR: eats the keys
	case k9:
	    // FIXME: subtitle menu
	    PlayerSendSubSelect();
	    break;

	default:
	    break;
    }

    return state;
}

/**
**	Play a file.
**
**	@param filename	path and file name
*/
static void PlayFile(const char *filename)
{
    dsyslog("[play]: play file '%s'\n", filename);
    cControl::Launch(new cMyControl(filename));
}

//////////////////////////////////////////////////////////////////////////////
//	cOsdMenu
//////////////////////////////////////////////////////////////////////////////

static char ShowBrowser;		///< flag show browser
static const char *BrowserStartDir;	///< browser start directory
static const NameFilter *BrowserFilters;	///< browser name filters
static int DirStackSize;		///< size of directory stack
static int DirStackUsed;		///< entries used of directory stack
static char **DirStack;			///< current path directory stack

/**
**	Table of supported video suffixes.
*/
static const NameFilter VideoFilters[] = {
#define FILTER(x) { sizeof(x) - 1, x }
    FILTER(".ts"), FILTER(".avi"), FILTER(".flv"), FILTER(".iso"),
    FILTER(".m4v"), FILTER(".mkv"), FILTER(".mov"), FILTER(".mp4"),
    FILTER(".mpg"), FILTER(".vdr"), FILTER(".vob"), FILTER(".wmv"),
#undef FILTER
    {
	0, NULL}
};

/**
**	Table of supported audio suffixes.
*/
static const NameFilter AudioFilters[] = {
#define FILTER(x) { sizeof(x) - 1, x }
    FILTER(".flac"), FILTER(".mp3"), FILTER(".ogg"), FILTER(".wav"),
#undef FILTER
    {
	0, NULL}
};

/**
**	Table of supported image suffixes.
*/
static const NameFilter ImageFilters[] = {
#define FILTER(x) { sizeof(x) - 1, x }
    FILTER(".cbr"), FILTER(".cbz"), FILTER(".zip"), FILTER(".rar"),
    FILTER(".jpg"), FILTER(".png"),
#undef FILTER
    {
	0, NULL}
};

/**
**	Menu class.
*/
class cBrowser:public cOsdMenu
{
  private:
    const NameFilter *Filter;		///< current filter

    /// Create a browser menu for current directory
    void CreateMenu(void);
    /// Create a browser menu for new directory
    void NewDir(const char *, const NameFilter *);
    /// Handle menu level up
    eOSState LevelUp(void);
    /// Handle menu item selection
    eOSState Selected(void);

  public:
    /// File browser constructor
    cBrowser(const char *, const char *, const NameFilter *);
    /// File browser destructor
    virtual ~ cBrowser();
    /// Process keyboard input
    virtual eOSState ProcessKey(eKeys);
};

/**
**	Add item to menu.  Called from C.
**
**	@param obj	cBrowser object
**	@param text	menu text
*/
extern "C" void cBrowser__Add(void *obj, const char *text)
{
    cBrowser *menu;

    // fucking stupid C++ can't assign void* without warning:
    menu = (typeof(menu)) obj;
    menu->Add(new cOsdItem(text));
}

/**
**	Create browser directory menu.
*/
void cBrowser::CreateMenu(void)
{
    Clear();				// start with empty directory
    // FIXME: should show only directory name in title
    //SetTitle(DirStack[0]);
    Skins.Message(mtStatus, tr("Scanning directory..."));

    if (DirStackUsed > 1) {
	// FIXME: should show only path
	Add(new cOsdItem(DirStack[0]));
    }
    ReadDirectory(DirStack[0], 1, NULL, cBrowser__Add, this);
    ReadDirectory(DirStack[0], 0, Filter, cBrowser__Add, this);
    // FIXME: handle errors!

    Display();				// display build menu
    Skins.Message(mtStatus, NULL);	// clear read message
}

/**
**	Create directory menu.
**
**	@param path	directory path file name
**	@param filter	name selection filter
*/
void cBrowser::NewDir(const char *path, const NameFilter * filter)
{
    int n;
    char *pathname;

    n = strlen(path);
#if 1
    // FIXME: force caller to do
    if (path[n - 1] == '/') {		// force '/' terminated
	pathname = strdup(path);
    } else {
	pathname = (char *)malloc(n + 2);
	stpcpy(stpcpy(pathname, path), "/");
    }
#endif

    // push on directory stack
    if (DirStackUsed >= DirStackSize) {	// increase stack size
	DirStackSize = DirStackUsed + 1;
	DirStack =
	    (typeof(DirStack)) realloc(DirStack,
	    DirStackSize * sizeof(*DirStack));
    }
    memmove(DirStack + 1, DirStack, DirStackUsed * sizeof(*DirStack));
    DirStackUsed++;
    DirStack[0] = pathname;

    Filter = filter;

    CreateMenu();
}

/**
**	Menu constructor.
**
**	@param title	menu title
**	@param path	directory path file name
**	@param filter	name selection filter
*/
cBrowser::cBrowser(const char *title, const char *path,
    const NameFilter * filter)
:cOsdMenu(title)
{
    dsyslog("[play]%s:\n", __FUNCTION__);

    if (path) {				// clear stack, start new
	int i;

	// free the stored directory stack
	for (i = 0; i < DirStackUsed; ++i) {
	    free(DirStack[i]);
	}
	//free(DirStack);
	//DirStack = NULL;		// reuse the old stack
	DirStackUsed = 0;

	NewDir(path, filter);
	return;
    }

    Filter = filter;

    CreateMenu();
}

/**
**	Menu destructor.
*/
cBrowser::~cBrowser()
{
    dsyslog("[play]%s:\n", __FUNCTION__);
}

/**
**	Handle level up.
*/
eOSState cBrowser::LevelUp(void)
{
    char *down;
    char *name;

    if (DirStackUsed <= 1) {		// top level reached
	return osEnd;
    }
    // go level up
    --DirStackUsed;
    down = DirStack[0];
    memmove(DirStack, DirStack + 1, DirStackUsed * sizeof(*DirStack));

    CreateMenu();

    // select item, where we gone down
    down[strlen(down) - 1] = '\0';	// remove trailing '/'
    name = strrchr(down, '/');
    if (name) {
	cOsdItem *item;
	const char *text;
	int i;

	for (i = 0; (item = Get(i)); ++i) {
	    text = item->Text();
	    if (!strcmp(text, name + 1)) {
		SetCurrent(item);
		// FIXME: Display already called!
		Display();		// display build menu
		break;
	    }
	}
    }

    free(down);

    return osContinue;
}

/**
**	Handle selected item.
*/
eOSState cBrowser::Selected(void)
{
    int current;
    const cOsdItem *item;
    const char *text;
    char *filename;
    char *tmp;

    current = Current();		// get current menu item index
    item = Get(current);
    text = item->Text();

    if (current == 0 && DirStackUsed > 1) {
	return LevelUp();
    }
    // +2: \0 + #
    filename = (char *)malloc(strlen(DirStack[0]) + strlen(text) + 2);
    // path is '/' terminated
    tmp = stpcpy(stpcpy(filename, DirStack[0]), text);
    if (!IsDirectory(filename)) {
	if (IsArchive(filename)) {	// handle archives
	    stpcpy(tmp, "#");
	    NewDir(filename, Filter);
	    free(filename);
	    // FIXME: if dir fails use keep old!
	    return osContinue;
	}
	PlayFile(filename);
	free(filename);
	return osEnd;
    }
    // handle DVD image
    if (!strcmp(text, "AUDIO_TS") || !strcmp(text, "VIDEO_TS")) {
	free(filename);
	tmp = (char *)malloc(sizeof("dvdnav:///") + strlen(DirStack[0]));
	strcpy(stpcpy(tmp, "dvdnav:///"), DirStack[0]);
	PlayFile(tmp);
	free(tmp);
	return osEnd;
    }
    stpcpy(tmp, "/");			// append '/'
    NewDir(filename, Filter);
    free(filename);
    // FIXME: if dir fails use keep old!
    return osContinue;
}

/**
**	Handle Menu key event.
**
**	@param key	key event
*/
eOSState cBrowser::ProcessKey(eKeys key)
{
    eOSState state;

    // call standard function
    state = cOsdMenu::ProcessKey(key);
    if (state || key != kNone) {
	dsyslog("[play]%s: state=%d key=%d\n", __FUNCTION__, state, key);
    }

    switch (state) {
	case osUnknown:
	    switch (key) {
		case kOk:
		    return Selected();
		case kBack:
		    return LevelUp();
		default:
		    break;
	    }
	    break;
	case osBack:
	    state = LevelUp();
	    if (state == osEnd) {	// top level reached
		ShowBrowser = 0;
		return osPlugin;
	    }
	default:
	    break;
    }
    return state;
}

//////////////////////////////////////////////////////////////////////////////
//	cOsdMenu
//////////////////////////////////////////////////////////////////////////////

/**
**	Play plugin menu class.
*/
class cPlayMenu:public cOsdMenu
{
  private:
  public:
    cPlayMenu(const char *, int = 0, int = 0, int = 0, int = 0, int = 0);
    virtual ~ cPlayMenu();
    virtual eOSState ProcessKey(eKeys);
};

/**
**	Play menu constructor.
*/
cPlayMenu::cPlayMenu(const char *title, int c0, int c1, int c2, int c3, int c4)
:cOsdMenu(title, c0, c1, c2, c3, c4)
{
    SetHasHotkeys();

    Add(new cOsdItem(hk(tr("Browse")), osUser1));
    Add(new cOsdItem(hk(tr("Play optical disc")), osUser2));
    Add(new cOsdItem(""));
    Add(new cOsdItem(""));
    Add(new cOsdItem(hk(tr("Play audio CD")), osUser5));
    Add(new cOsdItem(hk(tr("Play video DVD")), osUser6));
    Add(new cOsdItem(hk(tr("Browse audio")), osUser7));
    Add(new cOsdItem(hk(tr("Browse image")), osUser8));
    Add(new cOsdItem(hk(tr("Browse video")), osUser9));
}

/**
**	Play menu destructor.
*/
cPlayMenu::~cPlayMenu()
{
}

/**
**	Handle play plugin menu key event.
**
**	@param key	key event
*/
eOSState cPlayMenu::ProcessKey(eKeys key)
{
    eOSState state;

    if (key != kNone) {
	dsyslog("[play]%s: key=%d\n", __FUNCTION__, key);
    }
    // call standard function
    state = cOsdMenu::ProcessKey(key);

    switch (state) {
	case osUser1:
	    ShowBrowser = 1;
	    BrowserStartDir = ConfigBrowserRoot;
	    BrowserFilters = NULL;
	    return osPlugin;		// restart with OSD browser

	case osUser3:			// play audio cdrom
	    PlayFile("cdda://");
	    return osEnd;
	case osUser4:			// play dvd
	    PlayFile("dvdnav://");
	    return osEnd;

	case osUser5:
	    ShowBrowser = 1;
	    BrowserStartDir = ConfigBrowserRoot;
	    BrowserFilters = AudioFilters;
	    return osPlugin;		// restart with OSD browser
	case osUser6:
	    ShowBrowser = 1;
	    BrowserStartDir = ConfigBrowserRoot;
	    BrowserFilters = ImageFilters;
	    return osPlugin;		// restart with OSD browser
	case osUser7:
	    ShowBrowser = 1;
	    BrowserStartDir = ConfigBrowserRoot;
	    BrowserFilters = VideoFilters;
	    return osPlugin;		// restart with OSD browser

#if 0
	case osUser9:
	    free(ShowDiashow);
	    ShowDiashow = strdup(VideoDirectory);
	    return osPlugin;		// restart with OSD browser
#endif

	default:
	    break;
    }
    return state;
}

//////////////////////////////////////////////////////////////////////////////
//	cOsd
//////////////////////////////////////////////////////////////////////////////

/**
**	My device plugin OSD class.
*/
class cMyOsd:public cOsd
{
  public:
    static volatile char Dirty;		///< flag force redraw everything
    int OsdLevel;			///< current osd level

    cMyOsd(int, int, uint);		///< osd constructor
    virtual ~ cMyOsd(void);		///< osd destructor
    virtual void Flush(void);		///< commits all data to the hardware
    virtual void SetActive(bool);	///< sets OSD to be the active one
};

volatile char cMyOsd::Dirty;		///< flag force redraw everything

/**
**	Sets this OSD to be the active one.
**
**	@param on	true on, false off
**
**	@note only needed as workaround for text2skin plugin with
**	undrawn areas.
*/
void cMyOsd::SetActive(bool on)
{
    dsyslog("[play]%s: %d\n", __FUNCTION__, on);

    if (Active() == on) {
	return;				// already active, no action
    }
    cOsd::SetActive(on);

    // ignore sub-title, if menu is open
    if (OsdLevel >= OSD_LEVEL_SUBTITLES && IsOpen()) {
	return;
    }

    if (on) {
	Dirty = 1;
	// only flush here if there are already bitmaps
	//if (GetBitmap(0)) {
	//    Flush();
	//}
	OsdOpen();
    } else {
	OsdClose();
    }
}

/**
**	Constructor OSD.
**
**	Initializes the OSD with the given coordinates.
**
**	@param left	x-coordinate of osd on display
**	@param top	y-coordinate of osd on display
**	@param level	level of the osd (smallest is shown)
*/
cMyOsd::cMyOsd(int left, int top, uint level)
:cOsd(left, top, level)
{
    /* FIXME: OsdWidth/OsdHeight not correct!
       dsyslog("[play]%s: %dx%d+%d+%d, %d\n", __FUNCTION__, OsdWidth(),
       OsdHeight(), left, top, level);
     */

    OsdLevel = level;
    SetActive(true);
}

/**
**	OSD Destructor.
**
**	Shuts down the OSD.
*/
cMyOsd::~cMyOsd(void)
{
    dsyslog("[play]%s:\n", __FUNCTION__);
    SetActive(false);
    // done by SetActive: OsdClose();
}

/**
**	Actually commits all data to the OSD hardware.
*/
void cMyOsd::Flush(void)
{
    cPixmapMemory *pm;

    dsyslog("[play]%s: level %d active %d\n", __FUNCTION__, OsdLevel,
	Active());

    if (!Active()) {			// this osd is not active
	return;
    }
    // don't draw sub-title if menu is active
    if (OsdLevel >= OSD_LEVEL_SUBTITLES && IsOpen()) {
	return;
    }
    //
    //	VDR draws subtitle without clearing the old
    //
    if (OsdLevel >= OSD_LEVEL_SUBTITLES) {
	OsdClear();
	cMyOsd::Dirty = 1;
	dsyslog("[play]%s: subtitle clear\n", __FUNCTION__);
    }

    if (!IsTrueColor()) {
	cBitmap *bitmap;
	int i;

	// draw all bitmaps
	for (i = 0; (bitmap = GetBitmap(i)); ++i) {
	    uint8_t *argb;
	    int x;
	    int y;
	    int w;
	    int h;
	    int x1;
	    int y1;
	    int x2;
	    int y2;

	    // get dirty bounding box
	    if (Dirty) {		// forced complete update
		x1 = 0;
		y1 = 0;
		x2 = bitmap->Width() - 1;
		y2 = bitmap->Height() - 1;
	    } else if (!bitmap->Dirty(x1, y1, x2, y2)) {
		continue;		// nothing dirty continue
	    }
	    // convert and upload only dirty areas
	    w = x2 - x1 + 1;
	    h = y2 - y1 + 1;
	    if (1) {			// just for the case it makes trouble
		int width;
		int height;
		double video_aspect;

		::GetOsdSize(&width, &height, &video_aspect);
		if (w > width) {
		    w = width;
		    x2 = x1 + width - 1;
		}
		if (h > height) {
		    h = height;
		    y2 = y1 + height - 1;
		}
	    }
#ifdef DEBUG
	    if (w > bitmap->Width() || h > bitmap->Height()) {
		esyslog(tr("[play]: dirty area too big\n"));
		abort();
	    }
#endif
	    argb = (uint8_t *) malloc(w * h * sizeof(uint32_t));
	    for (y = y1; y <= y2; ++y) {
		for (x = x1; x <= x2; ++x) {
		    ((uint32_t *) argb)[x - x1 + (y - y1) * w] =
			bitmap->GetColor(x, y);
		}
	    }
	    dsyslog("[play]%s: draw %dx%d%+d%+d bm\n", __FUNCTION__, w, h,
		Left() + bitmap->X0() + x1, Top() + bitmap->Y0() + y1);
	    OsdDrawARGB(Left() + bitmap->X0() + x1, Top() + bitmap->Y0() + y1,
		w, h, argb);

	    bitmap->Clean();
	    // FIXME: reuse argb
	    free(argb);
	}
	cMyOsd::Dirty = 0;
	return;
    }

    LOCK_PIXMAPS;
    while ((pm = RenderPixmaps())) {
	int x;
	int y;
	int w;
	int h;

	x = Left() + pm->ViewPort().X();
	y = Top() + pm->ViewPort().Y();
	w = pm->ViewPort().Width();
	h = pm->ViewPort().Height();

	dsyslog("[play]%s: draw %dx%d%+d%+d %p\n", __FUNCTION__, w, h, x, y,
	    pm->Data());
	OsdDrawARGB(x, y, w, h, pm->Data());

	delete pm;
    }
    cMyOsd::Dirty = 0;
}

//////////////////////////////////////////////////////////////////////////////
//	cOsdProvider
//////////////////////////////////////////////////////////////////////////////

/**
**	My device plugin OSD provider class.
*/
class cMyOsdProvider:public cOsdProvider
{
  private:
    static cOsd *Osd;

  public:
    virtual cOsd * CreateOsd(int, int, uint);
    virtual bool ProvidesTrueColor(void);
    cMyOsdProvider(void);
};

cOsd *cMyOsdProvider::Osd;		///< single osd

/**
**	Create a new OSD.
**
**	@param left	x-coordinate of OSD
**	@param top	y-coordinate of OSD
**	@param level	layer level of OSD
*/
cOsd *cMyOsdProvider::CreateOsd(int left, int top, uint level)
{
    dsyslog("[play]%s: %d, %d, %d\n", __FUNCTION__, left, top, level);

    return Osd = new cMyOsd(left, top, level);
}

/**
**	Check if this OSD provider is able to handle a true color OSD.
**
**	@returns true we are able to handle a true color OSD.
*/
bool cMyOsdProvider::ProvidesTrueColor(void)
{
    return true;
}

/**
**	Create cOsdProvider class.
*/
cMyOsdProvider::cMyOsdProvider(void)
:  cOsdProvider()
{
    dsyslog("[play]%s:\n", __FUNCTION__);
}

//////////////////////////////////////////////////////////////////////////////
//	cMenuSetupPage
//////////////////////////////////////////////////////////////////////////////

/**
**	Play plugin menu setup page class.
*/
class cMyMenuSetupPage:public cMenuSetupPage
{
  protected:
    ///
    /// local copies of global setup variables:
    /// @{
    int HideMainMenuEntry;
    int DisableRemote;

    /// @}
    virtual void Store(void);

  public:
     cMyMenuSetupPage(void);
    virtual eOSState ProcessKey(eKeys);	// handle input
};

/**
**	Process key for setup menu.
*/
eOSState cMyMenuSetupPage::ProcessKey(eKeys key)
{
    eOSState state;

    state = cMenuSetupPage::ProcessKey(key);

    return state;
}

/**
**	Constructor setup menu.
**
**	Import global config variables into setup.
*/
cMyMenuSetupPage::cMyMenuSetupPage(void)
{
    HideMainMenuEntry = ConfigHideMainMenuEntry;
    DisableRemote = ConfigDisableRemote;

    Add(new cMenuEditBoolItem(tr("Hide main menu entry"), &HideMainMenuEntry,
	    trVDR("no"), trVDR("yes")));
    Add(new cMenuEditBoolItem(tr("Disable remote"), &DisableRemote,
	    trVDR("no"), trVDR("yes")));
}

/**
**	Store setup.
*/
void cMyMenuSetupPage::Store(void)
{
    SetupStore("HideMainMenuEntry", ConfigHideMainMenuEntry =
	HideMainMenuEntry);
    SetupStore("DisableRemote", ConfigDisableRemote = DisableRemote);
}

//////////////////////////////////////////////////////////////////////////////
//	cDevice
//////////////////////////////////////////////////////////////////////////////

/**
**	Dummy device class.
*/
class cMyDevice:public cDevice
{
  public:
    cMyDevice(void);
    virtual ~ cMyDevice(void);

    virtual void GetOsdSize(int &, int &, double &);

  protected:
    virtual void MakePrimaryDevice(bool);
};

/**
**	Device constructor.
*/
cMyDevice::cMyDevice(void)
{
    dsyslog("[play]%s\n", __FUNCTION__);
}

/**
**	Device destructor. (never called!)
*/
cMyDevice::~cMyDevice(void)
{
    dsyslog("[play]%s:\n", __FUNCTION__);
}

/**
**	Informs a device that it will be the primary device.
**
**	@param on	flag if becoming or loosing primary
*/
void cMyDevice::MakePrimaryDevice(bool on)
{
    dsyslog("[play]%s: %d\n", __FUNCTION__, on);

    cDevice::MakePrimaryDevice(on);
    if (on) {
	new cMyOsdProvider();
    }
}

/**
**	Returns the width, height and pixel_aspect ratio the OSD.
**
**	FIXME: Called every second, for nothing (no OSD displayed)?
*/
void cMyDevice::GetOsdSize(int &width, int &height, double &pixel_aspect)
{
    if (!&width || !&height || !&pixel_aspect) {
	esyslog(tr("[play]: GetOsdSize invalid pointer(s)\n"));
	return;
    }
    ::GetOsdSize(&width, &height, &pixel_aspect);
}

//////////////////////////////////////////////////////////////////////////////
//	cPlugin
//////////////////////////////////////////////////////////////////////////////

static cMyDevice *MyDevice;		///< dummy device needed for osd

class cMyPlugin:public cPlugin
{
  public:
    cMyPlugin(void);
    virtual ~ cMyPlugin(void);
    virtual const char *Version(void);
    virtual const char *Description(void);
    virtual const char *CommandLineHelp(void);
    virtual bool ProcessArgs(int, char *[]);
    virtual bool Initialize(void);
    virtual void MainThreadHook(void);
    virtual const char *MainMenuEntry(void);
    virtual cOsdObject *MainMenuAction(void);
    virtual cMenuSetupPage *SetupMenu(void);
    virtual bool SetupParse(const char *, const char *);
    virtual bool Service(const char *, void * = NULL);
    virtual const char **SVDRPHelpPages(void);
    virtual cString SVDRPCommand(const char *, const char *, int &);
};

/**
**	Initialize any member variables here.
**
**	@note DON'T DO ANYTHING ELSE THAT MAY HAVE SIDE EFFECTS, REQUIRE GLOBAL
**	VDR OBJECTS TO EXIST OR PRODUCE ANY OUTPUT!
*/
cMyPlugin::cMyPlugin(void)
{
    dsyslog("[play]%s:\n", __FUNCTION__);
}

/**
**	Clean up after yourself!
*/
cMyPlugin::~cMyPlugin(void)
{
    dsyslog("[play]%s:\n", __FUNCTION__);
}

/**
**	Return plugin version number.
**
**	@returns version number as constant string.
*/
const char *cMyPlugin::Version(void)
{
    return VERSION;
}

/**
**	Return plugin short description.
**
**	@returns short description as constant string.
*/
const char *cMyPlugin::Description(void)
{
    return tr(DESCRIPTION);
}

/**
**	Return a string that describes all known command line options.
**
**	@returns command line help as constant string.
*/
const char *cMyPlugin::CommandLineHelp(void)
{
    return::CommandLineHelp();
}

/**
**	Process the command line arguments.
*/
bool cMyPlugin::ProcessArgs(int argc, char *argv[])
{
    return::ProcessArgs(argc, argv);
}

/**
**	Start any background activities the plugin shall perform.
*/
bool cMyPlugin::Initialize(void)
{
    //dsyslog("[play]%s:\n", __FUNCTION__);

    // FIXME: can delay until needed?
    //Status = new cMyStatus;		// start monitoring
    // FIXME: destructs memory

    MyDevice = new cMyDevice;
    return true;
}

/**
**	Create main menu entry.
*/
const char *cMyPlugin::MainMenuEntry(void)
{
    return ConfigHideMainMenuEntry ? NULL : tr(MAINMENUENTRY);
}

/**
**	Perform the action when selected from the main VDR menu.
*/
cOsdObject *cMyPlugin::MainMenuAction(void)
{
    //dsyslog("[play]%s:\n", __FUNCTION__);

#if 0
    printf("plugin %s %d\n", ShowDiashow, ShowBrowser);
    if (ShowDiashow) {
	return new cDiashow(ShowDiashow);
    }
#endif
    if (ShowBrowser) {
	const char *start;

	start = BrowserStartDir;	// root only one time
	BrowserStartDir = NULL;
	return new cBrowser("Browse", start, BrowserFilters);
    }
    return new cPlayMenu("Play");
}

/**
**	Receive requests or messages.
**
**	@param id	unique identification string that identifies the
**			service protocol
**	@param data	custom data structure
*/
bool cMyPlugin::Service(const char *id, void *data)
{
    if (strcmp(id, PLAY_OSD_3DMODE_SERVICE) == 0) {
	VideoSetOsd3DMode(0);
	Play_Osd3DModeService_v1_0_t *r =
	    (Play_Osd3DModeService_v1_0_t *) data;
	VideoSetOsd3DMode(r->Mode);
	return true;
    }
    return false;
}

/**
**	Return SVDRP commands help pages.
**
**	return a pointer to a list of help strings for all of the plugin's
**	SVDRP commands.
*/
const char **cMyPlugin::SVDRPHelpPages(void)
{
    static const char *HelpPages[] = {
	"3DOF\n" "	  TURN OFF 3D", "3DTB\n" "    TURN ON 3D TB",
	"3DSB\n" "	  TURN ON 3D SBS", NULL
    };
    return HelpPages;
}

/**
**	Handle SVDRP commands.
**
**	@param command		SVDRP command
**	@param option		all command arguments
**	@param reply_code	reply code
*/
cString cMyPlugin::SVDRPCommand(const char *command,
    __attribute__ ((unused)) const char *option,
    __attribute__ ((unused)) int &reply_code)
{
    if (!strcasecmp(command, "3DOF")) {
	VideoSetOsd3DMode(0);
	return "3d off";
    }
    if (!strcasecmp(command, "3DSB")) {
	VideoSetOsd3DMode(1);
	return "3d sbs";
    }
    if (!strcasecmp(command, "3DTB")) {
	VideoSetOsd3DMode(2);
	return "3d tb";
    }
    return NULL;
}

/**
**	Called for every plugin once during every cycle of VDR's main program
**	loop.
*/
void cMyPlugin::MainThreadHook(void)
{
    // dsyslog("[play]%s:\n", __FUNCTION__);

    if (DoMakePrimary) {
	dsyslog("[play]: switching primary device to %d\n", DoMakePrimary);
	cDevice::SetPrimaryDevice(DoMakePrimary);
	DoMakePrimary = 0;
    }
}

/**
**	Return our setup menu.
*/
cMenuSetupPage *cMyPlugin::SetupMenu(void)
{
    //dsyslog("[play]%s:\n", __FUNCTION__);

    return new cMyMenuSetupPage;
}

/**
**	Parse setup parameters
**
**	@param name	paramter name (case sensetive)
**	@param value	value as string
**
**	@returns true if the parameter is supported.
*/
bool cMyPlugin::SetupParse(const char *name, const char *value)
{
    dsyslog("[play]%s: '%s' = '%s'\n", __FUNCTION__, name, value);

    if (!strcasecmp(name, "HideMainMenuEntry")) {
	ConfigHideMainMenuEntry = atoi(value);
	return true;
    }
    if (!strcasecmp(name, "DisableRemote")) {
	ConfigDisableRemote = atoi(value);
	return true;
    }
#if 0
    if (!strncasecmp(name, "Dia.", 4)) {
	return DiaConfigParse(name + 4, value);
    }
#endif

    return false;
}

//////////////////////////////////////////////////////////////////////////////

int OldPrimaryDevice;			///< old primary device

/**
**	Enable dummy device.
*/
extern "C" void EnableDummyDevice(void)
{
    OldPrimaryDevice = cDevice::PrimaryDevice()->DeviceNumber() + 1;
    DoMakePrimary = MyDevice->DeviceNumber() + 1;
    cOsdProvider::Shutdown();
}

/**
**	Disable dummy device.
*/
extern "C" void DisableDummyDevice(void)
{
    DoMakePrimary = OldPrimaryDevice;
    OldPrimaryDevice = 0;
    cOsdProvider::Shutdown();
}

VDRPLUGINCREATOR(cMyPlugin);		// Don't touch this!
