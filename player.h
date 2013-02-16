///
///	@file player.h	@brief A play plugin header file.
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

#ifdef __cplusplus
extern "C"
{
#endif
    /// C callback feed key press
    extern void FeedKeyPress(const char *, const char *, int, int);

    /// C callback enable dummy device
    extern void EnableDummyDevice(void);
    /// C callback disable dummy device
    extern void DisableDummyDevice(void);

    /// C plugin get osd size and ascpect
    extern void GetOsdSize(int *, int *, double *);

    /// C plugin open osd
    extern void OsdOpen(void);
    /// C plugin close osd
    extern void OsdClose(void);
    /// C plugin clear osd
    extern void OsdClear(void);
    /// C plugin draw osd pixmap
    extern void OsdDrawARGB(int, int, int, int, const uint8_t *);

    /// C plugin play audio packet
    extern int PlayAudio(const uint8_t *, int, uint8_t);
    /// C plugin play TS audio packet
    extern int PlayTsAudio(const uint8_t *, int);
    /// C plugin set audio volume
    extern void SetVolumeDevice(int);

    /// C plugin play video packet
    extern int PlayVideo(const uint8_t *, int);
    /// C plugin play TS video packet
    extern void PlayTsVideo(const uint8_t *, int);
    /// C plugin grab an image
    extern uint8_t *GrabImage(int *, int, int, int, int);

    /// C plugin set play mode
    extern int SetPlayMode(int);
    /// C plugin get current system time counter
    extern int64_t GetSTC(void);
    /// C plugin get video stream size and aspect
    extern void GetVideoSize(int *, int *, double *);
    /// C plugin set trick speed
    extern void TrickSpeed(int);
    /// C plugin clears all video and audio data from the device
    extern void Clear(void);
    /// C plugin sets the device into play mode
    extern void Play(void);
    /// C plugin sets the device into "freeze frame" mode
    extern void Freeze(void);
    /// C plugin mute audio
    extern void Mute(void);
    /// C plugin display I-frame as a still picture.
    extern void StillPicture(const uint8_t *, int);
    /// C plugin poll if ready
    extern int Poll(int);
    /// C plugin flush output buffers
    extern int Flush(int);

    /// C plugin command line help
    extern const char *CommandLineHelp(void);
    /// C plugin process the command line arguments
    extern int ProcessArgs(int, char *const[]);

    /// C plugin exit + cleanup
    extern void PlayExit(void);
    /// C plugin start code
    extern int Start(void);
    /// C plugin stop code
    extern void Stop(void);
    /// C plugin house keeping
    extern void Housekeeping(void);
    /// C plugin main thread hook
    extern void MainThreadHook(void);

    /// Browser root=start directory
    extern const char *ConfigBrowserRoot;
    extern const char *X11DisplayName;	///< x11 display name

    /// Start external player
    extern void PlayerStart(const char *name);
    /// Stop external player
    extern void PlayerStop(void);
    /// Is external player still running
    extern int PlayerIsRunning(void);

    /// Set player volume
    extern void PlayerSetVolume(int);

#ifdef __cplusplus
}
#endif
