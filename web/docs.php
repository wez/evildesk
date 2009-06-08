<?php
include 'layout.php';

head("Wez's Evil Shell\nDocumentation");
?>

<h2>Installation</h2>

<ol>
	<li><a href="downloads.php">Download</a> the installer</li>
	<li>Using administrative privileges, double click on the msi file to install it.</li>
	<li>If this is the first time you've ever installed a shell replacement,
	  you should reboot now so that Windows knows to use per-user shell settings.
	</li>
</ol>

<p>
	This will install the shell and set the registry to allow each user to
	chose their own shell.  It does not modify the shell of the person that ran
	the installer.
</p>

<p>
	For each user that wants to use the shell, you should take the following
	steps:
</p>

<ol>
	<li>Log in as that user</li>
	<li>Use the start menu and click on <tt>Start | Programs | Evil, as in Dr.
	| Shell | Set Shell</tt></li>
	<li>Log out, log in, and you're running the shell.</li>
</ol>


<h2>Usage</h2>

<p>
	<img src="images/dock-1.png" class="floatright">
	Once installed, the default configuration will leave you with a desktop
	that is mostly empty, with a partially transparent toolbar in the bottom
	right corner.
</p>
<p>
	The partially transparent toolbar is known as the slit, and it contains
	your quicklaunch icons (the "dock"), the system "tray" icons and a clock.
</p>
<p>
	To launch an application, click on its shortcut icon in the dock.  The
	shortcut icon will change into the window application icon and a small
	arrow will appear next to it to indicate that it is now a running
	application.  Clicking on the icon will now bring the application to the
	front.
</p>
<p>
	Application windows are grouped together, so if you have multiple web
	browser windows open, you'll see only a single icon in the dock.  Clicking
	the icon will bring the front-most instance of the application to the front
	of the screen.  If you want to activate one of the other windows, you may
	right click on the icon to select the one that you want.
</p>

<h3>Start Menu?</h3>

<p>
	<img src="images/evildesk-popup.png" class="floatright">
	You'll notice that the start button and taskbar are not present, so how to
	do you get at the equivalent functionality?  Right-clicking on the desktop
	or on the slit will popup a context menu (you may also press Win-S).
	The default context menu
	contains sub-menus to allow you to access your programs, your documents,
	configure you system (I call it Tweakage), or to shutdown/restart/log-off.
</p>

<h3>Workspaces (aka Virtual Desktops)</h3>

<p>
	EvilDesk supports Windows-friendly workspace management, allowing you to
	partition your desktop into up to 32 separate workspaces.  Workspaces allow
	you to group different sets of applications together, so you might want
	your email and web browser applications on one Workspace, and have
	development related applications together on another workspace.
</p>

<p>
	The replacement alt-tab task switch that is part of EvilDesk is workspace
	aware, showing you only the applications that are on the current workspace.
	You may press Enter while alt-tab is held down to cycle to the next
	workspace and view the applications that are active there.
</p>

<p>
	In the default configuration, you may only access workspaces 1 through 4.
</p>

<p>
	You can move windows between workspaces either by using the system menu or by
	bringing up the workspace context menu using the Win-W hotkey combination.
</p>

<h3>Launcher</h3>

<p>
	<img src="images/launcher.png" class="floatright">
	Win-R pops up the launcher which is combination of the "Run..." dialog and
	a smart application finder.  You may type in a command and press enter to
	launch it.  While you type, the launch will suggest applications or
	documents to launch.  Use the cursor up/down keys to select a suggestion
	and enter to launch it.  If you typed or selected a filesystem path, you
	may use the left/right cursor keys to navigate the filesystem.
</p>

<p>
	Press escape to cancel the launcher window.
</p>


<h3>Hotkeys</h3>

<p>
	The following hotkeys are defined in the default configuration:
</p>
	
<dl class="hotkey">

<dt>Win E</dt>
<dd>Opens up an explorer window for file management.</dd>

<dt>Win R</dt>
<dd>Opens up the launcher for launching commands.</dd>

<dt>Alt F1</dt>
<dt>Alt F2</dt>
<dt>Alt F3</dt>
<dt>Alt F4</dt>
<dd>Switches to Workspace 1, 2, 3 or 4</dd>

<dt>
<img src="images/evildesk-workspace-popup.png" class="floatright">
Win W</dt>
<dd>
Displays the Workspace menu for the current window.  You may use this menu to
move the window to an alternate workspace, make it appear on all workspaces (Sticky) and
also toggle transparency of the window.
</dd>

<dt>Alt TAB</dt>
<dt>Alt SHIFT TAB</dt>
<dd>
Displays the task switching window. Alt-Tab moves forwards through the list, Alt-Shift-Tab moves backwards.
The list of tasks is limited to the tasks on the current workspace.
While Alt is held down, you may press Enter to cycle to the next workspace.
When you release Alt, the highlighted task will be activated, switching the workspace if required.
</dd>

</dl>

<h1>Plugins</h1>

<p>
The shell has a plugin based architecture, and ships with a number of standard
plugins.  They are all loaded in the default configuration (which happens to be
what I use).  You can customize the extensions by editing the .evdm file and
adding or removing <tt>LOAD</tt> lines as appropriate.
</p>

<h2>General Plugin Configuration</h2>

<p>
Evildesk is configured using a configuration file that is <a
href="config.php">documented in detail here</a>.  Some configuration
information is also stored in the registry for the convenience of the user.
</p>

<p>
The plugins that dock with a slit will load into the current slit (defined by
the <tt>SLIT</tt> directive in the .evdm file).
</p>

<!--
<p>
Each plugin will have a registry key created for it under
<tt>HKEY_CURRENT_USER\Sofware\Evil, as in Dr.\WezDesk\pluginname</tt>.  It will
store its configuration under this point.
</p>

<p>
In addition to the slit, you may also configure the gravity of the plugin,
which affects its positioning within the slit, relative to other plugins.
The left and right slits respect vertical gravity, while the top and bottom
slits respect horizontal gravity.
</p>

<p>
A vertical gravity slit will position plugins with 'top' gravity starting from
the top of the slit, while plugins with 'bottom' gravity will be positioned
starting from the bottom of the slit.  The gravity setting can include a
weighting value to indicate how heavy the plugin is.  A heavier top gravity
gets positioned higher than normal (0) top gravity, for example.  A heavier
bottom gravity gets positioned lower than normal bottom gravity.
</p>

<p>
A horizontal gravity slit behaves similarly, positioning 'left' gravity
starting from the left, and 'right' gravity starting from the right.
</p>

<p>
The gravity setting is stored in the registry as a DWORD value named <tt>gravity</tt>; the high word
corresponds to the direction of the gravity, and the low word corresponds to
its weight.  If you're editing the values via regedit, it's easiest to view the
values as hexadecimal; the possible values for gravity are 0x0000 for 'none',
0x0001 for 'left', 0x0002 for 'right', 0x0003 for 'top' and 0x0004 for
'bottom'.  To specify top gravity with no weight, you would enter 0x00030000.
For heavy top gravity you might enter 0x00030064.  For heavy bottom gravity,
0x00040064.
</p>
-->


<h2>Tray</h2>

<p>
The tray plugin provides the System Notification Area, commonly referred to as
the tray.  There are currently no configuration options for the tray.  The tray
has moderate bottom gravity.
</p>

<h2>Clock</h2>

<p>
The clock plugin displays the day, date and time.  If your MUA (Mail User
Agent, or Email Application) registers its unread mail counts with the shell,
the total number of unread messages will also be displayed above the date.
The clock has strong bottom gravity.
</p>

<p>
The following configuration options are available.
The DateFormat value accepts any valid date format allowed by <a
href="http://msdn.microsoft.com/library/default.asp?url=/library/en-us/intl/nls_5w6s.asp">GetDateFormat()</a>.
The MailFormat value can accept a single %d specifier; the %d will be replaced
by the number of unread mail items for your user account.
</p>

<pre class="winreg">
# Displays the date, time and unread mail count
LOAD clock
# SET clock DateFormat "ddd MMM dd"
# SET clock MailFormat "Mail: %d"
# SET clock Font "Trebuchet MS,18,italic"
# SET clock Font.fg "#fff"
# SET clock Font.shadow "#444"
</pre>


<!--
<h2>Flasher</h2>

<p>
The flasher plugin handles task notification.  The name is slightly misleading;
in the traditional windows shell, a window that wants your attention can flash
at you in a non-intrusive way from the taskbar.  The flasher plugin fills this
role, but, because there is no taskbar, will bounce the icon of the flashing
task in the slit instead.
</p>

<p>
The flasher has bottom gravity; there are no configuration options.
</p>
<h2>Quicklaunch</h2>

<p>
The quicklaunch plugin provides a similar role as the regular quicklaunch area
of the traditional windows taskbar.  The quicklaunch plugin renders contents of
your quicklaunch as large icons; click once on an icon will launch the
shortcut.
</p>

<p>
Quicklaunch has weak bottom gravity.
While it has no configuration options, the <i>Tweakage</i> context menu
provides an <i>Open Quicklaunch</i> item that will open up the quicklaunch
folder using the windows file explorer, so that you can edit its contents.
</p>
-->

<h2>PuTTY</h2>

<p>
If you're a user of the <a
href="http://www.chiark.greenend.org.uk/~sgtatham/putty/">PuTTY</a> SSH client,
you might enjoy this plugin.  It adds provides a PuTTY menu item that can be
added to the context menu.  The PuTTY menu lists your saved PuTTY sessions.
Clicking on the session name will launch that session using PuTTY.  To load
this plugin, you need to add a <tt>LOAD putty</tt> line to your .evdm file, and
then add the context menu using something like this:
</p>

<pre class="winreg">
CONTEXTMENU root
	SubMenu   "PuTTY"		 putty
	MergeDirs "All Programs" $CSIDL_COMMON_STARTMENU $CSIDL_STARTMENU
	MergeDirs "My Documents" $CSIDL_COMMON_DOCUMENTS $CSIDL_PERSONAL
	Execute   "Task Manager" "runas" "taskmgr.exe"
</pre>

<p>
By default, it will assume that you have installed PuTTY under <tt>c:\Program
Files\PuTTY\putty.exe</tt>.  If your putty binary is in a different location,
you can configure an alternate path by setting the <tt>putty.exe</tt> configuration
value:
</p>

<pre class="winreg">
# Registers a "putty" contextmenu
LOAD putty
# SET putty "putty.exe" "C:\\Program Files\\PuTTY\\putty.exe"
# SET putty "New.Session" "New Session"
</pre>

<?php
foot();

?>
