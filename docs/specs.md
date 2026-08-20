## windows and posix gui versions

1. when started with the --headless or -h flag the app starts, loads the config(when there is no other argument given) then starts the server using the settings in the config, and the icon is visible in the tray but not in the taskbar. 
2. when the tray icon is left clicked the main window shows up like normal. 
3. when debug logs are enabled, the app currently writes to the log file and stdout and stderr. when started in console mode and debug mode is off, it should not hold the console session. by this i mean that when a user types `DLNA Server.exe --headless`, it outputs to the console 'server is up' and sends an exitcode 0 if the upnp server(not just the app) starts correctly. after that he should be able to use the same terminal while the app runs in background. but if debug logs are on, the app should use the terminal to show logs, just like the way ping command works on the same terminal till its done or user presses Ctrl+C the startup option should use this flag
4. when the test flags are passed, this one is tricky, pls suggest sth brilliant. wouldnt want the tests to stall coz debug mode is holding the session. the test fags should be classified so ti identify them from user flags.the session should never be held when a test flag is passed. when a test flag is passed with a debug flag, logs are written to file only. session not held, console output is what the flag specifies




## posix non-gui

1. when headless flag is passed, it switches to the gui version and beheaves like the gui version would when headless flag is passed to it. 

2. when debug logs are enabled, the app should write to the log file and stdout and stderr. this means it holds the console session. 

3. when debug logs are not enabled, the server just runs in the bg it outputs to the console 'server is up' and sends an exitcode 0 if the upnp server(not just the app) starts correctly. after that he should be able to use the same terminal while the app runs in background. 

4. when an instance is already running, and the app is called again, it acts as if restarted (kill and start over) with the new args

5. when called app starts, loads configs and sources then replaces the necessary ones with the flags passed, saves the flags into the config settings(so that user doesnt have to type them again next time) then starts the server. sources are not added to the config. the source passes by a flag is always temporary, just like in windows. 

6. when no source is passed the server just hosts the ones in the source list. if there are none in the source list and no source flag is passed, it outputs to console 'no sources found, please add a source or pass one with the --source flag' and exits with exitcode 1.

7. should contain no icons, except the one that the server uses as the server icon seen by clients. no tray no taskbar icon

8. when the test flags are passed, this one is tricky, pls suggest sth brilliant. wouldnt want the tests to stall coz debug mode is holding the session. the test fags should be classified so ti identify them from user flags.the session should never be held when a test flag is passed. when a test flag is passed with a debug flag, logs are written to file only. session not held, console output is what the flag specifies



## both
1. ability to add a single video/audio file as a media source. in the 'add media source' dialogue box make it read "Add a local source or a Network share URL:" as part of this intergration

2. the window should allow drag and drop onto the sources list. when a user drags a file or a folder, the folder is added automatically. the file is  checked if it is a supported filetype or media type then adds to the sojrce list if it os supported.  disallow this when server is already running the cursor could change to show this(if its allowed or not). change the source startup param to handle multiple sources using 1 flag. the argument should have paths closed in double quotes and delimated by commas eg "pathA","pathB"  . the config should store paths in this format too so work on the parsing function

3. i want to be able to add this tool as a context menu item on folders or supported file types. in this requirement, your work is to make the server have this beheviour: when server is on, then user starts another headless instance, the files being served are replaced with the files from the source flag. the source flag just replaces the content then triggers a rescan. however, these sources from the flag are not saved to the config. so every subsequent instance started after the first one changes the source if it has the source flag. mark the debug log setting as a restart required settings since it starts writing on files on next restart. i will use a .reg file (windows) to intergrate to context menus such that when a bunch of files and folders are selected they can be added by "dlna server.exe" --headless --source %*. 
when a media file is dragged on to the exe, it also starts up with --headless --source %*. 
the only time it starts up without auto adding the headless flag is when user opens the app by double clicking (meaning no other params were passed). if source parameter is provided, headless mode will always be on. 
when headless mode is the only flag specifies, it acts as it already does. (app starts as a tray icon,load configs as usual, starts the server.)

4. add `-- kill-server` and `-k` flag that stops the server then closes the app. when any of these two flags are specified, all other ones mentioned are overriden. meaning the server stops and app closes. if app was closed these do nothing

5. every window apart from the main window should not be minimizable. they should not have the maximize or minimize button. these windows take priority focus over the main window. that means that if they are open, you should not be able to operate on the main window. the subwindow flashes signaling you should act on it first

6. [figma designs](https://www.figma.com/design/aP7JU9IkjvALIfuoWqLmtt/DLNA-server-14ag?node-id=0-1&t=mI2omJrvskj4M88v-1)

7. i want accessibility features for standard windows applications. your goal is to come up with a ui accessibility framework that makes the app navigable with keyboard. for example that feature that underlines one letter kn the menu so that you can press that key and jump to it. add this to the buttons too. for the ones that start with the same letter use different underlined letters. let this underlining happen when the user presses a keyboard while the app is in focus. the underlining stops when the user presses any mouse key. apply these accessibility features across all sub windows. 
remove the view logs button from the settings.
 
8. add a command ribbon to the settings page. add menus 'logs' that opens what the button did. add a help menu too that opens a window that displays the apps info like the startup flags and meanings of the various settings available. to close this user will press the window's close button. this means that help window will have no other buttons except the ones on the titlebar.

9. add a functionality that greys out the 'add' button in the 'add media source' and 'default playlist entry' windows until any of the input has at least 1 char.

10. every setting under the media browsing group requires restart. if the user chooses to not restart, save the settings in the config. they will be applied on the next run

11. feature requirement: when user presses 'Stop' to stop server, all pening and ongoing media scans are stopped. every process that was trigerred with 'start' is actively stopped.

12
