<?php # vim:ts=2:sw=2:et:

# This script lists items that are missing translations

$master = array();
$xlate = array();

foreach (file("i18n/0409.rc") as $line) {
  if (preg_match('/(IDS_[A-Z0-9_]+)\s+"(.*)"\s*$/',
      $line, $matches)) {
    $master[$matches[1]] = $matches[2];
  }
}

foreach (glob("i18n/*.rc") as $file) {
  $key = basename($file);
  if ($key == '0409.rc') continue;

  foreach (file($file) as $line) {
    if (preg_match('/(IDS_[A-Z0-9_]+)\s+"(.*)"\s*$/',
          $line, $matches)) {
      $xlate[$key][$matches[1]] = $matches[2];
    }
  }
}

$master_keys = array_keys($master);
foreach ($xlate as $key => $data) {
  $xlate_keys = array_keys($data);
  $diff = array_diff($master_keys, $xlate_keys);
  if (count($diff)) {
    echo "$key is missing:\n";
    foreach ($diff as $name) {
      echo "  $name " . $master[$name] . "\n";
    }
  }
}

