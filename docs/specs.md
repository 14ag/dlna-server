## windows and posix gui versions
### A: flags
1. when started with the `--headless` or `-h` flag the app starts, loads the config(when there is no other argument given) then starts the server using the settings in the config, and the icon is visible in the tray but not in the taskbar. 


2. when debug logs are enabled, the app currently writes to the log file and stdout and stderr. when started in console mode and debug mode is off, it should not hold the console session. by this i mean that when a user types `DLNA Server.exe --headless`, it outputs to the console 'server is up' and sends an exitcode 0 if the upnp server(not just the app) starts correctly. after that he should be able to use the same terminal while the app runs in background. but if debug logs are on, the app should use the terminal to show logs, just like the way ping command works on the same terminal till its done or user presses Ctrl+C the startup option should use this flag

3. the test flags should be classified so to identify them from user flags.the session should never be held when a test flag is passed. when a test flag is passed with a debug flag, logs are written to file only. session not held, console output is what the flag specifies


---

### B: lifecycle
1. when the tray icon is left clicked the main window shows up like normal. 


---

### C: ux/ui
1. on the main ui, tab key switches focus between the buttons row(toolbar) section and the source-list(listbox) section while on the buttons row section, user presses side arrow keys to cycle between the buttons and up and down buttons behave as they currently do.

2. when user selects a source(s) in the source list section, pressing 'd' key should act as the acess key for deleting the selected item(s)

3. as soon as the access keys are underlined, if those keys are pressed as next input they should trigger the action 

4. the apps accessibility features should be extended to the source selection window (which is behind the 'add' button) and default playlist entry window. escape and backspace key should close any **subwindow** that is in focus emphasis on (in focus) as long as its not the main window. i am talking about any dialogue/warning box, the settings, help, default playlist window, add sources window. the logs and help menus in settings need access keys.

5. all the gui windows and subwindows should not be resizable

6. on the main window, when the server is on, change the purpose of the `add` button on the main UI to be used to `rescan` for new media from the folders that have already been added as a source.  this means that the button should change from 'Add' to 'Scan' the same way 'start' changes to 'stop' when the server is on. there will be no automatic background rescanning by default(toggled in settings). the scan process should be safely multi-threaded, and handles in a way that the app does not hang or crash when user clicks anything else on the window.

7. when the user presses `start`, the `Delete` `Start` and `Add` button buttons get greyed out as they wait to change to 'Stop' and 'Scan' respectively. the delete button remains active and if the user presses it to delete a selected source, that action is queued till the server is up. the settings button should always be available. i expect the functions with a longer burst time to be asyncronous in relation tothe write operations that the settings UI does when user clicks okay

8. on the main UI, simplify the verbose messages ie change 'DLNA Server is running on ${ip:port}' to 'Server running'. when the 'Rescan' is pressed, it should write 'scanning...' then back to 'Server running.' when the scan ends. instead of 'DLNA Server is stopped', let it show no text at all. the details ommited are already in the logs (when enabled) so this should clean up the UI

9. the delete button should be disabled by default. on the main window, when a user selects an entry from the source list, the delete button becomes active. when the user clicks on any other part of the main UI, the previously selected source returns to normal (unselected) and the delete button back to disabled state. if user clicks delete button after selecting a source, the source is removed from the list and the delete button returns to disabled state. if user selects another source, the delete button becomes active again. if user clicks on any other part of the main UI, the previously selected source returns to normal (unselected) and the delete button back to disabled state. the delete button must not become inactive when the user clicks on it right after selecting a source. the button is always active as long as a source in the source list is in the state (selected). after selecting a source, clicking any where else other than the delete button, changes the state of the source to not selected, hence delete button unavailable

10. in the settings, when the user clicks ok, changes are written to the config file. app then immediately applies the changes that do not need the server to restart. it then checks if he made any changes that require the server to be restarted for the changes to be applied. if there are, the app checks if the server is in state 'running'. if yes, a popup should show up right after clicking ok informing that a server restart will be needed to apply changes with a question at the end "restart server now?". this popup should have 2 buttons: yes and no. choosing yes will stop then start the server. choosing 'no' will just close this popup and do nothing

11. every window apart from the main window should not be minimizable. they should not have the maximize or minimize button. these windows take priority focus over the main window. that means that if they are open, you should not be able to operate on the main window. the subwindow flashes signaling you should act on it first

12. [figma designs](https://www.figma.com/design/aP7JU9IkjvALIfuoWqLmtt/DLNA-server-14ag?node-id=0-1&t=mI2omJrvskj4M88v-1)

13. i want accessibility features for standard windows applications. your goal is to come up with a ui accessibility framework that makes the app navigable with keyboard. for example that feature that underlines one letter kn the menu so that you can press that key and jump to it. add this to the buttons too. for the ones that start with the same letter use different underlined letters. let this underlining happen when the user presses a keyboard while the app is in focus. the underlining stops when the user presses any mouse key. apply these accessibility features across all sub windows. 
remove the view logs button from the settings.
 
14. add a command ribbon to the settings page. add menus 'logs' that opens what the button did. add a help menu too that opens a window that displays the apps info like the startup flags and meanings of the various settings available. to close this user will press the window's close button. this means that help window will have no other buttons except the ones on the titlebar.

15. add a functionality that greys out the 'add' button in the 'add media source' and 'default playlist entry' windows until any of the input has at least 1 char.

16. every setting under the media browsing group requires restart.

17. when user presses 'Stop' to stop server, all pending and ongoing media scans are stopped. every process that was trigerred with 'start' is actively stopped.



## windows gui
- the firewall function should generates and saves rules as `DLNA Server-n` where n is the first 5 digits of the hash calculated from the application executable's path




## posix-gui

1. the file browser should use the native file browse app

---

## posix non-gui

1. when headless flag is passed, it switches to the gui version and beheaves like the gui version would when headless flag is passed to it. 

2. when debug logs are enabled, the app should write to the log file and stdout and stderr. this means it holds the console session. 

3. when debug logs are not enabled, the server just runs in the bg it outputs to the console 'server is up' and sends an exitcode 0 if the upnp server(not just the app) starts correctly. after that he should be able to use the same terminal while the app runs in background. 

4. when an instance is already running, and the app is called again, it acts as if restarted (kill and start over) with the new args

5. when called app starts, loads configs and sources then replaces the necessary ones with the flags passed, saves the flags into the config settings(so that user doesnt have to type them again next time) then starts the server. sources are not added to the config. the source passes by a flag is always temporary, just like in windows. 

6. when no source is passed the server just hosts the ones in the source list. if there are none in the source list and no source flag is passed, it outputs to console 'no sources found, please add a source or pass one with the --source flag' and exits with exitcode 1.

7. should contain no icons, except the one that the server uses as the server icon seen by clients. no tray no taskbar icon

8. when the test flags are passed, this one is tricky, pls suggest sth brilliant. wouldnt want the tests to stall coz debug mode is holding the session. the test fags should be classified so ti identify them from user flags.the session should never be held when a test flag is passed. when a test flag is passed with a debug flag, logs are written to file only. session not held, console output is what the flag specifies

---

## dlna-server backend
1. ability to add a single video/audio file as a media source. in the 'add media source' dialogue box make it read "Add a local source or a Network share URL:" as part of this intergration

2. the window should allow drag and drop onto the sources list. when a user drags a file or a folder, the folder is added automatically. the file is  checked if it is a supported filetype or media type then adds to the sojrce list if it os supported.  disallow this when server is already running the cursor could change to show this(if its allowed or not). change the source startup param to handle multiple sources using 1 flag. the argument should have paths closed in double quotes and delimated by commas eg "pathA","pathB"  . the config should store paths in this format too so work on the parsing function

3. i want to be able to add this tool as a context menu item on folders or supported file types. in this requirement, your work is to make the server have this beheviour: when server is on, then user starts another headless instance, the files being served are replaced with the files from the source flag. the source flag just replaces the content then triggers a rescan. however, these sources from the flag are not saved to the config. so every subsequent instance started after the first one changes the source if it has the source flag. mark the debug log setting as a restart required settings since it starts writing on files on next restart. i will use a .reg file (windows) to intergrate to context menus such that when a bunch of files and folders are selected they can be added by "dlna server.exe" --headless --source %*. 
when a media file is dragged on to the exe, it also starts up with --headless --source %*. 
the only time it starts up without auto adding the headless flag is when user opens the app by double clicking (meaning no other params were passed). if source parameter is provided, headless mode will always be on. 
when headless mode is the only flag specifies, it acts as it already does. (app starts as a tray icon,load configs as usual, starts the server.)

4. add `-- kill-server` and `-k` flag that stops the server then closes the app. when any of these two flags are specified, all other ones mentioned are overriden. meaning the server stops and app closes. if app was closed these do nothing
