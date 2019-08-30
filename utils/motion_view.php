<?php
$dir    = 'cams';

echo "<html>
	<head>
		<style>
			body * { padding: 2px; vertical-align: top;}
			#divvideo { boder: 1px solid; display: none; }
		</style>
		<script>
		function hidevideo() {
			document.getElementById('divvideo').style.display = 'none'; 
		}
		function showvideo(div, v) {
			var div = document.getElementById(div);
			if (div.style.display == 'none') {
				var video = document.createElement('video');
				video.setAttribute('src', v);
				video.controls = true;
				div.appendChild(video);
				div.style.display = 'inline-block';
				video.play();
			} else {
				div.style.display = 'none';
				div.innerHTML = '';
			}
		}
		</script>
	<body>\r";

# events for the day
if ($_GET['day'] != null && $_GET['monitor'] != null) {

	$m = $_GET['monitor'];
	$d = $_GET['day'];
	$df = $_GET['dayfrag'];

	# day splits, add more if you need!
	$daysplt = array([0,6], [6,12], [12,18], [18,24]);

	$path = parse_url($_SERVER["REQUEST_URI"], PHP_URL_PATH);
	foreach($daysplt as $k => $v) {
		echo "<a href='$path?monitor=$m&day=$d&dayfrag=$k'>$v[0]h to $v[1]h</a>";
	}
	echo "<br>";

	$eventsize = 0;
	$count = array();
	$events = glob("$dir/$m/$d/*.jpg");
	$e = end($events);
	do {
		if ($e == '.' || $e == '..') continue;

		$fh = date('H', filemtime($e));
		if ($df == null || ($fh >= $daysplt[$df][0] && $fh < $daysplt[$df][1])) {
			$v = substr_replace($e, "mp4", -3);
			$id = basename($e, '.jpg');
			echo "<img width='200px' src='$e'
				onclick='showvideo(\"v$id\", \"$v\")'></img>
			      <div id='v$id' style='display: none;'></div>";
			$count[$fh] = ($count[$fh] ?? 0) + 1;
		}
		$eventsize += filesize($e);
		$eventsize += filesize($v);
	} while ($e = prev($events));

	echo "<br><h2>Histogram of events, hour:number</h2>";
	$t = 0;
	foreach($count as $k => $v) {
		echo "${k}h: $v<br>";
		$t += $v;
	}
	echo "total: $t events, " . number_format($eventsize/1024/1024/1024, 2) . " GB";
}

# main page
else {
	echo "<a href='/cam1'> <img src='/cam1' border=0 width=25%></a>
	      <a href='/cam2'> <img src='/cam2' border=0 width=25%></a>";

	$monitors = scandir($dir);
	foreach ($monitors as $m) {
		if ($m == '.' || $m == '..') continue;
		echo "<h1>Events for $m</h1>";

		$days = scandir($dir . "/$m", 1);
		foreach ($days as $d) {
			if ($d == '.' || $d == '..') continue;
			echo "<a href='index.php?monitor=$m&day=$d'>$d</a></br>";
		}
	}
}

echo "</body></html>";
?>
