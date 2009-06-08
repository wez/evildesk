<?php
include 'layout.php';

head("Wez's Evil Shell\nConfiguration File");
?>
<h1>Configuring the context menu</h1>

<p>
When EvilDesk starts, it looks for a file named <tt>%APPDATA%\Evil, as in
Dr.\Shell\evildesk.evdm</tt>; if it is not found, it will then try
<tt>%SystemDrive%\EvilDesk\default.evdm</tt>.  The contents of this file
determine what appears on the context menu.
</p>

<p>
The file is assumed to be UTF-8 encoded, so that symbols from any language may
be used on the context menu (EvilDesk is 100% unicode aware).  The <tt>#</tt>
symbol introduces a comment; the <tt>#</tt> and any text to the right of it
will be ignored.
</p>

<p>
The evdm file parser is line based, so you may not wrap the lines.
The input file is tokenized; there are three types of tokens:
</p>

<dl class="hotkey">

<dt>A string of alphanumeric characters, with no spaces</dt>
<dd>E.g.: <tt>CONTEXTMENU</tt>.  This string is tokenized as-is, so the parser will literally see <tt>CONTEXTMENU</tt></dd>

<dt>A C-style quoted string</dt>
<dd>E.g.: <tt>"Hello there"</tt>.  This string will have backslash escapes expanded and will be stored without the enclosing quotation marks.  Inside the quotes, a backslash introduces an escape character; the regular C-style escapes are supported.  A backslash that is followed by any other character will expand to the character that follows the backslash.
</dd>

<dt>A <tt>$</tt> sign, followed by alphanumeric characters</dt>
<dd>E.g.: <tt>$CSIDL_PERSONAL</tt>.  This is a symbolic constant, and will be replaced with the value of the constant.  For $CSIDL_PERSONAL, the path to the current users "My Documents" folder will be substituted.
</dd>
</dl>

<p>
The tokens in the file must be space separated.  The first token on a line specifies a configuration command; the following commands are recognized:
</p>

<dl class="hotkey">

<dt>CONTEXTMENU "name"</dt>
<dd>
Defines a new context menu (or sub menu).  The supplied name will be used to
invoke the context menu.  The only names that have special meaning to EvilDesk
are <tt>root</tt> and <tt>workspace</tt>; context menus with those names should
be defined, otherwise there will be no working context menus when the shell is
run.  The <tt>root</tt> menu is launched when you right click on the desktop.  The <tt>workspace</tt> menu is launched when you press Win-W (by default).
</dd>

<dt>MergeDirs "caption" "dir1" "dir2"</dt>
<dd>
Adds a menu item to the current CONTEXTMENU.  The item will have the caption
you specified and, when selected, will create a sub-menu that will reflect the
context of dir1 and dir2 merged together.  This is option you would use to
implement the "Programs" part of the start menu, or the "My Documents" part,
which merge the all-users version of those folders with your own personal
version.
</dd>

<dt>SubMenu "caption" "name"</dt>
<dd>
Adds a menu item that will create a sub-menu when selected.  The sub-menu will
be the menu that you specify by name; that menu must exist.
</dd>

<dt>Execute "caption" "verb" "file" "arguments"</dt>
<dd>
Adds a menu item that will execute a given file using the shell when clicked.
</dd>

<dt>Func "caption" "funcname" "arguments"</dt>
<dd>
Adds a menu item that will run a built-in function, passing the specified arguments, when clicked.
</dd>

<dt>Separator</dt>
<dd>
Adds a menu separator to the current context menu.
</dd>

<dt>SaferExec NORMAL "<a href="http://www.php.net/manual/en/reference.pcre.pattern.syntax.php">/perl compatible regex/i</a>"</dt>
<dd>
<p>
Executes applications whose file name matches the regex using a "Safer" token.
This protection only applies to application launched from EvilDesk itself; it
cannot protect applications run using the "Run" dialog or Explorer, as those
are not really launched directly by EvilDesk.
</p>

<p>
EvilDesk ships with this enabled for some common applications that are likely
to be exposed to the hostile internet (web browsers, email clients, instant
messaging).  The safer token should not adversely affect your experience with
the application, and reduces the impact that malware might have on your system
if it manages to compromise the application.  <a
	href="http://msdn.microsoft.com/security/securecode/columns/default.aspx?pull=/library/en-us/dncode/html/secure11152004.asp"
>Read more about the topic on MSDN</a>.
</p>

</dd>

<dt>SaferExec CONSTRAINED "<a href="http://www.php.net/manual/en/reference.pcre.pattern.syntax.php">/perl compatible regex/i</a>"</dt>
<dd>
Executes applications whose file name matches the regex using a constrained "Safer" token.
This is a more restrictive version of the NORMAL safer token; it might affect the functionality of some poorly written applications.
</dd>

<dt>SaferExec UNTRUSTED "<a href="http://www.php.net/manual/en/reference.pcre.pattern.syntax.php">/perl compatible regex/i</a>"</dt>
<dd>
Executes applications whose file name matches the regex using an untrusted "Safer" token.
This is the most restrictive level of safer tokens; likely to cause some applications to fail when run in this mode.
</dd>

<dt>HOTKEY <i>modifier</i> <i>key</i> Func "funcname" "arguments"</dt>
<dd>
Defines a hotkey that will cause function "funcname" to be run and passed
"arguments".  <i>modifier</i> can be a combination of <tt>ALT</tt>,
<tt>WIN</tt>, <tt>SHIFT</tt> and <tt>CONTROL</tt>; combine the values by
separating them with a vertical bar (<tt>|</tt>).  Key can be one of the
following symbolic names: <tt>F1</tt> through to <tt>F11</tt> (F12 is reserved
by Windows), or it can be a single letter, such as <tt>E</tt> to represent the
E key.  I'll add more symbolic names for other keys (just let me know which
ones you're using), but in the meantime, you can also use a number prefixed by
a <tt>#</tt> symbol; this will be interpreted as the number you've specified.
</dd>

<dt>HOTKEY <i>modifier</i> <i>key</i> Execute "verb" "exe" "arguments"</dt>
<dd>
Defines a hotkey that will execute a command.  Works similarly to the HOTKEY
syntax above, crossed with the Execute syntax further above.
</dd>

<dt>MaximumWorkspaces number</dt>
<dd>
Configures the maximum number of workspaces used by the alt-tab task switcher.
The number can be from 1 to 32.  Note that EvilDesk supports 32 workspaces at all
times; this setting is used by the task-switcher only, so that you don't have
to cycle through 32 workspaces if you're only using 4.
</dd>

<dt>SLIT <i>position</i> <i>gravity</i></dt>
<dd>
Creates a new Slit window at the specified position with the specified gravity.
Position (and gravity) can be one of <tt>LEFT</tt>, <tt>RIGHT</tt>,
<tt>TOP</tt> or <tt>BOTTOM</tt>.  Gravity may also have the value <tt>MIDDLE</tt> (since version 0.9).
</dd>

<dt>LOAD <i>pluginname</i></dt>
<dd>
Loads the specified plugin.  If the plugin needs to attach to a Slit window, it
will attach to the most recently created Slit.
</dd>

<dt>MATCH CREATE <i>property</i> "<a href="http://www.php.net/manual/en/reference.pcre.pattern.syntax.php">/perl compatible regex/i</a>" <i>function</i> <i>funcarg</i></dt>
<dd>
<p>
Defines a window match rule to perform actions for newly created windows that
match certain criteria.  Property can be one of <tt>CAPTION</tt>,
<tt>CLASS</tt> or <tt>MODULE</tt> to match against the window title, the window
class or the window module respectively.  Regex is a regular expression to
match against; if it matches the specified property, then the function is
invoked.
</p>
<p>
The following example will move all newly created windows whose title contains
hello (case insensitive) to workspace 4.
<pre class="winreg">
MATCH CREATE CAPTION "/hello/i" "set-window-workspace" "4"
</pre>
</p>
</dd>

</dl>



<?php
foot();

?>
