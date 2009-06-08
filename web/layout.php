<?php

$LATEST = 'v0.9, 26th January 2007';

function head($orig_title)
{
	global $LATEST;
	$title = htmlentities(str_replace("\n", ' - ', $orig_title));
	$titlebr = nl2br(htmlentities($orig_title));
	echo <<<HTML
<html lang="en">
<head>
	<title>$title</title>
	<link rel="stylesheet" href="evildesk.css">
</head>
<body>
<h1>
<div class="nav"><a href="index.php">Home</a> | <a href="docs.php">Documentation</a> | <a href="config.php">Configuration</a> | <a href="changelog.php">ChangeLog</a> | <a href="downloads.php">Download</a><br/>
<form action="https://www.paypal.com/cgi-bin/webscr" method="post" id="paypalform">
<input type="hidden" name="cmd" value="_s-xclick">
<input type="image" src="images/donate.gif" border="0" name="submit" alt="Donate to the project!">
<input type="hidden" name="encrypted" value="-----BEGIN PKCS7-----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-----END PKCS7-----
">
</form>
Latest is $LATEST <a title="Subscribe for software updates"
	rel="alternate" type="application/rss+xml"
	href="http://feeds.feedburner.com/EvildeskSoftwareUpdates"><img 
	src="http://www.feedburner.com/fb/images/pub/feed-icon16x16.png"
	alt="" style="border:0"/></a>
	<a href="http://www.feedburner.com/fb/a/emailverifySubmit?feedId=661477"
		title="Subscribe for software updates by email"><img
		src="images/email.gif" border="0"
		alt="Subscribe for software updates by email"/></a><br/>
</div>
$titlebr</h1>
<div>
HTML;
}

function foot()
{
	echo <<<HTML
	</div>
	<div class="uptodate">
		All content is Copyright &copy; 2004-2007 Wez Furlong, unless otherwise attributed.<br/>
		<a href="http://netevil.org">Stay up to date with this and other projects by the same author.</a>
		<br/>
	</div>
</body>
</html>
HTML;
}

?>
