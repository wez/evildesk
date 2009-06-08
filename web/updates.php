<?php
header('Content-Type: text/xml');
echo '<'.'?xml version="1.0" encoding="UTF-8"'.'?'.'>';
?>
<?php

$changes = array(
	array('BETA', '2007-01-23', '0.9.0.193', 
		'http://evildesk.netevil.org/downloads/evildesk-0.9.0.193.msi'),

	array('BETA', '2007-01-22', '0.9.0.184', 
		'http://evildesk.netevil.org/downloads/evildesk-0.9.0.184.msi'),

	array('RELEASE', '2006-01-01', '0.8',
		'http://evildesk.netevil.org/downloads.php'),
);

$filter = 'RELEASE';
if ($_GET['filter'] == 'BETA') $filter = 'BETA';
$PUBDATE = 0;
foreach ($changes as $item) {
	if ($item[0] == 'BETA' && $filter != 'BETA') continue;
	$t = strtotime($item[1]);
	if ($t > $PUBDATE) $PUBDATE = $t;
}

$PUBDATE = date('r', $PUBDATE);
?>
<rss
	xmlns:rdf="http://www.w3.org/1999/02/22-rdf-syntax-ns#"
	xmlns:admin="http://webns.net/mvcb/"
	xmlns:dc="http://purl.org/dc/elements/1.1/"
	xmlns:slash="http://purl.org/rss/1.0/modules/slash/"
	xmlns:wfw="http://wellformedweb.org/CommentAPI/"
	xmlns:content="http://purl.org/rss/1.0/modules/content/"
	version="2.0">
<channel>
	<title>EvilDesk Software Updates</title>
	<link>http://evildesk.netevil.org/</link>
	<description>A Windows Desktop Replacement</description>
	<pubDate><?= $PUBDATE ?></pubDate>
	<copyright>
		Copyright 2004-2007, Wez Furlong.  You may redistribute this update
		information provided you link back to the main evildesk website.  You
		may not re-distribute evildesk itself to any third party without my
		express consent.
	</copyright>
	<docs>http://blogs.law.harvard.edu/tech/rss</docs>
<?php

foreach ($changes as $item) {
	if ($item[0] == 'BETA' && $filter != 'BETA') continue;
	$t = strtotime($item[1]);

	$pub = date('r', $t);
	$title = $item[0] . ' ' . $item[2];
	$link = $item[3];
	if ($item[0] == 'BETA') {
		$html_link = 'http://evildesk.netevil.org/beta.php';
	} else {
		$html_link = 'http://evildesk.netevil.org/changelog.php';
	}
	$content = htmlspecialchars("<p>EvilDesk $title is now available from <a href=\"$link\">$link</a>.<br />Read more about this release at <a href=\"$html_link\">$html_link</a></p>", ENT_QUOTES, 'utf-8');
?>
<item>
	<title>EvilDesk <?= $title ?></title>
	<link><?= $html_link ?></link>
	<content:encoded><?= $content ?></content:encoded>
	<pubDate><?= $pub ?></pubDate>
</item>
<?php
}
?>
</channel>
</rss>
