<?php
include 'layout.php';

head("Wez's Evil Shell\nBETA");
?>

<p>
If you're interested in trying out a newer version than the current release,
please drop me an email.  I'm deliberately not telling you what my email
address is as a method of filtering out people that might not be a good beta
tester.  If you can deduce my email address, I'll send you details on beta
testing.
</p>

<h2>What's new/fixed in the beta</h2>

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
Issue #3: The installer might end up installing evildesk onto a removable drive (such
as a USB memory stick) which would cause problems if you were to log in and
that device was no longer present.
</li>

<li>
Issue #5: Overview crashes when there are no windows to overview.  Fixed!
</li>

<li>
Issue #6: Added localization framework and prototype German and French resource files.
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
this time; I strongly recommend that you don't bother.
</li>

<li>
Issue #1: 99% cpu usage after popping up a context menu.  Fixed!
</li>

</ul>

<?php
foot();
?>
