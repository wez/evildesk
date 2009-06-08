<?php
include 'layout.php';

head("Wez's Evil Shell\nChangeLog");
?>

<h2>New in 0.9 (26th January 2007)</h2>
<p>
It's been a year in the making, but worth the wait, I think.
Here's what's new and improved:
</p>

<ul>
<li>

<p>Issue #21: Improved task management experience.</p>
<p>
I've integrated the quicklaunch and flasher plugins to create a new plugin
called the dock.  (the flasher and quicklaunch plugins are still there if you
prefer those).  The dock is (not intentionally) OSXish and serves as a mixture
of the quicklaunch and taskbar in the regular explorer shell.
</p>

<p>
The dock shows you icons from your quicklaunch folder mixed in with the icons
from the running applications on the current workspace.  The icons are grouped
together by executable module (so all cmd.exe processes are represented by a
single icon) and where possible, the module is matched against your shortcuts.
</p>

<p>
If you have IE in your quicklaunch, clicking on the icon will launch IE and
then replace the shortcut icon with the icon from the IE window.  Clicking on
that icon will activate IE and bring it to the front.  If you have multiple IE
windows open, clicking the IE icon in the dock will activate the topmost IE
window.  If one of the windows is flashing, the icon will be animated in the
dock to grab your attention; click it will activate the flashing window.
</p>

<p>
If you want to activate a particular instance of an application, right click on
the icon to popup a list of windows and their titles.  In this menu, if the
application has a shortcut in the quicklaunch folder, you have the option of
launching the shortcut again, or opening up the properties dialog for that
shortcut.
</p>

</li>

<li>
<p>Issue #26: Replace the "Run" dialog with the launcher plugin.</p>
<p>
The launcher plugin provides similar functionality to the "Run" dialog (Win-R)
but also allows you to search your programs and documents for items that match
the text that you've typed.  For example, typing "fire" will show you your
firefox icons from the start menu, if you have it installed.
</p>
<p>
Select a matching item by using the up and down keys and pressing enter.
</p>
<p>
You can navigate your filesystem by typing in a filesystem path; matching items
are shown in the selection list and you can move up and down through them.
Press the right cursor key to descend into a directory, or the left cursor key
to ascend to the parent of the current item.
</p>
<p>
You can dismiss the dialog by pressing ESC.
</p>
<p>
The launcher will create an index of your programs and documents every 20
minutes; this is about 200k in size on my system (I don't have much stuff in my
documents or on my desktop) and takes a few moments to create.  While indexing,
you might notice an increase in CPU usage for wezdesk.exe, but it should remain
responsive (indexing happens in an alternate thread).  If you have a lot of
information in your documents folders and this seems to take a long time,
please let me know so that I can figure out ways to optimize this.
</p>
</li>

<li>
<p>
Issue #2: Expanded slit positioning and alignment.  The slit can now be used
with a horizontal orientation and middle gravity.
</p>
<p>
Added a new, preferred, syntax for declaring slits in the config file:
</p>
<pre class="winreg">
SLIT "default"
</pre>
<p>
Creates a slit with the default positioning (right, bottom).  You may use the
new slit context menu to change the positioning at runtime and those changes
will be saved in the registry for the next time you launch the shell.
</p>
<p>
Note that you can create multiple slits, if you desire, by adding multiple
"SLIT" lines to your config file.
</p>
</li>

<li>
Issue #3: The installer might have ended up installing evildesk onto a removable drive (such
as a USB memory stick) which would cause problems if you were to log in and
that device was no longer present.
</li>

<li>
Issue #5: Overview crashes when there are no windows to overview.  Fixed!
</li>

<li>
Issue #6: Added localization framework and prototype German and French resource files.
Thanks to Ralph Buchfelder and Bruno Pelletier for their input on this.
</li>

<li>
<p>
Issue #9: Auto-hide the slit.  You can choose to make the slit auto-hide or
float using the slit context menu options.  These settings are saved in the
registry, as mentioned under issue #2 above.
</p>
</li>

<li>
<p>Issue #17: bug in assigning hotkeys using CTRL and ALT modifiers.</p>
<p>The syntax for this is:</p>
<pre class="winreg">
HOTKEY CTRL|ALT F1 Func "set-current-workspace" "1"
</pre>
<p>In previous releases, this would cause evildesk to hang.</p>
</li>

<li>
<p>Issue #18: Added ability to turn off the alt-tab replacement.</p>
<p>I've implemented this by moving the alt-tab code into a plugin.  If you don't want
to use it, simply comment out the LOAD line in the evdm file.
</p>
<p>Because the taskswitcher is now a plugin, some of the configuration options have
changed slightly:
</p>
<pre class="winreg">
# To disable the alt-tab replacement, comment the next line.
LOAD taskswitch
# To change the size of the preview in the task switcher:
SET taskswitch Preview.Width 200
SET taskswitch Preview.Height 150
# Max number of workspaces to show in the taskswitcher.
# Replaces the old MaximumWorkspaces config statement.
SET taskswitch MaximumWorkspaces 4
</pre>
</li>

<li>
Issue #19: Some windows have no icon in the alt-tab task switcher. Fixed!
</li>

<li>
Issue #20: Added sticky and move-to-workspace items to the Window (also known
as the System) menu of windows running in the "win32" subsystem.  Since it is
not possible to hook console windows to acheive this, you can still use the
"workspace" context menu, which is bound to Win-W by default.
</li>

<li>
Issue #22: Add win64 support.  The range of platforms that I've personally run
evildesk on includes Windows XP Pro, Windows Server 2003 and Windows XP Pro
64-bit.  I've tried it on Vista too, but it doesn't work very well there at
this time; I strongly recommend that you don't bother.  I've heard reports
that it works fine on Media Center editions of Windows too.
</li>

<li>
Issue #1: 99% cpu usage after popping up a context menu.  Fixed!
</li>

</ul>


<h2>New in 0.8 (1st January 2006)</h2>
<ul>

<li>
Added new <tt>SET</tt> directive to replace registry based configuration.  All the values that can be configured are listed in the <tt>default.evdm</tt> file.
</li>

<li>
When alt-tabbing, pressing escape did not properly cancel the task-switcher.
</li>

<li>
You may now left-click icons in the alt-tab task switcher to switch to a task.
</li>

<li>
<p>
Added window capture code that maintains a cache thumbnails of application
windows (only those that show up in the task switcher).  These thumbnails are
used to preview the application windows while alt-tabbing.  You can configure
the size of the preview by changing the following settings:
</p>
<pre class="winreg">
SET core Switcher.Preview.Width 200
SET core Switcher.Preview.Height 150
</pre>

<p>
If you'd like better quality, larger, previews you can increase the size limit
for the thumbnail cache.  The default limit it 1.5MB per thumbnail.  You can
raise or lower the limit using the following option:
</p>

<pre class="winreg">SET core Thumbnail.Max.Size 1572864</pre>

<p>
If you want to disable the thumbnail cache, set the Thumbnail.Max.Size to 0.
</p>

<p>
Some applications can't be reliably thumbnailed.  This is because they're not
very well behaved win32 applications when it comes to painting their screens.
You may experience black or partially drawn areas for GTK+ based applications,
and occasionally from Mozilla based applications.  If anyone has suggestions
for improving the capture, I'm listening.
</p>

</li>

<li>
<p>
Added new <tt>Overview</tt> plugin.  Pressing <tt>Win-F9</tt> (configurable)
will activate the Overview window, which displays a zoomed out view of all the
applications running on each monitor.  Overview shows the same windows as the
alt-tab task switcher, plus dialog boxes (like Desktop Properties).  This
allows you to find those elusive dialogs when they got lost on your busy
desktop.  You may then use the mouse to switch to an application.  This feature
is inspired by OSX expos&eacute;, except that it doesn't perform
any animation.
</p>
<p>
Overview uses the same application thumbnail cache as the task switcher, so
that the initial display is fast.   Clicking on the desktop or pressing Win-F9
again will cancel the Overview.  I've tried hard to keep the resource
consumption low; this plugin uses no additional resources until the Overview
window is active.  As soon as it deactivates, all its resources are freed.
</p>
</li>

<li>
<p>
Revised rendering code for the slit and task switcher.  The font selection now
uses the more visually appealing defaults you have configured as part of your
windows desktop theme.  The slit and task switcher now use a graphic tile for
their background image, rather than the application workspace colour.  This
improves the appearance when running on Windows Vista, where the default
workspace colour is different from Windows XP.
</p>

<p>
You can set the background tile for a slit using <tt>SET slit Background.Image
"path\\to\\file.png"</tt>.  You may use any image format supported by GDI+
(BMP, PNG, JPG, GIF).  The image will be stretched to fit the region.
</p>
<p>
You can also set the appearance of the task switcher:
<pre class="winreg">
SET core Switcher.Background.Image "C:\\path\\to\\image.png"
SET core Switcher.Font "Trebuchet MS,14,italic"
SET core Switcher.Font.fg "#fff"
SET core Switcher.Font.shadow "#444"
</pre>
</p>
<p>
The clock can also be customized too:
<pre class="winreg">
SET clock Font "Trebuchet MS,18,italic"
SET clock Font.fg "#fff"
SET clock Font.shadow "#444"
</pre>
</p>
</li>
</ul>


<h2>New in 0.7 (27th December 2005)</h2>
<ul>
<li>
Added new <tt>MATCH CREATE</tt> window matching configuration option, which
allows actions to be taken when windows are created.  For example, you can
cause all VMWare windows to start on a given workspace, or have your mp3 player
automatically stick to all workspaces when it starts up.
</li>

<li>
Fixed a slit layout calculation bug.
</li>

<li>
Avoid blocking the flasher when the flashing app (eg: gaim 2.0 beta) hangs itself
</li>

<li>
Balloon tips now display the body of the balloon text, instead of the tooltip
from the tray area, and will rise all the way to the top of the z-order.
</li>

<li>
Improved detection of deleted tray tooltips, so that balloon tips are not wiped
out at the wrong time.
</li>

<li>
When a window was made sticky, it would remain in the minimized state on the
inactive workspaces.  This has now been corrected.
</li>

<li>
Fixed a string termination bug in the PuTTY plugin for sessions that have
spaces in their names.
</li>

<li>
Environmental variables are now expanded when processing the MergeDirs
directive in the .evdm file
</li>


</ul>


<h2>New in 0.6 (25th December 2005)</h2>
<ul>
<li>
Slit windows will hide themselves when an application goes fullscreen (tested with MS PowerPoint, Mozilla Firefox and PuTTY)
</li>
<li>
Plugin loading and Slit window creation are now controlled via the new LOAD and
SLIT directives in default.evdm instead of via the registry.  Users running
with custom evdm files will need to update their .evdm files.
</li>
<li>
Altered tray default gravity so that it gravitates lower than the flasher.
This avoids an annoying problem when double-clicking on a tray icon causes a
window to flash and the second click to land on a different tray icon.
</li>
<li>
Fixed a race condition when switching desktops that would cause some windows to
leak from the current desktop to the target desktop.
</li>
<li>
Fallback to the system default icon for the flasher and task-switcher when a window has no icon of its own.
</li>
<li>
Added some more common internet facing applications to the SaferExec line in default.evdm.
</li>

</ul>


<h2>New in 0.5.1 (18th July 2005)</h2>

<ul>
<li>
Corrected an uninstallation bug that would cause explorer.exe to start without the taskbar after uninstalling EvilDesk.  To manually correct this issue, you need to set the following registry key:

<pre class="winreg">
Windows Registry Editor Version 5.00

[HKEY_LOCAL_MACHINE\Software\Microsoft\Windows NT\CurrentVersion\IniFileMapping\system.ini\boot]
"Shell"="SYS:Microsoft\\Windows NT\\CurrentVersion\\WinLogon"
</pre>
</li>

<li>Added <tt>MaximumWorkspaces</tt> option to the .evdm file that controls the number of workspaces that the task switcher can cycle through.
</li>
</ul>


<h2>New in 0.5 (17th July 2005)</h2>

<ul>
<li>
Made all hotkeys used by evildesk user configurable via the .evdm file.
</li>

<li>
You may now also customize the "workspace" context menu (Win-W).
</li>

<li>Added a restart shell function to the root context menu.
This allows you to adjust your .evdm more conveniently. (Caveat emptor: sometimes the alt-tab hotkey goes missing when restarting the shell; the workaround is to restart it again if that happens).
</li>

<li>Added some localization options for the clock and task-switcher.
(read the documentation to find out about them)</li>
</ul>


<h2>New in 0.4 (16th July 2005)</h2>

<ul>
<li>SaferExec option for the evdm file.  This allows you to drop certain privilege
when running applications that match a particular regular expression.
</li>

<li>Added tooltips for items in the quicklaunch area</li>

<li>Now installs under a version-specific directory, so that multiple versions
may be installed at the same time.</li>

<li>Fixed some other problems with per-user .evdm files</li>

<li>Fixed an uninstallation bug that would leave you shell-less after
uninstalling the shell using the Add/Remove programs applet.</li>


</ul>



<?php
foot();
?>
